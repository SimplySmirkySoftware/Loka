#include "MacScenePlatformController.hpp"
#include "platform/MacPushButton.hpp"
#include "MacBitmapCapture.hpp"
#include "MacInputDoor.hpp"
#include "MacApp.hpp"
#include "app/layout/CanvasLayout.hpp"
#include "MacBuiltInSupport.hpp"
#include "MacObjCCompat.hpp"
#include "Utf8String.hpp"
#include "platform/StringUTF8.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/style/Style.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include <cassert>
#include <climits>
#include <AppKit/AppKit.h>
#include <vector>
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/RectSurface.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "MacSceneLayout.hpp"
#include "app/layout/StackGaps.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/layout/FramedLayout.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "app/layout/GridLayout.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/layout/ZStackLayout.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/scene/Node.hpp"
#include "context/MacEditTextContext.hpp"
#include "context/MacTextContext.hpp"
#include "context/MacTextEditorContext.hpp"
#include "context/MacOpenFileDialogContext.hpp"
#include "context/MacRectSurfaceContext.hpp"
#include "context/MacScrollViewContext.hpp"
#include "core/Profiler.hpp"
#include <map>

namespace
{
  static std::map<void *, MacScenePlatformController *> gControllerByRootView;

} // namespace

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class MacPlatformLayoutTraversal : public IPlatformLayoutTraversal
      {
      public:
        explicit MacPlatformLayoutTraversal(MacScenePlatformController *controller)
            : controller_(controller),
              layoutResultY_(0)
        {
        }

        virtual bool queryNaturalWidth(Node *child, short &width) const
        {
          return this->controller_ && this->controller_->queryNaturalWidth(child, width);
        }

        virtual int layoutChild(Node *child, const LayoutState &state)
        {
          if (!this->controller_)
          {
            return state.y;
          }
          const int result = this->controller_->layoutNodeFromSceneState(child, state);
          if (this->controller_->refuseNarrowingInScrollScope(result))
          {
            return state.y;
          }
          this->layoutResultY_ = static_cast<short>(result);
          return result;
        }

        virtual void setLayoutResultY(short y)
        {
          this->layoutResultY_ = y;
        }

        virtual short layoutResultY() const
        {
          return this->layoutResultY_;
        }

      private:
        MacScenePlatformController *controller_;
        short layoutResultY_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka


short MacScenePlatformController::measurePushButtonNaturalWidth(const loka::core::String &label) const
{
  std::string utf8;
  if (!loka::platform::CollectUtf8(label, utf8))
    return 0;
  // Measure the control MacButtonContext builds, not a standalone cell: on
  // macOS 10.14 through 13 an NSButtonCell made by initTextCell: reports its
  // title without the push bezel's padding, in 12 pt instead of the button's
  // 13 pt, so SimpleText's command band drew every button too narrow.
  NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
  if (!button)
    return 0;
  loka::macos::ConfigureMacPushButton(button);
  [button setTitle:loka::macos::CreateNSStringFromUtf8(utf8)];
  const CGFloat points = [[button cell] cellSize].width;
  [button release];
  // Bound before the floating-to-integer conversion; NaN also declines.
  const loka::app::Ratio &scale = this->projection_.railMetrics().spaceScale;
  if (!(points > 0 && points <= static_cast<double>(SHRT_MAX) * scale.num / scale.den))
    return 0;
  return static_cast<short>(this->projection_.measurementToLu(points));
}

namespace
{
  // Twin of measurePushButtonNaturalWidth's cell and bound. A NaN or
  // non-positive cell declines and keeps kButtonHeight.
  loka::app::layout::RowLayoutMetrics MeasureRowLayoutMetrics(const loka::macos::MacProjection &projection)
  {
    loka::app::layout::RowLayoutMetrics metrics = loka::app::layout::FallbackControlMetrics::rowLayout();
    metrics.gap = MacSceneLayout::kGap;
    NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
    if (!button)
      return metrics;
    loka::macos::ConfigureMacPushButton(button);
    const CGFloat points = [[button cell] cellSize].height;
    [button release];
    const loka::app::Ratio &scale = projection.railMetrics().spaceScale;
    if (points > 0 && points <= static_cast<double>(SHRT_MAX) * scale.num / scale.den)
      metrics.buttonHeight = projection.measurementToLu(points);
    return metrics;
  }
} // namespace

bool MacScenePlatformController::queryNaturalWidth(loka::app::scene::Node *child, short &width) const
{
  // A consulting NATURAL Row asks at most twice per allocation; each ask
  // measures only this leaf's current string or converts its image width in O(1),
  // with no foreign rows or cache.
  if (child && child->asTextNode())
    return MacTextContext::queryNaturalWidth(child->asTextNode(), this, width);
  if (loka::app::ImageViewNode *image = child ? child->asImageViewNode() : 0)
  {
    // No NSView, cell, or font is needed.
    int pixels = 0;
    if (!loka::app::queryIntrinsicImageWidth(image->props, pixels))
      return false;
    // Keep conversion aligned with ResolveImageLayoutWidth in MacImageViewContext.mm.
    const int lu = this->projection().intrinsicPixelsToLu(pixels);
    if (lu <= 0 || lu > SHRT_MAX)
      return false;
    width = static_cast<short>(lu);
    return true;
  }
  loka::app::ButtonNode *button = child ? child->asButtonNode() : 0;
  if (!button)
    return false;
  // With no text prop MacButtonContext leaves NSButton's default title alone.
  const loka::core::String label = button->props.text_
      ? button->props.text_->get() : loka::core::String::Literal("Button");
  const short measured = this->measurePushButtonNaturalWidth(label);
  if (measured <= 0)
    return false;
  width = measured;
  return true;
}

/** Measured 2026-09-19 (#818): Chicago 12 and the 13 pt system font share
    a 16-unit line height; fontScale follows their nominal sizes. A standard
    button is 72x20 lu versus 90x24 pt; "MMMMMMMM" is 72 lu versus 90.3 pt.
    These are candidate values for visual review, not golden-baked values. */
loka::app::RailMetrics loka::macos::DefaultRailMetrics()
{
  return app::RailMetrics(app::Ratio(13, 12), app::Ratio(5, 4));
}

namespace
{
  // This rail's answer for each size role, in points. Like the default row it
  // is a system size and exempt from fontScale: SmallLabel is AppKit's small
  // system font (11 pt). The Toolbox answers Geneva 9
  // (ToolboxTextFontDescriptor::resolve), Win32 the message face at 10 lu
  // (Win32DisplayFont.cpp), and headless 9 (SyntheticTextSize).
  CGFloat RolePoints(loka::app::TextSizeRole role)
  {
    switch (role)
    {
    case loka::app::TEXT_SIZE_ROLE_SMALL_LABEL:
      return [NSFont smallSystemFontSize];
    case loka::app::TEXT_SIZE_ROLE_COUNT:
      break;
    }
    assert(!"TEXT_SIZE_ROLE_COUNT names no row");
    return [NSFont systemFontSize];
  }
} // namespace

MacScenePlatformController::TextFontTable::TextFontTable(const loka::app::Ratio &fontScale)
{
  NSFontManager *manager = [NSFontManager sharedFontManager];
  const CGFloat defaultSize = [NSFont systemFontSize];
  for (int size = 0; size < kFontRowCount; ++size)
  {
    // Unset size means the rail's system default (13 pt) and a size role its
    // system size, both exempt from fontScale. Explicit lu sizes keep
    // fractional points: 9 lu -> 9.75 pt, no pixel rounding.
    CGFloat points = defaultSize;
    if (size < kRoleRowBase)
      points = static_cast<CGFloat>(loka::app::detail::StyleVocabularySizes[size]) * fontScale.num / fontScale.den;
    else if (size < kDefaultSizeRow)
      points = RolePoints(static_cast<loka::app::TextSizeRole>(size - kRoleRowBase));
    for (int fixed = 0; fixed < 2; ++fixed)
      for (int bold = 0; bold < 2; ++bold)
      {
        NSFont *base = fixed ? [NSFont userFixedPitchFontOfSize:points]
                            : (bold ? [NSFont boldSystemFontOfSize:points] : [NSFont systemFontOfSize:points]);
        if (fixed && bold && base)
        {
          NSFont *converted = [manager convertFont:base toHaveTrait:NSBoldFontMask];
          if (converted)
            base = converted;
        }
        NSFont *italic = base ? [manager convertFont:base toHaveTrait:NSItalicFontMask] : nil;
        this->fonts_[size][bold][0][fixed] = (void *)[base retain];
        this->fonts_[size][bold][1][fixed] = (void *)[(italic ? italic : base) retain];
      }
  }
}

MacScenePlatformController::TextFontTable::~TextFontTable()
{
  // Every admission retain has a release, even when conversion reused base.
  for (int size = 0; size < kFontRowCount; ++size)
    for (int bold = 0; bold < 2; ++bold)
      for (int italic = 0; italic < 2; ++italic)
        for (int fixed = 0; fixed < 2; ++fixed)
          [(NSFont *)this->fonts_[size][bold][italic][fixed] release];
}

void *MacScenePlatformController::TextFontTable::find(const loka::app::TextStyle &style, bool fixedPitch) const
{
  const int bold = style.hasWeight_ && style.weight_ == loka::app::TEXT_WEIGHT_BOLD ? 1 : 0;
  const int italic = style.hasItalic_ && style.italic_ ? 1 : 0;
  loka::app::TextSizeRole role;
  if (style.size_.queryRole(role))
    return this->fonts_[kRoleRowBase + role][bold][italic][fixedPitch ? 1 : 0];
  int points = 0;
  if (!style.size_.queryPoints(points))
    return this->fonts_[kDefaultSizeRow][bold][italic][fixedPitch ? 1 : 0];
  loka::app::SizeOf(points).size_.queryPoints(points);
  for (int size = 0; size < kSizeCount; ++size)
    if (loka::app::detail::StyleVocabularySizes[size] == points)
      return this->fonts_[size][bold][italic][fixedPitch ? 1 : 0];
  return 0;
}

void *MacScenePlatformController::textFont(const loka::app::TextStyle &style, bool fixedPitch) const
{
  return this->textFonts_.find(style, fixedPitch);
}

MacScenePlatformController::MacScenePlatformController(void *rootView,
                                                       const loka::app::RailMetrics &metrics,
                                                       PlatformContext *platformContext)
    : projection_(rootView, metrics),
      rowLayoutMetrics_(MeasureRowLayoutMetrics(this->projection_)),
      textShaping_(loka::app::WHOLE_LINE),
      textFonts_(this->projection_.railMetrics().fontScale),
      platformContext_(platformContext),
      rootView_(rootView),
      projectionParentScopes_(rootView),
      rootNode_(0),
      rectSurfaceExtentLedger_(),
      lastChangeFlags_(loka::app::scene::NODE_DIRTY_NONE),
      clientWidth_(0),
      clientHeight_(0),
      firstEditField_(0),
      lastEditField_(0),
      focusedEditTextState_(0),
      focusedEditTextControlTag_(0),
      relayoutPending_(false)
{
  RegisterMacBuiltInSupport(*this);
  if (rootView_)
  {
    gControllerByRootView[rootView_] = this;
  }
}

bool MacScenePlatformController::captureViewBitmap(void *view, loka::core::resource::Image &out) const
{
  return loka::macos::CaptureViewBitmap(this->platformContext_, view, out);
}

MacScenePlatformController::~MacScenePlatformController()
{
  if (rootView_)
  {
    std::map<void *, MacScenePlatformController *>::iterator it = gControllerByRootView.find(rootView_);
    if (it != gControllerByRootView.end() && it->second == this)
    {
      gControllerByRootView.erase(it);
    }
  }
  clearContexts();
  this->drainNativeRetirements();
}

bool MacScenePlatformController::registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler)
{
  return this->nodeHandlerRegistry_.registerHandler(handler);
}

bool MacScenePlatformController::prepareProjectedLayout(loka::app::scene::Node *node,
                                                        loka::app::scene::LayoutState &state)
{
  if (!node)
  {
    return false;
  }
  loka::app::scene::LayoutState handlerState = state;
  if (handlerState.height <= 0)
  {
    handlerState.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kTextHeight);
  }
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlerRegistry_.find(node);
  if (!handler)
  {
    assert(false && "no node handler registered for this node type -- register the handler or an explicit RefusedNodeHandler");
    return false;
  }
  loka::app::scene::NodeContext *context = handler->ensureContext(node, this, handlerState);
  if (!context)
  {
    return false;
  }
  return true;
}

int MacScenePlatformController::layoutNodeFromSceneState(loka::app::scene::Node *node,
                                                         const loka::app::scene::LayoutState &state)
{
  const LayoutState localState(state.x, state.y, state.width, state.height, state.spacing);
  return this->layoutNode(node, localState);
}

MacScenePlatformController *MacScenePlatformController::findForRootView(void *rootView)
{
  if (!rootView)
  {
    return 0;
  }
  std::map<void *, MacScenePlatformController *>::iterator it = gControllerByRootView.find(rootView);
  if (it == gControllerByRootView.end())
  {
    return 0;
  }
  return it->second;
}

void MacScenePlatformController::onChange(loka::app::scene::Node *rootNode,
                                          loka::app::scene::NodeDirtyFlags flags,
                                          bool fullRebuild)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  rootNode_ = rootNode;
  lastChangeFlags_ = flags;
  relayoutPending_ = false;
  if (!rootView_ || !rootNode_)
  {
    return;
  }

  const bool requiresLayout = (flags & loka::app::scene::NODE_DIRTY_INITIAL) != 0
                              || (flags & loka::app::scene::NODE_DIRTY_LAYOUT) != 0
                              || (flags & loka::app::scene::NODE_DIRTY_CHILD) != 0;
  if (!requiresLayout)
  {
    return;
  }

  NSView *view = (NSView *)rootView_;
  NSRect bounds = [view bounds];
  clientWidth_ = this->projection().clientCapacityToLu(bounds.size.width);
  clientHeight_ = this->projection().clientCapacityToLu(bounds.size.height);
  performLayout(clientWidth_, clientHeight_, fullRebuild);
}

void MacScenePlatformController::onBoundaryApply(loka::app::scene::Node *rootNode,
                                                 loka::app::scene::BoundaryNode *boundary,
                                                 const loka::app::scene::BoundaryLocalApplyInfo &info,
                                                 const loka::app::scene::PlatformApplyPlan &plan)
{
  if (rootNode)
  {
    rootNode_ = rootNode;
  }
  if (!rootView_ || !rootNode_ || !boundary || !plan.hasBoundaryApplyWork(boundary))
  {
    return;
  }
  if (info.hasStructureWork || info.hasLayoutWork || !info.hasPaintWork())
  {
    return;
  }

  NSView *view = (NSView *)rootView_;
  if (!info.hasBoundsHint() || info.hasCompositedPaintWork())
  {
    [view setNeedsDisplay:YES];
    return;
  }

  const NSRect dirtyRect = this->projection().damageToNative(
      loka::core::Frame(info.bounds->x, info.bounds->y, info.bounds->width, info.bounds->height)).r;
  [view setNeedsDisplayInRect:dirtyRect];
}

void MacScenePlatformController::synchronize()
{
  // Solid-mode（固定ツリー）では即時反映済みのため、現状何もしない。
}

bool MacScenePlatformController::hasPendingSync() const
{
  return !this->nativeRetirements_.empty();
}

void MacScenePlatformController::queueNativeRetirement(void *primary, void *auxiliary)
{
  if (primary || auxiliary)
  {
    this->nativeRetirements_.push_back(NativeRetirement(primary, auxiliary));
  }
}

void MacScenePlatformController::drainNativeRetirements()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  for (size_t i = 0; i < this->nativeRetirements_.size(); ++i)
  {
    NativeRetirement &entry = this->nativeRetirements_[i];
    if (entry.auxiliary)
    {
      [(id)entry.auxiliary release];
    }
    if (entry.primary)
    {
      [(id)entry.primary release];
    }
  }
  this->nativeRetirements_.clear();
}

void MacScenePlatformController::destroy()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  clearContexts();
  this->drainNativeRetirements();
  rootNode_ = 0;
  lastChangeFlags_ = loka::app::scene::NODE_DIRTY_NONE;
  clientWidth_ = 0;
  clientHeight_ = 0;
  relayoutPending_ = false;
  focusedEditTextState_ = 0;
  focusedEditTextControlTag_ = 0;
}

void MacScenePlatformController::relayout(int clientWidth, int clientHeight)
{
  relayoutPending_ = false;
  if (!rootNode_)
  {
    return;
  }
  if (clientWidth <= 0 || clientHeight <= 0)
  {
    if (rootView_)
    {
      NSView *view = (NSView *)rootView_;
      NSRect bounds = [view bounds];
      clientWidth = this->projection().clientCapacityToLu(bounds.size.width);
      clientHeight = this->projection().clientCapacityToLu(bounds.size.height);
    }
  }
  clientWidth_ = clientWidth;
  clientHeight_ = clientHeight;
  performLayout(clientWidth_, clientHeight_, false);
}

void MacScenePlatformController::requestRelayout()
{
  if (!rootNode_ || !rootView_)
  {
    return;
  }
  relayoutPending_ = true;
}

void MacScenePlatformController::flushPendingRelayouts()
{
  std::map<void *, MacScenePlatformController *>::iterator it = gControllerByRootView.begin();
  for (; it != gControllerByRootView.end(); ++it)
  {
    MacScenePlatformController *controller = it->second;
    if (!controller || !controller->hasPendingRelayout())
    {
      continue;
    }
    MacInputDoor::pendingRelayout(*controller);
  }
}

void MacScenePlatformController::handlePendingRelayout()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->borrowPhase().open());
#endif
  this->relayout(0, 0);
}

void MacScenePlatformController::performLayout(int clientWidth, int clientHeight, bool rebuildContexts)
{
  if (rebuildContexts)
  {
    captureFocusedEditField();
    // Do NOT clearContexts() here: retained nodes (from local diff) keep
    // their existing context and layoutNode() already creates new contexts
    // for nodes that have none.  Replaced/retired nodes have their contexts
    // released by the composition system via releaseNodeContexts().
  }
  if (!rootNode_ || !rootView_)
  {
    return;
  }
  firstEditField_ = 0;
  lastEditField_ = 0;
  const int rootInset = MacSceneLayout::kRootInset;
  int rootWidth = measureClientWidth(clientWidth) - 2 * rootInset;
  if (rootWidth < 0)
  {
    rootWidth = 0;
  }
  const LayoutState state(
      rootInset, rootInset, rootWidth, clientHeight > 0 ? clientHeight - 2 * rootInset : 0, MacSceneLayout::kGap);
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a macOS projection pass must begin at the root scope");
  const loka::core::Frame rootClip(0, 0, clientWidth, clientHeight);
  if (!this->projectionParentScopes_.resetRoot(this->rootView_, rootClip))
  {
    return;
  }
  PROFILE_SECTION("layout");
  this->layoutNode(this->rootNode_, state);
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a macOS projection pass must restore the root scope");
  this->rectSurfaceExtentLedger_.flush();
  finalizeKeyLoop();
  if (rebuildContexts)
  {
    restoreFocusedEditField();
  }
}

namespace
{
}

int MacScenePlatformController::applyBoundaryLayoutResult(loka::app::scene::BoundaryNode *boundary,
                                                          int x,
                                                          int y,
                                                          const LayoutNodeResult &result)
{
  if (boundary)
  {
    boundary->setLayoutBounds(x, y, result.boundaryWidth, result.resultY - y);
  }
  return result.resultY;
}

MacScenePlatformController::LayoutNodeResult
MacScenePlatformController::layoutRectSurfaceNode(loka::app::RectSurfaceNode *surface, const LayoutState &state)
{
  loka::app::scene::LayoutState projectedState;
  if (!this->narrowLayoutState(state, projectedState, true))
  {
    return LayoutNodeResult(state.width, state.y);
  }
  const int width = surface->props.width_ > 0 ? surface->props.width_ : state.width;
  const int height = surface->props.height_ > 0 ? surface->props.height_ : state.height;
  MacRectSurfaceContext *ctx = static_cast<MacRectSurfaceContext *>(surface->getContext());
  if (ctx)
  {
    ctx->relayout(projectedState.x,
                  projectedState.y,
                  width,
                  height);
  }
  else
  {
    ctx = new MacRectSurfaceContext(
        this,
        this->projectionParentView(),
        projectedState.x,
        projectedState.y,
        width,
        height,
        surface);
    if (!ctx)
    {
      return LayoutNodeResult(state.width, state.y);
    }
    if (!ctx->hasNativeSurface())
    {
      // No native surface was placed: nothing is installed and nothing is
      // published; the next layout pass retries creation.
      delete ctx;
      return LayoutNodeResult(state.width, state.y);
    }
    surface->setContext(ctx);
    ctx->readLifecycleFactOnAttach();
  }
  // The seat is a fact only once the surface was actually placed: a refused
  // projection or a failed native context records nothing.
  this->rectSurfaceExtentLedger_.record(
      surface, loka::core::Frame(state.x, state.y, width, height));
  return LayoutNodeResult(state.width, state.y + height);
}

MacScenePlatformController::LayoutNodeResult
MacScenePlatformController::layoutScrollViewNode(
    loka::app::ScrollViewNode *scrollView,
    const LayoutState &state)
{
  if (!scrollView || this->projectionParentScopes_.activeDepth() != 0)
  {
    // V1 admits one active viewport. Refuse before creating the inner native
    // view so the outer document remains the only structural clipping parent.
    return LayoutNodeResult(state.width, state.y);
  }
  if (state.x < SHRT_MIN || state.x > SHRT_MAX ||
      state.y < SHRT_MIN || state.y > SHRT_MAX ||
      state.width < 0 || state.width > SHRT_MAX ||
      state.height < 0 || state.height > SHRT_MAX ||
      (state.height > 0 && state.y > SHRT_MAX - state.height))
  {
    // The shared traversal above consumes a short result. Refuse the viewport
    // itself before its bottom or any child coordinate would narrow.
    return LayoutNodeResult(state.width, state.y);
  }

  MacScrollViewContext *ctx =
      static_cast<MacScrollViewContext *>(scrollView->getContext());
  if (ctx)
  {
    ctx->relayout(state.x, state.y, state.width, state.height);
  }
  else
  {
    ctx = new MacScrollViewContext(this,
                                   this->projectionParentView(),
                                   state.x,
                                   state.y,
                                   state.width,
                                   state.height,
                                   scrollView);
    if (!ctx || !ctx->isValid())
    {
      delete ctx;
      return LayoutNodeResult(state.width, state.y);
    }
    scrollView->setContext(ctx);
    ctx->readLifecycleFactOnAttach();
  }

  const int requestedOffset = scrollView->requestedOrigin().y;
  LayoutState childBase = state;
  childBase.width = ctx->visibleWidth();
  short contentWidthLu = 0;
  const bool known = loka::app::layout::queryContentOverflowWidth(scrollView, state.spacing(), this, contentWidthLu);
  // An unknown range must not scroll: otherwise content cannot be brought back
  // The range is measured in projected points, as the clip is: two logical
  // widths that project to the same native width leave no range.
  const int maximumX = known ? ctx->horizontalRange(contentWidthLu) : 0;
  const int clampedX = ctx->clampOffset(scrollView->requestedOrigin().x, maximumX);
  // Like Win32, decide chrome before traversal without changing native metrics.
  const bool horizontalBar = maximumX > 0 && state.height > 0;
  const int viewportHeight = horizontalBar ? this->projection().clientHeightBelowHorizontalScroller(
                                                 loka::core::Frame(state.x, state.y, state.width, state.height))
                                           : state.height;
  childBase.height = viewportHeight;
  const loka::core::Frame viewportClip(0, 0, state.width, viewportHeight);
  loka::app::scene::ProjectionParentScope childScope;
  const loka::app::scene::ProjectionParentScope &parentScope =
      this->projectionParentScopes_.current();
  // AppKit applies scrolling through the clip view's bounds origin. Children
  // therefore subtract the seat origin only; applying requestedOffset here
  // would move them twice.
  if (!parentScope.deriveScrolled(ctx->documentView(),
                                  state.x,
                                  state.y,
                                  viewportClip,
                                  childScope))
  {
    return LayoutNodeResult(state.width, state.y);
  }

  int currentY = state.y;
  int contentHeight = 0;
  bool shortRangeRefused = false;
  {
    loka::app::scene::ProjectionParentScopeGuard scopeGuard(
        this->projectionParentScopes_, childScope);
    if (!scopeGuard.isActive())
    {
      return LayoutNodeResult(state.width, state.y);
    }

    loka::app::scene::INestable *nestable = scrollView->asNestable();
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(
        nestable ? nestable->childrenHead() : 0,
        nestable ? nestable->childrenCount() : 0);
    const std::size_t ledgerMark = this->rectSurfaceExtentLedger_.mark();
    loka::app::layout::StackGaps gaps(state.spacing());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      const int childY = currentY + gaps.before(child);
      if (childY < SHRT_MIN || childY > SHRT_MAX)
      {
        this->refuseScrollViewShortRange();
        break;
      }
      LayoutState childState = childBase;
      childState.y = childY;
      const int nextY = this->layoutNode(child, childState);
      if (this->projectionParentScopes_.current().hasShortRangeRefusal())
      {
        break;
      }
      if (!this->projectionParentScopes_.current().tryAccumulateContentHeight(
              currentY, nextY))
      {
        this->refuseScrollViewShortRange();
        break;
      }
      currentY = nextY;
    }
    contentHeight = this->projectionParentScopes_.current().contentHeight();
    shortRangeRefused =
        this->projectionParentScopes_.current().hasShortRangeRefusal();
      if (shortRangeRefused)
    {
      // Seats recorded under a refused scope are not facts.
      this->rectSurfaceExtentLedger_.discardSince(ledgerMark);
    }
  }

  if (!shortRangeRefused)
  {
    const int clampedOffset = ctx->commitScrollMetrics(
        known ? contentWidthLu : 0, contentHeight, viewportHeight, clampedX, requestedOffset, horizontalBar);
    // Publish only after successful layout and after the scope has popped.
    scrollView->publishOrigin(clampedX, clampedOffset);
  }

  if (state.height > 0)
  {
    return LayoutNodeResult(state.width, state.y + state.height);
  }
  if (currentY < SHRT_MIN || currentY > SHRT_MAX)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  return LayoutNodeResult(state.width, currentY);
}

int MacScenePlatformController::layoutNode(loka::app::scene::Node *node, const LayoutState &state)
{
  if (!node)
  {
    return state.y;
  }
  if (this->projectionParentScopes_.activeDepth() != 0 &&
      this->projectionParentScopes_.current().hasShortRangeRefusal())
  {
    return state.y;
  }
  // Deliberate Win32 twin: resolve into a local copy before dispatching any layout branch.
  const LayoutState local(
      state.x,
      state.y,
      state.width,
      state.height,
      loka::app::layout::resolveLayoutGap(node, state.spacing(), MacSceneLayout::kGap, this->rowLayoutMetrics_.gap));
  return this->applyBoundaryLayoutResult(node->asBoundary(), local.x, local.y, this->computeLayoutResult(node, local));
}

MacScenePlatformController::LayoutNodeResult
MacScenePlatformController::computeLayoutResult(loka::app::scene::Node *node, const LayoutState &state)
{
  if (loka::app::ScrollViewNode *scrollView = node->asScrollViewNode())
  {
    return this->layoutScrollViewNode(scrollView, state);
  }
  // Canvas shares geometry with Null; this rail only adapts its result channel.
  if (node->asCanvasNode())
  {
    loka::app::scene::LayoutState handlerState;
    if (!this->narrowLayoutState(state, handlerState, false))
    {
      node->asCanvasNode()->recordLayoutStatus(loka::app::CANVAS_LAYOUT_SHORT_RANGE_REFUSED);
      return LayoutNodeResult(state.width, state.y);
    }
    loka::app::scene::MacPlatformLayoutTraversal traversal(this);
    loka::app::layout::CanvasPlatformLayoutHandler handler;
    return LayoutNodeResult(state.width, handler.layoutNode(node, handlerState, &traversal));
  }
  if (loka::app::StackNode *stack = node->asStackNode())
  {
    int resultY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(stack);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::MacPlatformLayoutTraversal traversal(this);
      resultY = handler->layoutNode(stack, handlerState, &traversal);
    }
    else if (stack->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN)
    {
      resultY = loka::app::layout::computeColumnLayoutResultY(
          stack,
          state,
          this,
          &MacScenePlatformController::layoutContainerChild,
          loka::app::layout::resolveLayoutGap(
              stack, state.spacing(), MacSceneLayout::kGap, this->rowLayoutMetrics_.gap),
          0);
    }
    else
    {
      resultY = loka::app::layout::computeRowLayoutResultY(
          stack,
          state,
          this->rowLayoutMetrics_,
          this,
          this,
          &MacScenePlatformController::layoutContainerChild,
          loka::app::layout::resolveLayoutGap(
              stack, state.spacing(), MacSceneLayout::kGap, this->rowLayoutMetrics_.gap),
          0);
    }
    return LayoutNodeResult(state.width, resultY);
  }

  if (loka::app::GridNode *grid = node->asGridNode())
  {
    int maxY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(grid);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::MacPlatformLayoutTraversal traversal(this);
      maxY = handler->layoutNode(grid, handlerState, &traversal);
    }
    else
    {
      loka::app::layout::GridLayoutMetrics metrics;
      metrics.gapX = 0;
      metrics.gapY = 0;
      maxY = loka::app::layout::computeGridLayoutResultY(
          grid, state, metrics, this, &MacScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, maxY);
  }

  if (loka::app::BoxNode *box = node->asBoxNode())
  {
    int resultY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(box);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::MacPlatformLayoutTraversal traversal(this);
      resultY = handler->layoutNode(box, handlerState, &traversal);
    }
    else
    {
      resultY = loka::app::layout::computeBoxLayoutResultY(
          box,
          state,
          this,
          &MacScenePlatformController::layoutContainerChild,
          loka::app::layout::resolveLayoutGap(box, state.spacing(), MacSceneLayout::kGap, this->rowLayoutMetrics_.gap),
          0);
    }
    return LayoutNodeResult(state.width, resultY);
  }

  if (node->asFramedNode())
  {
    loka::app::scene::Node *group = node;
    int resultY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(group);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::MacPlatformLayoutTraversal traversal(this);
      resultY = handler->layoutNode(group, handlerState, &traversal);
    }
    else
    {
      resultY = loka::app::layout::computeFramedLayoutResultY(
          group, state, this, &MacScenePlatformController::layoutContainerChild, 0);
    }
    return LayoutNodeResult(state.width, resultY);
  }

  if (loka::app::ZStackNode *stack = node->asZStackNode())
  {
    int maxY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(stack);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::MacPlatformLayoutTraversal traversal(this);
      maxY = handler->layoutNode(stack, handlerState, &traversal);
    }
    else
    {
      maxY = loka::app::layout::computeZStackLayoutResultY(
          stack, state, this, &MacScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, maxY);
  }

  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    LayoutState childState = state;
    loka::app::layout::StackGaps gaps(state.spacing());
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      const int childY = childState.y + gaps.before(child);
      if (this->refuseNarrowingInScrollScope(childY))
        break;
      childState.y = childY;
      const int nextY = this->layoutNode(child, childState);
      if (this->refuseNarrowingInScrollScope(nextY))
      {
        break;
      }
      childState.y = nextY;
    }
    return LayoutNodeResult(state.width, childState.y);
  }

  if (node->asProjectedLayoutNode())
  {
    return DispatchProjectedLayout(this, node, state);
  }

  if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
  {
    return this->layoutRectSurfaceNode(surface, state);
  }

  return LayoutNodeResult(state.width, state.y);
}

MacScenePlatformController::LayoutNodeResult
MacScenePlatformController::DispatchProjectedLayout(
    MacScenePlatformController *controller,
    loka::app::scene::Node *node,
    const LayoutState &state)
{
  if (!controller || !node)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  loka::app::scene::IProjectedLayoutNode *projected =
      node->asProjectedLayoutNode();
  if (!projected)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  loka::app::scene::LayoutState projectedState;
  if (!controller->narrowLayoutState(state, projectedState, true))
  {
    return LayoutNodeResult(state.width, state.y);
  }
  const int projectedResult =
      projected->layoutProjected(controller, projectedState);
  if (controller->projectionParentScopes_.activeDepth() == 0)
  {
    return LayoutNodeResult(state.width, projectedResult);
  }
  int contentResult = state.y;
  if (!controller->projectionParentScopes_.current().restoreContentY(
          projectedResult, contentResult))
  {
    controller->refuseScrollViewShortRange();
    return LayoutNodeResult(state.width, state.y);
  }
  return LayoutNodeResult(state.width, contentResult);
}

bool MacScenePlatformController::narrowLayoutState(
    const LayoutState &state,
    loka::app::scene::LayoutState &narrowed,
    bool applyProjection)
{
  if (state.x < SHRT_MIN || state.x > SHRT_MAX ||
      state.y < SHRT_MIN || state.y > SHRT_MAX ||
      state.width < SHRT_MIN || state.width > SHRT_MAX ||
      state.height < SHRT_MIN || state.height > SHRT_MAX)
  {
    this->refuseScrollViewShortRange();
    return false;
  }

  loka::app::scene::LayoutState content;
  content.x = static_cast<short>(state.x);
  content.y = static_cast<short>(state.y);
  content.width = static_cast<short>(state.width);
  content.height = static_cast<short>(state.height);
  content.lineHeight = 0;
  content.spacing = static_cast<short>(state.spacing());
  if (applyProjection && this->projectionParentScopes_.activeDepth() != 0)
  {
    if (!this->projectionParentScopes_.current().project(content, narrowed))
    {
      this->refuseScrollViewShortRange();
      return false;
    }
    narrowed.lineHeight = content.lineHeight;
    narrowed.spacing = content.spacing;
    return true;
  }
  narrowed = content;
  return true;
}

void MacScenePlatformController::refuseScrollViewShortRange()
{
  if (this->projectionParentScopes_.activeDepth() != 0)
  {
    this->projectionParentScopes_.current().markShortRangeRefused();
  }
}

bool MacScenePlatformController::refuseNarrowingInScrollScope(int resultY)
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

int MacScenePlatformController::layoutContainerChild(void *context,
                                                     loka::app::scene::Node *child,
                                                     const LayoutState &state)
{
  MacScenePlatformController *controller = static_cast<MacScenePlatformController *>(context);
  if (!controller)
  {
    return state.y;
  }
  return controller->layoutNode(child, state);
}

void MacScenePlatformController::registerEditField(void *field)
{
  if (!field)
  {
    return;
  }
  NSTextField *textField = (NSTextField *)field;
  if (!firstEditField_)
  {
    firstEditField_ = field;
  }
  if (lastEditField_)
  {
    NSTextField *lastField = (NSTextField *)lastEditField_;
    [lastField setNextKeyView:textField];
  }
  lastEditField_ = field;
}

void MacScenePlatformController::finalizeKeyLoop()
{
  if (!firstEditField_ || !lastEditField_)
  {
    return;
  }
  if (firstEditField_ == lastEditField_)
  {
    return;
  }
  NSTextField *firstField = (NSTextField *)firstEditField_;
  NSTextField *lastField = (NSTextField *)lastEditField_;
  [lastField setNextKeyView:firstField];

  if (rootView_)
  {
    NSView *view = (NSView *)rootView_;
    NSWindow *window = [view window];
    if (window)
    {
      [window setInitialFirstResponder:firstField];
    }
  }
}

bool MacScenePlatformController::readNativeFocus(loka::app::scene::NodeContext *&out)
{
  out = 0;
  NSWindow *window = [(NSView *)this->rootView_ window];
  if (![window isKeyWindow])
    return false;
  NSResponder *responder = [window firstResponder];
  out = MacEditTextContext::fromNativeFocus(responder);
  if (!out)
    out = MacTextEditorContext::fromNativeFocus(responder);
  return true;
}

bool MacScenePlatformController::applyNativeFocus(loka::app::scene::NodeContext &ctx)
{
  NSWindow *const window = [(NSView *)this->rootView_ window];
  if (!window || ![window isKeyWindow])
    return false;
  loka::app::scene::Node *owner = ctx.owner();
  if (!owner || owner->getContext() != &ctx || owner->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return false;
  NSView *view = nil;
  loka::app::scene::NodeContext *marked = 0;
  // Deliberate rail parallel: the same node-kind resolution as Win32 focus.
  if (owner->asEditTextNode())
  {
    view = (NSView *)static_cast<MacEditTextContext &>(ctx).nativeField();
    marked = MacEditTextContext::fromNativeFocus(view);
  }
  else if (owner->nodeTypeKey() == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>())
  {
    view = (NSView *)static_cast<MacTextEditorContext &>(ctx).nativeFocusView();
    marked = MacTextEditorContext::fromNativeFocus(view);
  }
  if (!view || [view window] != window || marked != &ctx)
    return false;
  // Becoming first responder again reselects a field's whole text, so a target
  // that already has focus is left alone.
  loka::app::scene::NodeContext *current = 0;
  if (this->readNativeFocus(current) && current == &ctx)
    return true;
  // Resigning/becoming first responder can commit text or notify selection and
  // run the Scene. Use only the copied native window/view; never touch ctx after.
  return [window makeFirstResponder:view] ? true : false;
}

void MacScenePlatformController::captureFocusedEditField()
{
  focusedEditTextState_ = 0;
  focusedEditTextControlTag_ = 0;
  if (!rootNode_ || !rootView_)
  {
    return;
  }
  void *state = findFocusedEditTextState(rootNode_);
  if (!state)
  {
    return;
  }
  focusedEditTextState_ = state;
}

void *MacScenePlatformController::findFocusedEditTextState(loka::app::scene::Node *node) const
{
  if (!node)
  {
    return 0;
  }
  if (loka::app::EditTextNode *edit = node->asEditTextNode())
  {
    MacEditTextContext *ctx = static_cast<MacEditTextContext *>(edit->getContext());
    NSTextField *field = ctx ? (NSTextField *)ctx->nativeField() : 0;
    if (field && [field currentEditor] != nil)
    {
      if (edit->props.controlTag_ != 0)
      {
        const_cast<MacScenePlatformController *>(this)->focusedEditTextControlTag_ = edit->props.controlTag_;
      }
      return edit->props.text_.state();
    }
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      void *state = findFocusedEditTextState(child);
      if (state)
      {
        return state;
      }
    }
  }
  return 0;
}

void *MacScenePlatformController::findFieldForFocusedEdit(loka::app::scene::Node *node) const
{
  if (!node)
  {
    return 0;
  }
  if (loka::app::EditTextNode *edit = node->asEditTextNode())
  {
    const bool controlTagMatches =
        focusedEditTextControlTag_ != 0 && edit->props.controlTag_ == focusedEditTextControlTag_;
    const bool stateMatches = focusedEditTextState_ != 0 && edit->props.text_.state() == focusedEditTextState_;
    if (controlTagMatches || (focusedEditTextControlTag_ == 0 && stateMatches))
    {
      MacEditTextContext *ctx = static_cast<MacEditTextContext *>(edit->getContext());
      return ctx ? ctx->nativeField() : 0;
    }
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      void *field = findFieldForFocusedEdit(child);
      if (field)
      {
        return field;
      }
    }
  }
  return 0;
}

void MacScenePlatformController::restoreFocusedEditField()
{
  if (!focusedEditTextState_ || !rootView_ || !rootNode_)
  {
    focusedEditTextState_ = 0;
    focusedEditTextControlTag_ = 0;
    return;
  }
  NSView *view = (NSView *)rootView_;
  NSWindow *window = [view window];
  if (!window)
  {
    focusedEditTextState_ = 0;
    focusedEditTextControlTag_ = 0;
    return;
  }
  NSTextField *field = (NSTextField *)findFieldForFocusedEdit(rootNode_);
  if (field)
  {
    [window makeFirstResponder:field];
  }
  focusedEditTextState_ = 0;
  focusedEditTextControlTag_ = 0;
}

void MacScenePlatformController::releaseNodeContexts(loka::app::scene::Node *node)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  clearNodeContexts(node);
}

void MacScenePlatformController::clearContexts()
{
  clearNodeContexts(rootNode_);
}

void MacScenePlatformController::clearNodeContexts(loka::app::scene::Node *node)
{
  if (!node)
  {
    return;
  }
  // Parked retained branches (Conditional slots) hand their native pairs
  // over here too — the retire door, not the reclaim drain.
  for (unsigned i = 0; loka::app::scene::Node *branch = node->retainedLifecycleBranch(i); ++i)
  {
    clearNodeContexts(branch);
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      clearNodeContexts(child);
    }
  }
  node->setContext(0);
}

int MacScenePlatformController::measureClientWidth(int requestedWidth) const
{
  if (requestedWidth > 0)
  {
    return requestedWidth;
  }
  if (rootView_)
  {
    NSView *view = (NSView *)rootView_;
    NSRect bounds = [view bounds];
    return this->projection().clientCapacityToLu(bounds.size.width);
  }
  return 260;
}

void MacScenePlatformController::releaseMenu()
{
  MacWindow *window = MacWindow::fromRootView(this->rootView());
  MacApp *app = window ? window->macApp() : 0;
  if (app)
    app->menuAttachment().releaseFrom(window->scene());
}
