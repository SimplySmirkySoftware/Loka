#ifndef LOKA_TOOLBOX_SCROLL_VIEW_PINS_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_PINS_HPP
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/ImageView.hpp"
#include "core/StateTracker.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include <vector>
#include "ToolboxScrollViewDecisions.hpp"

namespace
{
  struct ScrollFixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    loka::core::MutableState<loka::core::Frame> offset;
    loka::core::PushStateTracker tracker;
    NodeState<loka::core::Frame> fact;
    ScrollViewNode scroll;
    StackNode *column;
    explicit ScrollFixture(unsigned count = 20, bool button = false, const WindowProps &props = WindowProps())
        : window(props),
          port(window.window()),
          controller(&window),
          offset(loka::core::Frame(0, 0, 0, 0)),
          fact(&offset, &tracker),
          scroll(ScrollViewProps().viewport(fact)),
          column(new StackNode(StackProps(STACK_AXIS_COLUMN)))
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

  bool SameScrollRect(const Rect &a, const Rect &b)
  {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
  }

  void ScrollViewSizeBoxPins()
  {
    Pin("ToolboxScrollViewWindowSizeBox");
    for (unsigned mode = 0; mode != 4; ++mode)
    {
      const int margin = mode == 1 ? 12 : 0;
      ScrollFixture f(20, false, mode == 1 ? WindowProps() : WindowProps().contentMargin(0));
      Rect box;
      LOKA_VERIFY(f.window.querySizeBoxRect(box));
      LOKA_VERIFY(box.left == 345 && box.top == 245 && box.right == 360 && box.bottom == 260);
      if (mode == 3)
        f.column->addChild(new ImageViewNode(ImageViewProps().size(400, 10)));
      LayoutState seat = Seat(mode == 2 ? 100 : 360 - margin * 2);
      seat.x = margin;
      seat.y = margin;
      seat.height = mode == 2 ? 34 : 260 - margin * 2;
      seat.spacing = 0;
      f.controller.layoutScrollView(&f.scroll, seat, 0);
      const ToolboxScenePlatformController::ViewportScrollBarBinding &binding = f.controller.viewportScroll();
      const Rect expectedSeat = {static_cast<short>(margin),
                                 static_cast<short>(margin),
                                 static_cast<short>(mode == 2 ? 34 : 260 - margin),
                                 static_cast<short>(mode == 2 ? 100 : 360 - margin)};
      LOKA_VERIFY(SameScrollRect(binding.rect, expectedSeat));
      const Rect content = ToolboxScrollViewContentRect(
          ToolboxScrollViewFrameRect(
              expectedSeat, mode == 3, ToolboxScrollViewFrameEdges(expectedSeat, f.window.port.portRect, true)),
          mode == 3);
      LOKA_VERIFY(SameScrollRect(binding.contentRect, content));
      LOKA_VERIFY(content.left == margin && content.top == margin);
      LOKA_VERIFY(content.right == (mode == 2 ? 84 : mode == 1 ? 332 : 345));
      LOKA_VERIFY(content.bottom == (mode == 2 ? 34 : mode == 3 ? 245 : 260 - margin));
      const ToolboxScenePlatformController::ScrollBarControlBinding &vertical = f.controller.scrollControls()[0];
      LOKA_VERIFY(vertical.rect.left == content.right
                  && vertical.rect.right == expectedSeat.right + (mode == 0 || mode == 3 ? 1 : 0));
      LOKA_VERIFY(vertical.rect.top == (mode == 1 ? margin : -1));
      LOKA_VERIFY(vertical.rect.bottom == (mode == 2 ? 34 : 246));
      LOKA_VERIFY(vertical.maximum == (mode == 2 ? 306 : mode == 3 ? 105 : mode == 1 ? 104 : 80));
      LOKA_VERIFY(vertical.pageStep == (mode == 2 ? 33 : mode == 3 ? 244 : mode == 1 ? 235 : 259));
      if (mode == 3)
      {
        const ToolboxScenePlatformController::ScrollBarControlBinding &horizontal = f.controller.scrollControls()[1];
        LOKA_VERIFY(horizontal.rect.left == -1 && horizontal.rect.top == 245);
        LOKA_VERIFY(horizontal.rect.right == 346 && horizontal.rect.bottom == 261);
        LOKA_VERIFY(horizontal.maximum == 55 && horizontal.pageStep == 344);
      }
    }
    // Mode 0: a window with a size box; mode 1: a window that declines the
    // size box query (no grow box), so the bars run full length; mode 2: no
    // native window at all.
    for (unsigned mode = 0; mode != 3; ++mode)
    {
      ToolboxWindow window;
      window.answersSizeBox = mode == 0;
      ToolboxScenePlatformController controller(mode != 2 ? &window : 0);
      ScrollViewNode scroll((ScrollViewProps()));
      const Rect seat = {0, 0, 260, 370};
      LOKA_VERIFY(
          controller.ensureViewportScrollBarControl(seat, &scroll, 500, 244, 0, ToolboxScrollViewMetrics(46, 0), true)
          == 0);
      if (mode == 2)
      {
        // A missing native window cannot materialize either control.
        LOKA_VERIFY(controller.scrollControls().empty());
        continue;
      }
      LOKA_VERIFY(controller.scrollControls().size() == 2);
      // Without a size box the bars stop at each other and leave the corner
      // empty; with one, each shares the size box's line (#1406).
      LOKA_VERIFY(controller.scrollControls()[0].rect.bottom == (mode == 0 ? 246 : 245));
      LOKA_VERIFY(controller.scrollControls()[1].rect.right == (mode == 0 ? 346 : 354));
      LOKA_VERIFY(controller.scrollControls()[1].maximum == 46 && controller.scrollControls()[1].pageStep == 353);
    }
    // Offset seats also exercise the horizontal stop and each empty-bar floor.
    const ToolboxScrollViewRect box(345, 245, 360, 260);
    const ToolboxScrollViewRect crossing(0, 0, 370, 260);
    LOKA_VERIFY(
        ToolboxScrollViewHorizontalBarRect(crossing, box, ToolboxScrollViewEdges(false, false, false, false)).right
        == 346);
    const ToolboxScrollViewRect low(344, 250, 360, 260);
    LOKA_VERIFY(
        ToolboxScrollViewVerticalBarRect(low, false, box, ToolboxScrollViewEdges(false, false, false, false)).bottom
        == 250);
    const ToolboxScrollViewRect right(350, 244, 380, 260);
    LOKA_VERIFY(ToolboxScrollViewHorizontalBarRect(right, box, ToolboxScrollViewEdges(false, false, false, false)).right
                == 350);
    const ToolboxScrollViewRect away(0, 0, 100, 260);
    LOKA_VERIFY(
        ToolboxScrollViewVerticalBarRect(away, false, box, ToolboxScrollViewEdges(false, false, false, false)).bottom
        == 260);
    LOKA_VERIFY(ToolboxScrollViewVerticalBarRect(
                    crossing, false, ToolboxScrollViewRect(), ToolboxScrollViewEdges(false, false, false, false))
                    .bottom
                == 260);
  }

  void ScrollViewHorizontalPins()
  {
    Pin("ToolboxScrollViewHorizontalGeometryAndRange");
    {
      ScrollFixture f;
      f.column->addChild(new ImageViewNode(ImageViewProps().size(400, 10)));
      f.step(0);
      const ToolboxScenePlatformController::ViewportScrollBarBinding &binding = f.controller.viewportScroll();
      LOKA_VERIFY(binding.barId[SCROLL_BAR_VERTICAL] != binding.barId[SCROLL_BAR_HORIZONTAL]);
      LOKA_VERIFY(f.controller.scrollControls().size() == 2);
      LOKA_VERIFY(!f.controller.scrollControl(0).editor.editor() && !f.controller.scrollControl(1).editor.editor());
      const ToolboxScenePlatformController::ScrollBarControlBinding &horizontal = f.controller.scrollControls()[1];
      LOKA_VERIFY(horizontal.resourceId == binding.barId[SCROLL_BAR_HORIZONTAL]);
      LOKA_VERIFY(horizontal.maximum == 316 && horizontal.pageStep == 83);
      const Rect expectedHorizontal = {18, -1, 34, 84};
      LOKA_VERIFY(SameScrollRect(horizontal.rect, expectedHorizontal));
      LOKA_VERIFY(f.controller.scrollMaximum(binding.barId[SCROLL_BAR_VERTICAL]) == 350 - 18);
      const Rect expectedVertical = {-1, 84, 18, 100};
      LOKA_VERIFY(SameScrollRect(f.controller.scrollControls()[0].rect, expectedVertical));
      const Rect seat = {0, 0, 34, 100};
      LOKA_VERIFY(SameScrollRect(binding.contentRect, ToolboxScrollViewContentRect(seat, true)));
    }
    Pin("ToolboxScrollViewHorizontalAbsentCases");
    for (unsigned mode = 0; mode < 3; ++mode)
    {
      ScrollFixture f;
      const ImageViewProps props = mode == 1
                                       ? ImageViewProps().attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC))
                                       : ImageViewProps().size(mode == 0 ? 50 : 400, 10);
      f.column->addChild(new ImageViewNode(props));
      f.step(0, 100, mode == 2 ? 0 : 34);
      LOKA_VERIFY(f.controller.scrollControls().size() == 1);
      LOKA_VERIFY(f.controller.scrollControls()[0].resourceId
                  == f.controller.viewportScroll().barId[SCROLL_BAR_VERTICAL]);
      const int contentHeight = 340 + props.height_;
      LOKA_VERIFY(f.controller.scrollMaximum() == contentHeight - (mode == 2 ? 0 : 34));
      const Rect seat = {0, 0, static_cast<short>(mode == 2 ? 0 : 34), 100};
      LOKA_VERIFY(SameScrollRect(f.controller.viewportScroll().contentRect, ToolboxScrollViewContentRect(seat, false)));
    }
    Pin("ToolboxScrollViewHorizontalDisappearsAndAxisCommit");
    {
      ScrollFixture f;
      f.column->addChild(new ImageViewNode(ImageViewProps().size(400, 10)));
      f.step(17, 100, 34, 0, 0, 50);
      ControlRef controls[2] = {f.controller.scrollControl(0).control, f.controller.scrollControl(1).control};
      f.step(17, 100, 34, 0, 0, 50);
      LOKA_VERIFY(f.controller.scrollControl(0).usedThisFrame && f.controller.scrollControl(1).usedThisFrame);
      controls[SCROLL_BAR_HORIZONTAL]->value = 120;
      f.controller.commitViewportScrollBarValue(
          f.controller.viewportScroll(), f.controller.scrollControl(SCROLL_BAR_HORIZONTAL), SCROLL_BAR_HORIZONTAL);
      LOKA_VERIFY(f.fact.get().x == 120 && f.fact.get().y == 17);
      controls[SCROLL_BAR_VERTICAL]->value = 40;
      f.controller.commitViewportScrollBarValue(
          f.controller.viewportScroll(), f.controller.scrollControl(SCROLL_BAR_VERTICAL), SCROLL_BAR_VERTICAL);
      LOKA_VERIFY(f.fact.get().x == 120 && f.fact.get().y == 40);
      f.controller.scrollControl(0).usedThisFrame = false;
      f.controller.scrollControl(1).usedThisFrame = false;
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      f.step(0, 500);
      LOKA_VERIFY(f.controller.scrollControls().size() == 2);
      LOKA_VERIFY(!f.controller.scrollControl(0).editor.editor() && !f.controller.scrollControl(1).editor.editor());
      LOKA_VERIFY(f.controller.scrollControl(0).usedThisFrame);
      LOKA_VERIFY(!f.controller.scrollControl(1).usedThisFrame);
      // The departing bar is hidden by the layout pass itself, before render.
      LOKA_VERIFY(toolbox_host::controlCalls.hides == 1
                  && toolbox_host::controlCalls.lastHidden == controls[SCROLL_BAR_HORIZONTAL]);
      LOKA_VERIFY(!controls[SCROLL_BAR_HORIZONTAL]->visible);
      LOKA_VERIFY(!f.window.port.native.update.empty());
      const std::size_t hidesBeforeChildren = toolbox_host::controlCalls.hides;
      f.leaf(0)->getContext()->render(&f.controller);
      LOKA_VERIFY(toolbox_host::controlCalls.hides == hidesBeforeChildren);
      const Rect seat = {0, 0, 34, 500};
      LOKA_VERIFY(SameScrollRect(f.controller.viewportScroll().contentRect, ToolboxScrollViewContentRect(seat, false)));
    }
    Pin("ToolboxScrollViewReleasesBothIdsWithoutNativeRows");
    {
      ScrollFixture f;
      f.step(0);
      const short vertical = f.controller.viewportScroll().barId[SCROLL_BAR_VERTICAL];
      const short horizontal = f.controller.viewportScroll().barId[SCROLL_BAR_HORIZONTAL];
      f.controller.destroyViewportScrollBarControl(&f.scroll, f.scroll.nativeLifetimeHint());
      const short first = f.controller.allocateControlId();
      const short second = f.controller.allocateControlId();
      LOKA_VERIFY(first == horizontal && second == vertical);
    }
  }

  void ScrollViewXPins()
  {
    ScrollViewHorizontalPins();
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
      loka::app::scene::ProjectionMeasurementChannel measurement;

      ProjectionViewportScopeGuard guard(f.controller.projectionParentScopes_, scope, measurement);
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

  void ScrollViewInsetPins()
  {
    Pin("ToolboxScrollViewInsetsPlaceContentBelowTheTopInset");
    {
      ScrollFixture f(1);
      f.scroll.props.contentInsets(ScrollViewContentInsets::of(12, 3));
      const short seatTop = 20;
      const short viewportHeight = 10;
      const int childHeight = 17;
      LOKA_VERIFY(f.step(ScrollViewProps::topOrigin().y, 100, viewportHeight, seatTop) == 1);
      LOKA_VERIFY(f.rect(0).top == seatTop + 12);
      LOKA_VERIFY(f.rect(0).bottom - f.rect(0).top == childHeight);
      LOKA_VERIFY(f.controller.scrollMaximum() == childHeight + 15 - viewportHeight);
      LOKA_VERIFY(f.fact.get().y == -12);
      LOKA_VERIFY(f.controller.scrollSpans() && f.controller.scrollSpans()->valid());
      LOKA_VERIFY(f.controller.scrollSpans()->placedViewport().y == -12);
      LOKA_VERIFY(f.step(6, 100, viewportHeight, seatTop) == 1);
      LOKA_VERIFY(f.rect(0).top == seatTop - 6);
      LOKA_VERIFY(f.controller.scrollMaximum() == childHeight + 15 - viewportHeight);
      LOKA_VERIFY(f.fact.get().y == 6);
      LOKA_VERIFY(f.controller.scrollSpans()->placedViewport().y == 6);
    }
    Pin("ToolboxScrollViewInsetsHeightlessExtentAndRefusal");
    for (unsigned banded = 0; banded != 2; ++banded)
    {
      ScrollFixture f(1);
      if (!banded)
        f.scroll.addChild(new FragmentNode(FragmentProps()));
      f.scroll.props.contentInsets(ScrollViewContentInsets::of(12, 3));
      LayoutState seat = Seat(100);
      seat.x = 0;
      seat.y = 20;
      seat.height = 0;
      seat.spacing = 0;
      f.controller.layoutScrollView(&f.scroll, seat, 0);
      LOKA_VERIFY(seat.y == 20 + 17 + 15);
      // Empty native rectangles have no row; the heightless extent is pinned above.
      LOKA_VERIFY(f.controller.scrollControls().empty());
      const std::size_t committedCalls = f.controller.scrollControls().size();
      seat.y = SHRT_MAX - 25;
      f.scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
      f.controller.layoutScrollView(&f.scroll, seat, 0);
      LOKA_VERIFY(seat.y == SHRT_MAX - 25);
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(f.controller.scrollControls().size() == committedCalls);
      LOKA_VERIFY(f.fact.get().y == 0);
      if (banded)
        LOKA_VERIFY(!f.controller.scrollSpans()->valid());
    }
  }

  void ScrollViewInsetExtentPins()
  {
    Pin("ToolboxScrollViewInsetsRefuseAnExtentAboveShort");
    {
      // A seat with a height has no heightless check: the native maximum would
      // wrap negative once content + insets passes the short range.
      ScrollFixture f(1);
      f.scroll.props.contentInsets(ScrollViewContentInsets::of(SHRT_MAX, 0));
      const std::size_t committedCalls = f.controller.scrollControls().size();
      f.step(0, 100, 10, 20);
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(f.controller.scrollControls().size() == committedCalls);
      LOKA_VERIFY(f.fact.get().y == 0);
    }
  }

  void ScrollViewPins()
  {
    ScrollViewInsetPins();
    ScrollViewInsetExtentPins();
    ScrollViewSizeBoxPins();
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 0);
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 39);
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 420);
    }
    Pin("ToolboxScrollBarSetterPaintSuppressed");
    {
      ScrollFixture f;
      const Rect previous = {2, 3, 90, 120};
      const Rect viewport = {0, 0, 34, 100};
      ClipRect(&previous);
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      f.step(0);
      HostControl &control = *f.controller.scrollControl(0).control;
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
      // Inspect the owner clip as well as the current-port counter: a global
      // clip fake would conceal suppression applied to the wrong window.
      for (std::size_t i = control.calls.size() - 3; i < control.calls.size(); ++i)
      {
        const toolbox_host::ControlCall &call = control.calls[i];
        LOKA_VERIFY(call.currentPort == f.window.window());
        LOKA_VERIFY(EmptyRect(&call.ownerClip) && !call.painted);
      }
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
      HostPortScope port(window.window());
      LOKA_VERIFY(!controller.paintSuppressClipRgn_);
      LOKA_VERIFY(toolbox_host::failRegions == 0);
      ScrollViewNode scroll((ScrollViewProps()));
      const Rect previous = {2, 3, 90, 120};
      const Rect viewport = {0, 0, 34, 100};
      ClipRect(&previous);
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      LOKA_VERIFY(controller.ensureViewportScrollBarControl(
                      viewport, &scroll, 200, 34, 17, ToolboxScrollViewMetrics(0, 0), false)
                  == 17);
      HostControl &control = *controller.scrollControl(0).control;
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 306);
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 306);
      LOKA_VERIFY(f.step(51) == 24); // Four detection visits, then all twenty.
      LOKA_VERIFY(f.controller.scrollMaximum() == 318);
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
      LOKA_VERIFY(f.controller.scrollMaximum() == 546);
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
    Pin("ToolboxScrollViewFrameShortRangeRefusal");
    {
      ScrollFixture f;
      f.window.port.portRect.right = SHRT_MAX;
      LOKA_VERIFY(f.step(0, SHRT_MAX) == 0);
      LOKA_VERIFY(f.controller.scrollControls().empty());
      LOKA_VERIFY(f.scroll.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
      f.window.port.portRect.right = 360;
      f.window.port.portRect.bottom = SHRT_MAX;
      LOKA_VERIFY(f.step(0, 100, SHRT_MAX) == 0);
      LOKA_VERIFY(f.controller.scrollControls().empty());
    }
    Pin("ToolboxScrollViewFrameWidthInvalidatesSpans");
    {
      ScrollFixture f;
      LOKA_VERIFY(f.step(0, 360) == 20);
      LOKA_VERIFY(f.step(17, 360) == 3);
      // The seat stays fixed while the window ceases to touch its right edge.
      // The stored content width must invalidate the formerly edge-wide capture.
      f.window.port.portRect.right = 361;
      LOKA_VERIFY(f.step(34, 360) == 20);
      LOKA_VERIFY(f.step(51, 360) == 3);
      f.window.port.portRect.right = 360;
      LOKA_VERIFY(f.step(68, 360) == 20);
      LOKA_VERIFY(f.step(85, 360) == 3);
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
        LOKA_VERIFY(f.controller.scrollMaximum() == 1666);
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
