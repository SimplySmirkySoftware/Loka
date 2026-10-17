#include "app/nodes/controls/Ribbon.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "context/ToolboxTextEditorContext.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include "ToolboxWindow.hpp"
#include "core/Profiler.hpp"
#include <Quickdraw.h>
#include <Controls.h>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <string>
#include <ctime>
#include <climits>
#include <Memory.h>
#include <Menus.h>

#include "platform/StringUTF8.hpp"
#include "core/String.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "context/ToolboxProjectedNodeContext.hpp"
#include "context/ToolboxPopupMenuContext.hpp"
#include "context/ToolboxButtonContext.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "context/ToolboxTextContext.hpp"
#include "context/ToolboxImageViewContext.hpp"
#include "context/ToolboxRectSurfaceContext.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "context/ToolboxAttributedTextContext.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"

namespace
{
  const char kViewportPaintWidenReason[] = "paint-widened-viewport-render";

#include "ToolboxPaintAnswers.hpp"

#if !defined(pushButProc) && !defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  enum
  {
    pushButProc = 0
  };
#endif

  static const short kAutoControlBaseId = 128;

  void DrawStringAt(short x, short y, const loka::core::String &value)
  {
    Str255 text;
    if (!ToolboxEncodePascal(value, text))
    {
      return;
    }
    MoveTo(x, y);
    DrawString(text);
  }

  bool UseBoundaryDirty(const loka::app::scene::BoundaryNode *boundary)
  {
    return boundary && boundary->parentBoundary() && boundary->hasLayoutBounds();
  }

  Rect BoundaryToRect(const loka::app::scene::BoundaryNode *boundary, const Rect &fallback)
  {
    if (!UseBoundaryDirty(boundary))
    {
      return fallback;
    }
    const loka::app::scene::BoundaryNode::LayoutBounds &bounds = boundary->layoutBounds();
    Rect rect;
    rect.left = static_cast<short>(bounds.x);
    rect.top = static_cast<short>(bounds.y);
    rect.right = static_cast<short>(bounds.x + bounds.width);
    rect.bottom = static_cast<short>(bounds.y + bounds.height);
    return rect;
  }

  short MaxExplicitControlId(loka::app::scene::Node *node)
  {
    if (!node)
    {
      return 0;
    }
    short maxId = 0;
    if (loka::app::ButtonNode *button = node->asButtonNode())
    {
      short id = 0;
      if (button->props.controlTag_ > 0 && button->props.controlTag_ <= 32767)
      {
        id = static_cast<short>(button->props.controlTag_);
      }
      if (id > maxId)
      {
        maxId = id;
      }
    }
    if (loka::app::EditTextNode *edit = node->asEditTextNode())
    {
      short id = 0;
      if (edit->props.controlTag_ > 0 && edit->props.controlTag_ <= 32767)
      {
        id = static_cast<short>(edit->props.controlTag_);
      }
      if (id > maxId)
      {
        maxId = id;
      }
    }
    if (loka::app::PopupMenuNode *popup = node->asPopupMenuNode())
    {
      short id = 0;
      if (popup->props.controlTag_ > 0 && popup->props.controlTag_ <= 32767)
      {
        id = static_cast<short>(popup->props.controlTag_);
      }
      if (id > maxId)
      {
        maxId = id;
      }
    }
    if (loka::app::ScrollBarNode *scrollBar = node->asScrollBarNode())
    {
      short id = 0;
      if (scrollBar->props.controlTag_ > 0 && scrollBar->props.controlTag_ <= 32767)
      {
        id = static_cast<short>(scrollBar->props.controlTag_);
      }
      if (id > maxId)
      {
        maxId = id;
      }
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        short childMax = MaxExplicitControlId(child);
        if (childMax > maxId)
        {
          maxId = childMax;
        }
      }
    }
    return maxId;
  }

  bool HasRectSurfaceNode(loka::app::scene::Node *node)
  {
    if (!node)
    {
      return false;
    }
    if (node->kind() == loka::app::scene::NODE_KIND_RECT_SURFACE)
    {
      return true;
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        if (HasRectSurfaceNode(child))
        {
          return true;
        }
      }
    }
    return false;
  }

  bool HasImageViewNode(loka::app::scene::Node *node)
  {
    if (!node)
    {
      return false;
    }
    if (node->kind() == loka::app::scene::NODE_KIND_IMAGE_VIEW)
    {
      return true;
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        if (HasImageViewNode(child))
        {
          return true;
        }
      }
    }
    return false;
  }


  void RenderDirtyRectSurfaces(loka::app::scene::Node *node,
                               ToolboxScenePlatformController *controller,
                               const Rect &dirtyRect)
  {
    if (!node)
    {
      return;
    }
    if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
    {
      ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(surface->getContext());
      if (ctx)
      {
        ctx->renderDirty(dirtyRect);
      }
      return;
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        RenderDirtyRectSurfaces(child, controller, dirtyRect);
      }
    }
    (void)controller;
  }

  void RenderDirtyImageViews(loka::app::scene::Node *node,
                             ToolboxScenePlatformController *controller,
                             const Rect &dirtyRect)
  {
    if (!node)
    {
      return;
    }
    if (loka::app::ImageViewNode *image = node->asImageViewNode())
    {
      ToolboxImageViewContext *ctx = static_cast<ToolboxImageViewContext *>(image->getContext());
      Rect intersection;
      if (ctx && SectRect(&ctx->rect(), &dirtyRect, &intersection))
      {
        // Same erase-before-render ritual as composition replay. ZStack takes
        // that replay; disjoint ImageViews have only window ground beneath.
        EraseRect(&intersection);
        ctx->render(controller);
      }
      return;
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        RenderDirtyImageViews(child, controller, dirtyRect);
      }
    }
  }

  bool CollectRectSurfaceDirtyRect(loka::app::scene::Node *node, Rect &outRect, ToolboxSceneDebugStats &stats)
  {
    if (!node)
    {
      return false;
    }
    stats.noteCollectorVisit();
    bool hasRect = false;
    if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
    {
      ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(surface->getContext());
      if (ctx)
      {
        Rect rect;
        if (ctx->dirtyRect(rect))
        {
          outRect = rect;
          return true;
        }
      }
    }
    if (loka::app::scene::INestable *nestable = node->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      for (loka::app::scene::Node *child = it.next(); child; child = it.next())
      {
        Rect childRect;
        if (!CollectRectSurfaceDirtyRect(child, childRect, stats))
        {
          continue;
        }
        if (!hasRect)
        {
          outRect = childRect;
          hasRect = true;
        }
        else
        {
          if (childRect.left < outRect.left)
          {
            outRect.left = childRect.left;
          }
          if (childRect.top < outRect.top)
          {
            outRect.top = childRect.top;
          }
          if (childRect.right > outRect.right)
          {
            outRect.right = childRect.right;
          }
          if (childRect.bottom > outRect.bottom)
          {
            outRect.bottom = childRect.bottom;
          }
        }
      }
    }
    return hasRect;
  }

  bool ContainsOnlyRectSurfacePainting(loka::app::scene::Node *node, ToolboxSceneDebugStats &stats)
  {
    if (!node)
    {
      return false;
    }
    stats.noteCollectorVisit();
    if (node->asRectSurfaceNode())
    {
      return true;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    if (!nestable)
    {
      return false;
    }
    bool hasChild = false;
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      hasChild = true;
      if (!ContainsOnlyRectSurfacePainting(child, stats))
      {
        return false;
      }
    }
    return hasChild;
  }

  bool RectsIntersect(const Rect &a, const Rect &b)
  {
    if (a.right < b.left || a.left > b.right)
    {
      return false;
    }
    if (a.bottom < b.top || a.top > b.bottom)
    {
      return false;
    }
    return true;
  }
} // namespace

// Hard per-bucket bound so a one-off control spike cannot pin its high-water
// mark of native handles for the window's whole lifetime (4MB-class targets).
// Provisional policy: revisit with measured hit/miss/evict counters.
static const std::size_t kNativePoolBucketDepthCap = 8;

ToolboxScenePlatformController::ToolboxScenePlatformController(ToolboxWindow *window)
    : textShaping_(loka::app::PER_RUN),
      window_(window),
      projectionParentScopes_(window && window->window()
                                  ? static_cast<void *>(window->window())
                                  : 0),
      rootNode_(0),
      pendingRootNode_(0),
      rectSurfaceExtentLedger_(),
      scrollBarLedger_(kNativePoolBucketDepthCap),
      enabledStateBindingPath_(this),
      inBatchUpdate_(false),
      pendingFullInvalidate_(false),
      pendingInvalidateFlags_(loka::app::scene::NODE_DIRTY_NONE),
      forceFullRedraw_(false),
      pendingDirtyRects_(),
      retiredControls_(),
      retiredScrollBarControls_(),
      retiredTextEdits_(),
      pushButtonBucket_(kNativePoolBucketDepthCap),
      textEditBucket_(kNativePoolBucketDepthCap),
      poolIntakeAuditFailCount_(0),
      clipRgn_(NewRgn()),
      scrollViewClipRgn_(NewRgn()),
      paintSuppressClipRgn_(NewRgn()),
      hasClip_(false),
      controlIds_(kAutoControlBaseId),
      debugStats_(),
      activeLayoutBoundary_(0)
{
  const bool layoutHandlersRegistered = RegisterToolboxPlatformLayoutHandlers(this->layoutHandlerRegistry_);
  (void)layoutHandlersRegistered;
  assert(layoutHandlersRegistered && "Toolbox layout handler registration failed at boot");
  const bool builtInsRegistered = RegisterToolboxBuiltInSupport(*this);
  (void)builtInsRegistered;
  // A refused HandlerEntry allocation would leave that node kind silently
  // unprojectable for this controller's lifetime; surface it at boot.
  assert(builtInsRegistered && "Toolbox built-in node handler registration failed at boot");
}

ToolboxScenePlatformController::~ToolboxScenePlatformController()
{
  clearTextBindings();
  clearControls();
  flushRetiredNativeHandles();
  drainNativeHandleBuckets();
  if (clipRgn_)
  {
    DisposeRgn(clipRgn_);
    clipRgn_ = 0;
  }
  if (scrollViewClipRgn_)
  {
    DisposeRgn(scrollViewClipRgn_);
    scrollViewClipRgn_ = 0;
  }
  if (paintSuppressClipRgn_)
  {
    DisposeRgn(paintSuppressClipRgn_);
    paintSuppressClipRgn_ = 0;
  }
}

short ToolboxScenePlatformController::measureTextWidth(
    const loka::core::String &value,
    const ToolboxTextFontDescriptor &descriptor) const
{
  ToolboxTextMeasureScope scope(*this, descriptor);
  return scope.measure(value);
}

bool ToolboxScenePlatformController::registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler)
{
  if (!handler || IsToolboxPaintDrawerType(handler->nodeTypeKey()))
    return false;
  return this->nodeHandlerRegistry_.registerHandler(handler);
}

bool ToolboxScenePlatformController::prepareProjectedLayout(loka::app::scene::Node *node,
                                                            loka::app::scene::LayoutState &state)
{
  if (!node)
  {
    return false;
  }
  if (!this->projectLayoutState(state))
  {
    return false;
  }
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlerRegistry_.find(node);
  if (!handler)
  {
    assert(false && "no node handler registered for this node type -- register the handler or an explicit RefusedNodeHandler");
    return false;
  }
  loka::app::scene::NodeContext *previousContext = node->getContext();
  loka::app::scene::NodeContext *context = handler->ensureContext(node, this, state);
  if (!context)
  {
    return false;
  }
  if (context != previousContext)
  {
    this->requestStructurePresent();
  }
  // Type-safe hookup: only contexts that opt in through asBoundaryTagged
  // receive the tag, so a foreign handler returning a plain NodeContext (the
  // registry is public API) can never be written through a wrong downcast.
  // The OpenFileDialog context simply does not opt in.
  if (loka::app::scene::IBoundaryTaggedContext *tagged = context->asBoundaryTagged())
  {
    tagged->setBoundary(this->activeLayoutBoundary());
  }
  return true;
}

void ToolboxScenePlatformController::refuseScrollViewShortRange()
{
  if (this->projectionParentScopes_.activeDepth() != 0)
  {
    this->projectionParentScopes_.current().markShortRangeRefused();
  }
}

bool ToolboxScenePlatformController::refuseNarrowingInScrollScope(int resultY)
{
  if (this->projectionParentScopes_.activeDepth() == 0)
  {
    return false;
  }
  loka::app::scene::ProjectionParentScope &scope =
      this->projectionParentScopes_.current();
  if (scope.hasShortRangeRefusal())
  {
    return true;
  }
  if (resultY >= SHRT_MIN && resultY <= SHRT_MAX)
  {
    return false;
  }
  scope.markShortRangeRefused();
  return true;
}

#include "ToolboxScrollViewLayout.cpp"

void ToolboxScenePlatformController::renderScrollView(
    loka::app::ScrollViewNode *scrollView)
{
  const ViewportScrollBarBinding *binding = 0;
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
  {
    if (this->scrollBarLedger_.viewportScrollBars_[i].scrollView == scrollView)
    {
      binding = &this->scrollBarLedger_.viewportScrollBars_[i];
      break;
    }
  }
  if (!binding || !binding->usedThisFrame ||
      !this->scrollViewClipRgn_ ||
      this->projectionParentScopes_.activeDepth() != 0)
  {
    return;
  }
  const Rect viewportRect = binding->contentRect;
  const loka::core::Frame viewportClip(
      viewportRect.left,
      viewportRect.top,
      viewportRect.right - viewportRect.left,
      viewportRect.bottom - viewportRect.top);
  loka::app::scene::ProjectionParentScope renderScope(
      this->projectionParentScopes_.current().nativeParent,
      0,
      0,
      viewportClip);

  GetClip(this->scrollViewClipRgn_);
  ClipRect(&viewportRect);
  {
    loka::app::scene::ProjectionMeasurementChannel measurement;

    loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, renderScope, measurement);
    if (scopeGuard.isActive())
    {
      RenderChildren(scrollView, this);
    }
  }
  SetClip(this->scrollViewClipRgn_);
}

short ToolboxScenePlatformController::allocateControlId()
{
  return controlIds_.allocate();
}

void ToolboxScenePlatformController::onChange(loka::app::scene::Node *rootNode,
                                              loka::app::scene::NodeDirtyFlags flags,
                                              bool fullRebuild)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  rootNode_ = rootNode;
  debugStats_.begin(flags, fullRebuild);
  debugStats_.lastRootPresent = (rootNode != 0);
  if (!window_ || !window_->window())
  {
    return;
  }
  if (inBatchUpdate_)
  {
    ++debugStats_.batchOnChangeCount;
    ++debugStats_.batchAccumOnChangeCount;
    if (!debugStats_.batchAccumTrace.empty())
    {
      debugStats_.batchAccumTrace += " ";
    }
    debugStats_.batchAccumTrace += ToolboxSceneDebugStats::flagsToString(flags);
    debugStats_.batchAccumTrace += "/";
    debugStats_.batchAccumTrace += fullRebuild ? "1" : "0";
    debugStats_.batchAccumTrace += "/";
    debugStats_.batchAccumTrace += rootNode ? "1" : "0";
    if (!rootNode)
    {
      ++debugStats_.batchNullRootCount;
      ++debugStats_.batchAccumNullRootCount;
    }
    if (fullRebuild)
    {
      ++debugStats_.batchFullRebuildCount;
      debugStats_.batchAccumFullRebuild = true;
    }
    if (flags != loka::app::scene::NODE_DIRTY_NONE)
    {
      ++debugStats_.batchNonNoneFlagsCount;
    }
    debugStats_.batchAccumFlags = static_cast<loka::app::scene::NodeDirtyFlags>(debugStats_.batchAccumFlags | flags);
    pendingInvalidateFlags_ = static_cast<loka::app::scene::NodeDirtyFlags>(pendingInvalidateFlags_ | flags);
    if (rootNode)
    {
      pendingRootNode_ = rootNode;
    }
    if (fullRebuild)
    {
      pendingFullInvalidate_ = true;
    }
    return;
  }
  requestInvalidateForChange(rootNode, flags, fullRebuild);
}

#include "ToolboxBoundaryApply.cpp"

void ToolboxScenePlatformController::synchronize()
{
  // Toolbox doesn't have a retained scene graph; rely on Update events.
}

bool ToolboxScenePlatformController::hasPendingSync() const
{
  return !this->retiredControls_.empty() || !this->retiredScrollBarControls_.empty()
         || !this->retiredTextEdits_.empty();
}

void ToolboxScenePlatformController::drainNativeRetirements()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  this->flushRetiredNativeHandles();
}

void ToolboxScenePlatformController::destroy()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  rootNode_ = 0;
  hitLedger_.popupHits_.clear();
  clearTextBindings();
  clearEnabledBindings();
  clearControls();
  flushRetiredNativeHandles();
  drainNativeHandleBuckets();
}


#include "ToolboxStructurePresent.cpp"

void ToolboxScenePlatformController::retireNodeContext(loka::app::scene::NodeContext *context,
                                                       loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  if (context)
  {
    std::vector<loka::core::State<loka::core::String> *> retiredTextStates;
    std::vector<loka::core::State<bool> *> retiredEnabledStates;

    this->retireEditTextControl(context, lifetimeHint);
    assert(!editControls_.contains(context) &&
           "detach must strip the context's native edit binding before context reclaim");

    for (size_t i = 0; i < hitLedger_.buttonHits_.size();)
    {
      if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.buttonHits_[i].context) == context)
      {
        retiredEnabledStates.push_back(hitLedger_.buttonHits_[i].enabled);
        hitLedger_.buttonHits_.erase(hitLedger_.buttonHits_.begin() + i);
      }
      else
      {
        ++i;
      }
    }
    for (size_t i = 0; i < hitLedger_.cellHits_.size();)
    {
      if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.cellHits_[i].context) == context)
      {
        retiredTextStates.push_back(hitLedger_.cellHits_[i].text);
        hitLedger_.cellHits_.erase(hitLedger_.cellHits_.begin() + i);
      }
      else
      {
        ++i;
      }
    }
    for (size_t i = 0; i < hitLedger_.popupHits_.size();)
    {
      if (static_cast<loka::app::scene::NodeContext *>(hitLedger_.popupHits_[i].context) == context)
      {
        retiredEnabledStates.push_back(hitLedger_.popupHits_[i].enabled);
        hitLedger_.popupHits_.erase(hitLedger_.popupHits_.begin() + i);
      }
      else
      {
        ++i;
      }
    }

    for (size_t i = 0; i < hitLedger_.editHits_.size();)
    {
      if (hitLedger_.editHits_[i].context == context)
      {
        loka::core::State<loka::core::String> *text = hitLedger_.editHits_[i].text;
        retiredTextStates.push_back(text);
        hitLedger_.editHits_.erase(hitLedger_.editHits_.begin() + i);
      }
      else
        ++i;
    }
    for (size_t i = 0; i < hitLedger_.textHits_.size();)
    {
      if (hitLedger_.textHits_[i].context == context)
      {
        retiredTextStates.push_back(hitLedger_.textHits_[i].text);
        hitLedger_.textHits_.erase(hitLedger_.textHits_.begin() + i);
      }
      else
        ++i;
    }

    for (size_t i = 0; i < retiredTextStates.size(); ++i)
    {
      loka::core::State<loka::core::String> *text = retiredTextStates[i];
      if (text && !this->hasLiveBinding(text))
      {
        this->unbindTextState(text);
      }
    }
    for (size_t i = 0; i < retiredEnabledStates.size(); ++i)
    {
      loka::core::State<bool> *enabled = retiredEnabledStates[i];
      if (enabled && !this->hasLiveBinding(enabled))
      {
        this->unbindEnabledState(enabled);
      }
    }
  }
}

void ToolboxScenePlatformController::refreshContextProps(loka::app::scene::Node *node, short buttonResourceId)
{
  if (!node || !node->getContext())
    return;
  loka::app::scene::NodeContext *context = node->getContext();
  loka::core::State<loka::core::String> *previousText = 0;
  loka::core::State<loka::core::String> *liveText = 0;
  loka::core::State<bool> *previousEnabled = 0;
  loka::core::State<bool> *enabled = 0;

  // Props apply only reconciles existing projections of this kind.
  if (node->kind() == loka::app::scene::NODE_KIND_TEXT)
  {
    for (size_t i = 0; i < this->hitLedger_.textHits_.size(); ++i)
    {
      TextHit &hit = this->hitLedger_.textHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = hit.context->projectedTextState();
      liveText = hit.context->liveTextState();
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_CELL)
  {
    for (size_t i = 0; i < this->hitLedger_.cellHits_.size(); ++i)
    {
      CellHit &hit = this->hitLedger_.cellHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = node->asCellNode()->props.text_;
      hit.emitter = node->asCellNode()->props.onClick_;
      liveText = hit.context->liveTextState();
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_EDIT_TEXT)
  {
    for (size_t i = 0; i < this->hitLedger_.editHits_.size(); ++i)
    {
      EditHit &hit = this->hitLedger_.editHits_[i];
      if (hit.context != context)
        continue;
      previousText = hit.text;
      hit.text = hit.context->projectedTextState();
      liveText = hit.text;
    }
    size_t editIndex = 0;
    if (this->editControls_.find(context, editIndex))
    {
      EditTextControlBinding &binding = this->editControls_[editIndex];
      previousText = binding.text;
      binding.text = context->projectedTextState();
      binding.textSeat = static_cast<ToolboxEditTextContext *>(context)->projectedWriteSeat();
      liveText = binding.text;
      if (previousText != liveText)
        this->syncEditTextFromState(binding);
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_BUTTON)
  {
    for (size_t i = 0; i < this->hitLedger_.buttonHits_.size(); ++i)
    {
      ButtonHit &hit = this->hitLedger_.buttonHits_[i];
      if (hit.context != context)
        continue;
      previousEnabled = hit.enabled;
      hit.enabled = node->asButtonNode()->props.enabled_;
      hit.emitter = node->asButtonNode()->props.onClick_;
      enabled = hit.enabled;
    }
    for (size_t i = 0; buttonResourceId && i < this->buttonControls_.size(); ++i)
    {
      ButtonControlBinding &binding = this->buttonControls_[i];
      if (binding.resourceId != buttonResourceId)
        continue;
      previousEnabled = binding.enabled;
      binding.enabled = node->asButtonNode()->props.enabled_;
      binding.emitter = node->asButtonNode()->props.onClick_;
      enabled = binding.enabled;
      const loka::app::ButtonProps &props = node->asButtonNode()->props;
      ReconcileToolboxButtonControl(binding.control,
          props.text_ ? props.text_->get() : loka::core::String::Literal("Button"), binding.enabled, binding.label);
    }
  }
  else if (node->kind() == loka::app::scene::NODE_KIND_POPUP_MENU)
  {
    for (size_t i = 0; i < this->hitLedger_.popupHits_.size(); ++i)
    {
      PopupHit &hit = this->hitLedger_.popupHits_[i];
      if (hit.context != context)
        continue;
      const loka::app::PopupMenuProps &props = node->asPopupMenuNode()->props;
      previousEnabled = hit.enabled;
      hit.enabled = props.enabled_;
      enabled = hit.enabled;
    }
  }
  ReconcileToolboxTextSubscription(*this, previousText, liveText);
  if (previousEnabled != enabled)
  {
    this->bindEnabledState(enabled);
    if (previousEnabled && !this->hasLiveBinding(previousEnabled))
      this->unbindEnabledState(previousEnabled);
  }
}

#include "ToolboxRender.cpp"

#include "ToolboxDirtyPresentation.cpp"

#include "ToolboxHitLedger.cpp"

#include "ToolboxInputPublication.cpp"

#include "ToolboxFocus.cpp"

void ToolboxScenePlatformController::bindTextState(loka::core::State<loka::core::String> *text)
{
  if (!text)
  {
    return;
  }
  for (size_t i = 0; i < boundTextStates_.size(); ++i)
  {
    if (boundTextStates_[i] == text)
    {
      return;
    }
  }
  boundTextStates_.push_back(text);
  TextBinding *binding = new TextBinding();
  binding->state = text;
  binding->controller = this;
  textBindings_.push_back(binding);
  text->bind(&ToolboxScenePlatformController::TextStateChangedThunk, binding, false, false, 0);
}

void ToolboxScenePlatformController::bindEnabledState(loka::core::State<bool> *enabled)
{
  this->enabledStateBindingPath_.bind(enabled);
}

void ToolboxScenePlatformController::unbindTextState(loka::core::State<loka::core::String> *text)
{
  for (size_t i = 0; i < boundTextStates_.size(); ++i)
  {
    if (boundTextStates_[i] != text)
    {
      continue;
    }
    TextBinding *binding = i < textBindings_.size() ? textBindings_[i] : 0;
    if (binding)
    {
      if (binding->state)
      {
        binding->state->unbind(&ToolboxScenePlatformController::TextStateChangedThunk, binding);
      }
      binding->state = 0;
      binding->controller = 0;
      delete binding;
    }
    boundTextStates_.erase(boundTextStates_.begin() + i);
    if (i < textBindings_.size())
    {
      textBindings_.erase(textBindings_.begin() + i);
    }
    return;
  }
}

void ToolboxScenePlatformController::unbindEnabledState(loka::core::State<bool> *enabled)
{
  this->enabledStateBindingPath_.unbind(enabled);
}

bool ToolboxScenePlatformController::hasLiveBinding(loka::core::State<loka::core::String> *text) const
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    if (editControls_[i].text == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.cellHits_.size(); ++i)
  {
    if (hitLedger_.cellHits_[i].context->liveTextState() == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.editHits_.size(); ++i)
  {
    if (hitLedger_.editHits_[i].text == text)
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    if (hitLedger_.textHits_[i].context->liveTextState() == text)
    {
      return true;
    }
  }
  return false;
}

void ToolboxScenePlatformController::handleTextChanged(loka::core::State<loka::core::String> *text)
{
  if (!window_)
  {
    return;
  }
  // Native controls are state mirrors, not paint alternatives. Synchronize
  // every matching TE record before the first-match paint registries below can
  // return; a State may legitimately feed both an EditText and a Text/Cell.
  const size_t editControlMatchCount = editControls_.forEachTextBinding(
      text,
      this,
      &ToolboxScenePlatformController::refreshEditTextBindingForStateChange);
  for (size_t i = 0; i < hitLedger_.cellHits_.size(); ++i)
  {
    CellHit &hit = hitLedger_.cellHits_[i];
    if (hit.text == text)
    {
      ++debugStats_.textChangedCellCount;
      if (inBatchUpdate_)
      {
        addPendingDirty(hit.rect);
      }
      else
      {
        ++debugStats_.textChangedImmediateInvalidateCount;
        window_->requestInvalidateRect(hit.rect);
      }
      return;
    }
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    TextHit &hit = hitLedger_.textHits_[i];
    if (hit.text == text)
    {
      ++debugStats_.textChangedTextCount;
      if (hit.needsRelayoutOnChange)
      {
        ++debugStats_.relayoutTextCount;
        std::string utf8;
        if (loka::platform::CollectUtf8(text->get(), utf8))
        {
          if (utf8.size() > 48)
          {
            utf8.erase(48);
          }
          debugStats_.relayoutTextPreview = utf8;
        }
        else
        {
          debugStats_.relayoutTextPreview.clear();
        }
        if (inBatchUpdate_)
        {
          pendingFullInvalidate_ = true;
        }
        else
        {
          ++debugStats_.textChangedImmediateInvalidateCount;
          window_->requestInvalidateWithReason("text_relayout");
        }
        return;
      }
      short measuredWidth = hit.context ? hit.context->visibleWidth()
                                        : this->measureTextWidth(text->get());
      const short maxWidth = static_cast<short>(hit.rect.right - hit.rect.left);
      if (maxWidth > 0 && measuredWidth > maxWidth)
      {
        measuredWidth = maxWidth;
      }
      Rect dirtyRect = hit.rect;
      short redrawWidth = hit.lastMeasuredWidth;
      if (measuredWidth > redrawWidth)
      {
        redrawWidth = measuredWidth;
      }
      if (maxWidth > 0 && redrawWidth > maxWidth)
      {
        redrawWidth = maxWidth;
      }
      dirtyRect.right = static_cast<short>(dirtyRect.left + redrawWidth);
      if (inBatchUpdate_)
      {
        addPendingDirty(dirtyRect);
      }
      else
      {
        ++debugStats_.textChangedImmediateInvalidateCount;
        window_->requestInvalidateRect(dirtyRect);
      }
      return;
    }
  }
  for (size_t i = 0; i < hitLedger_.editHits_.size(); ++i)
  {
    EditHit &hit = hitLedger_.editHits_[i];
    if (hit.text == text)
    {
      ++debugStats_.textChangedEditHitCount;
      // Use text's own rect
      if (inBatchUpdate_)
      {
        addPendingDirty(hit.rect);
      }
      else
      {
        ++debugStats_.textChangedImmediateInvalidateCount;
        window_->requestInvalidateRect(hit.rect);
      }
      return;
    }
  }
  if (editControlMatchCount != 0)
  {
    return;
  }
  // State not found in current hitLedger_.textHits_/hitLedger_.editHits_/editControls_.
  // Add to pending list; will be resolved after next render populates hitLedger_.textHits_.
  if (inBatchUpdate_)
  {
    ++debugStats_.textChangedPendingCount;
    addPendingText(text);
  }
  else
  {
    // State not found, but scene invalidation will handle it
    // through normal recompose cycle. No need for full redraw.
  }
}

void ToolboxScenePlatformController::refreshEditTextBindingForStateChange(EditTextControlBinding &binding)
{
  ++debugStats_.textChangedEditControlCount;
  syncEditTextFromState(binding);
  if (inBatchUpdate_)
  {
    addPendingDirty(binding.rect);
    return;
  }
  ++debugStats_.textChangedImmediateInvalidateCount;
  window_->requestInvalidateRect(binding.rect);
}

void ToolboxScenePlatformController::refreshPopupEnabled(PopupHit &binding)
{
  if (this->inBatchUpdate_)
  {
    this->addPendingDirty(binding.rect);
  }
  else
  {
    this->window_->requestInvalidateRect(binding.rect);
  }
}

void ToolboxScenePlatformController::refreshButtonEnabled(ButtonControlBinding &binding)
{
  if (binding.control)
  {
    if (binding.enabled->get())
    {
      HiliteControl(binding.control, 0);
    }
    else
    {
      HiliteControl(binding.control, 255);
    }
  }
}

void ToolboxScenePlatformController::refreshScrollBarEnabled(ScrollBarControlBinding &binding)
{
  // A disabled bar and an unscrollable one share the inactive
  // presentation, so re-derive from both rather than from enabled alone.
  binding.active = binding.enabled->get() && loka::app::ScrollBarIsScrollable(binding.minimum, binding.maximum);
  if (binding.control)
  {
    HiliteControl(binding.control, binding.active ? 0 : 255);
  }
}

void ToolboxScenePlatformController::refreshButtonHitEnabled(ButtonHit &binding)
{
  if (this->inBatchUpdate_)
  {
    this->addPendingDirty(binding.rect);
  }
  else
  {
    this->window_->requestInvalidateRect(binding.rect);
  }
}

bool ToolboxScenePlatformController::applyEnabledChangeForKind(
    ToolboxEnabledControlKind kind,
    loka::core::State<bool> *enabled)
{
  switch (kind)
  {
  case TOOLBOX_ENABLED_POPUP_HIT:
    return ApplyToolboxEnabledChangeToBindings(this->hitLedger_.popupHits_.begin(), this->hitLedger_.popupHits_.end(), enabled, *this, &ToolboxScenePlatformController::refreshPopupEnabled);
  case TOOLBOX_ENABLED_BUTTON_CONTROL:
    return ApplyToolboxEnabledChangeToBindings(this->buttonControls_.begin(), this->buttonControls_.end(), enabled, *this, &ToolboxScenePlatformController::refreshButtonEnabled);
  case TOOLBOX_ENABLED_SCROLL_BAR_CONTROL:
    return ApplyToolboxEnabledChangeToBindings(this->scrollBarLedger_.scrollBarControls_.begin(), this->scrollBarLedger_.scrollBarControls_.end(), enabled, *this, &ToolboxScenePlatformController::refreshScrollBarEnabled);
  case TOOLBOX_ENABLED_BUTTON_HIT:
    return ApplyToolboxEnabledChangeToBindings(this->hitLedger_.buttonHits_.begin(), this->hitLedger_.buttonHits_.end(), enabled, *this, &ToolboxScenePlatformController::refreshButtonHitEnabled);
  case TOOLBOX_ENABLED_CONTROL_KIND_COUNT:
    return false;
  }
  return false;
}

void ToolboxScenePlatformController::beginBatchUpdate()
{
  inBatchUpdate_ = true;
  pendingDirtyRects_.clear();
  pendingTextStates_.clear();
  pendingFullInvalidate_ = false;
  pendingInvalidateFlags_ = loka::app::scene::NODE_DIRTY_NONE;
  pendingRootNode_ = 0;
  debugStats_.batchAccumOnChangeCount = 0;
  debugStats_.batchAccumNullRootCount = 0;
  debugStats_.batchAccumFullRebuild = false;
  debugStats_.batchAccumFlags = loka::app::scene::NODE_DIRTY_NONE;
}

void ToolboxScenePlatformController::endBatchUpdate()
{
  inBatchUpdate_ = false;
  if (window_)
  {
    if (pendingRootNode_)
    {
      rootNode_ = pendingRootNode_;
    }
    const bool handledLocalDirty = !pendingDirtyRects_.empty();
    const bool handledLocalText = !pendingTextStates_.empty();
    const bool hasChildDirty = (pendingInvalidateFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0;
    const bool skipFollowupInvalidate =
        !pendingFullInvalidate_ && !hasChildDirty && (handledLocalDirty || handledLocalText);
    // Record pending dirty rects; the app presents them after dispatch.
    for (size_t i = 0; i < pendingDirtyRects_.size(); ++i)
    {
      window_->requestInvalidateRect(pendingDirtyRects_[i]);
    }
    for (size_t i = 0; i < pendingTextStates_.size(); ++i)
    {
      requestInvalidateForText(pendingTextStates_[i]);
    }
    if (!skipFollowupInvalidate)
    {
      requestInvalidateForChange(
          pendingRootNode_ ? pendingRootNode_ : rootNode_, pendingInvalidateFlags_, pendingFullInvalidate_);
    }
  }
  pendingDirtyRects_.clear();
  pendingTextStates_.clear();
  pendingFullInvalidate_ = false;
  pendingInvalidateFlags_ = loka::app::scene::NODE_DIRTY_NONE;
  pendingRootNode_ = 0;
}

void ToolboxScenePlatformController::addPendingDirty(const Rect &rect)
{
  for (size_t i = 0; i < pendingDirtyRects_.size(); ++i)
  {
    Rect &pending = pendingDirtyRects_[i];
    if (rect.right < pending.left || rect.left > pending.right || rect.bottom < pending.top
        || rect.top > pending.bottom)
    {
      continue;
    }
    if (rect.left < pending.left)
    {
      pending.left = rect.left;
    }
    if (rect.top < pending.top)
    {
      pending.top = rect.top;
    }
    if (rect.right > pending.right)
    {
      pending.right = rect.right;
    }
    if (rect.bottom > pending.bottom)
    {
      pending.bottom = rect.bottom;
    }
    return;
  }
  pendingDirtyRects_.push_back(rect);
}

void ToolboxScenePlatformController::addPendingText(loka::core::State<loka::core::String> *text)
{
  if (!text)
  {
    return;
  }
  for (size_t i = 0; i < pendingTextStates_.size(); ++i)
  {
    if (pendingTextStates_[i] == text)
    {
      return;
    }
  }
  pendingTextStates_.push_back(text);
}

void ToolboxScenePlatformController::requestInvalidateForChange(loka::app::scene::Node *rootNodeForChange,
                                                                loka::app::scene::NodeDirtyFlags flags,
                                                                bool fullRebuild)
{
  if (debugStats_.requestInvalidateCallCount == 0)
  {
    debugStats_.requestInvalidateFirstRootPresent = (rootNodeForChange != 0);
    debugStats_.requestInvalidateFirstFullRebuild = fullRebuild;
    debugStats_.requestInvalidateFirstFlags = flags;
  }
  ++debugStats_.requestInvalidateCallCount;
  debugStats_.requestInvalidateRootPresent = (rootNodeForChange != 0);
  debugStats_.requestInvalidateFullRebuild = fullRebuild;
  debugStats_.requestInvalidateFlags = flags;
  if (!window_ || !window_->window())
  {
    return;
  }
  if ((flags == loka::app::scene::NODE_DIRTY_NONE) && !fullRebuild)
  {
    return;
  }
  if (flags & loka::app::scene::NODE_DIRTY_CHILD)
  {
    // Classic redraw currently prefers broad invalidation for child changes.
    ++debugStats_.fullInvalidateRequests;
    ++debugStats_.totalFullInvalidateRequests;
    window_->requestInvalidateWithReason("child_dirty");
    return;
  }
  if (fullRebuild || !rootNodeForChange)
  {
    ++debugStats_.fullInvalidateRequests;
    ++debugStats_.totalFullInvalidateRequests;
    window_->requestInvalidateWithReason("full_rebuild_or_no_root");
    return;
  }

  Rect fallback = window_->window()->portRect;
  debugStats_.fallbackQueuedByChild = false;
  loka::app::scene::BoundaryNode *boundary = rootNodeForChange->asBoundary();
  debugStats_.fallbackRootIsBoundary = (boundary != 0);
  debugStats_.fallbackRootHasLayoutBounds = boundary && boundary->hasLayoutBounds();
  if (boundary && boundary->hasLayoutBounds())
  {
    ++debugStats_.rectInvalidateRequests;
    ++debugStats_.totalRectInvalidateRequests;
    window_->requestInvalidateRect(BoundaryToRect(boundary, fallback));
  }
  else
  {
    debugStats_.fallbackUsedFullInvalidate = true;
    ++debugStats_.rectInvalidateRequests;
    ++debugStats_.totalRectInvalidateRequests;
    window_->requestInvalidateRect(fallback);
  }
}

std::string ToolboxScenePlatformController::debugStatsSummary() const
{
  return debugStats_.summary();
}

void ToolboxScenePlatformController::resetDebugStats()
{
  debugStats_.reset();
  // The pool counters are measurements too; leaving them cumulative would
  // let the next sync copy pre-reset activity into the fresh stats.
  pushButtonBucket_.resetCounters();
  textEditBucket_.resetCounters();
  poolIntakeAuditFailCount_ = 0;
  syncNativePoolStats();
}

bool ToolboxScenePlatformController::dumpDebugStatsToTimestampedFile() const
{
#if LOKA_RETRO68_DIAGNOSTICS
  return debugStats_.dumpToTimestampedFile();
#else
  // Compact profile (#135): the dump chain is compiled out; report failure.
  return false;
#endif
}

void ToolboxScenePlatformController::redrawTextHit(TextHit &hit)
{
  if (!window_ || !window_->window())
  {
    return;
  }
  if (hit.context)
  {
    GrafPtr previous;
    GetPort(&previous);
    SetPort(this->window_->window());
    hit.context->repaint();
    hit.lastMeasuredWidth = hit.context->visibleWidth();
    SetPort(previous);
    return;
  }
  short measuredWidth = hit.text ? this->measureTextWidth(hit.text->get()) : 0;
  const short maxWidth = static_cast<short>(hit.rect.right - hit.rect.left);
  if (maxWidth > 0 && measuredWidth > maxWidth)
  {
    measuredWidth = maxWidth;
  }
  Rect dirtyRect = hit.rect;
  short redrawWidth = hit.lastMeasuredWidth;
  if (measuredWidth > redrawWidth)
  {
    redrawWidth = measuredWidth;
  }
  if (maxWidth > 0 && redrawWidth > maxWidth)
  {
    redrawWidth = maxWidth;
  }
  dirtyRect.right = static_cast<short>(dirtyRect.left + redrawWidth);
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_->window());
  EraseRect(&dirtyRect);
  RgnHandle oldClip = NewRgn();
  if (oldClip != 0)
  {
    GetClip(oldClip);
    ClipRect(&dirtyRect);
    if (hit.text)
      DrawStringAt(hit.x, hit.y, hit.text->get());
    SetClip(oldClip);
    DisposeRgn(oldClip);
  }
  else
  {
    if (hit.text)
      DrawStringAt(hit.x, hit.y, hit.text->get());
  }
  hit.lastMeasuredWidth = measuredWidth;
  SetPort(oldPort);
}

void ToolboxScenePlatformController::redrawPopupHit(const PopupHit &hit)
{
  if (!window_ || !window_->window() || !hit.context)
  {
    return;
  }
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_->window());
  hit.context->repaint();
  SetPort(oldPort);
}

void ToolboxScenePlatformController::requestInvalidateForText(loka::core::State<loka::core::String> *text)
{
  if (!window_ || !text)
  {
    return;
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    if (hitLedger_.textHits_[i].text == text)
    {
      window_->requestInvalidateRect(hitLedger_.textHits_[i].rect);
      return;
    }
  }
}

void ToolboxScenePlatformController::clearTextBindings()
{
  for (size_t i = 0; i < textBindings_.size(); ++i)
  {
    TextBinding *binding = textBindings_[i];
    if (binding)
    {
      if (binding->state)
      {
        binding->state->unbind(&ToolboxScenePlatformController::TextStateChangedThunk, binding);
      }
      binding->state = 0;
      binding->controller = 0;
      delete binding;
    }
  }
  textBindings_.clear();
  boundTextStates_.clear();
  hitLedger_.textHits_.clear();
}

void ToolboxScenePlatformController::clearEnabledBindings()
{
  this->enabledStateBindingPath_.clear();
}

#include "ToolboxNativeRetirement.cpp"

void ToolboxScenePlatformController::flushRetiredNativeHandles()
{
  flushRetiredEntriesInto(retiredControls_, pushButtonBucket_);
  flushRetiredEntriesInto(retiredScrollBarControls_, scrollBarLedger_.scrollBarBucket_);
  flushRetiredEntriesInto(retiredTextEdits_, textEditBucket_);
  syncNativePoolStats();
}

namespace
{
  void DisposePooledControl(ControlRef control)
  {
    if (control)
    {
      DisposeControl(control);
    }
  }

  void DisposePooledTextEdit(TEHandle te)
  {
    if (te)
    {
      TEDispose(te);
    }
  }
} // namespace

void ToolboxScenePlatformController::drainNativeHandleBuckets()
{
  pushButtonBucket_.drainWith(DisposePooledControl);
  scrollBarLedger_.scrollBarBucket_.drainWith(DisposePooledControl);
  textEditBucket_.drainWith(DisposePooledTextEdit);
  syncNativePoolStats();
}

void ToolboxScenePlatformController::syncNativePoolStats()
{
  debugStats_.refreshNativePoolCounters(pushButtonBucket_.hitCount(),
                                        pushButtonBucket_.missCount(),
                                        pushButtonBucket_.evictCount(),
                                        static_cast<int>(pushButtonBucket_.depth()),
                                        textEditBucket_.hitCount(),
                                        textEditBucket_.missCount(),
                                        textEditBucket_.evictCount(),
                                        static_cast<int>(textEditBucket_.depth()),
                                        static_cast<int>(scrollBarLedger_.scrollBarBucket_.depth()),
                                        poolIntakeAuditFailCount_);
}

bool ToolboxScenePlatformController::ensureButtonControl(short resourceId,
                                                         const Rect &rect,
                                                         const loka::core::String &label,
                                                         loka::core::EmitterState *emitter,
                                                         loka::core::State<bool> *enabled,
                                                         loka::app::scene::NativeLifetimeHint lifetimeHint,
                                                         ToolboxButtonContext *context)
{
  if (!window_ || !window_->window() || resourceId <= 0)
  {
    return false;
  }
  Rect controlRect;
  if (!this->intersectWithProjectionClip(rect, controlRect))
  {
    return true;
  }
  ButtonControlBinding *binding = 0;
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    if (buttonControls_[i].resourceId == resourceId)
    {
      binding = &buttonControls_[i];
      break;
    }
  }
  bool created = false;
  if (!binding)
  {
    ControlRef control = 0;
    if (pushButtonBucket_.tryAcquire(control))
    {
      // Pooled handles keep their last title; restore the fresh-created
      // blank so the label diff below runs against the same baseline.
      Str255 title;
      title[0] = 0;
      SetControlTitle(control, title);
    }
    else
    {
      Rect rectCopy = controlRect;
      Str255 title;
      title[0] = 0;
      control = NewControl(window_->window(), &rectCopy, title, false, 0, 0, 1, pushButProc, 0);
      if (!control)
      {
        return false;
      }
      HideControl(control);
    }
    ButtonControlBinding entry;
    entry.context = context;
    entry.resourceId = resourceId;
    entry.control = control;
    entry.emitter = emitter;
    entry.enabled = enabled;
    entry.usedThisFrame = true;
    entry.rect = controlRect;
    entry.label = "";
    entry.lifetimeHint = lifetimeHint;
    buttonControls_.push_back(entry);
    binding = &buttonControls_.back();
    created = true;
  }
  binding->context = context;
  binding->emitter = emitter;
  binding->enabled = enabled;
  binding->lifetimeHint = lifetimeHint;
  bindEnabledState(enabled);
  binding->usedThisFrame = true;
  if (created || binding->rect.left != controlRect.left || binding->rect.top != controlRect.top || binding->rect.right != controlRect.right
      || binding->rect.bottom != controlRect.bottom)
  {
    MoveControl(binding->control, controlRect.left, controlRect.top);
    SizeControl(binding->control, controlRect.right - controlRect.left, controlRect.bottom - controlRect.top);
    binding->rect = controlRect;
  }
  const bool submitted = ReconcileToolboxButtonControl(binding->control, label, binding->enabled, binding->label);
  ShowControl(binding->control);
  return submitted;
}

void ToolboxScenePlatformController::destroyButtonControl(short resourceId,
                                                           loka::app::scene::NativeLifetimeHint lifetimeHint)
{
  for (size_t i = 0; i < buttonControls_.size(); ++i)
  {
    ButtonControlBinding &binding = buttonControls_[i];
    if (binding.resourceId != resourceId)
    {
      continue;
    }
    if (binding.context)
      binding.context->forgetPresentedControl();
    ControlRef control = binding.control;
    binding.control = 0;
    binding.emitter = 0;
    loka::core::State<bool> *retiredEnabled = binding.enabled;
    binding.enabled = 0;
    buttonControls_.erase(buttonControls_.begin() + i);
    if (retiredEnabled && !this->hasLiveBinding(retiredEnabled))
      this->unbindEnabledState(retiredEnabled);
    controlIds_.release(resourceId);
    if (control)
    {
      // Context destruction can run inside an update pass; disposal waits for
      // the platform safe point like every other retired native handle.
      HideControl(control);
      queueRetiredControl(control, lifetimeHint);
    }
    return;
  }
}

#include "ToolboxScrollBarLedger.cpp"

void ToolboxScenePlatformController::drawFallbackControl(const Rect &rect)
{
  FrameRect(&rect);
  MoveTo(rect.left + 2, rect.top + 2);
  LineTo(rect.right - 2, rect.bottom - 2);
  MoveTo(rect.left + 2, rect.bottom - 2);
  LineTo(rect.right - 2, rect.top + 2);
}

#include "ToolboxTextEditorBinding.cpp"

#include "ToolboxEditTextBinding.cpp"

#ifdef TEST_BUILD
bool ToolboxScenePlatformController::queryEditTextGeometryForTesting(
    ToolboxEditTextContext *ownerContext, EditTextGeometry &out) const
{
  size_t index = 0;
  if (!ownerContext || !this->editControls_.find(ownerContext, index))
    return false;
  TEHandle te = this->editControls_[index].te;
  if (!te || !*te)
    return false;
  out.destination = (**te).destRect;
  out.view = (**te).viewRect;
  return true;
}

bool ToolboxScenePlatformController::queryEditTextValueForTesting(
    ToolboxEditTextContext *ownerContext,
    std::string &out) const
{
  out.clear();
  size_t index = 0;
  if (!ownerContext || !editControls_.find(ownerContext, index))
  {
    return false;
  }
  const EditTextControlBinding &binding = editControls_[index];
  if (!binding.te || !*binding.te)
  {
    return false;
  }
  // TERec::hText is CharsHandle (unsigned char **) under Apple's Universal
  // Interfaces and Handle (char **) under Multiversal, and both toolchains are
  // supported. Reach it as a plain Handle so the declaration this file sees
  // does not decide whether it compiles.
  Handle textHandle = reinterpret_cast<Handle>((**binding.te).hText);
  const long length = (**binding.te).teLength;
  if (length < 0 || (length > 0 && !textHandle))
  {
    return false;
  }
  if (length > 0)
  {
    const char previousHandleState = HGetState(textHandle);
    HLock(textHandle);
    const char *bytes = reinterpret_cast<const char *>(*textHandle);
    if (!bytes)
    {
      HSetState(textHandle, previousHandleState);
      return false;
    }
    out.assign(bytes, static_cast<std::string::size_type>(length));
    HSetState(textHandle, previousHandleState);
  }
  return true;
}
#endif

#include "ToolboxEditPublication.cpp"

#include "ToolboxControlPresentation.cpp"

bool ToolboxScenePlatformController::isPointInEdit(const Point &point) const
{
  for (size_t i = 0; i < editControls_.size(); ++i)
  {
    const EditTextControlBinding &binding = editControls_[i];
    if (binding.te && PtInRect(point, &binding.rect))
    {
      return true;
    }
  }
  for (size_t i = 0; i < hitLedger_.editHits_.size(); ++i)
  {
    const EditHit &hit = hitLedger_.editHits_[i];
    if (hit.text && PtInRect(point, &hit.rect))
    {
      return true;
    }
  }
  return false;
}

bool ToolboxScenePlatformController::intersectWithProjectionClip(
    const Rect &rect,
    Rect &clipped) const
{
  if (this->projectionParentScopes_.activeDepth() == 0)
  {
    clipped = rect;
    return clipped.left < clipped.right && clipped.top < clipped.bottom;
  }
  const loka::core::Frame &clip =
      this->projectionParentScopes_.current().clipRect;
  Rect viewport;
  viewport.left = static_cast<short>(clip.x);
  viewport.top = static_cast<short>(clip.y);
  viewport.right = static_cast<short>(clip.x + clip.width);
  viewport.bottom = static_cast<short>(clip.y + clip.height);
  return SectRect(&rect, &viewport, &clipped) != 0 &&
         clipped.left < clipped.right && clipped.top < clipped.bottom;
}

void ToolboxScenePlatformController::beginClip(const Rect &rect)
{
  if (clipRgn_)
  {
    GetClip(clipRgn_);
    ClipRect(&rect);
    hasClip_ = true;
  }
}

void ToolboxScenePlatformController::endClip()
{
  if (clipRgn_ && hasClip_)
  {
    SetClip(clipRgn_);
    hasClip_ = false;
  }
}

void ToolboxScenePlatformController::TextStateChangedThunk(void *userData)
{
  TextBinding *binding = static_cast<TextBinding *>(userData);
  if (!binding || !binding->controller || !binding->state)
  {
    return;
  }
  binding->controller->handleTextChanged(binding->state);
}
