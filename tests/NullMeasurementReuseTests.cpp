#include "NullMeasurementReuseTests.hpp"
#include "support/NullLayoutRefusal.hpp"
#include "testing/scene/NodeObservedUsesTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/LokaAllocFailure.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::app::testing::NullTextMeasurementAccess;
  using loka::dsl::testing::SceneTestAccess;

  template <class Leaf, class Context>
  void reusePins(Leaf &node, Context &context, NullScenePlatformController &platform)
  {
    LayoutState state;
    state.width = 100;
    NotifySubtreeNodeAttached(&node);
    LOKA_VERIFY(node.layout(&platform, state) == 12);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 1);
    LOKA_VERIFY(context.measurement().width() == 16);
    LOKA_VERIFY(context.commitPresented(node.props.text_->get(), platform.paintScope()));
    state.x = 7;
    state.y = 41;
    state.spacing = 3;
    state.height = 999;
    state.lineHeight = 87; // Null uses font metrics, never this Toolbox fallback input.
    LOKA_VERIFY(node.layout(&platform, state) == 56);
    LOKA_VERIFY(state.height == 12);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 1);
    const PaintQuery query = {platform.paintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    LOKA_VERIFY(context.commitPresented(node.props.text_->get(), platform.paintScope()));
    const PaintAnswer moved = context.queryPaintDamage(query);
    LOKA_VERIFY(moved.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(moved.damage.x == 7 && moved.damage.y == 41);
    state.width = 90;
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 2);
    state.width = 0;
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 3);
    LOKA_VERIFY(context.measurement().width() == 16);
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 3);
    state.width = 16; // Intrinsic result is not its original zero constraint.
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 4);
    state.width = 80;
    NullTextMeasurementAccess::failMeasure(1);
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 5);
    LOKA_VERIFY(!context.measurement().reusable(80));
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 6);
    LOKA_VERIFY(context.measurement().width() == 16);
    node.layout(&platform, state);
    LOKA_VERIFY(NullTextMeasurementAccess::builds(context) == 6);
    LifecycleFactTestAccess::MarkSubtreeRetired(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(!context.measurement().reusable(80));
  }

  class Owner : public BoundaryNodeFor<Owner>
  {
  public:
    explicit Owner(const BoundaryPropsFor<Owner> &props = BoundaryPropsFor<Owner>())
        : BoundaryNodeFor<Owner>(props)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
} // namespace

void testNullPlainMeasurementReuse()
{
  NullScenePlatformController platform;
  TextNode node((TextProps("abcd")));
  NullTextContext *context = new NullTextContext(&node);
  node.setContext(context);
  reusePins(node, *context, platform);
}

void testNullAttributedMeasurementReuse()
{
  NullScenePlatformController platform;
  AttributedTextNode node(AttributedText(Styled("abcd", TextStyle())).props);
  NullAttributedTextContext *context = new NullAttributedTextContext(&node, platform);
  node.setContext(context);
  reusePins(node, *context, platform);
}

void testNullMeasurementInputPublications()
{
  NullScenePlatformController platform;
  MutableState<String> content(String::Literal("aa"));
  MutableState<TextStyle> style((TextStyle()));
  MutableState<AttributedString> rich(Styled("aa", TextStyle()));
  PushStateTracker tracker;
  tracker.addState(&content);
  tracker.addState(&style);
  tracker.addState(&rich);
  Owner owner;
  TextNode plain((Text(&content) + &style).props);
  AttributedTextNode attributed(AttributedText(&rich).props);
  plain.setPropsTypeId(TextProps::staticTypeId());
  attributed.setPropsTypeId(AttributedTextProps::staticTypeId());
  NullTextContext *p = new NullTextContext(&plain);
  NullAttributedTextContext *a = new NullAttributedTextContext(&attributed, platform);
  plain.setContext(p);
  attributed.setContext(a);
  BoundaryNode::declareBoundaryDirtySources(&plain, &owner);
  BoundaryNode::declareBoundaryDirtySources(&attributed, &owner);
  LayoutState state;
  state.width = 100;
  plain.layout(&platform, state);
  attributed.layout(&platform, state);
  {
    StateTrackerGuard guard(&tracker);
    content.set(String::Literal("aaaa"));
    rich.set(Styled("aaaa", TextStyle()));
  }
  plain.layout(&platform, state);
  attributed.layout(&platform, state);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 2);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*a) == 2);
  LOKA_VERIFY(p->measurement().width() == 16 && a->measurement().width() == 16);
  {
    StateTrackerGuard guard(&tracker);
    style.set(SizeOf(24));
    rich.set(Styled("aaaa", SizeOf(24)));
  }
  plain.layout(&platform, state);
  attributed.layout(&platform, state);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 3);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*a) == 3);
  LOKA_VERIFY(p->measurement().height() == 24 && a->measurement().height() == 24);
  LOKA_VERIFY(((Text(&content) + &style) + BlockStyle().align(TEXT_ALIGN_RIGHT)).applyPropsToNode(&plain));
  LOKA_VERIFY((AttributedText(&rich) + BlockStyle().align(TEXT_ALIGN_RIGHT)).applyPropsToNode(&attributed));
  plain.layout(&platform, state);
  attributed.layout(&platform, state);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 4);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*a) == 4);
  LOKA_VERIFY(NullTextMeasurementAccess::line(p->measurement(), 0).x == 68);
  LOKA_VERIFY(NullTextMeasurementAccess::line(a->measurement(), 0).x == 68);
  plain.layout(&platform, state);
  attributed.layout(&platform, state);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 4 && NullTextMeasurementAccess::builds(*a) == 4);
  {
    StateTrackerGuard guard(&tracker);
    style.set(SizeOf(24) + Red);
  }
  plain.layout(&platform, state);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 5);
  std::fprintf(
      stderr, "color-only style publication: measurement builds 4 -> %u\n", NullTextMeasurementAccess::builds(*p));
  owner.clearObservedStateEntries();
}

namespace
{
  class ParkedRoot : public BoundaryNodeFor<ParkedRoot>
  {
  public:
    NodeState<bool> shown;
    NodeState<String> text;
    NodeState<AttributedString> rich;
    explicit ParkedRoot(const BoundaryPropsFor<ParkedRoot> &props)
        : BoundaryNodeFor<ParkedRoot>(props)
    {
      this->state(this->shown, true);
      this->state(this->text, String::Literal("aa"));
      this->state(this->rich, Styled("aa", TextStyle()));
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Show(*this->shown.state())
                          << (Column() << (Text(this->text.state()) + BlockStyle().wrap(TEXT_WRAP_WORD)).testId("plain")
                                       << AttributedText(this->rich.state()).testId("rich")));
    }
  };
} // namespace

void testNullMeasurementParkedReattach()
{
  NullScenePlatformController platform;
  Scene scene((Boundary<ParkedRoot>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  ParkedRoot *root = static_cast<ParkedRoot *>(SceneTestAccess::rootBoundary(scene));
  TextNode *plain = 0;
  Node *rich = 0;
  loka::dsl::FlowError error;
  loka::dsl::testing::LookupNodeById(&scene, "plain", plain, error);
  loka::dsl::testing::LookupNodeById(&scene, "rich", rich, error);
  LOKA_VERIFY(plain && rich);
  NullTextContext *p = static_cast<NullTextContext *>(plain->getContext());
  NullAttributedTextContext *a = static_cast<NullAttributedTextContext *>(rich->getContext());
  LOKA_VERIFY(p && a);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 1 && NullTextMeasurementAccess::builds(*a) == 1);
  {
    StateTrackerGuard guard(root->tracker());
    root->shown.set(false);
  }
  scene.flushInvalidation();
  LOKA_VERIFY(plain->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(rich->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(p->measurement().width() == 8 && a->measurement().width() == 8);
  LOKA_VERIFY(p->measurement().reusable(100) && a->measurement().reusable(100));
  {
    StateTrackerGuard guard(root->tracker());
    root->text.set(String::Literal("aaaa"));
    root->rich.set(Styled("aaaa", TextStyle()));
  }
  scene.flushInvalidation();
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 1 && NullTextMeasurementAccess::builds(*a) == 1);
  {
    StateTrackerGuard guard(root->tracker());
    root->shown.set(true);
  }
  scene.flushInvalidation();
  LOKA_VERIFY(plain->getContext() == p && rich->getContext() == a);
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 2 && NullTextMeasurementAccess::builds(*a) == 2);
  LOKA_VERIFY(p->measurement().width() == 16 && a->measurement().width() == 16);
  scene.requestInvalidate(NODE_DIRTY_LAYOUT);
  scene.flushInvalidation();
  LOKA_VERIFY(NullTextMeasurementAccess::builds(*p) == 2 && NullTextMeasurementAccess::builds(*a) == 2);
  SceneTestAccess::unmount(scene);
}

void testSyntheticMeasurementRefusal()
{
  const String longText = String::Literal("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
  loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
  {
    const SyntheticTextWidthSource source(longText, TextStyle());
    const TextLineBreaker result(source, BlockStyle(), 100);
    Frame out(1, 2, 3, 4);
    LOKA_VERIFY(!SyntheticTextExtent(result, BlockStyle(), 100, out));
    const NullTextMeasurement refused(result, BlockStyle(), 100);
    LOKA_VERIFY(!refused.reusable(100));
    LOKA_VERIFY(out.x == 1 && out.y == 2 && out.width == 3 && out.height == 4);
  }
  loka::core::testing::allowLokaAllocRaw();
  LayoutState state;
  state.width = 100;
  NullTextMeasurement out;
  loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
  LOKA_VERIFY(!MeasureNullText(TextStyle(), BlockStyle(), &longText, state, out));
  LOKA_VERIFY(!out.reusable(100));
  loka::core::testing::allowLokaAllocRaw();
  LOKA_VERIFY(MeasureNullText(TextStyle(), BlockStyle(), &longText, state, out));
  LOKA_VERIFY(out.reusable(100));
  out.invalidate();
  LOKA_VERIFY(!out.reusable(100));
}

void testNullMeasurementRefusalRetry()
{
  using loka::app::testing::NodeObservedUsesTestAccess;
  for (int attributed = 0; attributed != 2; ++attributed)
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<ParkedRoot>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    ParkedRoot *root = static_cast<ParkedRoot *>(SceneTestAccess::rootBoundary(scene));
    Node *leaf = 0;
    loka::dsl::FlowError error;
    loka::dsl::testing::LookupNodeById(&scene, attributed ? "rich" : "plain", leaf, error);
    LOKA_VERIFY(leaf);
    NullTextContext *plain = attributed ? 0 : static_cast<NullTextContext *>(leaf->getContext());
    NullAttributedTextContext *rich = attributed ? static_cast<NullAttributedTextContext *>(leaf->getContext()) : 0;
    {
      StateTrackerGuard guard(root->tracker());
      if (attributed) root->rich.set(Styled("aaaa", TextStyle()));
      else root->text.set(String::Literal("aaaa"));
    }
    const NodeDirtyFlags consumed = NodeObservedUsesTestAccess::mark(*leaf);
    LOKA_VERIFY(consumed != NODE_DIRTY_NONE);
    LOKA_VERIFY(!(consumed & NODE_DIRTY_INITIAL));
    loka::testing::failNullTextMeasurements(3, leaf);
    for (unsigned attempt = 0; attempt != 3; ++attempt)
    {
      scene.flushInvalidation();
      const unsigned builds = attributed ? NullTextMeasurementAccess::builds(*rich) : NullTextMeasurementAccess::builds(*plain);
      std::fprintf(stderr, "refusal attributed=%d flush=%u builds=%u mark=%u pending=%d\n", attributed, attempt + 1, builds, static_cast<unsigned>(NodeObservedUsesTestAccess::mark(*leaf)), scene.hasPendingInvalidation());
      LOKA_VERIFY(builds == attempt + 2);
      const NodeDirtyFlags pending = NodeObservedUsesTestAccess::mark(*leaf);
      LOKA_VERIFY((pending & consumed) == consumed);
      LOKA_VERIFY(pending & NODE_DIRTY_INITIAL);
      LOKA_VERIFY(scene.hasPendingInvalidation());
    }
    loka::testing::failNullTextMeasurements(0);
    scene.flushInvalidation();
    LOKA_VERIFY((attributed ? rich->measurement().width() : plain->measurement().width()) == 16);
    LOKA_VERIFY((attributed ? NullTextMeasurementAccess::builds(*rich) : NullTextMeasurementAccess::builds(*plain)) == 5);
    LOKA_VERIFY(NodeObservedUsesTestAccess::mark(*leaf) == NODE_DIRTY_NONE);
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    scene.flushInvalidation();
    LOKA_VERIFY((attributed ? NullTextMeasurementAccess::builds(*rich) : NullTextMeasurementAccess::builds(*plain)) == 5);
    SceneTestAccess::unmount(scene);
  }
}
