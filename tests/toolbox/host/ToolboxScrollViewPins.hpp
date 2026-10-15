#ifndef LOKA_TOOLBOX_SCROLL_VIEW_PINS_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_PINS_HPP
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/ImageView.hpp"
#include "core/StateTracker.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include <vector>

namespace
{
  struct ScrollFixture
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller;
    loka::core::MutableState<loka::core::Frame> offset;
    loka::core::PushStateTracker tracker;
    NodeState<loka::core::Frame> fact;
    ScrollViewNode scroll;
    StackNode *column;
    explicit ScrollFixture(unsigned count = 20, bool button = false)
        : controller(&window), offset(loka::core::Frame(0, 0, 0, 0)), fact(&offset, &tracker),
          scroll(ScrollViewProps().viewport(fact)), column(new StackNode(StackProps(STACK_AXIS_COLUMN)))
    {
      tracker.addState(&offset);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      scroll.setPropsTypeId(ScrollViewProps::staticTypeId());
      scroll.addChild(column);
      for (unsigned i = 0; i < count; ++i)
      {
        Node *node = button && i == count - 1 ? static_cast<Node *>(new ButtonNode(ButtonProps()))
            : new AttributedTextNode(AttributedText(Styled("abcd", FontSize<12>())).props);
        node->setPropsTypeId(AttributedTextProps::staticTypeId());
        column->addChild(node);
      }
    }
    ~ScrollFixture()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&scroll);
      LifecycleFactTestAccess::DeliverFacts(&scroll);
      controller.releaseNodeContexts(&scroll);
      LOKA_VERIFY(!controller.scrollSpans());
      // Gap pins seat several direct children; detachChildren() hands back only the chain head.
      std::vector<Node *> children;
      scroll.detachChildrenTo(children);
      for (std::size_t i = 0; i != children.size(); ++i)
        delete children[i];
      controller.retired.clear();
    }
    unsigned step(int value, short width = 100, short height = 34, short y = 0, short gap = 0, int x = 0)
    {
      fact.set(loka::core::Frame(x, value, 0, 0));
      scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
      LayoutState seat = Seat(width);
      seat.x = 0;
      seat.y = y;
      seat.height = height;
      seat.spacing = gap;
      const unsigned before = controller.leafLayouts;
      controller.layoutScrollView(&scroll, seat, 0);
      LOKA_VERIFY(seat.x == 0);
      return controller.leafLayouts - before;
    }
    AttributedTextNode *leaf(unsigned index)
    {
      Node *node = column->childrenHead();
      while (index--) node = node->nextInComposition;
      return static_cast<AttributedTextNode *>(node);
    }
    Rect paint(unsigned index)
    {
      return ToolboxAttributedTextContextAccess::paintRect(
          *static_cast<ToolboxAttributedTextContext *>(leaf(index)->getContext()));
    }
    Rect rect(unsigned index)
    {
      return ToolboxAttributedTextContextAccess::rect(
          *static_cast<ToolboxAttributedTextContext *>(leaf(index)->getContext()));
    }
  };

  class ScrollBarColumnHitProbe : public AttributedTextNode
  {
  public:
    ScrollBarColumnHitProbe()
        : AttributedTextNode(AttributedText(Styled("hit", FontSize<12>())).props)
    {
    }
    virtual short layout(IPlatformController *controller, LayoutState &state)
    {
      Rect hit = {0, 90, 10, 99};
      Rect out;
      LOKA_VERIFY(!static_cast<ToolboxScenePlatformController *>(controller)->intersectWithProjectionClip(hit, out));
      return AttributedTextNode::layout(controller, state);
    }
  };

  void ScrollViewXPins()
  {
    Pin("ToolboxScrollViewContentClipDropsScrollbarHit");
    {
      ScrollFixture f(0);
      f.column->addChild(new ScrollBarColumnHitProbe());
      LOKA_VERIFY(f.step(0) == 1);
    }
    Pin("ToolboxScrollViewXTranslationClampAndSeat");
    {
      ScrollFixture f(3);
      f.column->addChild(new ImageViewNode(ImageViewProps().size(400, 10)));
      const int requests[] = {50, 1000, -7};
      const int expected[] = {50, 316, 0};
      for (unsigned step = 0; step < 3; ++step)
      {
        LOKA_VERIFY(f.step(0, 100, 34, 0, 0, requests[step]) == 4);
        LOKA_VERIFY(f.fact.get().x == expected[step]);
        LOKA_VERIFY(f.fact.get().y == 0);
        for (unsigned i = 0; i < 3; ++i)
        {
          LOKA_VERIFY(f.rect(i).left == -expected[step]);
          LOKA_VERIFY(f.rect(i).top == static_cast<short>(i * 17));
        }
      }
    }
    Pin("ToolboxScrollViewXRestoreBetweenDirectSiblings");
    {
      ScrollFixture f(0);
      f.scroll.addChild(new ImageViewNode(ImageViewProps().size(400, 10)));
      for (unsigned i = 0; i < 3; ++i)
        f.scroll.addChild(new AttributedTextNode(AttributedText(Styled("abcd", FontSize<12>())).props));
      LOKA_VERIFY(f.step(0, 100, 34, 0, 0, 50) == 4);
      Node *node = f.column->nextInComposition->nextInComposition;
      for (; node; node = node->nextInComposition)
      {
        const Rect rect =
            ToolboxAttributedTextContextAccess::rect(*static_cast<ToolboxAttributedTextContext *>(node->getContext()));
        LOKA_VERIFY(rect.left == -50);
      }
    }
    Pin("ToolboxScrollViewUnknownWidthClampsX");
    {
      ScrollFixture f(3);
      f.column->addChild(
          new ImageViewNode(ImageViewProps().attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC))));
      LOKA_VERIFY(f.step(0, 100, 34, 0, 0, 50) == 4);
      LOKA_VERIFY(f.fact.get().x == 0);
      for (unsigned i = 0; i < 3; ++i)
        LOKA_VERIFY(f.rect(i).left == 0);
    }
    Pin("ToolboxScrollViewXStepRevisitsAndRecapturesBand");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0, 100, 34, 0, 6) == 20);
      LOKA_VERIFY(f.step(23, 100, 34, 0, 6) == 3);
      // Text-only eligibility has no overflow claim. Seed a prior X placement
      // through the real table door to discriminate the X mismatch policy.
      f.controller.scrollSpans()->placed(loka::core::Frame(50, 23, 100, 34));
      LOKA_VERIFY(f.step(23, 100, 34, 0, 6) == 20);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollSpans()->placedViewport().x == 0);
      LOKA_VERIFY(f.step(46, 100, 34, 0, 6) == 3);
    }
    Pin("ToolboxScrollViewXShortRangeRefusal");
    {
      ScrollFixture f(0);
      ProjectionParentScope scope(0, SHRT_MIN, 0, loka::core::Frame(0, 0, 100, 34));
      ProjectionParentScopeGuard guard(f.controller.projectionParentScopes_, scope);
      LOKA_VERIFY(guard.isActive());
      LayoutState state = Seat(100);
      state.x = 0;
      LOKA_VERIFY(f.controller.projectLayoutState(state));
      LOKA_VERIFY(state.x == SHRT_MIN);
      LOKA_VERIFY(f.controller.restoreProjectedLayoutState(state));
      LOKA_VERIFY(state.x == 0);
      state.x = -1;
      LOKA_VERIFY(!f.controller.projectLayoutState(state));
      LOKA_VERIFY(f.controller.projectionParentScopes_.current().hasShortRangeRefusal());
    }
  }

  void ScrollViewPins()
  {
    Pin("ToolboxScrollViewColumnBoxesGapRefusal");
    {
      ScrollFixture f(0);
      f.column->addChild(new BoxNode(BoxProps().setSize(10, 32760)));
      f.column->addChild(new BoxNode(BoxProps().setSize(10, 1)));
      LOKA_VERIFY(f.step(0, 100, 10, 0, 12) == 1);
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!f.controller.scrollSpans());
    }
    Pin("ToolboxScrollViewGapRefusalAndContentTotal");
    for (unsigned accumulated = 0; accumulated != 2; ++accumulated)
    {
      ScrollFixture f(0);
      delete f.scroll.detachChildren();
      f.column = 0;
      f.scroll.addChild(new BoxNode(BoxProps().setSize(10, accumulated ? 20000 : 32760)));
      f.scroll.addChild(new BoxNode(BoxProps().setSize(10, accumulated ? 12760 : 1)));
      LOKA_VERIFY(f.step(0, 100, 10, accumulated ? -10000 : 0, 12) == (accumulated ? 2u : 1u));
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!f.controller.scrollSpans());
      LOKA_VERIFY(f.controller.scrollMaximum == 0);
    }
    Pin("ToolboxScrollViewDirectContentGapsAndNoSeats");
    {
      loka::core::MutableState<ConfirmResult> storage;
      NodeState<ConfirmResult> result(&storage, 0);
      ScrollFixture f(0);
      delete f.scroll.detachChildren();
      f.column = 0;
      f.scroll.addChild(new ButtonNode(ButtonProps()));
      f.scroll.addChild(new FragmentNode(FragmentProps()));
      f.scroll.addChild(new ConfirmDialogNode(ConfirmDialogProps().result(result)));
      // Hidden Show arms materialize as empty Fragments, as the common pin verifies.
      f.scroll.addChild(new FragmentNode(FragmentProps()));
      f.scroll.addChild(new ButtonNode(ButtonProps()));
      LOKA_VERIFY(f.step(0, 100, 10, 0, 9) == 5);
      LOKA_VERIFY(f.controller.scrollMaximum == 39);
    }
    Pin("ToolboxScrollViewBandWithGap");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0, 100, 34, 0, 6) == 20);
      LOKA_VERIFY(f.controller.scrollSpans()->total() == 20 * 17 + 19 * 6);
      const short middle = f.rect(2).top;
      LOKA_VERIFY(middle == 46);
      LOKA_VERIFY(f.step(23, 100, 34, 0, 6) == 3);
      LOKA_VERIFY(f.rect(2).top == middle - 23);
      LOKA_VERIFY(f.step(46, 100, 34, 0, 6) == 3);
      LOKA_VERIFY(f.rect(2).top == middle - 46);
      LOKA_VERIFY(f.controller.scrollMaximum == 420);
    }
    Pin("ToolboxScrollBarSetterPaintSuppressed");
    {
      ScrollFixture f;
      HostControl control = {0};
      ToolboxScenePlatformController::ScrollBarControlBinding row =
          ToolboxScenePlatformController::ScrollBarControlBinding();
      row.resourceId = 100; // First auto id in the host controller.
      row.control = &control;
      f.controller.installScroll(row);
      const Rect previous = {2, 3, 90, 120};
      const Rect viewport = {0, 0, 34, 100};
      ClipRect(&previous);
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      f.step(0);
      LOKA_VERIFY(control.value == 0);
      f.controller.drawControlsInRect(viewport);
      const toolbox_host::ControlCalls mounted = toolbox_host::controlCalls;
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      f.step(17);
      const toolbox_host::ControlCalls &calls = toolbox_host::controlCalls;
      std::fprintf(stderr, "scroll offset step value=%u empty=%d show=%u draw=%u\n",
                   calls.values, EmptyRect(&calls.valueClip), calls.shows, calls.draws);
      LOKA_VERIFY(calls.values == 1 && EmptyRect(&calls.valueClip));
      LOKA_VERIFY(calls.hilites == 1 && EmptyRect(&calls.hiliteClip));
      LOKA_VERIFY(calls.shows == 1 && EmptyRect(&calls.showClip));
      LOKA_VERIFY(calls.draws == 0 && control.value == 17);
      const Rect restored = toolbox_host::currentClip();
      LOKA_VERIFY(restored.top == previous.top && restored.left == previous.left
                  && restored.bottom == previous.bottom && restored.right == previous.right);
      const std::size_t contentDraws = toolbox_host::draws.size();
      f.leaf(1)->getContext()->render(&f.controller);
      LOKA_VERIFY(toolbox_host::draws.size() > contentDraws && calls.draws == 0);
      f.controller.drawControlsInRect(viewport);
      LOKA_VERIFY(calls.draws == 1);
      LOKA_VERIFY(calls.drawClip.top == previous.top && calls.drawClip.left == previous.left
                  && calls.drawClip.bottom == previous.bottom && calls.drawClip.right == previous.right);
      LOKA_VERIFY(mounted.values == 1 && EmptyRect(&mounted.valueClip));
      LOKA_VERIFY(mounted.shows == 1 && EmptyRect(&mounted.showClip) && mounted.draws == 1);
      LOKA_VERIFY(mounted.drawClip.top == previous.top && mounted.drawClip.left == previous.left
                  && mounted.drawClip.bottom == previous.bottom && mounted.drawClip.right == previous.right);
      const Rect unclipped = {-30000, -30000, 30000, 30000};
      ClipRect(&unclipped);
    }
    Pin("ToolboxScrollBarSuppressionAllocationRefused");
    {
      ToolboxWindow window;
      toolbox_host::failRegions = 2; // Refuse both controller scratch regions.
      ToolboxScenePlatformController controller(&window);
      LOKA_VERIFY(!controller.paintSuppressClipRgn_);
      LOKA_VERIFY(toolbox_host::failRegions == 0);
      ScrollViewNode scroll((ScrollViewProps()));
      HostControl control = {0};
      ToolboxScenePlatformController::ScrollBarControlBinding row =
          ToolboxScenePlatformController::ScrollBarControlBinding();
      row.resourceId = 100;
      row.control = &control;
      controller.installScroll(row);
      const Rect previous = {2, 3, 90, 120};
      const Rect viewport = {0, 0, 34, 100};
      ClipRect(&previous);
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      LOKA_VERIFY(controller.ensureViewportScrollBarControl(viewport, &scroll, 200, 34, 17) == 17);
      const toolbox_host::ControlCalls &calls = toolbox_host::controlCalls;
      LOKA_VERIFY(calls.values == 1 && !EmptyRect(&calls.valueClip) && control.value == 17);
      LOKA_VERIFY(calls.hilites == 1 && !EmptyRect(&calls.hiliteClip));
      LOKA_VERIFY(calls.shows == 1 && !EmptyRect(&calls.showClip));
      controller.drawControlsInRect(viewport);
      LOKA_VERIFY(calls.draws == 1);
      LOKA_VERIFY(calls.drawClip.top == previous.top && calls.drawClip.left == previous.left
                  && calls.drawClip.bottom == previous.bottom && calls.drawClip.right == previous.right);
      const Rect unclipped = {-30000, -30000, 30000, 30000};
      ClipRect(&unclipped);
    }
    Pin("ToolboxScrollViewBandExitsAndEntries");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      LOKA_VERIFY(f.controller.scrollSpans() && f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollMaximum == 306);
      LOKA_VERIFY(f.step(17) == 3);
      const Rect exited = f.paint(0);
      LOKA_VERIFY(EmptyRect(&exited));
      LOKA_VERIFY(f.rect(2).top == 17);
      for (unsigned i = 3; i < 20; ++i) { const Rect r = f.paint(i); LOKA_VERIFY(EmptyRect(&r)); }
      LOKA_VERIFY(f.step(255) == 16); // Bounding union includes the seek gap.
      LOKA_VERIFY(f.rect(15).top == 0);
      for (unsigned i = 0; i < 15; ++i) { const Rect r = f.paint(i); LOKA_VERIFY(EmptyRect(&r)); }
      LOKA_VERIFY(f.step(272) == 3); // Nonzero seek, not a full layout visit.
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_NONE);
    }
    Pin("ToolboxScrollViewButtonFullPass");
    {
      ScrollFixture f(20, true);
      LOKA_VERIFY(f.step(0) == 20);
      LOKA_VERIFY(f.step(17) == 20);
      LOKA_VERIFY(!f.controller.scrollSpans());
    }
    Pin("ToolboxScrollViewEligibilityLoss");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      f.column->addChild(new ButtonNode(ButtonProps()));
      LOKA_VERIFY(f.step(17) == 21);
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
    }
    Pin("ToolboxScrollViewStaleLeafSamePassRepair");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      LOKA_VERIFY(AttributedText(Styled("abcd", FontSize<24>())).applyPropsToNode(f.leaf(4)));
      LOKA_VERIFY(f.step(17) == 3);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollMaximum == 306);
      LOKA_VERIFY(f.step(51) == 24); // Four detection visits, then all twenty.
      LOKA_VERIFY(f.controller.scrollMaximum == 318);
      LOKA_VERIFY(f.rect(5).top == 46);
      LOKA_VERIFY(f.controller.scrollSpans()->total() == 352);
    }
    Pin("ToolboxScrollViewFontPropsRepair");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      for (unsigned i = 0; i < 20; ++i)
        LOKA_VERIFY(AttributedText(Styled("abcd", FontSize<24>())).applyPropsToNode(f.leaf(i)));
      LOKA_VERIFY(f.step(17) == 21);
      LOKA_VERIFY(f.controller.scrollSpans()->total() == 580);
      LOKA_VERIFY(f.controller.scrollMaximum == 546);
    }
    Pin("ToolboxScrollViewInvalidationAndReattach");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      LOKA_VERIFY(f.step(17, 101) == 20);
      LOKA_VERIFY(f.step(34, 101) == 3);
      LOKA_VERIFY(ScrollView().viewport(f.fact).applyPropsToNode(&f.scroll));
      LOKA_VERIFY(f.step(51, 101) == 20);
      f.controller.requestStructurePresent();
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.step(68, 101) == 20);
      NotifySubtreeNodeDetached(&f.scroll);
      LifecycleFactTestAccess::DeliverFacts(&f.scroll);
      NotifySubtreeNodeAttached(&f.scroll);
      LifecycleFactTestAccess::DeliverFacts(&f.scroll);
      LOKA_VERIFY(f.step(85, 101) == 20);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.step(85, 101, 34, SHRT_MAX) == 0);
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(f.step(85, 101) == 20);
      DisposeRgn(f.controller.scrollViewClipRgn_);
      f.controller.scrollViewClipRgn_ = 0;
      LOKA_VERIFY(f.step(102, 101) == 0);
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      f.controller.scrollViewClipRgn_ = NewRgn();
    }
    Pin("ToolboxScrollViewBoundaryStructureBeforeLayoutReturn");
    {
      ScrollFixture f;
      MeasurementBoundary boundary;
      LOKA_VERIFY(f.step(0) == 20);
      for (unsigned route = 0; route < 2; ++route)
      {
        BoundaryLocalApplyInfo info;
        info.hasLayoutWork = true;
        info.hasStructureWork = route == 0;
        PlatformApplyPlan plan;
        plan.layoutChanged = true;
        plan.structureChanged = route != 0;
        plan.setPrimaryRoot(&boundary);
        f.controller.onBoundaryApply(&f.scroll, &boundary, info, plan);
        LOKA_VERIFY(!f.controller.scrollSpans()->valid());
        LOKA_VERIFY(f.step(17 + 17 * route) == 20);
      }
      // Same-count off-screen replacement crosses the real release/present door.
      AttributedTextNode *old = f.leaf(18);
      Node *replacement = new AttributedTextNode(AttributedText(Styled("new", FontSize<24>())).props);
      LifecycleFactTestAccess::MarkSubtreeRetired(old);
      f.controller.releaseNodeContexts(old);
      LOKA_VERIFY(f.column->replaceChild(old, replacement));
      delete old;
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.step(51) == 20);
      // Context creation may revoke an in-progress derived capture; the next
      // full pass collects once every context is installed.
      LOKA_VERIFY(f.step(68) == 20);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollSpans()->total() == 352);
    }
    Pin("ToolboxScrollViewLeafMeasurementRefusal");
    loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Break", 0);
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0) == 20);
      LOKA_VERIFY(AttributedText(Styled("changed", FontSize<24>())).applyPropsToNode(f.leaf(1)));
      loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Break", 1);
      f.step(17);
      LOKA_VERIFY(!f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(f.step(17) == 20);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
    }
    loka::core::testing::allowLokaAllocRaw();
    Pin("ToolboxScrollViewAllocationRefusalFullPlacement");
    const char *owners[] = {"ToolboxScrollView", "Managed", "TextLineBreaker"};
    const char *purposes[] = {"StackSpans", "ControlBlock", "Table"};
    for (unsigned failure = 0; failure < 3; ++failure)
    {
      loka::core::testing::failLokaAllocRaw(owners[failure], purposes[failure], 0);
      {
        ScrollFixture f(100);
        // Warm leaf tables so the injected table failure selects StackSpans only.
        LOKA_VERIFY(f.step(0) == 100);
        f.controller.destroyViewportScrollBarControl(&f.scroll, f.scroll.nativeLifetimeHint());
        loka::core::testing::failLokaAllocRaw(owners[failure], purposes[failure], 1);
        LOKA_VERIFY(f.step(17) == 100);
        loka::core::testing::failLokaAllocRaw(owners[failure], purposes[failure], 0);
        LOKA_VERIFY(!f.controller.scrollSpans() || !f.controller.scrollSpans()->valid());
        LOKA_VERIFY(f.controller.scrollMaximum == 1666);
        LOKA_VERIFY(f.rect(2).top == 17);
        const Rect exited = f.paint(0);
        LOKA_VERIFY(EmptyRect(&exited));
        LOKA_VERIFY(f.step(34) == 100);
        LOKA_VERIFY(f.controller.scrollSpans()->valid());
        LOKA_VERIFY(f.step(51) == 3);
      }
      loka::core::testing::allowLokaAllocRaw();
    }
    Pin("ToolboxScrollViewLeafContextRefusalRejectsCapture");
    {
      // A leaf whose context allocation is refused during the full capture must
      // not leave a zero-height span in a valid table (bot P2 on #1016): the
      // refused leaf leaves the pass refused, no table is retained, and the
      // next pass captures it in full. Characterization of the existing
      // refusal route; no production change was needed.
      loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Context", 0);
      ScrollFixture f(100);
      loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Context", 1);
      f.step(0);
      loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Context", 0);
      LOKA_VERIFY(!f.leaf(0)->getContext());
      LOKA_VERIFY(!f.controller.scrollSpans() || !f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.step(17) == 100);
      LOKA_VERIFY(f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollSpans()->total() == f.controller.scrollSpans()->end(99));
    }
    loka::core::testing::allowLokaAllocRaw();
  }
}
#endif
