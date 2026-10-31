#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/Card.hpp"
#include "ToolboxNodeDispatch.hpp"
#include <cassert>
#include "app/nodes/ImageView.hpp"
#include "app/layout/StackGaps.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include <climits>
#include "app/layout/CanvasLayout.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "context/ToolboxRectSurfaceContext.hpp"
#include "context/ToolboxClipBoxContext.hpp"

  short LayoutChildren(loka::app::scene::INestable *nestable,
                       loka::app::scene::LayoutState &state,
                       ToolboxScenePlatformController *controller,
                       loka::app::scene::BoundaryNode *currentBoundary)
  {
    assert(controller && "Toolbox layout requires a controller");
    if (!nestable)
    {
      return 0;
    }
    short maxWidth = 0;
    loka::app::layout::StackGaps gaps(state.spacing);
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      const int childY = static_cast<int>(state.y) + gaps.before(child);
      if (controller && controller->refuseNarrowingInScrollScope(childY))
        break;
      state.y = static_cast<short>(childY);
      short width = LayoutNode(child, state, controller, currentBoundary);
      if (controller && controller->refuseNarrowingInScrollScope(state.y))
      {
        break;
      }
      if (width > maxWidth)
      {
        maxWidth = width;
      }
    }
    return maxWidth;
  }

namespace
{
  /** Dispatch asks once per visited node; O(1), walks no rows. */
  bool ToolboxPresentAdmitsKind(loka::app::scene::Node &node, loka::app::scene::BoundaryNode *boundary)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    if (boundary)
      return true;
    switch (node.kind())
    {
    case NODE_KIND_UNKNOWN:
      // Transparent components paint nothing; custom leaves may read undeclared state.
      return !node.asProjectedLayoutNode() && !node.asCanvasNode();
    case NODE_KIND_BOX:
      // Child clipping can change without leaf inputs.
      return node.asBoxNode()->props.childClip_ != layout::CHILD_CLIP_OWN_RECT;
    case NODE_KIND_IMAGE_VIEW:
    {
      // COVER PICTs have no trustworthy native clip until #1560.
      const ImageViewProps &props = static_cast<ImageViewNode &>(node).props;
      return !(props.hasAttr_ && props.attr_.hasFitValue_ && props.attr_.fitValue_ == IMAGE_FIT_COVER);
    }
    case NODE_KIND_STACK:
    case NODE_KIND_CELL:
    case NODE_KIND_CARD:
    case NODE_KIND_CARD_BAND:
    case NODE_KIND_TEXT:
    case NODE_KIND_ATTRIBUTED_TEXT:
    case NODE_KIND_BUTTON:
    case NODE_KIND_SCROLL_BAR:
      return true;
    case NODE_KIND_SCROLL_VIEW:
      // Nested viewport.
      return false;
    case NODE_KIND_ZSTACK:
    case NODE_KIND_GRID:
      // No trustworthy translated unit/grid clip.
      return false;
    case NODE_KIND_EDIT_TEXT:
    case NODE_KIND_POPUP_MENU:
    case NODE_KIND_OPEN_FILE_DIALOG:
    case NODE_KIND_RECT_SURFACE:
    case NODE_KIND_CANVAS:
    case NODE_KIND_TEXT_EDITOR:
    case NODE_KIND_CONFIRM_DIALOG:
    case NODE_KIND_RIBBON_GROUP:
    case NODE_KIND_RIBBON_GROUP_EDGE:
      // Paint can read state not covered by the sampled inputs.
      return false;
    }
    return false;
  }

  /** Restores the caller's gap on every dispatch return. */
  class LayoutSpacingScope
  {
  public:
    LayoutSpacingScope(loka::app::scene::LayoutState &state, loka::app::scene::Node *node)
        : state_(state),
          previous_(state.spacing)
    {
      this->state_.spacing = static_cast<short>(
          loka::app::layout::resolveLayoutGap(node, this->previous_, ToolboxLayoutMetrics::spacing()));
    }
    ~LayoutSpacingScope()
    {
      this->state_.spacing = this->previous_;
    }

  private:
    LayoutSpacingScope(const LayoutSpacingScope &);
    LayoutSpacingScope &operator=(const LayoutSpacingScope &);
    loka::app::scene::LayoutState &state_;
    const short previous_;
  };

  class ToolboxLayoutTraversal : public loka::app::scene::IPlatformLayoutTraversal
  {
  public:
    ToolboxLayoutTraversal(ToolboxScenePlatformController *controller, loka::app::scene::BoundaryNode *currentBoundary)
        : controller_(controller),
          currentBoundary_(currentBoundary),
          layoutResultY_(0)
    {
    }

    virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const
    {
      return this->controller_ && this->controller_->queryNaturalWidth(child, width);
    }

    virtual bool queryNaturalHeight(loka::app::scene::Node *child, short &height) const
    {
      return this->controller_ && this->controller_->queryNaturalHeight(child, height);
    }

    virtual int layoutChild(loka::app::scene::Node *child, const loka::app::scene::LayoutState &state)
    {
      loka::app::scene::LayoutState childState = state;
      const short width = LayoutNode(child, childState, controller_, currentBoundary_);
      if (controller_ && controller_->refuseNarrowingInScrollScope(childState.y))
      {
        return 0;
      }
      layoutResultY_ = childState.y;
      return width;
    }

    virtual bool refuseLayoutResultY(int y)
    {
      return this->controller_ ? this->controller_->refuseNarrowingInScrollScope(y)
                               : loka::app::scene::IPlatformLayoutTraversal::refuseLayoutResultY(y);
    }

    virtual void setLayoutResultY(short y)
    {
      layoutResultY_ = y;
    }

    virtual short layoutResultY() const
    {
      return layoutResultY_;
    }

  private:
    ToolboxScenePlatformController *controller_;
    loka::app::scene::BoundaryNode *currentBoundary_;
    short layoutResultY_;
  };

  /** Both Box/Card layout routes, unchanged beneath an optional clip frame. */
  short LayoutBoxContents(loka::app::scene::Node *node,
                          loka::app::scene::LayoutState &state,
                          ToolboxScenePlatformController *controller,
                          loka::app::scene::BoundaryNode *boundary)
  {
    short width = 0;
    ToolboxLayoutTraversal traversal(controller, boundary);
    if (!controller->layoutHandlerRegistry()
        || !ApplyToolboxPlatformLayoutHandler(*controller->layoutHandlerRegistry(), *node, state, traversal, width))
    {
      width = static_cast<short>(node->asBoxNode() ? ComputeToolboxBoxLayout(node->asBoxNode(), state, &traversal)
                                                   : ComputeToolboxFramedLayout(node, state, &traversal));
      state.y = traversal.layoutResultY();
    }
    return width;
  }

  short ClipFarEdge(int origin, int extent)
  {
    // Declared extents may be int-sized; clamp before addition can overflow.
    return static_cast<short>(extent > SHRT_MAX - origin ? SHRT_MAX : origin + extent);
  }

  /** One placement, with guard lifetime ending before the final fact is written.
      Cost: O(1) plus the existing child traversal; admission checks the stack's own depth. */
  short LayoutClipBox(loka::app::scene::Node *node,
                      loka::app::scene::LayoutState &state,
                      ToolboxScenePlatformController *controller,
                      loka::app::scene::BoundaryNode *boundary,
                      bool flagged,
                      int declaredWidth,
                      int declaredHeight)
  {
    using namespace loka::app::scene;
    const LayoutState incoming = state;
    // Mirrors macOS layoutClipBox in apple/macos/src/MacScenePlatformController.mm.
    NodeContext *installed = node->getContext();
    IClipBoxContext *clip = installed ? installed->asClipBoxContext() : 0;
    if (flagged && installed && !clip)
    {
#ifdef LOKA_LIFECYCLE_AUDIT
      assert(false && "clip box requires its own context; never replace a foreign context");
#endif
      return LayoutBoxContents(node, state, controller, boundary);
    }
    if (flagged)
      controller->ensureClipBoxContext(node, incoming);
    installed = node->getContext();
    clip = installed ? installed->asClipBoxContext() : 0;
    ToolboxClipBoxContext *context = clip ? static_cast<ToolboxClipBoxContext *>(clip) : 0;
    LayoutState projected;
    short width = 0;
    ToolboxClipPlacement placement;
    if (flagged && context && controller->projectionParentScopes_.current().project(incoming, projected))
    {
      ProjectionParentScopeStack &stack = controller->projectionParentScopes_;
      const loka::core::Frame &parentClip = stack.current().clipRect;
      int extent;
      const bool resolvedWidth = loka::app::layout::ResolveClipBoxWidth(declaredWidth, incoming.width, extent);
      Rect provisional = {
          projected.y,
          projected.x,
          declaredHeight > 0 ? ClipFarEdge(projected.y, declaredHeight)
                             : static_cast<short>(stack.activeDepth() ? parentClip.y + parentClip.height : SHRT_MAX),
          resolvedWidth ? ClipFarEdge(projected.x, extent)
                        : static_cast<short>(stack.activeDepth() ? parentClip.x + parentClip.width : SHRT_MAX)};
      // ROOT has no enclosing clip. Empty intersections still admit a frame.
      Rect clipped = provisional;
      if (stack.activeDepth() != 0)
        controller->intersectWithProjectionClip(provisional, clipped);
      ProjectionParentScope frame = stack.current();
      frame.clipRect =
          loka::core::Frame(clipped.left, clipped.top, clipped.right - clipped.left, clipped.bottom - clipped.top);
      {
        ProjectionClipScopeGuard guard(stack, frame);
        width = LayoutBoxContents(node, state, controller, boundary);
        if (guard.isActive())
          placement = ToolboxClipPlacement(provisional);
      }
    }
    else
      width = LayoutBoxContents(node, state, controller, boundary);

    if (flagged && node->asBoxNode() && incoming.width > 0 && width > incoming.width)
      width = incoming.width;
    if (context)
    {
      Rect rect;
      const int height = static_cast<int>(state.y) - incoming.y;
      if (placement.query(rect) && width > 0 && height > 0
          && !controller->projectionParentScopes_.current().hasShortRangeRefusal())
      {
        rect.right = ClipFarEdge(rect.left, width);
        rect.bottom = ClipFarEdge(rect.top, height);
        context->setPlacement(ToolboxClipPlacement(rect));
      }
      else
      {
        if (width > 0 && height > 0)
          LOKA_PAINT_REFUSE(*controller, TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
        context->setPlacement(ToolboxClipPlacement());
      }
    }
    return width;
  }

  class ActiveLayoutBoundaryScope
  {
  public:
    ActiveLayoutBoundaryScope(ToolboxScenePlatformController *controller, loka::app::scene::BoundaryNode *boundary)
        : controller_(controller),
          previous_(controller ? controller->activeLayoutBoundary() : 0)
    {
      if (controller_)
      {
        controller_->setActiveLayoutBoundary(boundary);
      }
    }

    ~ActiveLayoutBoundaryScope()
    {
      if (controller_)
      {
        controller_->setActiveLayoutBoundary(previous_);
      }
    }

  private:
    ToolboxScenePlatformController *controller_;
    loka::app::scene::BoundaryNode *previous_;
  };
} // namespace

  short LayoutNode(loka::app::scene::Node *node,
                   loka::app::scene::LayoutState &state,
                   ToolboxScenePlatformController *controller,
                   loka::app::scene::BoundaryNode *currentBoundary,
                   const loka::app::layout::LazyWindow *range,
                   loka::app::layout::StackSpans *spans)
  {
    assert(controller && "Toolbox layout requires a controller");
    if (!node)
    {
      return 0;
    }
    if (controller && controller->refuseNarrowingInScrollScope(state.y))
    {
      return 0;
    }
    LayoutSpacingScope spacingScope(state, node);
    loka::app::scene::BoundaryNode *boundary = node->asBoundary();
    if (controller->activeViewportContent_ && !node->asProjectedLayoutNode()
        && !ToolboxPresentAdmitsKind(*node, boundary))
      controller->activeViewportContent_->noteUnsupportedKind();
    loka::app::scene::BoundaryNode *activeBoundary = boundary ? boundary : currentBoundary;
    const short startX = state.x;
    const short startY = state.y;
    if (loka::app::scene::IProjectedLayoutNode *projected = node->asProjectedLayoutNode())
    {
      ActiveLayoutBoundaryScope boundaryScope(controller, activeBoundary);
      const loka::app::scene::Node::LayoutInputsCheckpoint inputs(*node);
      short width = projected->layoutProjected(controller, state);
      if (controller && !controller->restoreProjectedLayoutState(state))
      {
        inputs.requeue();
        return 0;
      }
      if (controller->activeViewportContent_ && !ToolboxPresentAdmitsKind(*node, boundary))
        controller->activeViewportContent_->noteUnsupportedKind();
      if (controller->activeViewportContent_ && node->kind() == loka::app::scene::NODE_KIND_CARD_BAND)
        controller->activeViewportContent_->notePhaseBound();
      if (controller->activeViewportContent_ && inputs.inputs() != loka::app::scene::NODE_DIRTY_NONE)
        controller->activeViewportContent_->noteContentInputs();
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    // Toolbox returns width and mutates Y; shared Canvas returns an int bottom.
    if (loka::app::CanvasNode *canvas = node->asCanvasNode())
    {
      loka::core::Frame extent;
      loka::app::CanvasLayoutStatus status =
          loka::app::layout::CanvasPlatformLayoutHandler::contentExtent(*canvas, extent);
      if (status == loka::app::CANVAS_LAYOUT_READY &&
          (extent.width > SHRT_MAX || extent.height > SHRT_MAX - state.y))
        status = loka::app::CANVAS_LAYOUT_SHORT_RANGE_REFUSED;
      if (status != loka::app::CANVAS_LAYOUT_READY)
      {
        canvas->recordLayoutStatus(status);
        return 0;
      }
      ToolboxLayoutTraversal traversal(controller, activeBoundary);
      loka::app::layout::CanvasPlatformLayoutHandler handler;
      state.y = static_cast<short>(handler.layoutNode(canvas, state, &traversal));
      return static_cast<short>(extent.width);
    }
    switch (node->kind())
    {
    case loka::app::scene::NODE_KIND_STACK:
    {
      loka::app::StackNode *stack = static_cast<loka::app::StackNode *>(node);
      short width = 0;
      bool usedHandler = false;
      if (controller && controller->layoutHandlerRegistry())
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        usedHandler = ApplyToolboxPlatformLayoutHandler(
            *controller->layoutHandlerRegistry(), *stack, state, traversal, width, range, spans);
      }
      if (!usedHandler && spans) spans->invalidate();
      if (!usedHandler)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        if (stack->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN)
        {
          width = static_cast<short>(ComputeToolboxColumnLayout(stack, state, &traversal));
        }
        else
        {
          width = static_cast<short>(ComputeToolboxRowLayout(stack, state, &traversal));
        }
        state.y = traversal.layoutResultY();
      }
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_BOX:
    {
      loka::app::BoxNode *box = static_cast<loka::app::BoxNode *>(node);
      const short width = LayoutClipBox(
          box,
          state,
          controller,
          activeBoundary,
          box->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT,
          box->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT ? box->props.effectiveWidth() : 0,
          box->props.height);
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_CARD:
    case loka::app::scene::NODE_KIND_RIBBON_GROUP:
    {
      const bool card = node->kind() == loka::app::scene::NODE_KIND_CARD;
      const short width = card ? LayoutClipBox(node,
                                               state,
                                               controller,
                                               activeBoundary,
                                               static_cast<loka::app::CardNode *>(node)->props.childClip_
                                                   == loka::app::layout::CHILD_CLIP_OWN_RECT,
                                               0,
                                               0)
                               : LayoutBoxContents(node, state, controller, activeBoundary);
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_ZSTACK:
    {
      loka::app::ZStackNode *stack = static_cast<loka::app::ZStackNode *>(node);
      const short maxWidth = controller->layoutZStack(stack, state, activeBoundary);
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, maxWidth, static_cast<short>(state.y - startY));
      }
      return maxWidth;
    }
    case loka::app::scene::NODE_KIND_GRID:
    {
      loka::app::GridNode *grid = static_cast<loka::app::GridNode *>(node);
      short maxWidth = 0;
      bool usedHandler = false;
      if (controller && controller->layoutHandlerRegistry())
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        usedHandler = ApplyToolboxPlatformLayoutHandler(
            *controller->layoutHandlerRegistry(), *grid, state, traversal, maxWidth);
      }
      if (!usedHandler)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        maxWidth = static_cast<short>(ComputeToolboxGridLayout(grid, state, &traversal));
        state.y = traversal.layoutResultY();
      }
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, maxWidth, static_cast<short>(state.y - startY));
      }
      return maxWidth;
    }
    case loka::app::scene::NODE_KIND_RECT_SURFACE:
    {
      loka::app::RectSurfaceNode *surface = static_cast<loka::app::RectSurfaceNode *>(node);
      if (controller)
      {
        EnsureToolboxRectSurfaceContext(surface, controller);
      }
      if (surface->getContext())
      {
        ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(surface->getContext());
        ctx->setBoundary(activeBoundary);
      }
      loka::app::scene::LayoutState projectedState = state;
      const short seatX = state.x;
      const short seatY = static_cast<short>(state.y);
      const short resolvedWidth =
          surface->props.width_ > 0 ? surface->props.width_ : state.width;
      const short resolvedHeight =
          surface->props.height_ > 0 ? surface->props.height_ : state.height;
      projectedState.width = resolvedWidth;
      projectedState.height = resolvedHeight;
      if (controller)
      {
        // RectSurface is the one hand-routed projected leaf in this switch;
        // give it the same translation and restore discipline as handler-
        // backed leaves.
        if (!controller->projectLayoutState(projectedState))
        {
          return 0;
        }
      }
      // The hand-routed leaf must refuse before its context narrows the seat
      // or consumes layout inputs. Scroll refusal also stops later siblings;
      // outside a ScrollView the same unrepresentable seat still refuses.
      const int right = static_cast<int>(projectedState.x) + resolvedWidth;
      const int bottom = static_cast<int>(projectedState.y) + resolvedHeight;
      if (right < SHRT_MIN || right > SHRT_MAX
          || bottom < SHRT_MIN || bottom > SHRT_MAX)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        traversal.refuseLayoutResultY(right);
        traversal.refuseLayoutResultY(bottom);
        return 0;
      }
      const loka::app::scene::Node::LayoutInputsCheckpoint inputs(*node);
      short width = node->layout(controller, projectedState);
      if (controller && !controller->restoreProjectedLayoutState(projectedState))
      {
        inputs.requeue();
        return 0;
      }
      if (controller && surface->getContext())
      {
        // The seat is a fact only once the surface was actually placed: a
        // refused projection or restore, or a surface without a context,
        // records nothing.
        controller->recordRectSurfaceExtent(
            surface, loka::core::Frame(seatX, seatY, resolvedWidth, resolvedHeight));
      }
      state.y = projectedState.y;
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_SCROLL_VIEW:
    {
      short width = 0;
      if (controller)
      {
        width = loka::app::SettleScrollViewVisit(*controller,
                                                 &ToolboxScenePlatformController::layoutScrollView,
                                                 &ToolboxScenePlatformController::applyScrollViewRequest,
                                                 static_cast<loka::app::ScrollViewNode *>(node),
                                                 state,
                                                 activeBoundary)
                    .width;
        // The visit borrow closes before setLayoutBounds below.
      }
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width,
                                  static_cast<short>(state.y - startY));
      }
      return width;
    }
    // App-defined nodes report NODE_KIND_UNKNOWN (Node::kind()), so this case
    // is the open door: boundaries, components, and app-defined containers lay
    // out their children here; app-defined leaves were answered above.
    case loka::app::scene::NODE_KIND_UNKNOWN:
      break;
    // Built-in kinds answered above by asProjectedLayoutNode() or asCanvasNode().
    case loka::app::scene::NODE_KIND_CELL:
    case loka::app::scene::NODE_KIND_TEXT:
    case loka::app::scene::NODE_KIND_BUTTON:
    case loka::app::scene::NODE_KIND_EDIT_TEXT:
    case loka::app::scene::NODE_KIND_POPUP_MENU:
    case loka::app::scene::NODE_KIND_OPEN_FILE_DIALOG:
    case loka::app::scene::NODE_KIND_IMAGE_VIEW:
    case loka::app::scene::NODE_KIND_SCROLL_BAR:
    case loka::app::scene::NODE_KIND_CANVAS:
    case loka::app::scene::NODE_KIND_ATTRIBUTED_TEXT:
    case loka::app::scene::NODE_KIND_TEXT_EDITOR:
    case loka::app::scene::NODE_KIND_CONFIRM_DIALOG:
    case loka::app::scene::NODE_KIND_CARD_BAND:
    case loka::app::scene::NODE_KIND_RIBBON_GROUP_EDGE:
      assert(false && "asProjectedLayoutNode() or asCanvasNode() answered above");
      break;
    }
    short width = LayoutChildren(node->asNestable(), state, controller, activeBoundary);
    if (boundary)
    {
      boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
    }
    return width;
  }
  void RenderChildren(loka::app::scene::INestable *nestable, ToolboxScenePlatformController *controller)
  {
    if (!nestable)
    {
      return;
    }
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      RenderNode(child, controller);
    }
  }

  void RenderNode(loka::app::scene::Node *node, ToolboxScenePlatformController *controller)
  {
    if (!node)
    {
      return;
    }
    if (node->asProjectedLayoutNode())
    {
      node->render(controller);
      return;
    }
    switch (node->kind())
    {
    case loka::app::scene::NODE_KIND_STACK:
      RenderChildren(node->asNestable(), controller);
      return;
    case loka::app::scene::NODE_KIND_RECT_SURFACE:
      node->render(controller);
      return;
    case loka::app::scene::NODE_KIND_SCROLL_VIEW:
      if (controller)
      {
        controller->renderScrollView(
            static_cast<loka::app::ScrollViewNode *>(node));
      }
      return;
    case loka::app::scene::NODE_KIND_ZSTACK:
      controller->renderZStack(static_cast<loka::app::ZStackNode *>(node));
      return;
    case loka::app::scene::NODE_KIND_BOX:
      if (controller && node->asBoxNode()->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT)
      {
        controller->renderClipBox(node);
        return;
      }
      break;
    case loka::app::scene::NODE_KIND_CARD:
      if (controller
          && static_cast<loka::app::CardNode *>(node)->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT)
      {
        controller->renderClipBox(node);
        return;
      }
      break;
    // Nodes without a built-in kind (boundaries, components, app-defined
    // containers) and these built-in containers render their children.
    case loka::app::scene::NODE_KIND_UNKNOWN:
    case loka::app::scene::NODE_KIND_GRID:
    case loka::app::scene::NODE_KIND_CANVAS:
    case loka::app::scene::NODE_KIND_RIBBON_GROUP:
      break;
    // Built-in kinds answered above by asProjectedLayoutNode().
    case loka::app::scene::NODE_KIND_CELL:
    case loka::app::scene::NODE_KIND_TEXT:
    case loka::app::scene::NODE_KIND_BUTTON:
    case loka::app::scene::NODE_KIND_EDIT_TEXT:
    case loka::app::scene::NODE_KIND_POPUP_MENU:
    case loka::app::scene::NODE_KIND_OPEN_FILE_DIALOG:
    case loka::app::scene::NODE_KIND_IMAGE_VIEW:
    case loka::app::scene::NODE_KIND_SCROLL_BAR:
    case loka::app::scene::NODE_KIND_ATTRIBUTED_TEXT:
    case loka::app::scene::NODE_KIND_TEXT_EDITOR:
    case loka::app::scene::NODE_KIND_CONFIRM_DIALOG:
    case loka::app::scene::NODE_KIND_CARD_BAND:
    case loka::app::scene::NODE_KIND_RIBBON_GROUP_EDGE:
      assert(false && "asProjectedLayoutNode() answered above");
      break;
    }
    RenderChildren(node->asNestable(), controller);
  }
