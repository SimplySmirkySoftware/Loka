#include "ToolboxPaintDrawerType.hpp"
#include "ToolboxPaintSuppressScope.hpp"
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
  const ToolboxFullDrawReason kViewportPaintWidenReason = TOOLBOX_FULL_DRAW_PAINT_WIDENED_VIEWPORT_RENDER;

#include "ToolboxPaintAnswers.hpp"

#if !defined(pushButProc) && !defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  enum
  {
    pushButProc = 0
  };
#endif

  static const short kAutoControlBaseId = 128;

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

#include "ToolboxDirtyRectReplay.inc"

#include "ToolboxDirtyImageReplay.inc"

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
      activeViewportContent_(0),
      projectionParentScopes_(window && window->window() ? static_cast<void *>(window->window()) : 0),
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

#include "ToolboxScrollViewLayout.cpp"

#include "ToolboxScrollViewRender.cpp"
#include "ToolboxClipBoxRender.cpp"

short ToolboxScenePlatformController::allocateControlId()
{
  return controlIds_.allocate();
}

#include "ToolboxBatchUpdate.cpp"
#include "ToolboxBoundaryApply.cpp"

void ToolboxScenePlatformController::synchronize()
{
  // Toolbox doesn't have a retained scene graph; rely on Update events.
}

bool ToolboxScenePlatformController::hasPendingSync() const
{
  return !this->retiredControls_.empty() || !this->retiredScrollBarControls_.empty() || !this->retiredTextEdits_.empty()
         || this->unicodeEditors_.hasPending();
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
  this->pressLedger_.clear();
  hitLedger_.popupHits_.clear();
  clearTextBindings();
  clearEnabledBindings();
  clearControls();
  flushRetiredNativeHandles();
  drainNativeHandleBuckets();
  this->staticBlur_.clear();
  this->bandBlur_.clear();
}


#include "ToolboxStructurePresent.cpp"

void ToolboxScenePlatformController::retireNodeContext(loka::app::scene::NodeContext *context,
                                                       loka::app::scene::NativeLifetimeHint lifetimeHint)
{
#include "ToolboxRetireContextRows.inc"
}

#include "ToolboxContextProps.cpp"

#include "ToolboxZStack.cpp"
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
          window_->requestInvalidateWithReason(TOOLBOX_FULL_DRAW_TEXT_RELAYOUT);
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
    addPendingDirty(binding.placement.visible());
    return;
  }
  ++debugStats_.textChangedImmediateInvalidateCount;
  window_->requestInvalidateRect(binding.placement.visible());
}

void ToolboxScenePlatformController::refreshPopupEnabled(PopupHit &binding)
{
  this->requestControlRepaint(binding.rect);
}

#include "ToolboxEnabledControls.cpp"

void ToolboxScenePlatformController::refreshButtonHitEnabled(ButtonHit &binding)
{
  this->requestControlRepaint(binding.rect);
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

std::string ToolboxScenePlatformController::debugStatsSummary() const
{
  return debugStats_.summary();
}

void ToolboxScenePlatformController::resetDebugStats()
{
  this->observation_.reset();
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

#include "ToolboxTextHitPaint.cpp"

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
  this->unicodeEditors_.drain();
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

#include "ToolboxButtonControl.cpp"

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
  out.placement = this->editControls_[index].placement;
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
