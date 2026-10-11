#include "PaintContractTests.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "../example/FloppyBird/src/MainNode.hpp"
#include "../example/SmirkBench/src/MainNode.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/projection/PaintEnumeration.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullRectSurfaceContext.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/String.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;

  class PaintPlatform : public NullScenePlatformController
  {
  public:
    struct Observation
    {
      ApplyPaintPlan plan;
      unsigned long ownerKey;
    };
    PaintPlatform()
        : count(0),
          queries(0),
          commits(0),
          applies(0),
          preLayoutCommits(0),
          refuseSurfaceProjection(false)
    {
    }
    virtual void beginApplyCycle()
    {
      count = queries = commits = applies = preLayoutCommits = 0;
    }
    virtual void onChange(Node *root, NodeDirtyFlags, bool)
    {
      LayoutState state;
      state.width = 640;
      state.height = 600;
      state.lineHeight = 20;
      this->projectLayoutForTesting(root, state);
    }
    virtual void
    onBoundaryApply(Node *root, BoundaryNode *owner, const BoundaryLocalApplyInfo &info, const PlatformApplyPlan &plan)
    {
      ++applies;
      const unsigned before = commits;
      NullScenePlatformController::onBoundaryApply(root, owner, info, plan);
      if (plan.hasLayoutWork() || plan.hasStructureWork())
        preLayoutCommits += commits - before;
    }
    virtual void onPaintPlanSubmitted(BoundaryNode *, const ApplyPaintPlan &plan)
    {
      LOKA_VERIFY(count < 32);
      observations[count].plan = plan;
      observations[count].ownerKey = this->paintScope().ownerKey;
      ++count;
    }
    virtual void onPaintQueried()
    {
      ++queries;
    }
    virtual void onPaintCommitted()
    {
      ++commits;
    }
    void forget(NullRectSurfaceContext &context)
    {
      context.invalidatePaintHistory();
    }
    /** Simulates a refused projection of every RectSurface while set. */
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (refuseSurfaceProjection && node && node->asRectSurfaceNode())
        return false;
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
    Observation observations[32];
    unsigned count, queries, commits, applies, preLayoutCommits;
    bool refuseSurfaceProjection;
  };
  void settle(Scene &scene)
  {
    for (int i = 0; scene.hasPendingInvalidation() && i < 12; ++i)
      LOKA_VERIFY(scene.flushInvalidation());
    assert(!scene.hasPendingInvalidation());
  }
  Node *find(Node *root, NodeKind kind, unsigned &index)
  {
    if (root->kind() == kind && index-- == 0)
      return root;
    INestable *nest = root->asNestable();
    for (Node *n = nest ? nest->childrenHead() : 0; n; n = n->nextInComposition)
    {
      Node *found = find(n, kind, index);
      if (found)
        return found;
    }
    return 0;
  }
  Node *find(Node *root, NodeKind kind)
  {
    unsigned index = 0;
    return find(root, kind, index);
  }
  RectSurfaceModel sprite(short x)
  {
    RectSurfaceModel value;
    value.rectCount = 1;
    value.rects[0] = RectSprite(x, 10, 8, 8);
    return value;
  }
  void change(Scene &scene, MutableState<RectSurfaceModel> &state, short x)
  {
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      state.set(sprite(x));
    }
    settle(scene);
  }
  /** A platform String that refuses UTF-8 materialization: nothing can render it. */
  class RefusingUtf8String : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };
  String unrenderable()
  {
    return String(Managed<loka::platform::String>::Wrap(new RefusingUtf8String()));
  }
  void scoreValue(Scene &scene, MutableState<String> &state, const String &value)
  {
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      state.set(value);
    }
    settle(scene);
  }
  void score(Scene &scene, MutableState<String> &state, const char *value)
  {
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      state.set(String::Literal(value));
    }
    settle(scene);
  }
  const ApplyPaintPlan &only(PaintPlatform &platform)
  {
    LOKA_VERIFY(platform.count == 1);
    return platform.observations[0].plan;
  }
  void exactOne(PaintPlatform &platform)
  {
    LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_EXACT);
    LOKA_VERIFY(only(platform).exactCount() == 1);
  }
  void refused(PaintPlatform &platform, PaintRefusalReason reason)
  {
    LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_WIDENED);
    LOKA_VERIFY(only(platform).exactCount() == 0);
    LOKA_VERIFY(only(platform).widenReason() == APPLY_PAINT_WIDEN_REFUSED);
    LOKA_VERIFY(only(platform).refusalReason() == reason);
  }
  PaintQuery query(PaintPlatform &platform)
  {
    const PaintQuery q = {platform.paintScope(), PLACEMENT_ELIGIBLE};
    return q;
  }

  struct TreeData
  {
    TreeData()
        : a(String::Literal("AAAA")),
          b(String::Literal("BBBB")),
          selector(0),
          count(1),
          refusal(0),
          nested(false),
          shared(false),
          scroll(false)
    {
    }
    MutableState<RectSurfaceModel> models[9];
    MutableState<String> a, b;
    int selector, count, refusal;
    bool nested, shared, scroll;
  };
  class PaintTree;
  struct TreeProps : NodePropsBase<TreeProps>
  {
    typedef PaintTree NodeType;
    struct TypeTag
    {
    };
    TreeProps(TreeData *value = 0, int child = -1)
        : data(value),
          childIndex(child)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      const TreeProps &other = static_cast<const TreeProps &>(rhs);
      return data != other.data ? data < other.data : childIndex < other.childIndex;
    }
    TreeData *data;
    int childIndex;
  };
  class PaintTree : public StdCompositionBoundaryNodeBase<TreeProps>
  {
  public:
    explicit PaintTree(const TreeProps &props)
        : StdCompositionBoundaryNodeBase<TreeProps>(props)
    {
    }

    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      TreeData &d = *this->props.data;
      if (d.nested && this->props.childIndex < 0)
      {
        c.declare(VStack() << Boundary<PaintTree>(TreeProps(&d, 0)) << Boundary<PaintTree>(TreeProps(&d, 1))
                           << RectSurface(&d.models[8]).size(100, 60));
      }
      else
      {
        VStack stack;
        ImageView image;
        RectSurface surfaces[9];
        Text text = d.selector == 2 ? Text("CCCC") : Text(d.selector ? &d.b : &d.a);
        if (d.refusal == 1)
          stack << image;
        if (d.count == 0)
        {
          // selector 3: the same State drawn bold, a style-only change of a retained Text.
          if (d.selector == 3)
            stack << Text(&d.a) + Bold;
          else
            stack << text;
        }
        for (int i = 0; i < d.count; ++i)
        {
          const int index = this->props.childIndex < 0 ? i : d.shared ? 0 : this->props.childIndex;
          surfaces[i] = RectSurface(&d.models[index]).size(100, 60);
          stack << surfaces[i];
        }
        if (d.refusal == 2)
          stack << image;
        if (d.scroll)
          c.declare(Box().size(90, 40) << (ScrollView() << stack));
        else
          c.declare(stack);
      }
    }
  };
  void mount(Scene &scene, PaintPlatform &platform)
  {
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    settle(scene);
    platform.beginApplyCycle();
  }
} // namespace

void testRectSurfaceStateChangeExcludesSiblingTextFromPaintDamage()
{
  floppybird::SharedModel model;
  PaintPlatform platform;
  Scene scene(Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
  mount(scene, platform);
  change(scene, model.surfaceModel_, 10);
  change(scene, model.surfaceModel_, 12);
  exactOne(platform);
  const PaintDamage &d = only(platform).entry(0);
  LOKA_VERIFY(d.x == 10 && d.y == 22 && d.width == 10 && d.height == 8);
  LOKA_VERIFY(d.width < loka_floppy_bird::kWindowWidth && d.height < loka_floppy_bird::kWindowHeight);
  LOKA_VERIFY(d.y >= 20 && d.y + d.height <= 20 + loka_floppy_bird::kWindowHeight);
  LOKA_VERIFY(platform.queries == 2 && platform.commits == 2);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testTextFirstPaintRefusesUnknownHistory()
{
  NullScenePlatformController platform;
  TextNode node(TextProps("First paint"));
  NullTextContext context(&node);
  LayoutState state;
  state.width = 100;
  state.lineHeight = 14;
  context.layout(&platform, state);
  const PaintQuery eligible = {platform.paintScope(), PLACEMENT_ELIGIBLE};
  const PaintAnswer first = context.queryPaintDamage(eligible);
  // Placement alone is not presentation, even when the first apply is
  // paint-only and the current string has never changed.
  LOKA_VERIFY(first.kind == PAINT_ANSWER_REFUSED);
  LOKA_VERIFY(first.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
  LOKA_VERIFY(context.commitPresented(node.props.text_->get(), platform.paintScope()));
  const PaintAnswer drawn = context.queryPaintDamage(eligible);
  LOKA_VERIFY(drawn.kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(drawn.damage.width == 0 && drawn.damage.height == 0);
}

void testTextStateChangeYieldsExactTextDamageOnly()
{
  floppybird::SharedModel model;
  PaintPlatform platform;
  Scene scene(Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
  mount(scene, platform);
  score(scene, model.scoreText_, "Score: 1");
  exactOne(platform);
  const PaintDamage &d = only(platform).entry(0);
  LOKA_VERIFY(d.x == 0 && d.y == 0 && d.width == 640 && d.height == 12);
  LOKA_VERIFY(d.coverage == PAINT_COVERAGE_ERASE_AND_PAINT);
  score(scene,
        model.scoreText_,
        "This unwrapped score is deliberately longer than the six hundred and forty pixel seat: one hundred and sixty "
        "characters are insufficient to fit all of this deliberately verbose score text.");
  refused(platform, PAINT_REFUSED_PLACEMENT_UNSETTLED);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testNativeControlAnswersNativeScheduledAndDoesNotWiden()
{
  smirkbench::SmirkModel model(640, 400, true);
  PaintPlatform platform;
  Scene scene(Boundary<smirkbench::MainNode>(smirkbench::MainProps(&model)));
  mount(scene, platform);
  model.advanceFrame(1.0 / 60.0);
  settle(scene);
  exactOne(platform);
  // The Button is a native control: the rail answers NATIVE_SCHEDULED by kind and
  // never queries (or casts) its context, so the plan above stayed exact with
  // the Button present and three residents were queried (Button, Text, surface).
  Node *button = find(SceneTestAccess::rootBoundary(scene), NODE_KIND_BUTTON);
  LOKA_VERIFY(button && button->getContext());
  LOKA_VERIFY(platform.queries == 3);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testRefusedDrawerWidensWholePlan()
{
  for (int order = 1; order <= 2; ++order)
  {
    TreeData data;
    data.refusal = order;
    PaintPlatform platform;
    Scene scene(Boundary<PaintTree>(TreeProps(&data)));
    mount(scene, platform);
    change(scene, data.models[0], 4);
    refused(platform, PAINT_REFUSED_NO_CONTEXT);
    LOKA_VERIFY(platform.queries == 2);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
}
void testForeignContextIsNeverCastAndOwnedHandlersCannotBeReplaced()
{
  // A replaced ImageView handler installs a plain NodeContext (not a
  // NativeNodeContext). The paint walk must answer UNSUPPORTED_KIND for that
  // resident without casting it; a wrong cast is what the ASan run caught in the
  // SimpleViewer geometry fixtures before this rule existed.
  class PlainContext : public NodeContext
  {
  };
  class PlainImageHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<ImageViewNode>();
    }
    virtual NodeContext *ensureContext(Node *node, IPlatformController *, const LayoutState &)
    {
      if (!node->getContext())
        node->setContext(new PlainContext());
      return node->getContext();
    }
  } plainImage;
  TreeData data;
  data.refusal = 1;
  PaintPlatform platform;
  LOKA_VERIFY(platform.registerNodeHandler(&plainImage));
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 4);
  refused(platform, PAINT_REFUSED_UNSUPPORTED_KIND);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  // The two owned-drawer handlers are addressed by concrete type in the
  // presenter, so a foreign handler for their kinds is refused at registration.
  class ForeignTextHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NullTextNodeHandlerKey();
    }
    virtual NodeContext *ensureContext(Node *, IPlatformController *, const LayoutState &)
    {
      return 0;
    }
  } foreignText;
  class ForeignSurfaceHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NullRectSurfaceNodeHandlerKey();
    }
    virtual NodeContext *ensureContext(Node *, IPlatformController *, const LayoutState &)
    {
      return 0;
    }
  } foreignSurface;
  PaintPlatform second;
  LOKA_VERIFY(!second.registerNodeHandler(&foreignText));
  LOKA_VERIFY(!second.registerNodeHandler(&foreignSurface));
}
void testCompressedParentIncludesNestedOwners()
{
  for (int refusal = 0; refusal < 2; ++refusal)
  {
    TreeData data;
    data.nested = true;
    data.refusal = refusal;
    PaintPlatform platform;
    Scene scene(Boundary<PaintTree>(TreeProps(&data)));
    mount(scene, platform);
    BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
    root->markViewDirty(NODE_DIRTY_PROPS);
    {
      StateTrackerGuard guard(root->tracker());
      data.models[0].set(sprite(2));
      data.models[1].set(sprite(3));
    }
    settle(scene);
    LOKA_VERIFY(platform.applies == 1);
    if (refusal)
      refused(platform, PAINT_REFUSED_NO_CONTEXT);
    else
    {
      LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_EXACT && only(platform).count() == 2);
      LOKA_VERIFY(only(platform).entry(0).y != only(platform).entry(1).y);
      LOKA_VERIFY(platform.queries == 3 && platform.commits == 3);
    }
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
}
void testSameStateTwoBoundariesDamagesBothOwners()
{
  TreeData data;
  data.nested = true;
  data.shared = true;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 3);
  LOKA_VERIFY(platform.applies == 2 && platform.count == 2);
  LOKA_VERIFY(platform.observations[0].plan.precision() == APPLY_PAINT_EXACT);
  LOKA_VERIFY(platform.observations[1].plan.precision() == APPLY_PAINT_EXACT);
  LOKA_VERIFY(platform.observations[0].plan.entry(0).y != platform.observations[1].plan.entry(0).y);
  platform.beginApplyCycle();
  change(scene, data.models[0], 3);
  LOKA_VERIFY(platform.count == 0);
  Node *stack = SceneTestAccess::rootBoundary(scene)->childrenHead();
  Node *a = stack->asNestable()->childrenHead();
  a->asBoundary()->markViewDirty(NODE_DIRTY_PROPS);
  a->nextInComposition->asBoundary()->markViewDirty(NODE_DIRTY_PROPS);
  settle(scene);
  LOKA_VERIFY(platform.count == 2);
  LOKA_VERIFY(platform.observations[0].plan.precision() == APPLY_PAINT_NONE);
  LOKA_VERIFY(platform.observations[1].plan.precision() == APPLY_PAINT_NONE);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testUnknownHistoryRecoversThroughWidenedPresentation()
{
  TreeData data;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  Node *surface = find(SceneTestAccess::rootBoundary(scene), NODE_KIND_RECT_SURFACE);
  platform.forget(*static_cast<NullRectSurfaceContext *>(surface->getContext()));
  change(scene, data.models[0], 3);
  refused(platform, PAINT_REFUSED_HISTORY_UNKNOWN);
  LOKA_VERIFY(platform.commits == 1);
  change(scene, data.models[0], 4);
  exactOne(platform);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testPlacementRefusals()
{
  TreeData data;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
  root->markViewDirty(NODE_DIRTY_LAYOUT);
  settle(scene);
  LOKA_VERIFY(platform.count >= 2 && platform.preLayoutCommits == 0);
  LOKA_VERIFY(platform.observations[0].plan.refusalReason() == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  Node *surface = find(root, NODE_KIND_RECT_SURFACE);
  NativeNodeContext *context = static_cast<NativeNodeContext *>(surface->getContext());
  LOKA_VERIFY(context->queryPaintDamage(query(platform)).kind == PAINT_ANSWER_EXACT);
  context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(context->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  NullScenePlatformController skipped;
  Scene second(Boundary<PaintTree>(TreeProps(&data)));
  skipped.skipNextProjectionForTesting();
  second.mount(&skipped);
  loka::dsl::testing::SceneTestAccess::updateAttached(second, true);
  Node *unplaced = find(SceneTestAccess::rootBoundary(second), NODE_KIND_RECT_SURFACE);
  LOKA_VERIFY(unplaced && !unplaced->getContext());
  loka::dsl::testing::SceneTestAccess::unmount(second);
  NullTextContext absent(0);
  LayoutState state;
  absent.layout(&skipped, state);
  const PaintQuery missing = {skipped.paintScope(), PLACEMENT_ELIGIBLE};
  LOKA_VERIFY(absent.queryPaintDamage(missing).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
}
void testRetainedTextRebindsToNewStateAndCompares()
{
  TreeData data;
  data.count = 0;
  Text replacement;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
  Node *text = find(root, NODE_KIND_TEXT);
  NodeContext *context = text->getContext();
  replacement = Text(&data.b);
  LOKA_VERIFY(replacement.applyPropsToNode(text));
  root->markViewDirty(NODE_DIRTY_PROPS);
  settle(scene);
  LOKA_VERIFY(find(root, NODE_KIND_TEXT) == text && text->getContext() == context);
  LOKA_VERIFY(text->asTextNode()->props.text_ == &data.b);
  exactOne(platform);
  platform.beginApplyCycle();
  score(scene, data.a, "DDDD");
  LOKA_VERIFY(platform.count == 0);
  score(scene, data.b, "EEEE");
  exactOne(platform);
  replacement = Text("CCCC");
  LOKA_VERIFY(replacement.applyPropsToNode(text));
  root->markViewDirty(NODE_DIRTY_PROPS);
  settle(scene);
  LOKA_VERIFY(find(root, NODE_KIND_TEXT) == text && text->getContext() == context);
  LOKA_VERIFY(text->asTextNode()->props.ownsText);
  exactOne(platform);
  platform.beginApplyCycle();
  score(scene, data.b, "FFFF");
  LOKA_VERIFY(platform.count == 0);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testCapacityOverflowWidens()
{
  for (int count = 8; count <= 9; ++count)
  {
    TreeData data;
    data.count = 9;
    PaintPlatform platform;
    Scene scene(Boundary<PaintTree>(TreeProps(&data)));
    mount(scene, platform);
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      for (int i = 0; i < count; ++i)
        data.models[i].set(sprite(3));
    }
    settle(scene);
    if (count == 8)
      LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_EXACT && only(platform).exactCount() == 8);
    else
    {
      LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_WIDENED && only(platform).exactCount() == 0);
      LOKA_VERIFY(only(platform).widenReason() == APPLY_PAINT_WIDEN_CAPACITY);
    }
    LOKA_VERIFY(platform.queries == 9 && platform.commits == 9);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
}
void testPlanHoldsNoPointersAndIsVisitLocal()
{
  ApplyPaintPlan observation;
  {
    TreeData data;
    PaintPlatform platform;
    Scene scene(Boundary<PaintTree>(TreeProps(&data)));
    mount(scene, platform);
    change(scene, data.models[0], 3);
    observation = only(platform);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
  {
    TreeData data;
    PaintPlatform platform;
    Scene scene(Boundary<PaintTree>(TreeProps(&data)));
    mount(scene, platform);
    change(scene, data.models[0], 9);
    LOKA_VERIFY(only(platform).entry(0).x == 9);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
  // Source-reviewed types contain enums, integer coordinates and fixed value arrays only.
  LOKA_VERIFY(observation.precision() == APPLY_PAINT_EXACT && observation.entry(0).width == 8);
  LOKA_VERIFY(observation.entry(0).scope.ownerKey == 1);
}
void testPaintLifecycleAndScope()
{
  TreeData data;
  data.scroll = true;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 3);
  refused(platform, PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(platform.commits == 0);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  class Census : public IPaintResidentVisitor
  {
  public:
    Census()
        : count(0)
    {
    }
    unsigned count;
    virtual void visit(Node *, NodeContext *, BoundaryNode *)
    {
      ++count;
    }
  } census;
  PaintTree unattached((TreeProps(&data)));
  NotifySubtreeNodeDetached(&unattached);
  enumerateAttachedResidents(&unattached, census);
  LOKA_VERIFY(census.count == 0);
  class ParkedRoot : public PaintTree
  {
  public:
    explicit ParkedRoot(const TreeProps &props)
        : PaintTree(props)
    {
    }
    virtual Node *retainedLifecycleBranch(unsigned index)
    {
      return index == 0 ? &this->parked_ : 0;
    }

  private:
    Node parked_;
  } parked((TreeProps(&data)));
  enumerateAttachedResidents(&parked, census);
  LOKA_VERIFY(census.count == 1); // Only the root, even before a parked fact is delivered.

  class UnsupportedRoot : public PaintTree, public IProjectedLayoutNode
  {
  public:
    explicit UnsupportedRoot(const TreeProps &props)
        : PaintTree(props)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      return state.y;
    }
  } unsupported((TreeProps(&data)));
  platform.beginApplyCycle();
  BoundaryLocalApplyInfo info;
  info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
  PlatformApplyPlan plan;
  plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
  plan.setPrimaryRoot(&unsupported);
  platform.onBoundaryApply(&unsupported, &unsupported, info, plan);
  refused(platform, PAINT_REFUSED_UNSUPPORTED_KIND);
}

void testPaintPolicyScopeAndLifecycleInvalidation()
{
  floppybird::SharedModel model;
  PaintPlatform platform;
  Scene scene(Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
  mount(scene, platform);
  BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
  RectSurfaceNode *surface = find(root, NODE_KIND_RECT_SURFACE)->asRectSurfaceNode();
  TextNode *text = find(root, NODE_KIND_TEXT)->asTextNode();
  NativeNodeContext *surfaceContext = static_cast<NativeNodeContext *>(surface->getContext());
  NativeNodeContext *textContext = static_cast<NativeNodeContext *>(text->getContext());
  change(scene, model.surfaceModel_, 4);
  PaintQuery q = query(platform);
  ++q.scope.ownerKey;
  LOKA_VERIFY(surfaceContext->queryPaintDamage(q).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(textContext->queryPaintDamage(q).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  const PaintAnswer ground = surfaceContext->queryPaintDamage(query(platform));
  LOKA_VERIFY(ground.kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(ground.damage.coverage == PAINT_COVERAGE_PAINT_ONLY);
  LOKA_VERIFY(ground.damage.width == 0 && ground.damage.height == 0);
  RectSurfaceModel hints = model.surfaceModel_.get();
  hints.dirtyRectCount = 1;
  hints.dirtyRects[0] = RectSurfaceModel::DirtyRect(0, 0, 100, 100);
  {
    StateTrackerGuard guard(root->tracker());
    model.surfaceModel_.set(hints);
  }
  settle(scene);
  LOKA_VERIFY(only(platform).precision() == APPLY_PAINT_NONE);
  text->props.textStyle_ = Red;
  LOKA_VERIFY(textContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  text->props.textStyle_ = Bold;
  LOKA_VERIFY(textContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  text->props.textStyle_ = TextStyle();
  NotifySubtreeNodeDetached(root);
  LifecycleFactTestAccess::DeliverFacts(root);
  LOKA_VERIFY(surfaceContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(textContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  class Census : public IPaintResidentVisitor
  {
  public:
    Census()
        : count(0)
    {
    }
    unsigned count;
    virtual void visit(Node *, NodeContext *, BoundaryNode *)
    {
      ++count;
    }
  } census;
  enumerateAttachedResidents(root, census);
  LOKA_VERIFY(census.count == 0);
  NotifySubtreeNodeAttached(root);
  LifecycleFactTestAccess::DeliverFacts(root);
  LOKA_VERIFY(surfaceContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LayoutState layout;
  layout.width = 640;
  layout.height = 600;
  layout.lineHeight = 20;
  platform.projectLayoutForTesting(root, layout);
  LOKA_VERIFY(surfaceContext->queryPaintDamage(query(platform)).kind == PAINT_ANSWER_EXACT);
  LOKA_VERIFY(textContext->queryPaintDamage(query(platform)).kind == PAINT_ANSWER_EXACT);
  LifecycleFactTestAccess::MarkSubtreeRetired(root);
  LifecycleFactTestAccess::DeliverFacts(root);
  LOKA_VERIFY(surfaceContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(textContext->queryPaintDamage(query(platform)).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
void testPaintPlanEmptyCapacityAndScopeValues()
{
  const PaintScope scope = {1, 0, 0, 0, 0, 100, 100};
  PaintScope other = scope;
  LOKA_VERIFY(other == scope);
  ++other.originX;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.originY;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.clipX;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.clipY;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.clipWidth;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.clipHeight;
  LOKA_VERIFY(other != scope);
  other = scope;
  ++other.ownerKey;
  LOKA_VERIFY(other != scope);
  ApplyPaintPlan plan;
  const PaintDamage damage = {scope, 1, 2, 3, 4, PAINT_COVERAGE_ERASE_AND_PAINT};
  for (int i = 0; i < 8; ++i)
    LOKA_VERIFY(plan.addExact(damage));
  PaintDamage empty = damage;
  empty.width = 0;
  LOKA_VERIFY(plan.addExact(empty) && plan.count() == 8);
  LOKA_VERIFY(!plan.addExact(damage));
  plan.widen(APPLY_PAINT_WIDEN_CAPACITY, scope);
  LOKA_VERIFY(!plan.addExact(damage));
  plan.widen(APPLY_PAINT_WIDEN_REFUSED, other, PAINT_REFUSED_NO_CONTEXT);
  LOKA_VERIFY(plan.exactCount() == 0 && plan.count() == 1);
  LOKA_VERIFY(plan.entry(0).scope == scope && plan.widenReason() == APPLY_PAINT_WIDEN_CAPACITY);
}

void testUnrenderableTextNeverBecomesPresented()
{
  // A String whose platform refuses materialization measures as nothing. That
  // measurement must not pass the seat check, and the value must never be
  // committed as presented; otherwise a later identical handle compares equal
  // and answers EXACT with empty damage for pixels that were never drawn.
  floppybird::SharedModel model;
  PaintPlatform platform;
  Scene scene(Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
  mount(scene, platform);
  scoreValue(scene, model.scoreText_, unrenderable());
  refused(platform, PAINT_REFUSED_PLACEMENT_UNSETTLED);
  // The widened reconstruction must not have committed the unrenderable value.
  scoreValue(scene, model.scoreText_, unrenderable());
  refused(platform, PAINT_REFUSED_HISTORY_UNKNOWN);
  // A renderable value recovers through the widened presentation, then compares exactly.
  score(scene, model.scoreText_, "Score: 2");
  refused(platform, PAINT_REFUSED_HISTORY_UNKNOWN);
  score(scene, model.scoreText_, "Score: 3");
  exactOne(platform);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testRefusedReprojectionInvalidatesPlacement()
{
  // A placed surface whose re-projection is refused before its layout() runs
  // must lose its placement and history in that pass: the post-projection
  // completion walk may commit only residents the pass actually placed.
  floppybird::SharedModel model;
  PaintPlatform platform;
  Scene scene(Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
  mount(scene, platform);
  change(scene, model.surfaceModel_, 4);
  exactOne(platform);
  platform.refuseSurfaceProjection = true;
  scene.requestInvalidate(NODE_DIRTY_LAYOUT);
  settle(scene);
  platform.beginApplyCycle();
  change(scene, model.surfaceModel_, 6);
  refused(platform, PAINT_REFUSED_PLACEMENT_UNSETTLED);
  LOKA_VERIFY(platform.commits == 1); // the Text was placed and reconstructed; the surface was not
  platform.refuseSurfaceProjection = false;
  scene.requestInvalidate(NODE_DIRTY_LAYOUT);
  settle(scene);
  platform.beginApplyCycle();
  change(scene, model.surfaceModel_, 8);
  exactOne(platform);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

static void checkStyleOnlyApplyRecoversTextHistory(const TextStyle &style)
{
  // A retained Text whose resolved style changes through a props-only apply has
  // no layout pass to refresh its placed style. The query refuses (widen), and
  // the widened presentation must adopt the current style and re-establish the
  // history, instead of refusing forever until an unrelated layout.
  TreeData data;
  data.count = 0;
  TextDefinitionWithAttr replacement = Text(&data.a) + style;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
  Node *text = find(root, NODE_KIND_TEXT);
  LOKA_VERIFY(replacement.applyPropsToNode(text));
  root->markViewDirty(NODE_DIRTY_PROPS);
  settle(scene);
  LOKA_VERIFY(find(root, NODE_KIND_TEXT) == text);
  refused(platform, PAINT_REFUSED_PLACEMENT_UNSETTLED);
  platform.beginApplyCycle();
  score(scene, data.a, "GGGG");
  exactOne(platform);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testStyleOnlyApplyRecoversTextHistory()
{
  checkStyleOnlyApplyRecoversTextHistory(Bold);
}

void testColorOnlyApplyRecoversTextHistory()
{
  checkStyleOnlyApplyRecoversTextHistory(Red);
}

namespace
{
  /** Inspects borrowed residents synchronously; only completed counts escape. */
  class AnswerPlatform : public PaintPlatform
  {
  public:
    enum Expectation
    {
      IGNORE,
      ONE_DAMAGE,
      ONE_REFUSAL,
      TWO_DAMAGE
    };
    AnswerPlatform()
        : expectation(IGNORE),
          inspections(0)
    {
    }
    virtual void onPaintAnswersCollected(BoundaryNode *owner,
                                         const BoundaryLocalApplyInfo &info,
                                         const PaintAnswerBuffer<> &answers,
                                         const PaintApplyVerdict &verdict)
    {
      if (expectation == IGNORE)
        return;
      ++inspections;
      LOKA_VERIFY(!info.hasLayoutWork && !info.hasStructureWork);
      LOKA_VERIFY(verdict.nativeScheduledCount() == 0);
      LOKA_VERIFY(verdict.overflowCount() == 0);
      if (expectation == ONE_REFUSAL)
      {
        LOKA_VERIFY(!verdict.canSkipBroadPaint(info));
        LOKA_VERIFY(verdict.refusedCount() == 1);
        LOKA_VERIFY(verdict.exactCount() == 1);
        return;
      }
      LOKA_VERIFY(verdict.canSkipBroadPaint(info));
      LOKA_VERIFY(verdict.exactCount() == 2);
      LOKA_VERIFY(verdict.refusedCount() == 0);
      LOKA_VERIFY(answers.count() == (expectation == ONE_DAMAGE ? 1u : 2u));
      unsigned index = 0;
      Node *written = find(owner, NODE_KIND_RECT_SURFACE, index);
      LOKA_VERIFY(answers.entry(0).resident == written);
      const PaintDamage &damage = answers.entry(0).damage;
      const PaintAnswer direct =
          static_cast<NativeNodeContext *>(written->getContext())->queryPaintDamage(query(*this));
      LOKA_VERIFY(direct.kind == PAINT_ANSWER_EXACT);
      LOKA_VERIFY(damage.scope == direct.damage.scope);
      LOKA_VERIFY(damage.x == direct.damage.x && damage.y == direct.damage.y);
      LOKA_VERIFY(damage.width == direct.damage.width && damage.height == direct.damage.height);
      LOKA_VERIFY(damage.coverage == direct.damage.coverage);
      BoundaryLocalApplyInfo composited = info;
      composited.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
      LOKA_VERIFY(!verdict.canSkipBroadPaint(composited));
      BoundaryLocalApplyInfo layout = info;
      layout.hasLayoutWork = true;
      LOKA_VERIFY(!verdict.canSkipBroadPaint(layout));
      if (expectation == TWO_DAMAGE)
      {
        PaintAnswerBuffer<1> small;
        const PaintApplyVerdict overflow = CollectPaintAnswers(*owner, query(*this), small, *this);
        LOKA_VERIFY(!overflow.canSkipBroadPaint(info));
        LOKA_VERIFY(overflow.exactCount() == 2);
        LOKA_VERIFY(overflow.refusedCount() == 0);
        LOKA_VERIFY(overflow.overflowCount() == 1);
        LOKA_VERIFY(small.count() == 1);
        const ApplyPaintPlan widened = BuildApplyPaintPlan(query(*this).scope, small, overflow);
        LOKA_VERIFY(widened.precision() == APPLY_PAINT_WIDENED);
        LOKA_VERIFY(widened.widenReason() == APPLY_PAINT_WIDEN_CAPACITY);
        // Nested collection owns its own buffer and must not reset the outer visit.
        LOKA_VERIFY(answers.count() == 2);
        unsigned siblingIndex = 1;
        LOKA_VERIFY(answers.entry(1).resident == find(owner, NODE_KIND_RECT_SURFACE, siblingIndex));
      }
    }
    Expectation expectation;
    unsigned inspections;
  };
} // namespace

void testPaintAnswersHandOverWrittenResident()
{
  TreeData data;
  data.count = 2;
  AnswerPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 2); // Establish presentation history for both drawers.
  platform.expectation = AnswerPlatform::ONE_DAMAGE;
  change(scene, data.models[0], 4);
  LOKA_VERIFY(platform.inspections == 1);
  SceneTestAccess::unmount(scene);
}

void testPaintAnswersRefusalDisablesGate()
{
  TreeData data;
  data.count = 2;
  AnswerPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 2);
  unsigned index = 1;
  Node *sibling = find(SceneTestAccess::rootBoundary(scene), NODE_KIND_RECT_SURFACE, index);
  LOKA_VERIFY(sibling && sibling->getContext());
  platform.forget(*static_cast<NullRectSurfaceContext *>(sibling->getContext()));
  platform.expectation = AnswerPlatform::ONE_REFUSAL;
  change(scene, data.models[0], 4);
  LOKA_VERIFY(platform.inspections == 1);
  refused(platform, PAINT_REFUSED_HISTORY_UNKNOWN);
  SceneTestAccess::unmount(scene);
}

void testPaintAnswersCapacityDisablesGate()
{
  TreeData data;
  data.count = 2;
  AnswerPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  change(scene, data.models[0], 2);
  platform.expectation = AnswerPlatform::TWO_DAMAGE;
  {
    StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    data.models[0].set(sprite(4));
    data.models[1].set(sprite(6));
  }
  settle(scene);
  LOKA_VERIFY(platform.inspections == 1);
  SceneTestAccess::unmount(scene);
}

namespace
{
  /** Alters completed answers, while using real attached Null drawers. */
  class AnswerPolicyProbe
  {
  public:
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
    enum Mode
    {
      EMPTY,
      NATIVE,
      WRONG_SCOPE,
      REFUSE
    };
    AnswerPolicyProbe(Mode mode, PaintRefusalReason reason = PAINT_REFUSED_UNSUPPORTED_KIND)
        : mode_(mode),
          reason_(reason)
    {
    }
    bool queryPaintAnswer(Node *node, NodeContext *, const PaintQuery &q, PaintAnswer &answer)
    {
      if (!node->asRectSurfaceNode())
        return false;
      const PaintDamage empty = {q.scope, 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
      switch (this->mode_)
      {
      case EMPTY:
        answer = PaintAnswer::exact(empty);
        break;
      case NATIVE:
        answer = PaintAnswer::nativeScheduled();
        break;
      case WRONG_SCOPE:
        answer = PaintAnswer::exact(empty);
        ++answer.damage.scope.ownerKey;
        break;
      case REFUSE:
        answer = PaintAnswer::refused(this->reason_);
        break;
      }
      return true;
    }

  private:
    const Mode mode_;
    const PaintRefusalReason reason_;
  };
} // namespace

void testPaintAnswerVerdictCountsAndBufferReuse()
{
  TreeData data;
  data.count = 2;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  BoundaryNode &root = *SceneTestAccess::rootBoundary(scene);
  BoundaryLocalApplyInfo info;
  info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
  const PaintQuery q = query(platform);
  PaintAnswerBuffer<1> answers;
  const PaintDamage stale = {q.scope, 0, 0, 1, 1, PAINT_COVERAGE_PAINT_ONLY};
  LOKA_VERIFY(answers.append(&root, stale));
  AnswerPolicyProbe empty(AnswerPolicyProbe::EMPTY);
  const PaintApplyVerdict unchanged = CollectPaintAnswers(root, q, answers, empty);
  LOKA_VERIFY(unchanged.canSkipBroadPaint(info));
  LOKA_VERIFY(unchanged.exactCount() == 2 && unchanged.nativeScheduledCount() == 0);
  LOKA_VERIFY(unchanged.refusedCount() == 0 && unchanged.overflowCount() == 0);
  LOKA_VERIFY(answers.count() == 0);
  LOKA_VERIFY(BuildApplyPaintPlan(q.scope, answers, unchanged).precision() == APPLY_PAINT_NONE);
  AnswerPolicyProbe native(AnswerPolicyProbe::NATIVE);
  const PaintApplyVerdict scheduled = CollectPaintAnswers(root, q, answers, native);
  LOKA_VERIFY(scheduled.canSkipBroadPaint(info));
  LOKA_VERIFY(scheduled.exactCount() == 0 && scheduled.nativeScheduledCount() == 2);
  LOKA_VERIFY(scheduled.refusedCount() == 0 && scheduled.overflowCount() == 0);
  LOKA_VERIFY(answers.count() == 0);
  BoundaryLocalApplyInfo composited = info;
  composited.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
  LOKA_VERIFY(!scheduled.canSkipBroadPaint(composited));
  BoundaryLocalApplyInfo structure = info;
  structure.hasStructureWork = true;
  LOKA_VERIFY(!scheduled.canSkipBroadPaint(structure));
  info.paintKind = LOCAL_APPLY_PAINT_NONE;
  LOKA_VERIFY(!scheduled.canSkipBroadPaint(info));
  info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
  AnswerPolicyProbe wrongScope(AnswerPolicyProbe::WRONG_SCOPE);
  const PaintApplyVerdict mismatch = CollectPaintAnswers(root, q, answers, wrongScope);
  LOKA_VERIFY(!mismatch.canSkipBroadPaint(info));
  LOKA_VERIFY(mismatch.exactCount() == 0 && mismatch.refusedCount() == 2);
  LOKA_VERIFY(mismatch.refusalReason() == PAINT_REFUSED_PLACEMENT_UNSETTLED);
  const PaintRefusalReason reasons[] = {PAINT_REFUSED_UNSUPPORTED_KIND,
                                        PAINT_REFUSED_NO_CONTEXT,
                                        PAINT_REFUSED_PLACEMENT_UNSETTLED,
                                        PAINT_REFUSED_HISTORY_UNKNOWN,
                                        PAINT_REFUSED_PROPS_UNRECONCILED};
  for (unsigned i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
  {
    AnswerPolicyProbe refusal(AnswerPolicyProbe::REFUSE, reasons[i]);
    const PaintApplyVerdict refusedAnswer = CollectPaintAnswers(root, q, answers, refusal);
    LOKA_VERIFY(!refusedAnswer.canSkipBroadPaint(info));
    LOKA_VERIFY(refusedAnswer.refusedCount() == 2 && refusedAnswer.overflowCount() == 0);
    LOKA_VERIFY(refusedAnswer.widenReason() == APPLY_PAINT_WIDEN_REFUSED);
    LOKA_VERIFY(refusedAnswer.refusalReason() == reasons[i]);
  }
  SceneTestAccess::unmount(scene);
}

namespace
{
  class AnswerSequence
  {
  public:
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
    AnswerSequence(const PaintAnswer *answers)
        : answers_(answers),
          index_(0)
    {
    }
    bool queryPaintAnswer(Node *node, NodeContext *, const PaintQuery &, PaintAnswer &answer)
    {
      if (!node->asRectSurfaceNode())
        return false;
      LOKA_VERIFY(this->index_ < 4);
      answer = this->answers_[this->index_++];
      return true;
    }

  private:
    const PaintAnswer *answers_;
    unsigned index_;
  };
} // namespace

void testPaintAnswerFirstWidenReasonSurvivesLaterAnswers()
{
  TreeData data;
  data.count = 4;
  PaintPlatform platform;
  Scene scene(Boundary<PaintTree>(TreeProps(&data)));
  mount(scene, platform);
  const PaintQuery q = query(platform);
  const PaintDamage damage = {q.scope, 1, 2, 3, 4, PAINT_COVERAGE_ERASE_AND_PAINT};
  for (int overflowFirst = 0; overflowFirst < 2; ++overflowFirst)
  {
    const PaintAnswer answer[] = {
        PaintAnswer::exact(damage),
        overflowFirst ? PaintAnswer::exact(damage) : PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN),
        overflowFirst ? PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN) : PaintAnswer::exact(damage),
        PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT)};
    AnswerSequence source(answer);
    PaintAnswerBuffer<1> buffer;
    const PaintApplyVerdict verdict = CollectPaintAnswers(*SceneTestAccess::rootBoundary(scene), q, buffer, source);
    LOKA_VERIFY(verdict.exactCount() == 2 && verdict.refusedCount() == 2);
    LOKA_VERIFY(verdict.overflowCount() == 1 && buffer.count() == 1);
    LOKA_VERIFY(verdict.widenReason() == (overflowFirst ? APPLY_PAINT_WIDEN_CAPACITY : APPLY_PAINT_WIDEN_REFUSED));
    if (!overflowFirst)
      LOKA_VERIFY(verdict.refusalReason() == PAINT_REFUSED_HISTORY_UNKNOWN);
    const ApplyPaintPlan plan = BuildApplyPaintPlan(q.scope, buffer, verdict);
    LOKA_VERIFY(plan.precision() == APPLY_PAINT_WIDENED);
    LOKA_VERIFY(plan.widenReason() == verdict.widenReason());
    LOKA_VERIFY(plan.refusalReason() == verdict.refusalReason());
  }
  SceneTestAccess::unmount(scene);
}

namespace
{
  /** Synchronous synthetic answers in one conservative, non-erasing destination. */
  class MergingAnswerProbe
  {
  public:
    enum { kMergesExactDamage = 1 };
    PaintAnswer answer;
    bool queryPaintAnswer(Node *, NodeContext *, const PaintQuery &, PaintAnswer &out)
    {
      out = this->answer;
      return true;
    }
  };

  template <unsigned Capacity> void mergingAnswerContract()
  {
    const PaintScope scope = {1, 0, 0, 0, 0, 1000, 1000};
    const PaintQuery query = {scope, PLACEMENT_ELIGIBLE};
    BoundaryLocalApplyInfo info;
    info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
    for (unsigned mode = 0; mode < 4; ++mode)
    {
      PaintAnswerBuffer<Capacity> buffer;
      MergingAnswerProbe source;
      paint_detail::AnswerCollector<Capacity, MergingAnswerProbe> collector(query, buffer, source);
      if (mode == 1)
      {
        source.answer = PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
        collector.visit(0, 0, 0);
      }
      for (unsigned i = 0; i < 12; ++i)
      {
        const PaintDamage damage = {scope, static_cast<int>(i * 10), 0, 10, 10,
                                    PAINT_COVERAGE_PAINT_ONLY};
        source.answer = PaintAnswer::exact(damage);
        collector.visit(0, 0, 0);
      }
      if (mode == 1 || mode == 2)
      {
        source.answer = PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        collector.visit(0, 0, 0);
      }
      if (mode == 3)
      {
        const PaintDamage empty = {scope, 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
        source.answer = PaintAnswer::exact(empty);
        ++source.answer.damage.scope.ownerKey;
        collector.visit(0, 0, 0);
      }
      const PaintApplyVerdict verdict = collector.verdict();
      LOKA_VERIFY(verdict.exactCount() == 12 && verdict.overflowCount() == 0);
      LOKA_VERIFY(buffer.count() == Capacity);
      LOKA_VERIFY(verdict.widened() == (mode != 0));
      LOKA_VERIFY(verdict.canSkipBroadPaint(info) == (mode == 0));
      LOKA_VERIFY(verdict.refusedCount() == (mode == 0 ? 0u : mode == 1 ? 2u : 1u));
      if (mode != 0)
      {
        LOKA_VERIFY(verdict.widenReason() == APPLY_PAINT_WIDEN_REFUSED);
        LOKA_VERIFY(verdict.refusalReason() == (mode == 1 ? PAINT_REFUSED_HISTORY_UNKNOWN
                    : mode == 2 ? PAINT_REFUSED_NO_CONTEXT : PAINT_REFUSED_PLACEMENT_UNSETTLED));
      }
      for (unsigned i = 0; i < 12; ++i)
      {
        bool covered = false;
        for (unsigned j = 0; j < buffer.count(); ++j)
        {
          const PaintDamage &d = buffer.entry(j).damage;
          if (d.x <= static_cast<int>(i * 10) && d.x + d.width >= static_cast<int>((i + 1) * 10)
              && d.y <= 0 && d.y + d.height >= 10)
            covered = true;
        }
        LOKA_VERIFY(covered);
      }
    }
  }
} // namespace

void testMergingPaintAnswersKeepCoverageAndRefusals()
{
  mergingAnswerContract<8>();
  mergingAnswerContract<1>();
}

void testMergingPaintAnswersStrengthenCoverage()
{
  const PaintScope scope = {1, 0, 0, 0, 0, 100, 100};
  Node first;
  Node incoming;
  for (unsigned reverse = 0; reverse < 2; ++reverse)
  {
    PaintAnswerBuffer<1> buffer;
    const PaintDamage a = {scope, 0, 0, 10, 10,
                          reverse ? PAINT_COVERAGE_ERASE_AND_PAINT : PAINT_COVERAGE_PAINT_ONLY};
    const PaintDamage b = {scope, 10, 0, 10, 10,
                          reverse ? PAINT_COVERAGE_PAINT_ONLY : PAINT_COVERAGE_ERASE_AND_PAINT};
    LOKA_VERIFY(buffer.append(&first, a));
    buffer.mergeExactDamage(&incoming, b);
    LOKA_VERIFY(buffer.count() == 1 && buffer.entry(0).resident == &first);
    LOKA_VERIFY(buffer.entry(0).damage.coverage == PAINT_COVERAGE_ERASE_AND_PAINT);
  }
}
