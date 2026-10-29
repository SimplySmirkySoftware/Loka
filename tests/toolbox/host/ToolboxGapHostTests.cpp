#include "context/ToolboxClipBoxContext.hpp"
#include "context/ToolboxCardBandContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPaintDrawerType.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "debug/ToolboxSceneDebugStats.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <new>
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include <cstdio>
#include <cstring>

namespace loka
{
  namespace testing
  {
    void failToolboxClipBoxAllocations(unsigned count);
    unsigned toolboxClipBoxAllocationAttempts();
    void watchToolboxClipBoxDeletion(void *context);
    unsigned toolboxClipBoxDeletes();
  } // namespace testing
} // namespace loka

namespace
{
#include "ToolboxPaintAnswers.hpp"

  using namespace loka::app;
  using namespace loka::app::scene;

  // Native drawing is outside these pins; Text/Button keep their real layout
  // classification and natural measurement while recording offered seats.
  short placeLeaf(IPlatformController *platform, LayoutState &state, LayoutState &offer)
  {
    ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
    if (!controller->projectLayoutState(state))
      return 0;
    offer = state;
    state.y = static_cast<short>(state.y + 7);
    return state.width;
  }

  class GapText : public TextNode
  {
  public:
    LayoutState offer;
    unsigned calls;
    GapText()
        : TextNode(Text("x").props),
          calls(0)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ++this->calls;
      return placeLeaf(platform, state, this->offer);
    }
  };
  class GapButton : public ButtonNode
  {
  public:
    LayoutState offer;
    GapButton()
        : ButtonNode(Button("B").props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      return placeLeaf(platform, state, this->offer);
    }
  };

  struct Fixture
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller;
    explicit Fixture(const WindowProps &props = WindowProps(), bool handlers = true)
        : window(props),
          controller(&window)
    {
      const bool registered = !handlers || RegisterToolboxPlatformLayoutHandlers(this->controller.gapLayoutRegistry);
      LOKA_VERIFY(registered);
    }
    LayoutState seat(short spacing = 31) const
    {
      LayoutState state;
      state.width = 200;
      state.height = 100;
      state.lineHeight = 12;
      state.spacing = spacing;
      return state;
    }
    void layout(Node &node, short spacing = 31)
    {
      LayoutState state = this->seat(spacing);
      LayoutNode(&node, state, &this->controller, 0);
      LOKA_VERIFY(state.spacing == spacing);
    }
  };

  FragmentNode *pair(GapText *&a, GapText *&b)
  {
    FragmentNode *fragment = new FragmentNode(FragmentProps());
    a = new GapText();
    b = new GapText();
    fragment->addChild(a);
    fragment->addChild(b);
    return fragment;
  }

  class ClipChild : public TextNode
  {
  public:
    unsigned layouts;
    unsigned draws;
    const Node &parent;
    const bool expectsContext;
    ClipChild(const Node &owner, bool expected)
        : TextNode(Text("clip child").props),
          layouts(0),
          draws(0),
          parent(owner),
          expectsContext(expected)
    {
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      LOKA_VERIFY((this->parent.getContext() != 0) == this->expectsContext);
      ++this->layouts;
      state.y = static_cast<short>(state.y + 7);
      return 23;
    }
    virtual void render(IPlatformController *)
    {
      ++this->draws;
    }
  };

  // A legal foreign Ribbon handler makes an accidental dispatch observable:
  // it would install a context even though built-in setup reserves only Box/Card.
  class RibbonClipSentinel : public RetainedNodeHandler<RibbonClipSentinel, RibbonGroupNode, ToolboxClipBoxContext>
  {
  public:
    static RibbonGroupNode *cast(Node *node)
    {
      return static_cast<RibbonGroupNode *>(node);
    }
    static ToolboxClipBoxContext *create(RibbonGroupNode *, IPlatformController *, const LayoutState &)
    {
      return new (std::nothrow) ToolboxClipBoxContext();
    }
  };

  void clipRoutingPin(int kind, bool flagged, bool refuse)
  {
    for (int handler = 0; handler != 2; ++handler)
    {
      Fixture f(WindowProps(), handler != 0);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
      RibbonClipSentinel ribbon;
      LOKA_VERIFY(f.controller.registerNodeHandler(&ribbon));
      BoxProps boxProps;
      CardProps cardProps;
      boxProps.childClip_ = cardProps.childClip_ = flagged ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
      NestableNode *box = kind == 0   ? static_cast<NestableNode *>(new BoxNode(boxProps))
                          : kind == 1 ? static_cast<NestableNode *>(new CardNode(cardProps))
                                      : static_cast<NestableNode *>(new RibbonGroupNode(RibbonGroupProps()));
      const bool expected = flagged && kind != 2 && !refuse;
      ClipChild *child = new ClipChild(*box, expected);
      box->addChild(child);
      LOKA_VERIFY((f.controller.gapLayoutRegistry.find(box) != 0) == (handler != 0));
      loka::testing::failToolboxClipBoxAllocations(refuse ? 10 : 0);
      LayoutState seat = f.seat();
      const short width = LayoutNode(box, seat, &f.controller, 0);
      LOKA_VERIFY(width == (kind == 0 ? 23 : 200));
      LOKA_VERIFY(child->layouts == 1 && seat.y > 0);
      LOKA_VERIFY((box->getContext() != 0) == expected);
      LOKA_VERIFY(loka::testing::toolboxClipBoxAllocationAttempts() == (flagged && kind != 2 ? 1u : 0u));
      RenderNode(box, &f.controller);
      LOKA_VERIFY(child->draws == 1);
      LOKA_VERIFY(loka::testing::toolboxClipBoxAllocationAttempts() == (flagged && kind != 2 ? 1u : 0u));
      if (expected)
      {
        ToolboxClipBoxContext *context = static_cast<ToolboxClipBoxContext *>(box->getContext());
        Rect rect = {1, 2, 3, 4};
        LOKA_VERIFY(context->placement().query(rect));
        ToolboxSceneDebugStats stats;
        ToolboxPaintAnswerSource source(stats);
        PaintQuery query;
        PaintAnswer answer;
        LOKA_VERIFY(!source.queryPaintAnswer(box, context, query, answer));
        LayoutNode(box, seat, &f.controller, 0);
        LOKA_VERIFY(box->getContext() == context);
        LOKA_VERIFY(loka::testing::toolboxClipBoxAllocationAttempts() == 1);
      }
      loka::testing::failToolboxClipBoxAllocations(0);
      delete box;
    }
  }

  class ClipKeyProbe : public Node
  {
  public:
    explicit ClipKeyProbe(const void *key)
        : key_(key)
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return this->key_;
    }

  private:
    const void *key_;
  };

  void clipRegistryPin()
  {
    Fixture f;
    RefusedNodeHandler box(NodeTypeToken<BoxNode>()), card(NodeTypeToken<CardNode>());
    LOKA_VERIFY(!f.controller.registerNodeHandler(&box));
    LOKA_VERIFY(!f.controller.registerNodeHandler(&card));
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    const void *keys[] = {NodeTypeToken<BoxNode>(),
                          NodeTypeToken<CardNode>(),
                          NodeTypeToken<AttributedTextNode>(),
                          NodeTypeToken<RibbonGroupEdgeNode>(),
                          NodeTypeToken<CardBandNode>(),
                          NodeTypeToken<ButtonNode>(),
                          NodeTypeToken<TextNode>(),
                          NodeTypeToken<ImageViewNode>(),
                          NodeTypeToken<EditTextNode>(),
                          NodeTypeToken<TextEditorNode>(),
                          NodeTypeToken<PopupMenuNode>(),
                          NodeTypeToken<CellNode>(),
                          NodeTypeToken<ScrollBarNode>(),
                          NodeTypeToken<OpenFileDialogNode>(),
                          NodeTypeToken<ConfirmDialogNode>()};
    IPlatformNodeHandler *before[sizeof(keys) / sizeof(keys[0])];
    for (unsigned i = 0; i != sizeof(keys) / sizeof(keys[0]); ++i)
    {
      ClipKeyProbe node(keys[i]);
      before[i] = f.controller.nodeHandlerRegistry_.find(&node);
      // Native neighbors omitted by this host remain absent on both setups.
      if (i < 5)
        LOKA_VERIFY(before[i]);
    }
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    LOKA_VERIFY(!f.controller.registerNodeHandler(&box));
    LOKA_VERIFY(!f.controller.registerNodeHandler(&card));
    LOKA_VERIFY(!IsToolboxPaintDrawerType(keys[0]) && !IsToolboxPaintDrawerType(keys[1]));
    for (unsigned i = 0; i != sizeof(keys) / sizeof(keys[0]); ++i)
    {
      ClipKeyProbe node(keys[i]);
      LOKA_VERIFY(f.controller.nodeHandlerRegistry_.find(&node) == before[i]);
    }
  }

  void clipLifecyclePin()
  {
    for (int kind = 0; kind != 2; ++kind)
    {
      Fixture f;
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
      NestableNode *node = kind == 0 ? static_cast<NestableNode *>(new BoxNode(Box().clipChildren().props))
                                     : static_cast<NestableNode *>(new CardNode(Card().clipChildren().props));
      f.layout(*node);
      NodeContext *context = node->getContext();
      LOKA_VERIFY(context && context->owner() == node);
      loka::testing::watchToolboxClipBoxDeletion(context);
      NotifySubtreeNodeDetached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      LOKA_VERIFY(node->getContext() == context);
      LOKA_VERIFY(context->deliveredFact() == NODE_FACT_DETACHED_RETAINED);
      LOKA_VERIFY(loka::testing::toolboxClipBoxDeletes() == 0);
      NotifySubtreeNodeAttached(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      f.layout(*node);
      LOKA_VERIFY(node->getContext() == context);
      LifecycleFactTestAccess::MarkSubtreeRetired(node);
      f.controller.releaseNodeContexts(node);
      LOKA_VERIFY(!node->getContext());
      LOKA_VERIFY(loka::testing::toolboxClipBoxDeletes() == 1);
      LOKA_VERIFY(f.controller.retired.empty());
      delete node;
      LOKA_VERIFY(loka::testing::toolboxClipBoxDeletes() == 1);
    }
  }

  void clipPlacementPin()
  {
    ToolboxClipPlacement none;
    Rect out = {1, 2, 3, 4};
    LOKA_VERIFY(!none.query(out));
    LOKA_VERIFY(out.top == 1 && out.left == 2 && out.bottom == 3 && out.right == 4);
    ToolboxClipBoxContext context;
    NodeContext base;
    LOKA_VERIFY(base.asClipBoxContext() == 0);
    LOKA_VERIFY(context.asClipBoxContext() == &context);
    NodeContext &installed = context;
    LOKA_VERIFY(installed.asClipBoxContext() == &context);
    LOKA_VERIFY(!context.placement().query(out));
    const Rect first = {10, 20, 30, 40}, empty = {4, 5, 4, 5};
    context.setPlacement(ToolboxClipPlacement(first));
    LOKA_VERIFY(context.placement().query(out));
    LOKA_VERIFY(out.top == 10 && out.left == 20 && out.bottom == 30 && out.right == 40);
    context.setPlacement(ToolboxClipPlacement(empty));
    LOKA_VERIFY(context.placement().query(out));
    LOKA_VERIFY(out.top == 4 && out.left == 5 && out.bottom == 4 && out.right == 5);
    context.setPlacement(ToolboxClipPlacement());
    LOKA_VERIFY(!context.placement().query(out));
  }

  class PlacementLeaf : public TextNode
  {
  public:
    const short natural;
    const short height;
    unsigned calls;
    unsigned depth;
    loka::core::Frame clip;
    Rect paintRect_;
    PlacementLeaf(short width = 80, short h = 20)
        : TextNode(Text("placement").props),
          natural(width),
          height(h),
          calls(0),
          depth(0),
          paintRect_()
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
      ++this->calls;
      this->depth = controller.projectionParentScopes_.activeDepth();
      this->clip = controller.projectionParentScopes_.current().clipRect;
      LayoutState projected;
      // A non-native child can still complete when the clip box cannot project.
      if (controller.projectionParentScopes_.current().project(state, projected))
        state = projected;
      else
        state.x = state.y = 0;
      const Rect rect = {
          state.y, state.x, static_cast<short>(state.y + this->height), static_cast<short>(state.x + this->natural)};
      controller.intersectWithProjectionClip(rect, this->paintRect_);
      state.y = static_cast<short>(state.y + this->height);
      return this->natural;
    }
  };

  void clipForeignContextPin()
  {
#ifdef LOKA_LIFECYCLE_AUDIT
    std::printf("[skip] foreign clip context: lifecycle audit intentionally asserts; run without audit\n");
#else
    struct Foreign : NodeContext
    {
    };
    struct ForeignChild : PlacementLeaf
    {
      virtual void render(IPlatformController *platform)
      {
        ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
        LOKA_VERIFY(controller.projectionParentScopes_.activeDepth() == 0);
        DrawText("x", 0, 1);
      }
    };
    for (int handler = 0; handler != 2; ++handler)
    {
      Fixture f(WindowProps(), handler != 0);
      HostPortScope port(f.window.window());
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
      BoxNode box(Box().clipChildren().props);
      ForeignChild *child = new ForeignChild();
      box.addChild(child);
      NodeContext *foreign = new Foreign();
      box.setContext(foreign);
      for (unsigned pass = 0; pass != 2; ++pass)
      {
        LayoutState state = f.seat();
        state.width = 50;
        LOKA_VERIFY(LayoutNode(&box, state, &f.controller, 0) == 80);
        LOKA_VERIFY(box.getContext() == foreign && !foreign->asClipBoxContext());
        LOKA_VERIFY(child->calls == pass + 1 && child->depth == 0 && state.y == 20);
        LOKA_VERIFY(f.controller.projectionParentScopes_.activeDepth() == 0);
      }
      const Rect broad = {0, 0, 300, 300};
      ClipRect(&broad);
      toolbox_host::draws.clear();
      RenderNode(&box, &f.controller);
      LOKA_VERIFY(box.getContext() == foreign && !foreign->asClipBoxContext());
      LOKA_VERIFY(toolbox_host::draws.size() == 1);
      LOKA_VERIFY(EqualRect(&toolbox_host::draws.back().clip, &broad));
    }
#endif
  }

  bool placementOf(Node &node, Rect &rect)
  {
    return node.getContext() && static_cast<ToolboxClipBoxContext *>(node.getContext())->placement().query(rect);
  }

  void clipWidthPin()
  {
    for (int handler = 0; handler != 2; ++handler)
      for (int flagged = 0; flagged != 2; ++flagged)
        for (int offer = -1; offer != 3; ++offer)
        {
          Fixture f(WindowProps(), handler != 0);
          LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
          BoxNode box(flagged ? Box().clipChildren().props : Box().props);
          PlacementLeaf *leaf = new PlacementLeaf();
          box.addChild(leaf);
          LayoutState state = f.seat();
          state.x = 10;
          state.y = 15;
          state.width = static_cast<short>(offer * 50);
          const short width = LayoutNode(&box, state, &f.controller, 0);
          const short expected = flagged && offer == 1 ? 50 : 80;
          LOKA_VERIFY(width == expected);
          LOKA_VERIFY(leaf->depth == (flagged ? 1u : 0u));
          Rect rect;
          LOKA_VERIFY(placementOf(box, rect) == (flagged != 0));
          if (flagged)
          {
            LOKA_VERIFY(rect.left == 10 && rect.top == 15 && rect.right == 10 + expected && rect.bottom == 35);
            LOKA_VERIFY(leaf->clip.x == 10 && leaf->clip.y == 15);
            LOKA_VERIFY(leaf->clip.x + leaf->clip.width == (offer > 0 ? 10 + offer * 50 : SHRT_MAX));
            LOKA_VERIFY(leaf->clip.y + leaf->clip.height == SHRT_MAX);
            LOKA_VERIFY(leaf->paintRect_.right == 10 + expected && leaf->paintRect_.bottom == 35);
          }
          else
            LOKA_VERIFY(!box.getContext());
          LOKA_VERIFY(f.controller.projectionParentScopes_.activeDepth() == 0);
        }
  }

  void clipDeclaredWiderPin()
  {
    for (int handler = 0; handler != 2; ++handler)
      for (int nested = 0; nested != 2; ++nested)
      {
        Fixture f(WindowProps(), handler != 0);
        LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
        ProjectionParentScopeStack &scopes = f.controller.projectionParentScopes_;
        if (nested)
        {
          // The parent trims the left edge but is wider than the offer, so it
          // cannot mask a missing provisional width cap on the right.
          const ProjectionParentScope parent(0, 0, 0, loka::core::Frame(20, 15, 70, 80));
          LOKA_VERIFY(scopes.pushClip(parent));
        }
        BoxNode box(Box().width(100).clipChildren().props);
        PlacementLeaf *leaf = new PlacementLeaf(100, 20);
        box.addChild(leaf);
        LayoutState state = f.seat();
        state.x = 10;
        state.y = 20;
        state.width = 50;
        LOKA_VERIFY(LayoutNode(&box, state, &f.controller, 0) == 50);
        LOKA_VERIFY(leaf->calls == 1);
        LOKA_VERIFY(leaf->paintRect_.left == (nested ? 20 : 10) && leaf->paintRect_.right == 60);
        LOKA_VERIFY(leaf->paintRect_.top == 20 && leaf->paintRect_.bottom == 40);
        Rect rect;
        LOKA_VERIFY(placementOf(box, rect) && rect.left == 10 && rect.right - rect.left == 50);
        if (nested)
          LOKA_VERIFY(scopes.pop());
        LOKA_VERIFY(scopes.activeDepth() == 0);
      }
  }

  void clipCardUnconstrainedPin()
  {
    for (int handler = 0; handler != 2; ++handler)
      for (int nested = 0; nested != 2; ++nested)
        for (short offer = -1; offer <= 0; ++offer)
        {
          Fixture f(WindowProps(), handler != 0);
          LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
          ProjectionParentScopeStack &scopes = f.controller.projectionParentScopes_;
          if (nested)
          {
            const ProjectionParentScope parent(0, 0, 0, loka::core::Frame(10, 20, 100, 100));
            LOKA_VERIFY(scopes.pushClip(parent));
          }
          CardNode card(Card().clipChildren().props);
          PlacementLeaf *leaf = new PlacementLeaf(80, 20);
          card.addChild(leaf);
          LayoutState state = f.seat();
          state.x = 10;
          state.y = 20;
          state.width = offer;
          LOKA_VERIFY(LayoutNode(&card, state, &f.controller, 0) == offer);
          LOKA_VERIFY(leaf->calls == 1);
          LOKA_VERIFY(leaf->paintRect_.left < leaf->paintRect_.right && leaf->paintRect_.top < leaf->paintRect_.bottom);
          LOKA_VERIFY(leaf->clip.x + leaf->clip.width == (nested ? 110 : SHRT_MAX));
          Rect rect;
          LOKA_VERIFY(card.getContext() && !placementOf(card, rect));
          if (nested)
            LOKA_VERIFY(scopes.pop());
        }
  }

  void clipResultPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    for (int empty = 0; empty != 2; ++empty)
    {
      BoxNode box(Box().clipChildren().props);
      box.addChild(new PlacementLeaf(empty ? 80 : 0, empty ? 0 : 20));
      f.layout(box);
      Rect rect;
      LOKA_VERIFY(box.getContext() && !placementOf(box, rect));
    }
    BoxNode edge(Box().width(100).clipChildren().props);
    PlacementLeaf *leaf = new PlacementLeaf();
    edge.addChild(leaf);
    LayoutState state = f.seat();
    state.x = SHRT_MAX - 7;
    state.width = 50;
    LOKA_VERIFY(LayoutNode(&edge, state, &f.controller, 0) == 50);
    Rect rect;
    LOKA_VERIFY(placementOf(edge, rect) && rect.right == SHRT_MAX);
    LOKA_VERIFY(leaf->clip.x + leaf->clip.width == SHRT_MAX);
    ProjectionParentScope frame(0, 0, 0, loka::core::Frame(0, 0, 200, 200));
    ProjectionViewportScopeGuard guard(f.controller.projectionParentScopes_, frame);
    LOKA_VERIFY(guard.isActive());
    edge.props.height = SHRT_MAX;
    state = f.seat();
    state.y = 20;
    LOKA_VERIFY(LayoutNode(&edge, state, &f.controller, 0) == 0);
    LOKA_VERIFY(!placementOf(edge, rect));
    LOKA_VERIFY(f.controller.projectionParentScopes_.current().hasShortRangeRefusal());
  }

  void clipLatestPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    BoxNode box(Box().clipChildren().props);
    box.setPropsTypeId(BoxProps::staticTypeId());
    box.addChild(new PlacementLeaf());
    LayoutState state = f.seat();
    LayoutNode(&box, state, &f.controller, 0);
    Rect rect;
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 0 && rect.bottom == 20);
    state = f.seat();
    state.y = 42;
    LayoutNode(&box, state, &f.controller, 0);
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 42 && rect.bottom == 62);
    // LazyView's retained refresh door applies props without placing the node.
    LOKA_VERIFY(Box().width(90).clipChildren().applyPropsToNode(&box));
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 42 && rect.right == 80);
    NotifySubtreeNodeDetached(&box);
    LifecycleFactTestAccess::DeliverFacts(&box);
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 42);
    NotifySubtreeNodeAttached(&box);
    LifecycleFactTestAccess::DeliverFacts(&box);
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 42);
#ifndef LOKA_LIFECYCLE_AUDIT
    LOKA_VERIFY(Box().applyPropsToNode(&box));
    state = f.seat();
    state.y = 77;
    LayoutNode(&box, state, &f.controller, 0);
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 77);
#else
    std::printf("[skip] clipLatestPin differing flag (lifecycle audit asserts here)\n");
#endif
    // Test-only reach of the unflag branch: apps cannot take this path after #1560 Q-M1.
    box.props.childClip_ = layout::CHILD_CLIP_NONE;
    state = f.seat();
    state.y = 99;
    LayoutNode(&box, state, &f.controller, 0);
    LOKA_VERIFY(!placementOf(box, rect));
    box.props.childClip_ = layout::CHILD_CLIP_OWN_RECT;
    RenderNode(&box, &f.controller);
    LOKA_VERIFY(!placementOf(box, rect));
    f.controller.releaseNodeContexts(&box);
    LOKA_VERIFY(!box.getContext());
    f.controller.ensureClipBoxContext(&box, state);
    LOKA_VERIFY(box.getContext() && !placementOf(box, rect));
  }

  void clipNestedPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    BoxNode parent(Box().size(50, 12).clipChildren().props);
    BoxNode *inner = new BoxNode(Box().size(100, 30).clipChildren().props);
    PlacementLeaf *leaf = new PlacementLeaf(100, 30);
    inner->addChild(leaf);
    parent.addChild(inner);
    LayoutState state = f.seat();
    state.x = 10;
    state.y = 20;
    LayoutNode(&parent, state, &f.controller, 0);
    Rect outer, child;
    LOKA_VERIFY(placementOf(parent, outer) && placementOf(*inner, child));
    LOKA_VERIFY(outer.right == 60 && outer.bottom == 32 && child.right == 60 && child.bottom == 50);
    LOKA_VERIFY(leaf->depth == 2 && leaf->paintRect_.right == 60 && leaf->paintRect_.bottom == 32);
    BoxNode sibling(Box().clipChildren().props);
    sibling.addChild(new PlacementLeaf(22, 7));
    state = f.seat();
    state.y = 70;
    LayoutNode(&sibling, state, &f.controller, 0);
    LOKA_VERIFY(placementOf(sibling, child) && child.top == 70 && child.bottom == 77);
    LOKA_VERIFY(placementOf(parent, outer) && outer.top == 20);
  }

  void clipAdmissionPin(bool projectionFailure)
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    ProjectionParentScopeStack &stack = f.controller.projectionParentScopes_;
    ProjectionParentScope frame(0, 0, projectionFailure ? -100 : 0, loka::core::Frame(0, 0, 200, 200));
    if (projectionFailure)
      LOKA_VERIFY(stack.pushViewport(frame));
    const int clips = projectionFailure ? 0 : 6;
    for (int i = 0; i != clips; ++i)
      LOKA_VERIFY(stack.pushClip(frame));
    BoxNode box(Box().clipChildren().props);
    PlacementLeaf *leaf = new PlacementLeaf();
    box.addChild(leaf);
    // Seed an old fact so every refusal must actively replace it with none.
    f.controller.ensureClipBoxContext(&box, f.seat());
    const Rect old = {1, 2, 3, 4};
    static_cast<ToolboxClipBoxContext *>(box.getContext())->setPlacement(ToolboxClipPlacement(old));
    LayoutState state = f.seat();
    if (projectionFailure)
      state.y = SHRT_MIN + 10;
    LayoutNode(&box, state, &f.controller, 0);
    Rect rect;
    LOKA_VERIFY(leaf->calls == 1 && leaf->depth == (projectionFailure ? 1u : 6u));
    LOKA_VERIFY(!placementOf(box, rect));
    LOKA_VERIFY(!stack.current().hasShortRangeRefusal());
    for (int i = 0; i != clips + (projectionFailure ? 1 : 0); ++i)
      LOKA_VERIFY(stack.pop());
  }

  void clipScrollPin()
  {
    Rect content = {0, 0, 0, 0};
    loka::core::Frame published(0, 0, 0, 0);
    short maximum = 0;
    // Build separate twins through the same setup; only the outer birth flag differs.
    for (int flagged = 1; flagged >= 0; --flagged)
    {
      Fixture f;
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
      Box definition = Box().height(30);
      if (flagged)
        definition.clipChildren();
      BoxNode box(definition.props);
      loka::core::MutableState<loka::core::Frame> fact(loka::core::Frame(0, 0, 0, 0));
      NodeState<loka::core::Frame> viewport(&fact, 0);
      ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(viewport));
      PlacementLeaf *leaf = new PlacementLeaf(80, 170);
      scroll->addChild(leaf);
      BoxNode *inner = new BoxNode(Box().height(100).clipChildren().props);
      inner->addChild(scroll);
      box.addChild(inner);
      LayoutState state = f.seat();
      LayoutNode(&box, state, &f.controller, 0);
      LOKA_VERIFY(leaf->calls > 0 && leaf->depth == (flagged ? 3u : 2u));
      LOKA_VERIFY(leaf->clip.height == (flagged ? 30 : 100));
      LOKA_VERIFY(!f.controller.scrollControls().empty());
      const Rect after = f.controller.viewportScroll().contentRect;
      LOKA_VERIFY(after.bottom == 100);
      if (flagged)
      {
        content = after;
        published = fact.get();
        maximum = f.controller.scrollControls()[0].maximum;
      }
      else
      {
        LOKA_VERIFY(content.left == after.left && content.top == after.top && content.right == after.right
                    && content.bottom == after.bottom);
        LOKA_VERIFY(fact.get() == published && f.controller.scrollControls()[0].maximum == maximum);
      }
      f.controller.releaseNodeContexts(&box);
    }
  }

  void clipUnitPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    BoxNode box(Box().size(50, 10).clipChildren().props);
    ZStackNode *stack = new ZStackNode(ZStack().props);
    stack->addChild(new PlacementLeaf(80, 20));
    stack->addChild(new PlacementLeaf(80, 20));
    box.addChild(stack);
    f.layout(box);
    LOKA_VERIFY(f.controller.unitLedger_.mark() == 1);
    const ToolboxUnitLedger::UnitSeat &row = f.controller.unitLedger_.at(0);
    LOKA_VERIFY(row.rect.right == 50 && row.rect.bottom == 10 && row.unit.bottom == 20);
  }

  void clipCardScrollPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    loka::core::MutableState<loka::core::Frame> fact(loka::core::Frame(0, 9, 0, 0));
    NodeState<loka::core::Frame> viewport(&fact, 0);
    ScrollViewNode scroll((ScrollViewProps(viewport)));
    CardNode *card = new CardNode(Card().clipChildren().props);
    card->addChild(new PlacementLeaf(80, 150));
    scroll.addChild(card);
    LayoutState state = f.seat();
    state.x = 10;
    state.y = 20;
    LayoutNode(&scroll, state, &f.controller, 0);
    Rect rect;
    LOKA_VERIFY(placementOf(*card, rect));
    LOKA_VERIFY(rect.left == 10 && rect.top == 11);
    LOKA_VERIFY(rect.right == 10 + fact.get().width);
    LOKA_VERIFY(rect.bottom - rect.top == 150 + card->frameGeometry().insets.top + card->frameGeometry().insets.bottom);
    LOKA_VERIFY(fact.get().y == 9);
    f.controller.releaseNodeContexts(&scroll);
  }

  // This fixture substitutes the controller's projected preparation door.
  // The real handler, context layout and paintRect-backed answer run below.
  class PlacementBand : public CardBandNode
  {
  public:
    PlacementBand()
        : CardBandNode(CardBandProps(CARD_BAND_TOP))
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
      if (!controller.projectLayoutState(state))
        return 0;
      IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(this);
      LOKA_VERIFY(handler && handler->ensureContext(this, platform, state));
      return this->Node::layout(platform, state);
    }
  };

  void clipNativePin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    BoxNode box(Box().size(50, 10).clipChildren().props);
    BoxNode *inner = new BoxNode(Box().size(100, 30).props);
    CardBandNode *band = new PlacementBand();
    inner->addChild(band);
    box.addChild(inner);
    f.layout(box);
    NotifySubtreeNodeAttached(&box);
    LifecycleFactTestAccess::DeliverFacts(&box);
    LOKA_VERIFY(band->getContext());
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    const PaintAnswer answer = static_cast<ToolboxCardBandContext *>(band->getContext())->queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(answer.damage.x == 0 && answer.damage.y == 0 && answer.damage.width == 50
                && answer.damage.height == 10);
    f.controller.releaseNodeContexts(&box);
  }

  void clipGridPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    GridNode grid(Grid().rows(2).cols(1).props);
    BoxNode *first = new BoxNode(Box().clipChildren().props);
    BoxNode *second = new BoxNode(Box().clipChildren().props);
    first->addChild(new PlacementLeaf());
    second->addChild(new PlacementLeaf());
    grid.addChild(first);
    grid.addChild(second);
    f.layout(grid);
    Rect rect;
    LOKA_VERIFY(placementOf(*second, rect) && rect.top == 50 && rect.bottom == 70);
    grid.props.rows = 1;
    f.layout(grid);
    LOKA_VERIFY(placementOf(*second, rect) && rect.top == 50 && rect.bottom == 70);
  }

  class RestartLeaf : public PlacementLeaf
  {
  public:
    RestartLeaf()
        : PlacementLeaf(80, 20)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      const short width = PlacementLeaf::layoutProjected(platform, state);
      // Force the cached band's first attempt to disagree, then vary the retry.
      state.y = static_cast<short>(state.y + this->calls);
      return width;
    }
  };

  // Band eligibility is Text-only today. This test adapter places a Box through
  // production dispatch on each text visit, exposing the real restart sequence.
  class BandBoxText : public TextNode
  {
    BoxNode &box_;

  public:
    explicit BandBoxText(BoxNode &box)
        : TextNode(Text("band").props),
          box_(box)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      return LayoutNode(&this->box_, state, static_cast<ToolboxScenePlatformController *>(platform), 0);
    }
  };

  void clipRestartPin()
  {
    Fixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    BoxNode box(Box().clipChildren().props);
    StackNode column(Column().gap(0).props);
    column.setPropsTypeId(StackProps::staticTypeId());
    RestartLeaf *leaf = new RestartLeaf();
    box.addChild(leaf);
    column.addChild(new BandBoxText(box));
    loka::app::layout::StackSpans spans;
    LayoutState state = f.seat();
    LayoutNode(&column, state, &f.controller, 0, 0, &spans);
    LOKA_VERIFY(spans.valid() && leaf->calls == 1);
    const loka::app::layout::LazyWindow band = {0, 1};
    state = f.seat();
    state.y = 30;
    LayoutNode(&column, state, &f.controller, 0, &band, &spans);
    Rect rect;
    LOKA_VERIFY(leaf->calls == 3);
    LOKA_VERIFY(placementOf(box, rect) && rect.top == 30 && rect.bottom == 53);
  }

  class PartialBoxHandler : public IPlatformLayoutHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<BoxNode>();
    }
    static bool accept(void *, int y)
    {
      return y < 30;
    }
    static int child(void *context, Node *node, const LayoutState &state)
    {
      IPlatformLayoutTraversal *traversal = static_cast<IPlatformLayoutTraversal *>(context);
      traversal->layoutChild(node, state);
      return traversal->layoutResultY();
    }
    virtual int layoutNode(Node *node,
                           const LayoutState &state,
                           IPlatformLayoutTraversal *traversal,
                           const loka::app::layout::LazyWindow * = 0,
                           loka::app::layout::StackSpans * = 0)
    {
      const int bottom = layout::computeBoxLayoutResultY(
          node->asBoxNode(), ToolboxLayoutMetrics::spacing(), state, traversal, child, 31, accept);
      traversal->setLayoutResultY(static_cast<short>(bottom));
      return 80;
    }
  };

  void clipPartialPin()
  {
    Fixture f(WindowProps(), false);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    LOKA_VERIFY(f.controller.gapLayoutRegistry.registerHandler(new PartialBoxHandler()));
    BoxNode box(Box().height(100).clipChildren().props);
    PlacementLeaf *first = new PlacementLeaf(), *second = new PlacementLeaf();
    box.addChild(first);
    box.addChild(second);
    LayoutState state = f.seat();
    state.width = 50;
    LOKA_VERIFY(LayoutNode(&box, state, &f.controller, 0) == 50);
    Rect rect;
    LOKA_VERIFY(first->calls == 1 && second->calls == 0 && state.y == 20);
    LOKA_VERIFY(placementOf(box, rect) && rect.bottom == 20 && rect.right == 50);
  }

  void columnPin()
  {
    Fixture f;
    StackNode column(Column().gap(16).props);
    GapText *a = new GapText(), *b, *c;
    GapButton *d = new GapButton();
    column.addChild(a);
    column.addChild(pair(b, c));
    column.addChild(d);
    f.layout(column);
    LOKA_VERIFY(a->offer.y == 0 && b->offer.y == 23 && c->offer.y == 46 && d->offer.y == 69);
  }

  void siblingsPin()
  {
    // Exercise both Column traversal and the shared-state generic LayoutChildren loop.
    for (int generic = 0; generic != 2; ++generic)
    {
      Fixture f;
      StackNode column(Column().gap(20).props);
      FragmentNode *group = new FragmentNode(FragmentProps());
      NestableNode *parent = generic ? static_cast<NestableNode *>(group) : &column;
      GapText *a, *b, *c, *d;
      BoxNode *box = new BoxNode(BoxProps());
      box->addChild(pair(a, b));
      parent->addChild(box);
      parent->addChild(pair(c, d));
      if (generic)
        column.addChild(group);
      else
        delete group;
      f.layout(column);
      LOKA_VERIFY(b->offer.y - a->offer.y == 15);
      LOKA_VERIFY(c->offer.y - b->offer.y == 27 && d->offer.y - c->offer.y == 27);
    }
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    GapText *a, *b, *c, *d;
    BoxNode *box = new BoxNode(Box().gap(20).props);
    box->addChild(pair(a, b));
    scroll.addChild(box);
    scroll.addChild(pair(c, d));
    f.layout(scroll, 23);
    LOKA_VERIFY(b->offer.y - a->offer.y == 27);
    LOKA_VERIFY(c->offer.y - b->offer.y == 15 && d->offer.y - c->offer.y == 15);
    f.controller.releaseNodeContexts(&scroll);
    // Projected early return resets its own input, then restores its caller.
    GapText projected;
    f.layout(projected, 29);
    LOKA_VERIFY(projected.offer.spacing == 8);
  }

  void resetsPin()
  {
    // Independent unspecified pins for every class-(e) Column/Box/Row fixture.
    for (int kind = 0; kind != 6; ++kind)
    {
      Fixture f;
      StackNode outer(Column().gap(20).props);
      NestableNode *container = 0;
      if (kind == 0)
        container = new StackNode(Column().props);
      if (kind == 1)
        container = new BoxNode(BoxProps());
      if (kind == 2)
        container = new ZStackNode(ZStackProps());
      if (kind == 3)
        container = new GridNode(GridProps(1, 1));
      if (kind == 4)
        container = new CardNode(CardProps());
      if (kind == 5)
        container = new ScrollViewNode(ScrollViewProps());
      GapText *a, *b;
      container->addChild(pair(a, b));
      outer.addChild(container);
      f.layout(outer);
      LOKA_VERIFY(b->offer.y - a->offer.y == 15);
      f.controller.releaseNodeContexts(&outer);
    }
    Fixture f;
    StackNode row(Row().props);
    GapText *a, *b;
    row.addChild(pair(a, b));
    f.layout(row, 10000);
    LOKA_VERIFY(b->offer.y - a->offer.y == 15);
  }

  void rowPin()
  {
    Fixture f;
    StackNode row(Row().gap(20).props);
    GapText *a, *b;
    row.addChild(pair(a, b));
    f.layout(row);
    LOKA_VERIFY(a->offer.x == b->offer.x && b->offer.y - a->offer.y == 27);
  }

  void measurementPin()
  {
    for (int mode = 0; mode != 3; ++mode)
    {
      Fixture f;
      StackProps outerProps = Row().props, innerProps = Row().props;
      outerProps.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
      innerProps.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
      if (mode == 0)
        innerProps.gap(20);
      if (mode == 2)
        outerProps.gap(20);
      StackNode outer(outerProps);
      StackNode *inner = new StackNode(innerProps);
      GapButton *a = new GapButton(), *b = new GapButton(), *c = new GapButton();
      outer.addChild(a);
      outer.addChild(inner);
      inner->addChild(b);
      inner->addChild(c);
      short buttonWidth = 0;
      LOKA_VERIFY(f.controller.queryNaturalWidth(a, buttonWidth));
      loka::app::layout::RowWidthConsultation widths(outer.childrenHead(),
                                                     2,
                                                     200,
                                                     mode == 2 ? 20 : 8,
                                                     loka::app::layout::RailSpacing(12, 8, 9),
                                                     ROW_UNDECLARED_WIDTH_NATURAL,
                                                     &f.controller);
      widths.next(a);
      const int innerWidth = widths.next(inner).width();
      const int gap = mode == 0 ? 20 : 8;
      LOKA_VERIFY(innerWidth == 2 * buttonWidth + gap);
      f.layout(outer);
      LOKA_VERIFY(b->offer.width == buttonWidth && c->offer.width == buttonWidth);
      LOKA_VERIFY(c->offer.x - b->offer.x == buttonWidth + gap);
      LOKA_VERIFY(b->offer.x - a->offer.x == buttonWidth + (mode == 2 ? 20 : 8));
      // The nested offer must equal its measured width, not merely happen to fit its children.
      // Return width includes both nested seats and both resolved gaps.
      LayoutState seat = f.seat();
      const short width = LayoutNode(&outer, seat, &f.controller, 0);
      LOKA_VERIFY(width == 3 * buttonWidth + gap + (mode == 2 ? 20 : 8));
    }
  }

  class CountedBox : public BoxNode
  {
  public:
    unsigned visits;
    CountedBox()
        : BoxNode(Box().size(1, 1).props),
          visits(0)
    {
    }
    virtual BoundaryNode *asBoundary()
    {
      ++this->visits;
      return 0;
    }
  };

  class RejectedCoordinate : public IPlatformLayoutTraversal
  {
  public:
    int rejected;
    short y;
    RejectedCoordinate()
        : rejected(0),
          y(0)
    {
    }
    virtual bool refuseLayoutResultY(int value)
    {
      if (value >= SHRT_MIN && value <= SHRT_MAX)
        return false;
      this->rejected = value;
      return true;
    }
    virtual int layoutChild(Node *, const LayoutState &state)
    {
      this->y = state.y;
      return 1;
    }
    virtual void setLayoutResultY(short value)
    {
      this->y = value;
    }
    virtual short layoutResultY() const
    {
      return this->y;
    }
  };

  void overflowPin(bool edge)
  {
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    StackNode *row = new StackNode(Row().gap(SHRT_MAX).props);
    CountedBox *a = new CountedBox(), *b = new CountedBox();
    row->addChild(a);
    row->addChild(b);
    scroll.addChild(row);
    scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
    LayoutState state = f.seat();
    state.x = edge ? -1 : 0;
    LayoutNode(&scroll, state, &f.controller, 0);
    LOKA_VERIFY(a->visits == 1 && b->visits == 0);
    LOKA_VERIFY(f.controller.scrollControls().empty());
    LOKA_VERIFY(scroll.takeLayoutInputs() != NODE_DIRTY_NONE);
    LOKA_VERIFY(state.spacing == 31);
    f.controller.releaseNodeContexts(&scroll);

    // Observe the existing traversal acceptance door as well as the ScrollView
    // outcome: removing the origin check would otherwise be masked by its edge.
    RejectedCoordinate traversal;
    LayoutState rowState = f.seat(SHRT_MAX);
    rowState.x = edge ? -1 : 0;
    ComputeToolboxRowLayout(row, rowState, &traversal);
    LOKA_VERIFY(traversal.rejected == SHRT_MAX + 1);
  }

  void spansPin()
  {
    for (int band = 0; band != 3; ++band)
    {
      Fixture f;
      ScrollViewNode scroll((ScrollViewProps()));
      StackNode *column = new StackNode(Column().props);
      column->setPropsTypeId(StackProps::staticTypeId());
      GapText *leaves[5];
      for (int i = 0; i != 5; ++i)
      {
        leaves[i] = new GapText();
        column->addChild(leaves[i]);
      }
      scroll.addChild(column);
      LayoutState state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      loka::app::layout::StackSpans *spans = f.controller.scrollSpans();
      LOKA_VERIFY(spans && spans->validForGap(8) && spans->total() == 67);
      // Head-only and empty ledger selections must reject the old gap before reusing totals.
      if (band == 2)
        spans->placed(loka::core::Frame(0, 1000, 200, 0));
      LOKA_VERIFY(Column().gap(20).applyPropsToNode(column));
      scroll.takeLayoutInputs();
      scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
      const NodeDirtyFlags inputs = scroll.takeLayoutInputs();
      LOKA_VERIFY(inputs == NODE_DIRTY_LAYOUT);
      scroll.requeueLayoutInputs(inputs);
      state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      LOKA_VERIFY(spans->validForGap(20) && spans->total() == 115);
      short positions[5];
      for (int i = 0; i != 5; ++i)
      {
        positions[i] = leaves[i]->offer.y;
        LOKA_VERIFY(leaves[i]->calls == 2);
      }
      spans->invalidate();
      state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      LOKA_VERIFY(spans->total() == 115);
      for (int i = 0; i != 5; ++i)
        LOKA_VERIFY(leaves[i]->offer.y == positions[i] && positions[i] == i * 27);
      f.controller.releaseNodeContexts(&scroll);
    }
  }

  void overflowDefaultPin()
  {
    // Direct entry intentionally bypasses dispatch: horizontal measurement must
    // depend on the named horizontal default, not this offered vertical transport.
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    StackProps props = Row().props;
    props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    StackNode *row = new StackNode(props);
    row->addChild(new BoxNode(Box().size(70, 1).props));
    row->addChild(new BoxNode(Box().size(70, 1).props));
    scroll.addChild(row);
    LayoutState state = f.seat(23);
    state.width = 100;
    f.controller.layoutScrollView(&scroll, state, 0);
    LOKA_VERIFY(f.controller.scrollControls().size() == 2);
    LOKA_VERIFY(f.controller.scrollControls()[1].maximum == 64);
    f.controller.releaseNodeContexts(&scroll);
  }

  class RootOffer : public TextNode
  {
  public:
    LayoutState offer;
    unsigned calls;
    RootOffer()
        : TextNode(Text("root").props),
          calls(0)
    {
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      this->offer = state;
      ++this->calls;
      return state.width;
    }
  };

  class ScrollOffer : public ImageViewNode
  {
  public:
    LayoutState offer;
    explicit ScrollOffer(bool horizontal)
        : ImageViewNode(ImageViewProps().size(horizontal ? 400 : 100, 500))
    {
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      this->offer = state;
      state.y = static_cast<short>(state.y + 500);
      return state.width;
    }
  };

  void windowMarginPin()
  {
    const layout::RailSpacing &spacing = ToolboxLayoutMetrics::spacing();
    LOKA_VERIFY(resolveWindowContentMargin(0, spacing) == 12);
    const layout::RailSpacing other(3, 4, 19);
    LOKA_VERIFY(resolveWindowContentMargin(0, other) == 19);
    const layout::LayoutGap margins[] = {layout::LayoutGap(),
                                         layout::LayoutGap::of(0),
                                         layout::LayoutGap::of(20),
                                         StandardWindowMargin(),
                                         layout::LayoutGap::of(SHRT_MAX),
                                         layout::LayoutGap::of(211)};
    const int origins[] = {12, 0, 20, 12, SHRT_MAX, 211};
    const int widths[] = {396, 420, 380, 396, 0, 0};
    const int heights[] = {236, 260, 220, 236, 0, 0};
    for (unsigned i = 0; i != 6; ++i)
    {
      Fixture f(WindowProps().contentMargin(margins[i]));
      SetRect(&f.window.port.portRect, 0, 0, 420, 260);
      RootOffer root;
      f.controller.rootNode_ = &root;
      LOKA_VERIFY(resolveWindowContentMargin(&f.window, spacing) == origins[i]);
      ToolboxInputDoor::render(f.controller);
      LOKA_VERIFY(root.calls == 1);
      LOKA_VERIFY(root.offer.x == origins[i] && root.offer.y == origins[i]);
      LOKA_VERIFY(root.offer.width == widths[i] && root.offer.height == heights[i]);
      f.controller.rootNode_ = 0;
    }
    for (unsigned i = 0; i != 3; ++i)
    {
      Fixture f(i != 1 ? WindowProps().contentMargin(0) : WindowProps());
      ScrollViewNode root((ScrollViewProps()));
      ScrollOffer *child = new ScrollOffer(i == 2);
      root.addChild(child);
      f.controller.rootNode_ = &root;
      ToolboxInputDoor::render(f.controller);
      LOKA_VERIFY(child->offer.width == (i == 1 ? 320 : 345));
      LOKA_VERIFY(f.controller.scrollControls().size() == (i == 2 ? 2 : 1));
      const ToolboxScenePlatformController::ScrollBarControlBinding &bar = f.controller.scrollControls()[0];
      const ToolboxScenePlatformController::ViewportScrollBarBinding &binding = f.controller.viewportScroll();
      LOKA_VERIFY(binding.rect.left == (i == 1 ? 12 : 0) && binding.rect.top == (i == 1 ? 12 : 0));
      LOKA_VERIFY(binding.rect.right == (i == 1 ? 348 : 360) && binding.rect.bottom == (i == 1 ? 248 : 260));
      LOKA_VERIFY(binding.contentRect.left == (i == 1 ? 12 : 0) && binding.contentRect.top == (i == 1 ? 12 : 0));
      LOKA_VERIFY(binding.contentRect.right == (i == 1 ? 332 : 345));
      LOKA_VERIFY(binding.contentRect.bottom == (i == 1 ? 248 : i == 2 ? 245 : 260));
      LOKA_VERIFY(bar.placement.full().left == (i == 1 ? 332 : 345)
                  && bar.placement.full().right == (i == 1 ? 348 : 361));
      LOKA_VERIFY(bar.placement.full().bottom == 246 && bar.placement.full().top == (i == 1 ? 12 : -1));
      LOKA_VERIFY(bar.maximum == (i == 1 ? 264 : i == 2 ? 255 : 240));
      LOKA_VERIFY(bar.pageStep == (i == 1 ? 235 : i == 2 ? 244 : 259));
      if (i == 2)
      {
        const ToolboxScenePlatformController::ScrollBarControlBinding &horizontal = f.controller.scrollControls()[1];
        LOKA_VERIFY(horizontal.placement.full().left == -1 && horizontal.placement.full().top == 245);
        LOKA_VERIFY(horizontal.placement.full().right == 346 && horizontal.placement.full().bottom == 261);
        LOKA_VERIFY(horizontal.maximum == 55 && horizontal.pageStep == 344);
      }
      f.controller.releaseNodeContexts(&root);
      f.controller.rootNode_ = 0;
    }
    Fixture f(WindowProps().contentMargin(0));
    RootOffer root;
    f.controller.rootNode_ = &root;
    SetRect(&f.window.port.portRect, -1, 0, SHRT_MAX, 260);
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(root.calls == 0);
    SetRect(&f.window.port.portRect, 0, -1, 420, SHRT_MAX);
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(root.calls == 0);
    f.controller.rootNode_ = 0;
  }

  void rootPin()
  {
    windowMarginPin();
    Fixture f;
    FragmentNode root((FragmentProps()));
    GapText *a = new GapText(), *b = new GapText();
    root.addChild(a);
    root.addChild(b);
    f.controller.rootNode_ = &root;
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(a->offer.x == 12 && a->offer.y == 12);
    LOKA_VERIFY(b->offer.y - a->offer.y == 7 + ToolboxLayoutMetrics::kDefaultGap);
    f.controller.rootNode_ = 0;
  }
} // namespace

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const char *mode = argv[1];
  if (!std::strcmp(mode, "column"))
    columnPin();
  else if (!std::strcmp(mode, "siblings"))
    siblingsPin();
  else if (!std::strcmp(mode, "resets"))
    resetsPin();
  else if (!std::strcmp(mode, "row"))
    rowPin();
  else if (!std::strcmp(mode, "measurement"))
    measurementPin();
  else if (!std::strcmp(mode, "origin"))
    overflowPin(false);
  else if (!std::strcmp(mode, "edge"))
    overflowPin(true);
  else if (!std::strcmp(mode, "spans"))
    spansPin();
  else if (!std::strcmp(mode, "overflow-default"))
    overflowDefaultPin();
  else if (!std::strcmp(mode, "root"))
    rootPin();
  else if (!std::strcmp(mode, "clip-box"))
    clipRoutingPin(0, true, false);
  else if (!std::strcmp(mode, "clip-card"))
    clipRoutingPin(1, true, false);
  else if (!std::strcmp(mode, "clip-unflagged"))
  {
    clipRoutingPin(0, false, false);
    clipRoutingPin(1, false, false);
  }
  else if (!std::strcmp(mode, "clip-ribbon"))
    clipRoutingPin(2, true, false);
  else if (!std::strcmp(mode, "clip-refusal"))
  {
    clipRoutingPin(0, true, true);
    clipRoutingPin(1, true, true);
  }
  else if (!std::strcmp(mode, "clip-registry"))
    clipRegistryPin();
  else if (!std::strcmp(mode, "clip-lifecycle"))
    clipLifecyclePin();
  else if (!std::strcmp(mode, "clip-foreign-context"))
    clipForeignContextPin();
  else if (!std::strcmp(mode, "clip-placement"))
    clipPlacementPin();
  else if (!std::strcmp(mode, "clip-width"))
    clipWidthPin();
  else if (!std::strcmp(mode, "clip-declared-wider"))
    clipDeclaredWiderPin();
  else if (!std::strcmp(mode, "clip-card-unconstrained"))
    clipCardUnconstrainedPin();
  else if (!std::strcmp(mode, "clip-result"))
    clipResultPin();
  else if (!std::strcmp(mode, "clip-latest"))
    clipLatestPin();
  else if (!std::strcmp(mode, "clip-nested"))
    clipNestedPin();
  else if (!std::strcmp(mode, "clip-admission"))
    clipAdmissionPin(false);
  else if (!std::strcmp(mode, "clip-projection"))
    clipAdmissionPin(true);
  else if (!std::strcmp(mode, "clip-scroll"))
    clipScrollPin();
  else if (!std::strcmp(mode, "clip-unit"))
    clipUnitPin();
  else if (!std::strcmp(mode, "clip-card-scroll"))
    clipCardScrollPin();
  else if (!std::strcmp(mode, "clip-native"))
    clipNativePin();
  else if (!std::strcmp(mode, "clip-grid"))
    clipGridPin();
  else if (!std::strcmp(mode, "clip-restart"))
    clipRestartPin();
  else if (!std::strcmp(mode, "clip-partial"))
    clipPartialPin();
  else
    LOKA_VERIFY(false);
  std::printf("Toolbox gap %s passed\n", mode);
}
