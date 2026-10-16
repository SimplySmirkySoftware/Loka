#include "app/nodes/controls/Ribbon.hpp"
#include "ToolboxNodeDispatch.hpp"
#include <cassert>
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
      short width = 0;
      bool usedHandler = false;
      if (controller && controller->layoutHandlerRegistry())
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        usedHandler = ApplyToolboxPlatformLayoutHandler(
            *controller->layoutHandlerRegistry(), *box, state, traversal, width);
      }
      if (!usedHandler)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        width = static_cast<short>(ComputeToolboxBoxLayout(box, state, &traversal));
        state.y = traversal.layoutResultY();
      }
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_CARD:
    case loka::app::scene::NODE_KIND_RIBBON_GROUP:
    {
      loka::app::scene::Node *group = node;
      short width = 0;
      bool usedHandler = false;
      if (controller && controller->layoutHandlerRegistry())
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        usedHandler =
            ApplyToolboxPlatformLayoutHandler(*controller->layoutHandlerRegistry(), *group, state, traversal, width);
      }
      if (!usedHandler)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        width = static_cast<short>(ComputeToolboxFramedLayout(group, state, &traversal));
        state.y = traversal.layoutResultY();
      }
      if (boundary)
      {
        boundary->setLayoutBounds(startX, startY, width, static_cast<short>(state.y - startY));
      }
      return width;
    }
    case loka::app::scene::NODE_KIND_ZSTACK:
    {
      loka::app::ZStackNode *stack = static_cast<loka::app::ZStackNode *>(node);
      short maxWidth = 0;
      bool usedHandler = false;
      if (controller && controller->layoutHandlerRegistry())
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        usedHandler = ApplyToolboxPlatformLayoutHandler(
            *controller->layoutHandlerRegistry(), *stack, state, traversal, maxWidth);
      }
      if (!usedHandler)
      {
        ToolboxLayoutTraversal traversal(controller, activeBoundary);
        maxWidth = static_cast<short>(ComputeToolboxZStackLayout(stack, state, &traversal));
        state.y = traversal.layoutResultY();
      }
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
      short width = controller
                        ? controller->layoutScrollView(
                              static_cast<loka::app::ScrollViewNode *>(node),
                              state,
                              activeBoundary)
                        : 0;
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
    // Nodes without a built-in kind (boundaries, components, app-defined
    // containers) and these built-in containers render their children.
    case loka::app::scene::NODE_KIND_UNKNOWN:
    case loka::app::scene::NODE_KIND_BOX:
    case loka::app::scene::NODE_KIND_ZSTACK:
    case loka::app::scene::NODE_KIND_GRID:
    case loka::app::scene::NODE_KIND_CANVAS:
    case loka::app::scene::NODE_KIND_CARD:
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
