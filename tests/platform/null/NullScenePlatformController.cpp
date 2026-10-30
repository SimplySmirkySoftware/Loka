#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "support/NullLayoutRefusal.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/context/NullTextEditorContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"

#include <cassert>
#include <climits>
#include "platform/StringUTF8.hpp"
#include <algorithm>
#include "app/scene/projection/PaintEnumeration.hpp"

#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "platform/null/context/NullButtonContext.hpp"
#include "platform/null/context/NullEditTextContext.hpp"
#include "platform/null/context/NullScrollBarContext.hpp"
#include "platform/null/context/NullRectSurfaceContext.hpp"
#include "app/scene/projection/PlatformApplyPlan.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "platform/null/context/NullTextContext.hpp"

namespace
{
  const loka::app::layout::RailSpacing kNullSpacing(12, 4, 9);

  /** Intersects two clips in Null projection coordinates, once per nested frame. */
  loka::core::Frame intersectClip(const loka::core::Frame &a, const loka::core::Frame &b)
  {
    const int left = std::max(a.x, b.x);
    const int top = std::max(a.y, b.y);
    return loka::core::Frame(left,
                             top,
                             std::max(0, std::min(a.x + a.width, b.x + b.width) - left),
                             std::max(0, std::min(a.y + a.height, b.y + b.height) - top));
  }
}

/** Null measures intrinsic images 1:1 in pixels and push buttons at eight units per
    UTF-32 scalar plus sixteen units of title inset. Text uses its current value and
    resolved style through the layout synthetic width source, declining empty,
    multiline, unmeasurable or unrepresentable values without writing. Answers
    do not depend on placement and remain stable during layout. */
bool NullScenePlatformController::queryNaturalWidth(loka::app::scene::Node *child, short &width) const
{
  if (loka::app::ImageViewNode *image = child ? child->asImageViewNode() : 0)
  {
    int pixels = 0;
    if (!loka::app::queryIntrinsicImageWidth(image->props, pixels) || pixels > SHRT_MAX)
      return false;
    width = static_cast<short>(pixels);
    return true;
  }
  if (loka::app::TextNode *text = child ? child->asTextNode() : 0)
  {
    if (!text->props.text_)
      return false;
    const loka::app::SyntheticTextWidthSource source(text->props.text_->get(), text->props.resolvedTextStyle());
    if (!source.valid() || source.length() == 0)
      return false;
    for (std::size_t i = 0; i < source.length(); ++i)
      if (source.character(i).value == '\r' || source.character(i).value == '\n')
        return false;
    int measured = 0;
    // Measure before the line breaker's short saturation, using its width source.
    if (!source.width(0, source.length(), 0, measured) || measured <= 0 || measured > SHRT_MAX)
      return false;
    width = static_cast<short>(measured);
    return true;
  }
  loka::app::ButtonNode *button = child ? child->asButtonNode() : 0;
  if (!button)
    return false;
  const loka::core::String label = button->props.text_
      ? button->props.text_->get() : loka::core::String::Literal("Button");
  const std::size_t units = label.bufferWithEncoding(loka::core::StringEncodingUtf32).length();
  if (units > static_cast<std::size_t>((SHRT_MAX - 16) / 8))
    return false;
  width = static_cast<short>(units * 8 + 16);
  return width > 0;
}

bool NullScenePlatformController::queryNaturalHeight(loka::app::scene::Node *child, short &height) const
{
  loka::app::TextNode *text = child ? child->asTextNode() : 0;
  if (!text || !text->props.text_)
    return false;
  const loka::app::BlockStyle &block = text->props.blockStyle_;
  if (block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE)
    return false;
  const loka::app::TextStyle style = text->props.resolvedTextStyle();
  std::string value;
  if (!loka::platform::CollectUtf8(text->props.text_->get(), value) || value.find_first_of("\r\n") != std::string::npos)
    return false;
  const int measured = loka::app::SyntheticTextSize(style.size_);
  if (measured <= 0 || measured > SHRT_MAX)
    return false;
  height = static_cast<short>(measured);
  return true;
}

class NullScenePlatformController::LayoutTraversal
    : public loka::app::scene::IPlatformLayoutTraversal
{
public:
  explicit LayoutTraversal(NullScenePlatformController *controller)
      : controller_(controller),
        resultY_(0)
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

  virtual int layoutChild(loka::app::scene::Node *child,
                          const loka::app::scene::LayoutState &state)
  {
    if (!this->controller_)
    {
      return state.y;
    }
    const int result = this->controller_->layoutNode(child, state);
    if (this->controller_->refuseNarrowingInScrollScope(result))
    {
      return state.y;
    }
    this->resultY_ = static_cast<short>(result);
    return result;
  }

  virtual bool refuseLayoutResultY(int y)
  {
    return this->controller_->refuseNarrowingInScrollScope(y);
  }

  virtual void setLayoutResultY(short y)
  {
    this->resultY_ = y;
  }

  virtual short layoutResultY() const
  {
    return this->resultY_;
  }

private:
  NullScenePlatformController *controller_;
  short resultY_;
};

NullScenePlatformController::RefusedProjectedNodeHandlers::RefusedProjectedNodeHandlers()
    : cardBand_(loka::app::scene::NodeTypeToken<loka::app::CardBandNode>()),
      ribbonGroupEdge_(loka::app::scene::NodeTypeToken<loka::app::RibbonGroupEdgeNode>()),
      cell_(loka::app::scene::NodeTypeToken<loka::app::CellNode>()),
      popupMenu_(loka::app::scene::NodeTypeToken<loka::app::PopupMenuNode>()),
      imageView_(loka::app::scene::NodeTypeToken<loka::app::ImageViewNode>()),
      openFileDialog_(loka::app::scene::NodeTypeToken<loka::app::OpenFileDialogNode>()),
      confirmDialog_(loka::app::scene::NodeTypeToken<loka::app::ConfirmDialogNode>())
{
}

void NullScenePlatformController::RefusedProjectedNodeHandlers::registerWith(
    NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&this->cardBand_);
  controller.registerNodeHandler(&this->ribbonGroupEdge_);
  controller.registerNodeHandler(&this->cell_);
  controller.registerNodeHandler(&this->popupMenu_);
  controller.registerNodeHandler(&this->imageView_);
  controller.registerNodeHandler(&this->openFileDialog_);
  controller.registerNodeHandler(&this->confirmDialog_);
}

unsigned NullScenePlatformController::RefusedProjectedNodeHandlers::cellCount() const
{
  return this->cell_.refusalCount();
}

NullScenePlatformController::NullScenePlatformController(std::size_t bucketDepthCap, loka::app::TextShaping shaping)
    : leafLayoutVisits_(0),
      textShaping_(shaping),
      layoutState_(),
      paintScope_(),
      layoutHandlers_(),
      refusedProjectedNodeHandlers_(),
      nodeHandlers_(),
      rectSurfaceExtentLedger_(),
      rootNode_(0),
      ledger_(),
      lastOnChangeFlags_(loka::app::scene::NODE_DIRTY_NONE),
      onChangeCallCount_(0),
      retired_(),
      allHandles_(),
      buttonBucket_(bucketDepthCap),
      editTextBucket_(bucketDepthCap),
      scrollBarBucket_(bucketDepthCap),
      teardownCounters_(),
      intakeCheckFailCount_(0),
      createdCount_(0),
      disposedCount_(0),
      injectedDeliveryCount_(0),
      nextEventSequence_(1),
      nextHandleId_(1),
      projectionParentScopes_(this),
      scrollViewShortRangeRefusalCount_(0),
      nestedScrollViewRefusalCount_(0),
      preserveNextRetiredOwner_(false),
      skipNextProjection_(false),
      destroyed_(false),
      eventLog_()
{
  this->layoutState_.width = 100;
  this->layoutState_.height = 20;
  this->layoutState_.lineHeight = 20;
  loka::app::layout::RowLayoutMetrics rowMetrics;
  rowMetrics.fallbackHeight = 10;
  rowMetrics.buttonHeight = 10;
  rowMetrics.editTextHeight = 10;
  rowMetrics.popupMenuHeight = 10;
  rowMetrics.textHeight = 10;
  rowMetrics.imageFallbackHeight = 10;
  loka::app::layout::GridLayoutMetrics gridMetrics;
  gridMetrics.gapX = 2;
  gridMetrics.gapY = 4;
  loka::app::layout::RegisterBuiltinPlatformLayoutHandlers(
      this->layoutHandlers_, &rowMetrics, &gridMetrics, kNullSpacing);
  RegisterNullButtonNodeHandler(*this);
  RegisterNullEditTextNodeHandler(*this);
  RegisterNullTextEditorNodeHandler(*this);
  RegisterNullScrollBarNodeHandler(*this);
  RegisterNullTextNodeHandler(*this);
  RegisterNullAttributedTextNodeHandler(*this);
  RegisterNullRectSurfaceNodeHandler(*this);
  this->refusedProjectedNodeHandlers_.registerWith(*this);
}

NullScenePlatformController::~NullScenePlatformController()
{
  this->destroy();
  for (std::size_t i = 0; i < this->allHandles_.size(); ++i)
  {
    delete this->allHandles_[i];
  }
  this->allHandles_.clear();
}

void NullScenePlatformController::onChange(loka::app::scene::Node *rootNode,
                                           loka::app::scene::NodeDirtyFlags flags,
                                           bool fullRebuild)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->borrowPhase().open());
#endif
  (void)fullRebuild;
  this->lastOnChangeFlags_ = flags;
  ++this->onChangeCallCount_;
  this->rootNode_ = rootNode;
  if (rootNode)
    this->destroyed_ = false;

  if (this->skipNextProjection_)
  {
    this->skipNextProjection_ = false;
    return;
  }
  this->projectLayout(rootNode, this->layoutState_);
}

namespace
{
  void syncNativeControlsInSubtree(loka::app::scene::Node *node)
  {
    if (!node)
    {
      return;
    }
    if (node->nodeTypeKey() == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>() && node->getContext())
      static_cast<NullTextEditorContext *>(node->getContext())->syncFromNode();
    if (node->kind() == loka::app::scene::NODE_KIND_SCROLL_BAR && node->getContext())
    {
      static_cast<NullScrollBarContext *>(node->getContext())->syncFromNode();
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    if (!nestable)
    {
      return;
    }
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(
        nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      syncNativeControlsInSubtree(child);
    }
  }
} // namespace

namespace
{
  enum NullPaintRole
  {
    NULL_PAINT_SKIP,
    NULL_PAINT_OWNED_DRAWER,   // RectSurface / Text / AttributedText: contexts the rail itself installs (registration refuses replacement)
    NULL_PAINT_NATIVE_CONTROL, // Button / EditText / TextEditor / ScrollBar: native ownership, answered by kind, context never cast
    NULL_PAINT_FOREIGN,        // ImageView / Cell / PopupMenu: refused by default, any installed context is foreign
    NULL_PAINT_UNSUPPORTED
  };
  /** Classification is a rail contract, independent of handler registration. */
  NullPaintRole paintRole(loka::app::scene::Node *node)
  {
    using namespace loka::app::scene;
    switch (node->kind())
    {
    case NODE_KIND_RECT_SURFACE:
    case NODE_KIND_ATTRIBUTED_TEXT:
    case NODE_KIND_TEXT:
      return NULL_PAINT_OWNED_DRAWER;
    case NODE_KIND_BUTTON:
    case NODE_KIND_EDIT_TEXT:
    case NODE_KIND_TEXT_EDITOR:
    case NODE_KIND_SCROLL_BAR:
      return NULL_PAINT_NATIVE_CONTROL;
    case NODE_KIND_IMAGE_VIEW:
    case NODE_KIND_CELL:
    case NODE_KIND_POPUP_MENU:
      return NULL_PAINT_FOREIGN;
    case NODE_KIND_OPEN_FILE_DIALOG:
    case NODE_KIND_CONFIRM_DIALOG:
    case NODE_KIND_SCROLL_VIEW:
    case NODE_KIND_BOX:
    case NODE_KIND_RIBBON_GROUP_EDGE:
    case NODE_KIND_CARD_BAND:
    case NODE_KIND_CARD:
    case NODE_KIND_RIBBON_GROUP:
    case NODE_KIND_ZSTACK:
    case NODE_KIND_GRID:
    case NODE_KIND_STACK:
    case NODE_KIND_CANVAS:
      return NULL_PAINT_SKIP;
    case NODE_KIND_UNKNOWN:
      return node->asProjectedLayoutNode() ? NULL_PAINT_UNSUPPORTED : NULL_PAINT_SKIP;
    }
    return NULL_PAINT_UNSUPPORTED;
  }
} // namespace
bool NullScenePlatformController::queryPaintAnswer(loka::app::scene::Node *node,
                                                   loka::app::scene::NodeContext *context,
                                                   const loka::app::scene::PaintQuery &query,
                                                   loka::app::scene::PaintAnswer &answer)
{
  using namespace loka::app::scene;
  switch (paintRole(node))
  {
  case NULL_PAINT_SKIP:
    return false;
  case NULL_PAINT_OWNED_DRAWER:
    // Safe by the rail's installation contract: registerNodeHandler refuses
    // foreign handlers for Text, AttributedText and RectSurface. Common never casts contexts.
    answer = context ? static_cast<NativeNodeContext *>(context)->queryPaintDamage(query)
                     : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
    break;
  case NULL_PAINT_NATIVE_CONTROL:
    answer = PaintAnswer::nativeScheduled();
    break;
  case NULL_PAINT_FOREIGN:
    answer = PaintAnswer::refused(context ? PAINT_REFUSED_UNSUPPORTED_KIND : PAINT_REFUSED_NO_CONTEXT);
    break;
  case NULL_PAINT_UNSUPPORTED:
    answer = PaintAnswer::refused(PAINT_REFUSED_UNSUPPORTED_KIND);
    break;
  }
  this->onPaintQueried();
  return true;
}
class NullScenePlatformController::PaintCompletionVisitor : public loka::app::scene::IPaintResidentVisitor
{
public:
  explicit PaintCompletionVisitor(NullScenePlatformController &controller)
      : controller_(controller)
  {
  }
  virtual void
  visit(loka::app::scene::Node *node, loka::app::scene::NodeContext *context, loka::app::scene::BoundaryNode *)
  {
    if (paintRole(node) != NULL_PAINT_OWNED_DRAWER || !context)
      return;
    bool committed = false;
    // Capture and commit synchronously, with no callbacks or State writes between.
    if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
    {
      if (surface->props.model_)
      {
        const loka::app::RectSurfaceModel value = surface->props.model_->get();
        committed = static_cast<NullRectSurfaceContext *>(context)->commitPresented(
            value, this->controller_.paintScope());
      }
    }
    else if (loka::app::TextNode *text = node->asTextNode())
    {
      if (text->props.text_)
      {
        const loka::core::String value = text->props.text_->get();
        committed = static_cast<NullTextContext *>(context)->commitPresented(value, this->controller_.paintScope());
      }
    }
    else if (loka::app::AttributedTextNode *attributedText = node->asAttributedTextNode())
    {
      if (attributedText->props.text_)
      {
        const loka::app::AttributedString value = attributedText->props.text_->get();
        committed = static_cast<NullAttributedTextContext *>(context)->commitPresented(value, this->controller_.paintScope());
      }
    }
    if (committed)
      this->controller_.onPaintCommitted();
  }

private:
  NullScenePlatformController &controller_;
};
class NullScenePlatformController::PaintInvalidationVisitor : public loka::app::scene::IPaintResidentVisitor
{
public:
  virtual void
  visit(loka::app::scene::Node *node, loka::app::scene::NodeContext *context, loka::app::scene::BoundaryNode *)
  {
    if (paintRole(node) != NULL_PAINT_OWNED_DRAWER || !context)
      return;
    if (node->asRectSurfaceNode())
      static_cast<NullRectSurfaceContext *>(context)->invalidatePresentation();
    else if (node->asAttributedTextNode())
      static_cast<NullAttributedTextContext *>(context)->invalidatePresentation();
    else if (node->asTextNode())
      static_cast<NullTextContext *>(context)->invalidatePresentation();
  }
};
loka::app::scene::PaintScope NullScenePlatformController::paintScope() const
{
  return this->paintScope_;
}
bool NullScenePlatformController::queryPaintProjectionScope(loka::app::scene::PaintScope &scope) const
{
  const loka::app::scene::ProjectionParentScope &projection = this->projectionParentScopes_.current();
  const loka::core::Frame &clip = projection.clipRect;
  if (projection.translationX != 0 || projection.translationY != 0 || projection.hasShortRangeRefusal()
      || clip.x != this->paintScope_.clipX || clip.y != this->paintScope_.clipY
      || clip.width != this->paintScope_.clipWidth || clip.height != this->paintScope_.clipHeight)
    return false;
  scope = this->paintScope_;
  return true;
}
void NullScenePlatformController::presentPaintPlan(loka::app::scene::BoundaryNode *root,
                                                   const loka::app::scene::ApplyPaintPlan &plan,
                                                   loka::app::scene::PaintPlacementEligibility eligibility)
{
  this->onPaintPlanSubmitted(root, plan);
  if (eligibility != loka::app::scene::PLACEMENT_ELIGIBLE)
    return;
  PaintCompletionVisitor completion(*this);
  loka::app::scene::enumerateAttachedResidents(root, completion);
}

void NullScenePlatformController::onBoundaryApply(loka::app::scene::Node *rootNode,
                                                  loka::app::scene::BoundaryNode *boundary,
                                                  const loka::app::scene::BoundaryLocalApplyInfo &info,
                                                  const loka::app::scene::PlatformApplyPlan &plan)
{
  (void)rootNode;
  if (!boundary || !plan.hasBoundaryApplyWork(boundary) || !info.hasPaintWork())
  {
    return;
  }
  syncNativeControlsInSubtree(static_cast<loka::app::scene::Node *>(boundary));
  const loka::app::scene::PaintQuery query = {this->paintScope(),
                                              plan.hasStructureWork() || plan.hasLayoutWork()
                                                  ? loka::app::scene::PLACEMENT_PENDING
                                                  : loka::app::scene::PLACEMENT_ELIGIBLE};
  loka::app::scene::PaintAnswerBuffer<> answers;
  const loka::app::scene::PaintApplyVerdict verdict =
      loka::app::scene::CollectPaintAnswers(*boundary, query, answers, *this);
  this->onPaintAnswersCollected(boundary, info, answers, verdict);
  const loka::app::scene::ApplyPaintPlan paint = loka::app::scene::BuildApplyPaintPlan(query.scope, answers, verdict);
  this->presentPaintPlan(boundary, paint, query.placement);
}

void NullScenePlatformController::synchronize() {}

void NullScenePlatformController::drainNativeRetirements()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->borrowPhase().open());
#endif
  if (this->retired_.empty())
  {
    return;
  }
  this->appendEvent(EVENT_FLUSH_BEGIN, 0);
  this->flushRetired();
  this->appendEvent(EVENT_FLUSH_END, 0);
}

bool NullScenePlatformController::hasPendingSync() const
{
  return !this->retired_.empty();
}

void NullScenePlatformController::destroy()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->borrowPhase().open());
#endif
  if (this->destroyed_)
  {
    return;
  }
  if (this->rootNode_)
  {
    this->releaseNodeContexts(this->rootNode_);
  }
  this->appendEvent(EVENT_DRAIN_BEGIN, 0);
  this->flushRetired();
  this->drainBuckets();
  this->appendEvent(EVENT_DRAIN_END, 0);
  this->rootNode_ = 0;
  this->destroyed_ = true;
}

bool NullScenePlatformController::prepareProjectedLayout(loka::app::scene::Node *node,
                                                         loka::app::scene::LayoutState &state)
{
  if (!node)
  {
    return false;
  }
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlers_.find(node);
  if (!handler)
  {
    assert(false && "no node handler registered for this node type -- register the handler or an explicit RefusedNodeHandler");
    return false;
  }
  return handler->ensureContext(node, this, state) != 0;
}

bool NullScenePlatformController::registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler)
{
  // Always-on refusal: RectSurface, Text, AttributedText and TextEditor contexts
  // are addressed by their concrete Null type in the presenter's completion and
  // invalidation walks, so no foreign handler may install a different context
  // for those kinds. Every other kind may be replaced; the paint walk never
  // casts a context it did not install (see paintRole).
  if (handler
      && ((handler->nodeTypeKey() == NullTextNodeHandlerKey() && !IsNullTextNodeHandler(handler))
          || (handler->nodeTypeKey() == NullAttributedTextNodeHandlerKey() && !IsNullAttributedTextNodeHandler(handler))
          || (handler->nodeTypeKey() == NullTextEditorNodeHandlerKey() && !IsNullTextEditorNodeHandler(handler))
          || (handler->nodeTypeKey() == NullRectSurfaceNodeHandlerKey() && !IsNullRectSurfaceNodeHandler(handler))))
  {
    return false;
  }
  return this->nodeHandlers_.registerHandler(handler);
}

int NullScenePlatformController::projectLayoutForTesting(
    loka::app::scene::Node *node,
    const loka::app::scene::LayoutState &state,
    const loka::app::layout::LazyWindow *range,
    loka::app::layout::StackSpans *spans)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->borrowPhase().open());
#endif
  this->layoutState_ = state;
  return this->projectLayout(node, this->layoutState_, range, spans);
}

const std::vector<NullScenePlatformController::LedgerRow> &NullScenePlatformController::ledger() const
{
  return this->ledger_;
}

const std::vector<NullScenePlatformController::FakeControlHandle *> &
NullScenePlatformController::allHandles() const
{
  return this->allHandles_;
}

const std::vector<NullScenePlatformController::EventRecord> &NullScenePlatformController::eventLog() const
{
  return this->eventLog_;
}

std::size_t NullScenePlatformController::retiredCount() const
{
  return this->retired_.size();
}

const NullScenePlatformController::TeardownCounters &NullScenePlatformController::teardownCounters() const
{
  return this->teardownCounters_;
}

unsigned long NullScenePlatformController::intakeCheckFailCount() const
{
  return this->intakeCheckFailCount_;
}

unsigned long NullScenePlatformController::createdCount() const
{
  return this->createdCount_;
}

unsigned long NullScenePlatformController::disposedCount() const
{
  return this->disposedCount_;
}

unsigned NullScenePlatformController::cellRefusalCount() const
{
  return this->refusedProjectedNodeHandlers_.cellCount();
}

unsigned NullScenePlatformController::scrollViewShortRangeRefusalCount() const
{
  return this->scrollViewShortRangeRefusalCount_;
}

unsigned NullScenePlatformController::nestedScrollViewRefusalCount() const
{
  return this->nestedScrollViewRefusalCount_;
}

unsigned NullScenePlatformController::projectionParentScopeDepthForTesting() const
{
  return this->projectionParentScopes_.activeDepth();
}

loka::app::scene::ExactMatchHandleBucket<NullScenePlatformController::FakeControlHandle *> &
NullScenePlatformController::bucketFor(ControlRecipe recipe)
{
  if (recipe == CONTROL_RECIPE_BUTTON)
  {
    return this->buttonBucket_;
  }
  if (recipe == CONTROL_RECIPE_SCROLL_BAR)
  {
    return this->scrollBarBucket_;
  }
  return this->editTextBucket_;
}

const loka::app::scene::ExactMatchHandleBucket<NullScenePlatformController::FakeControlHandle *> &
NullScenePlatformController::bucketFor(ControlRecipe recipe) const
{
  return const_cast<NullScenePlatformController *>(this)->bucketFor(recipe);
}

NullScenePlatformController::BucketStats
NullScenePlatformController::bucketStats(ControlRecipe recipe) const
{
  const loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> &bucket = this->bucketFor(recipe);
  return BucketStats(bucket.hitCount(), bucket.missCount(), bucket.evictCount(), bucket.depth());
}

unsigned long NullScenePlatformController::eventCount(EventKind kind) const
{
  unsigned long count = 0;
  for (std::size_t i = 0; i < this->eventLog_.size(); ++i)
  {
    if (this->eventLog_[i].kind == kind)
    {
      ++count;
    }
  }
  return count;
}

const NullScenePlatformController::LedgerRow *
NullScenePlatformController::findLedgerRow(ControlRecipe recipe) const
{
  for (std::size_t i = 0; i < this->ledger_.size(); ++i)
  {
    if (this->ledger_[i].recipe == recipe)
    {
      return &this->ledger_[i];
    }
  }
  return 0;
}

bool NullScenePlatformController::hasHitTarget(ControlRecipe recipe) const
{
  const LedgerRow *row = this->findLedgerRow(recipe);
  return row && row->handle && row->handle->hitOwner;
}

bool NullScenePlatformController::injectNotification(ControlRecipe recipe)
{
  if (!this->hasHitTarget(recipe))
  {
    return false;
  }
  ++this->injectedDeliveryCount_;
  return true;
}

unsigned long NullScenePlatformController::injectedDeliveryCount() const
{
  return this->injectedDeliveryCount_;
}

void NullScenePlatformController::preserveNextRetiredOwnerForTesting()
{
  this->preserveNextRetiredOwner_ = true;
}

void NullScenePlatformController::skipNextProjectionForTesting()
{
  this->skipNextProjection_ = true;
}

NullScenePlatformController::FakeControlHandle *
NullScenePlatformController::createLedgerRow(ControlRecipe recipe,
                                             loka::app::scene::NodeContext *owner,
                                             loka::app::scene::NativeLifetimeHint hint)
{
  FakeControlHandle *handle = 0;
  const bool acquired = this->bucketFor(recipe).tryAcquire(handle);
  if (!acquired)
  {
    handle = new FakeControlHandle(this->nextHandleId_++, owner);
    if (!handle)
    {
      return 0;
    }
    this->allHandles_.push_back(handle);
    ++this->createdCount_;
    this->appendEvent(EVENT_CONTROL_CREATED, handle->id);
  }
  handle->owner = owner;
  handle->hitOwner = owner;
  handle->disposed = false;
  handle->leakedDeliberately = false;
  this->ledger_.push_back(LedgerRow(handle, recipe, hint));
  return handle;
}

void NullScenePlatformController::completeContextTeardown(FakeControlHandle *handle)
{
  if (!handle)
  {
    return;
  }
  LedgerRow *row = this->findLedgerRow(handle);
  if (!row)
  {
    return;
  }
  const ControlRecipe recipe = row->recipe;
  const loka::app::scene::NativeLifetimeHint hint = row->hint;

  if (this->preserveNextRetiredOwner_)
  {
    this->preserveNextRetiredOwner_ = false;
  }
  else
  {
    handle->owner = 0;
    ++this->teardownCounters_.backPointerCleared;
  }
  if (handle->hitOwner)
  {
    handle->hitOwner = 0;
    ++this->teardownCounters_.hitRouteRemoved;
  }

  this->retired_.push_back(RetiredEntry(handle, recipe, hint));
  ++this->teardownCounters_.queuedForNativeRetirement;
}

void NullScenePlatformController::setVisible(FakeControlHandle *handle, bool visible)
{
  LedgerRow *row = this->findLedgerRow(handle);
  if (!row || row->visible == visible)
  {
    return;
  }
  row->visible = visible;
  this->appendEvent(visible ? EVENT_CONTROL_SHOWN : EVENT_CONTROL_HIDDEN, handle->id);
}

void NullScenePlatformController::observeHint(FakeControlHandle *handle,
                                              loka::app::scene::NativeLifetimeHint hint)
{
  LedgerRow *row = this->findLedgerRow(handle);
  if (row)
  {
    row->hint = hint;
  }
}

NullScenePlatformController::LedgerRow *
NullScenePlatformController::findLedgerRow(FakeControlHandle *handle)
{
  for (std::size_t i = 0; i < this->ledger_.size(); ++i)
  {
    if (this->ledger_[i].handle == handle)
    {
      return &this->ledger_[i];
    }
  }
  return 0;
}

/** The reached helper returns both its geometry result and request capability. */
struct NullScenePlatformController::ScrollViewLayoutResult
{
  const int bottom;
  const loka::app::ScrollViewVisit visit;
  ScrollViewLayoutResult(int value, const loka::app::ScrollViewVisit &outcome)
      : bottom(value),
        visit(outcome)
  {
  }
};
bool NullScenePlatformController::applyScrollViewRequest(loka::app::ScrollViewNode &node,
                                                         const ScrollViewLayoutResult &,
                                                         const loka::core::Frame &target)
{
  node.publishOrigin(target.x, target.y);
  return true;
}

int NullScenePlatformController::layoutNode(loka::app::scene::Node *node,
                                            const loka::app::scene::LayoutState &received,
                                            const loka::app::layout::LazyWindow *range,
                                            loka::app::layout::StackSpans *spans)
{
  loka::app::scene::LayoutState state = received;
  state.spacing = static_cast<short>(loka::app::layout::resolveLayoutGap(node, received.spacing, kNullSpacing));
  if (!node)
  {
    return state.y;
  }

  if (!node->asNestable())
    ++this->leafLayoutVisits_;

  if (this->projectionParentScopes_.activeDepth() != 0 &&
      this->projectionParentScopes_.current().hasShortRangeRefusal())
  {
    // The scope's measurement channel already refused: a shared container
    // loop above may hand us a narrowed coordinate, so nothing below this
    // point may materialize from it.
    return state.y;
  }

  if (loka::app::ScrollViewNode *scrollView = node->asScrollViewNode())
    return loka::app::SettleScrollViewVisit(*this,
                                            &NullScenePlatformController::layoutScrollView,
                                            &NullScenePlatformController::applyScrollViewRequest,
                                            scrollView,
                                            state,
                                            range,
                                            spans)
        .bottom;

  if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
  {
    const short width = surface->props.width_ > 0 ? surface->props.width_ : state.width;
    const short height = surface->props.height_ > 0 ? surface->props.height_ : state.height;
    loka::app::scene::LayoutState projectedState;
    if (!this->projectionParentScopes_.current().project(state, projectedState))
    {
      this->refuseScrollViewShortRange();
      return state.y;
    }
    if (!this->prepareProjectedLayout(surface, projectedState))
    {
      return state.y;
    }
    const loka::app::scene::Node::LayoutInputsCheckpoint inputs(*surface);
    const int projectedResult = surface->layout(this, projectedState);
    int contentResult = state.y;
    if (
#ifdef TEST_BUILD
        loka::testing::declineNullProjectedLayoutRestore() ||
#endif
        !this->projectionParentScopes_.current().restoreContentY(
            projectedResult, contentResult))
    {
      inputs.requeue();
      this->refuseScrollViewShortRange();
      return state.y;
    }
    // The seat is a fact only once the surface was actually placed: a
    // refused projection, context, or restore records nothing.
    this->rectSurfaceExtentLedger_.record(
        surface, loka::core::Frame(state.x, state.y, width, height));
    return contentResult;
  }

  loka::app::scene::IPlatformLayoutHandler *layoutHandler = this->layoutHandlers_.find(node);
  if (layoutHandler)
  {
    LayoutTraversal traversal(this);
    const loka::app::BoxNode *box = node->asBoxNode();
    const loka::app::CardNode *card =
        node->kind() == loka::app::scene::NODE_KIND_CARD ? static_cast<loka::app::CardNode *>(node) : 0;
    if ((box && box->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT)
        || (card && card->props.childClip_ == loka::app::layout::CHILD_CLIP_OWN_RECT))
    {
      const loka::app::scene::ProjectionParentScope &parent = this->projectionParentScopes_.current();
      loka::app::scene::LayoutState projected;
      if (parent.project(state, projected))
      {
        const bool atRoot = this->projectionParentScopes_.activeDepth() == 0;
        const int width = box && box->props.effectiveWidth() > 0 ? box->props.effectiveWidth() : state.width;
        const int height = box ? box->props.height : 0;
        // Layout uses a provisional rect: automatic edges reach the parent's clip.
        // Null returns only Y; there is no reported width to cap here.
        const int right = width > 0 ? std::min<int>(SHRT_MAX, projected.x + width)
                                    : (atRoot ? SHRT_MAX : parent.clipRect.x + parent.clipRect.width);
        const int bottom = height > 0 ? std::min<int>(SHRT_MAX, projected.y + height)
                                      : (atRoot ? SHRT_MAX : parent.clipRect.y + parent.clipRect.height);
        loka::app::scene::ProjectionParentScope clipScope = parent;
        const loka::core::Frame rect(
            projected.x, projected.y, std::max(0, right - projected.x), std::max(0, bottom - projected.y));
        clipScope.clipRect = atRoot ? rect : intersectClip(parent.clipRect, rect);
        loka::app::scene::ProjectionClipScopeGuard guard(this->projectionParentScopes_, clipScope);
        // A refused push leaves the parent frame active; children still lay out.
        return layoutHandler->layoutNode(node, state, &traversal, range, spans);
      }
    }
    return layoutHandler->layoutNode(node, state, &traversal, range, spans);
  }

  if (loka::app::scene::IProjectedLayoutNode *projected = node->asProjectedLayoutNode())
  {
    loka::app::scene::LayoutState projectedState;
    if (!this->projectionParentScopes_.current().project(state, projectedState))
    {
      this->refuseScrollViewShortRange();
      return state.y;
    }
    const loka::app::scene::Node::LayoutInputsCheckpoint inputs(*node);
    const int projectedResult = projected->layoutProjected(this, projectedState);
    int contentResult = state.y;
    if (
#ifdef TEST_BUILD
        loka::testing::declineNullProjectedLayoutRestore() ||
#endif
        !this->projectionParentScopes_.current().restoreContentY(
            projectedResult, contentResult))
    {
      inputs.requeue();
      this->refuseScrollViewShortRange();
      return state.y;
    }
    return contentResult;
  }

  loka::app::scene::INestable *nestable = node->asNestable();
  if (!nestable)
  {
    return state.y;
  }
  loka::app::scene::LayoutState childState = state;
  loka::app::layout::StackGaps gaps(state.spacing);
  for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
  {
    const int childY = childState.y + gaps.before(child);
    if (this->refuseNarrowingInScrollScope(childY))
      break;
    childState.y = static_cast<short>(childY);
    const int nextY = this->layoutNode(child, childState);
    if (this->refuseNarrowingInScrollScope(nextY))
    {
      break;
    }
    childState.y = static_cast<short>(nextY);
  }
  return childState.y;
}

NullScenePlatformController::ScrollViewLayoutResult
NullScenePlatformController::layoutScrollView(loka::app::scene::Node *node,
                                              const loka::app::scene::LayoutState &state,
                                              const loka::app::layout::LazyWindow *range,
                                              loka::app::layout::StackSpans *spans)
{
  loka::app::ScrollViewNode *scrollView = node ? node->asScrollViewNode() : 0;
  if (!scrollView)
  {
    if (spans)
      spans->invalidate();
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }
  if (this->projectionParentScopes_.insideViewport() || this->projectionParentScopes_.insideUnit())
  {
    this->refuseNestedScrollView();
    if (spans)
      spans->invalidate();
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }
  if (state.height > 0 && state.y + state.height > SHRT_MAX)
  {
    // The viewport bottom itself would leave the short range; the caller
    // casts the result back to LayoutState::Coordinate, so refuse before
    // materializing a subtree whose seat cannot be represented.
    this->refuseScrollViewShortRange();
    if (spans)
      spans->invalidate();
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }

  const loka::core::Frame translation = scrollView->contentTranslation();
  const loka::core::Frame rect(state.x, state.y, state.width, state.height);
  loka::app::scene::ProjectionParentScope childScope;
  const loka::app::scene::ProjectionParentScope &parentScope = this->projectionParentScopes_.current();
  const loka::core::Frame clip =
      this->projectionParentScopes_.activeDepth() == 0 ? rect : intersectClip(parentScope.clipRect, rect);
  if (!parentScope.deriveScrolled(parentScope.nativeParent, translation.x, translation.y, clip, childScope))
  {
    this->refuseScrollViewShortRange();
    if (spans)
      spans->invalidate();
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }

  loka::app::scene::ProjectionViewportScopeGuard scopeGuard(this->projectionParentScopes_, childScope);
  if (!scopeGuard.isActive())
  {
    this->refuseNestedScrollView();
    if (spans)
      spans->invalidate();
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }

  int currentY = state.y;
  const std::size_t ledgerMark = this->rectSurfaceExtentLedger_.mark();
  bool refused = false;
  loka::app::scene::INestable *nestable = scrollView->asNestable();
  loka::app::StackNode *column = nestable && nestable->childrenHead() ? nestable->childrenHead()->asStackNode() : 0;
  const bool directColumn = spans && nestable->childrenCount() == 1 && column
                            && column->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN;
  if (spans && !directColumn)
    spans->invalidate();
  loka::app::layout::StackGaps gaps(state.spacing);
  for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
  {
    const int childY = currentY + gaps.before(child);
    if (childY < SHRT_MIN || childY > SHRT_MAX)
    {
      this->refuseScrollViewShortRange();
      refused = true;
      break;
    }
    loka::app::scene::LayoutState childState = state;
    childState.y = static_cast<short>(childY);
    const int nextY = this->layoutNode(child, childState, directColumn ? range : 0, directColumn ? spans : 0);
    if (this->projectionParentScopes_.current().hasShortRangeRefusal())
    {
      // A nested traversal edge already refused and counted; do not count
      // the stale return value a second time.
      refused = true;
      break;
    }
    if (!this->projectionParentScopes_.current().tryAccumulateContentHeight(currentY, nextY))
    {
      this->refuseScrollViewShortRange();
      refused = true;
      break;
    }
    currentY = nextY;
  }
  const int extentBottom = currentY + scrollView->props.contentInsets_.extentExtra();
  if (!refused && state.height <= 0 && extentBottom > SHRT_MAX)
  {
    this->refuseScrollViewShortRange();
    refused = true;
  }
  if (refused)
  {
    // Seats recorded under a refused scope are not facts.
    this->rectSurfaceExtentLedger_.discardSince(ledgerMark);
    if (spans)
      spans->invalidate();
  }

  if (!refused)
    scrollView->publishViewport(
        translation.x, translation.y + scrollView->props.contentInsets_.top(), state.width, state.height);

  if (!refused && spans && spans->valid())
    spans->placed(loka::core::Frame(translation.x, translation.y, state.width, state.height));
  if (state.height > 0)
  {
    return ScrollViewLayoutResult(state.y + state.height,
                                  refused ? loka::app::ScrollViewVisit::refused()
                                          : loka::app::ScrollViewVisit::succeededUnclamped());
  }
  if (refused)
  {
    return ScrollViewLayoutResult(state.y, loka::app::ScrollViewVisit::refused());
  }
  return ScrollViewLayoutResult(extentBottom, loka::app::ScrollViewVisit::succeededUnclamped());
}

int NullScenePlatformController::projectLayout(loka::app::scene::Node *node,
                                               const loka::app::scene::LayoutState &state,
                                               const loka::app::layout::LazyWindow *range,
                                               loka::app::layout::StackSpans *spans)
{
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a projection pass must begin at the root scope");
  // Placement is a derived cache: clear every self-drawer's seat and history
  // before this fallible pass, so only residents this pass actually places can
  // be completed afterwards. A refused or skipped projection leaves them UNKNOWN.
  if (node && node->asBoundary())
  {
    PaintInvalidationVisitor invalidation;
    loka::app::scene::enumerateAttachedResidents(node->asBoundary(), invalidation);
  }
  const loka::core::Frame rootClip(state.x, state.y, state.width, state.height);
  if (!this->projectionParentScopes_.resetRoot(this, rootClip))
  {
    return state.y;
  }
  const loka::app::scene::PaintScope scope = {1, 0, 0, state.x, state.y, state.width, state.height};
  this->paintScope_ = scope;
  loka::app::scene::LayoutState rootState = state;
  rootState.spacing = static_cast<short>(kNullSpacing.verticalGap());
  const int result = this->layoutNode(node, rootState, range, spans);
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a projection pass must restore the root scope");
  if (node && node->asBoundary())
  {
    loka::app::scene::ApplyPaintPlan paint;
    paint.widen(loka::app::scene::APPLY_PAINT_WIDEN_UNSUPPORTED, this->paintScope());
    this->presentPaintPlan(node->asBoundary(), paint);
  }
  this->rectSurfaceExtentLedger_.flush();
  return result;
}

void NullScenePlatformController::refuseScrollViewShortRange()
{
  ++this->scrollViewShortRangeRefusalCount_;
}

void NullScenePlatformController::refuseNestedScrollView()
{
  ++this->nestedScrollViewRefusalCount_;
}

bool NullScenePlatformController::refuseNarrowingInScrollScope(int resultY)
{
  if (!this->projectionParentScopes_.current().channel)
  {
    return false;
  }
  loka::app::scene::ProjectionParentScope &scope = this->projectionParentScopes_.current();
  if (scope.hasShortRangeRefusal())
  {
    return true;
  }
  if (resultY >= SHRT_MIN && resultY <= SHRT_MAX)
  {
    return false;
  }
  this->refuseScrollViewShortRange();
  scope.markShortRangeRefused();
  return true;
}

void NullScenePlatformController::flushRetired()
{
  for (std::size_t i = 0; i < this->retired_.size(); ++i)
  {
    RetiredEntry &entry = this->retired_[i];
    FakeControlHandle *handle = entry.handle;
    if (!handle)
    {
      continue;
    }
    for (std::size_t rowIndex = 0; rowIndex < this->ledger_.size(); ++rowIndex)
    {
      if (this->ledger_[rowIndex].handle == handle)
      {
        this->ledger_.erase(this->ledger_.begin() + rowIndex);
        ++this->teardownCounters_.ledgerRowRemovedAtSafePoint;
        break;
      }
    }
    if (entry.hint == loka::app::scene::NATIVE_HINT_EAGER_RELEASE)
    {
      this->disposeHandle(handle);
      continue;
    }
    // Bag entries must hold zero pointers into Loka; a live back-pointer
    // means the teardown sequence did not complete. Leaking the handle
    // (counted) is the safe arm — disposing it would hand the live owner
    // a dead handle, and pooling it would pay the same handle out twice.
    if (handle->owner)
    {
      ++this->intakeCheckFailCount_;
      handle->leakedDeliberately = true;
      continue;
    }
    const bool accepted = this->bucketFor(entry.recipe).offer(handle);
    if (!accepted)
    {
      this->disposeHandle(handle);
    }
  }
  this->retired_.clear();
}

void NullScenePlatformController::drainBuckets()
{
  DisposePooledHandle dispose(this);
  this->buttonBucket_.drainWith(dispose);
  this->editTextBucket_.drainWith(dispose);
  this->scrollBarBucket_.drainWith(dispose);
}

void NullScenePlatformController::disposeHandle(FakeControlHandle *handle)
{
  if (!handle || handle->disposed)
  {
    return;
  }
  handle->owner = 0;
  handle->disposed = true;
  ++this->disposedCount_;
  this->appendEvent(EVENT_CONTROL_DISPOSED, handle->id);
}

void NullScenePlatformController::appendEvent(EventKind kind, int handleId)
{
  this->eventLog_.push_back(EventRecord(this->nextEventSequence_++, kind, handleId));
}

void NullScenePlatformController::recordWindowDisposed()
{
  this->appendEvent(EVENT_WINDOW_DISPOSED, 0);
}

void NullScenePlatformController::DisposePooledHandle::operator()(FakeControlHandle *handle) const
{
  if (this->controller_)
  {
    this->controller_->disposeHandle(handle);
  }
}

bool NullScenePlatformController::applyNativeFocus(loka::app::scene::NodeContext &ctx)
{
  using namespace loka::app;
  using namespace loka::app::scene;
  NodeContext *const identity = &ctx;
  Node *node = ctx.owner();
  if (!node || node->getContext() != identity || node->lifecycleFact() != NODE_FACT_ATTACHED) return false;
  FocusParticipant *row = FocusParticipant::from(node->asFocusParticipant());
  // This rail's projected tree owns the participant; never accept a foreign Scene.
  BoundaryNode *root = this->rootNode_ ? this->rootNode_->asBoundary() : 0;
  if (!row || !root || !root->getScene() || !row->belongsTo(root->getScene()->focus())) return false;
  row->connectSource(this->focusSource_);
  NodeContext *actual = 0;
  return this->readNativeFocus(actual) && actual == identity;
}

void NullScenePlatformController::requestRelayout()
{
  this->requestSceneRelayout(this->rootNode_);
}

bool NullScenePlatformController::handleEditCommand(loka::app::EditVerb verb)
{
  loka::app::scene::NodeContext *context = 0;
  if (!this->readNativeFocus(context) || !context || !context->owner())
    return false;
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlers_.find(context->owner());
  if (IsNullEditTextNodeHandler(handler))
    return static_cast<NullEditTextContext *>(context)->editCommands.record(verb);
  if (IsNullTextEditorNodeHandler(handler))
    return static_cast<NullTextEditorContext *>(context)->editCommands.record(verb);
  return false;
}
