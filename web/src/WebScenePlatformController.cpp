#include <algorithm>
#include "WebWindow.hpp"
#include "WebStackLayoutHandler.hpp"
#include "WebGridLayoutHandler.hpp"
#include "WebScrollContext.hpp"
#include "WebTextDescriptor.hpp"
#include "WebScenePlatformController.hpp"
#include "WebSession.hpp"
#include "WebTestHooks.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "platform/StringUTF8.hpp"
#include <climits>
#include <new>
using namespace loka::app;
using namespace loka::app::scene;
namespace
{
  template <class N, class C, loka::web::Kind K> class Handler : public RetainedNodeHandler<Handler<N, C, K>, N, C>
  {
  public:
    static N *cast(Node *node)
    {
      return node && node->nodeTypeKey() == NodeTypeToken<N>() ? static_cast<N *>(node) : 0;
    }
    static C *create(N *node, IPlatformController *controller, const LayoutState &)
    {
      C *ctx = new (std::nothrow) C(*node, *static_cast<WebScenePlatformController *>(controller));
      if (ctx && !ctx->materialize(K))
      {
        delete ctx;
        return 0;
      }
      return ctx;
    }
  };
  Handler<TextNode, WebElementContext, loka::web::TEXT> TextHandler;
  Handler<ButtonNode, WebElementContext, loka::web::BUTTON> ButtonHandler;
  Handler<CellNode, WebElementContext, loka::web::BUTTON> CellHandler;
  Handler<ScrollViewNode, WebScrollContext, loka::web::SCROLL> ScrollHandler;
  RefusedNodeHandler Refused[] = {RefusedNodeHandler(NodeTypeToken<EditTextNode>()),
                                  RefusedNodeHandler(NodeTypeToken<TextEditorNode>()),
                                  RefusedNodeHandler(NodeTypeToken<PopupMenuNode>()),
                                  RefusedNodeHandler(NodeTypeToken<ImageViewNode>()),
                                  RefusedNodeHandler(NodeTypeToken<RectSurfaceNode>()),
                                  RefusedNodeHandler(NodeTypeToken<ScrollBarNode>()),
                                  RefusedNodeHandler(NodeTypeToken<CardBandNode>())};
  short Narrow(int n)
  {
    return static_cast<short>(n < SHRT_MIN ? SHRT_MIN : n > SHRT_MAX ? SHRT_MAX : n);
  }
} // namespace
class WebScenePlatformController::Traversal : public IPlatformLayoutTraversal
{
public:
  explicit Traversal(WebScenePlatformController &c)
      : controller_(c),
        bottom_(0)
  {
  }
  virtual bool queryNaturalWidth(Node *node, short &out) const
  {
    return this->controller_.queryNaturalWidth(node, out);
  }
  virtual bool queryNaturalHeight(Node *node, short &out) const
  {
    return this->controller_.queryNaturalHeight(node, out);
  }
  virtual int layoutChild(Node *node, const LayoutState &state)
  {
    return this->controller_.layoutNode(node, state);
  }
  virtual void setLayoutResultY(short y)
  {
    this->bottom_ = y;
  }
  virtual bool refuseLayoutResultY(int y)
  {
    if (y >= SHRT_MIN && y <= SHRT_MAX)
      return false;
    this->controller_.refuseMeasurement();
    return true;
  }
  virtual short layoutResultY() const
  {
    return this->bottom_;
  }

private:
  WebScenePlatformController &controller_;
  short bottom_;
};
WebScenePlatformController::WebScenePlatformController(WebSession &session,
                                                       loka::web::Token root,
                                                       int width,
                                                       int height)
    : session_(session),
      root_(root),
      tree_(0),
      layout_(),
      retired_(0),
      projectionParents_(reinterpret_cast<void *>(static_cast<uintptr_t>(root)))
{
  this->layout_.width = Narrow(width);
  this->layout_.height = Narrow(height);
  this->layout_.lineHeight = 16;
  this->layout_.spacing = 4;
  loka::app::layout::RegisterBuiltinPlatformLayoutHandlers(this->layouts_, 0, 0, WebLayoutSpacing);
  this->layouts_.registerHandler(new WebStackLayoutHandler(*this));
  this->layouts_.registerHandler(new WebGridLayoutHandler());
}
WebScenePlatformController::~WebScenePlatformController()
{
  this->destroy();
}
void WebScenePlatformController::onChange(Node *root, NodeDirtyFlags, bool)
{
  this->tree_ = root;
  this->projectionParents_.resetRoot(reinterpret_cast<void *>(static_cast<uintptr_t>(this->root_)),
                                     loka::core::Frame());
  this->layoutNode(root, this->layout_);
}
void WebScenePlatformController::synchronize() {}
bool WebScenePlatformController::hasPendingSync() const
{
  return this->retired_ != 0;
}
void WebScenePlatformController::destroy()
{
  if (this->tree_)
    this->releaseNodeContexts(this->tree_);
  this->tree_ = 0;
  this->drainNativeRetirements();
}
void WebScenePlatformController::drainNativeRetirements()
{
  assert(!this->borrowPhase().open());
  while (this->retired_)
  {
    WebResource *row = this->retired_;
    this->retired_ = row->next;
    const int result = web_release(row->token);
    assert(result == loka::web::CONSUMED);
    (void)result;
    delete row;
  }
}
void WebScenePlatformController::retire(WebElement &element)
{
  WebResource *row = element.revoke(this->session_.routes());
  if (row)
  {
    row->next = this->retired_;
    this->retired_ = row;
  }
}
bool WebScenePlatformController::materialize(WebElement &element, loka::web::Kind kind, WebResourceContext &receiver)
{
  int width = 0, height = 0;
  if (!element.create(
          this->session_.routes(),
          this->session_.tokens(),
          kind,
          static_cast<loka::web::Token>(reinterpret_cast<uintptr_t>(this->projectionParents_.current().nativeParent)),
          width,
          height))
    return false;
  this->session_.routes().find(element.token())->receiver = &receiver;
  return true;
}
bool WebScenePlatformController::prepareProjectedLayout(Node *node, LayoutState &state)
{
  if (!node)
    return false;
  if (node->asTextNode())
    return TextHandler.ensureContext(node, this, state) != 0;
  if (node->asButtonNode())
    return ButtonHandler.ensureContext(node, this, state) != 0;
  if (node->asCellNode())
    return CellHandler.ensureContext(node, this, state) != 0;
  for (unsigned i = 0; i < sizeof(Refused) / sizeof(Refused[0]); ++i)
    if (Refused[i].nodeTypeKey() == node->nodeTypeKey())
    {
      Refused[i].ensureContext(node, this, state);
      return false;
    }
  return false;
}
void WebScenePlatformController::requestRelayout()
{
  this->requestSceneRelayout(this->tree_);
}
void WebScenePlatformController::resize(int width, int height)
{
  this->layout_.width = Narrow(width);
  this->layout_.height = Narrow(height);
  this->requestRelayout();
}
bool WebScenePlatformController::readNativeFocus(NodeContext *&out)
{
  const loka::web::Token token = web_read_native_focus(this->root_);
  WebResource *row = this->session_.routes().find(token);
  out = row && row->leaf() && &row->leaf()->controller() == this ? row->leaf() : 0;
  return true;
}
namespace
{
  /** Cost: layout/query once per leaf; snapshots only the leaf's own current props. */
  struct TextInput
  {
    explicit TextInput(Node *node)
        : singleLine(node && (node->asButtonNode() || node->asCellNode())),
          style(),
          block(),
          text()
    {
      loka::core::State<loka::core::String> *state = 0;
      if (TextNode *t = node ? node->asTextNode() : 0)
      {
        state = t->props.text_;
        style = t->props.resolvedTextStyle();
        block = t->props.blockStyle_;
      }
      else if (ButtonNode *b = node ? node->asButtonNode() : 0)
        state = b->props.text_;
      else if (CellNode *c = node ? node->asCellNode() : 0)
      {
        state = c->props.text_;
        block = BlockStyle().align(TEXT_ALIGN_CENTER);
      }
      if (state)
        text = state->get();
    }
    const bool singleLine;
    TextStyle style;
    BlockStyle block;
    loka::core::String text;
  };
  /** Cost: leaf layout/render/query once; reads only this node's props, no retained borrow. */
  TextInput ReadLeafText(Node &node)
  {
    return TextInput(&node);
  }
  /** Cost: ingress/dispatch/render once each; O(1) typed policy, no rows. */
  bool LeafEnabled(Node &node)
  {
    if (ButtonNode *button = node.asButtonNode())
      return !button->props.enabled_ || button->props.enabled_->get();
    return node.asCellNode() != 0;
  }
  /** Cost: admitted dispatch once; borrows only the current node's emitter until emission. */
  loka::core::EmitterState *ClickEmitter(Node &node)
  {
    if (ButtonNode *button = node.asButtonNode())
      return button->props.onClick_;
    if (CellNode *cell = node.asCellNode())
      return cell->props.onClick_;
    return 0;
  }
  /** Cost: leaf layout once; O(1) own offer/snapshot, before coordinate checks. */
  int LeafHeight(Node &node, int offeredHeight, const WebTextSnapshot *snapshot)
  {
    if (node.asCellNode())
      return offeredHeight > 0 ? offeredHeight : loka::app::layout::FallbackControlMetrics::kCellHeight;
    const int measured = snapshot ? snapshot->extent().height : 0;
    return measured + (node.asButtonNode() && snapshot ? 2 * (kWebButtonInsetY + kWebButtonBorder) : 0);
  }
} // namespace
bool WebScenePlatformController::queryNaturalWidth(Node *node, short &out) const
{
  if (!node || (!node->asTextNode() && !node->asButtonNode()))
    return false;
  const TextInput input = ReadLeafText(*node);
  return WebTextLayout::natural(
      input.text, WebTextConstraint(WebFontKey(input.style), input.block, 0), node->asButtonNode() != 0, false, out);
}
bool WebScenePlatformController::queryNaturalHeight(Node *node, short &out) const
{
  if (!node || (!node->asTextNode() && !node->asButtonNode()))
    return false;
  const TextInput input = ReadLeafText(*node);
  return WebTextLayout::natural(
      input.text, WebTextConstraint(WebFontKey(input.style), input.block, 0), node->asButtonNode() != 0, true, out);
}
int WebScenePlatformController::layoutNode(Node *node, const LayoutState &received)
{
  if (this->projectionParents_.current().hasShortRangeRefusal())
    return received.y;
  if (!node || node->lifecycleFact() != NODE_FACT_ATTACHED)
    return received.y;
  LayoutState state = received;
  state.spacing = Narrow(loka::app::layout::resolveLayoutGap(node, received.spacing, WebLayoutSpacing));
  if (node->asScrollViewNode())
  {
    WebResource *root = this->session_.routes().find(this->root_);
    if (!root || !root->root || !root->root->rootShown())
      return state.y + state.height;
    return SettleScrollViewVisit(*this,
                                 &WebScenePlatformController::layoutScroll,
                                 &WebScenePlatformController::applyScroll,
                                 node->asScrollViewNode(),
                                 state)
        .bottom;
  }
  if (IPlatformLayoutHandler *handler = this->layouts_.find(node))
  {
    Traversal traversal(*this);
    return handler->layoutNode(node, state, &traversal);
  }
  if (IProjectedLayoutNode *projected = node->asProjectedLayoutNode())
  {
    const int result = projected->layoutProjected(this, state);
    node->render(this);
    return result;
  }
  INestable *nestable = node->asNestable();
  if (!nestable)
    return state.y;
  loka::app::layout::StackGaps gaps(state.spacing);
  for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
  {
    const int start = state.y + gaps.before(child);
    if (!this->checkCoordinate(start))
      break;
    state.y = Narrow(start);
    const int bottom = this->layoutNode(child, state);
    if (this->projectionParents_.current().hasShortRangeRefusal() || !this->checkCoordinate(bottom))
      break;
    state.y = Narrow(bottom);
  }
  return state.y;
}
WebResourceContext::WebResourceContext(Node &node, WebScenePlatformController &controller)
    : controller_(controller)
{
  this->setOwner(&node);
}
WebResourceContext::~WebResourceContext()
{
  assert(!this->element_.token());
}
bool WebResourceContext::materialize(loka::web::Kind kind)
{
  return this->controller_.materialize(this->element_, kind, *this);
}
void WebResourceContext::readLifecycleFactOnAttach()
{
  web_set_hidden(this->element_.token(), this->owner()->lifecycleFact() != NODE_FACT_ATTACHED);
}
void WebResourceContext::onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
{
  if (next == NODE_FACT_RETIRED)
  {
    this->controller_.retire(this->element_);
  }
  else
    web_set_hidden(this->element_.token(), next != NODE_FACT_ATTACHED);
}
bool WebScenePlatformController::acceptsInput() const
{
  return this->session_.acceptsRoot(this->root_);
}
bool WebElementContext::active() const
{
  if (!this->controller_.acceptsInput())
    return false;
  if (!this->element_.token() || !this->owner() || this->owner()->lifecycleFact() != NODE_FACT_ATTACHED)
    return false;
  return LeafEnabled(*this->owner());
}
short WebElementContext::layout(IPlatformController *, LayoutState &state)
{
  const TextInput input = ReadLeafText(*this->owner());
  const WebTextConstraint constraint(WebFontKey(input.style), input.block, state.width);
  bool reusable = false;
  int status = WEB_MEASURE_OK;
  if (this->projection_.get() && this->projection_.get()->constraint() == constraint)
    status = this->projection_.get()->matches(input.text, input.singleLine, reusable);
  if (status == WEB_MEASURE_OK && !reusable)
  {
    WebTextSnapshot *built = 0;
    status = WebTextLayout::build(input.text, constraint, input.singleLine, built);
    if (status == WEB_MEASURE_OK)
      this->projection_.replace(built);
  }
  if (status == WEB_MEASURE_REFUSED)
  {
    this->controller_.refuseTextMeasurement(this->owner(), state);
    this->controller_.refuseMeasurement();
  }
  const int height = LeafHeight(*this->owner(), state.height, this->projection_.get());
  if (!this->controller_.checkCoordinate(height) || !this->controller_.checkCoordinate(state.y + height))
    return state.y;
  state.height = Narrow(height);
  LayoutState projected;
  if (this->controller_.project(state, projected))
    web_set_rect(this->element_.token(), projected.x, projected.y, projected.width, projected.height);
  return Narrow(state.y + state.height);
}
void WebElementContext::render(IPlatformController *)
{
  const TextInput input = ReadLeafText(*this->owner());
  if (this->owner()->asButtonNode() || this->owner()->asCellNode())
    web_set_enabled(this->element_.token(), LeafEnabled(*this->owner()));
  const WebTextSnapshot *snapshot = this->projection_.get();
  if (!snapshot)
  {
    web_set_projected(this->element_.token(), 0);
    web_set_text(this->element_.token(), "", 0);
    return;
  }
  // Unconditional projection order: projected, style, line-height, block, text.
  web_set_projected(this->element_.token(), 1);
  const WebTextDescriptor descriptor = WebMetricDescriptor(snapshot->constraint().fontKey, input.style);
  if (!web_set_text_style(this->element_.token(), descriptor.data(), descriptor.count()))
  {
    assert(false && "Web text protocol refusal");
    return;
  }
  web_set_line_height(this->element_.token(), snapshot->metrics().ascent + snapshot->metrics().descent);
  const BlockStyle &block = snapshot->constraint().block;
  web_set_block(this->element_.token(),
                block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT,
                block.hasTruncation_ ? block.truncation_ : TEXT_TRUNCATION_NONE);
  web_set_text(this->element_.token(), snapshot->joined(), snapshot->joinedLength());
}
bool WebResourceContext::acceptsInstallation() const
{
#ifdef TEST_BUILD
  return !loka::web::testing::refuseInstallation();
#else
  return true;
#endif
}
bool WebElementContext::click()
{
#ifdef TEST_BUILD
  loka::web::testing::beforeClick(*this);
#endif
  if (!this->active())
    return false;
  loka::core::EmitterState *emitter = ClickEmitter(*this->owner());
  if (emitter)
  {
    emitter->emit();
    return true;
  }
  return false;
}

void WebElementContext::onFactChanged(NodeLifecycleFact old, NodeLifecycleFact next)
{
  if (next == NODE_FACT_RETIRED)
    this->projection_.clear();
  WebResourceContext::onFactChanged(old, next);
}
bool WebResourceContext::attached() const
{
  return this->element_.token() && this->owner() && this->owner()->lifecycleFact() == NODE_FACT_ATTACHED;
}

bool WebScenePlatformController::checkCoordinate(int value)
{
  if (!this->projectionParents_.insideViewport() || (value >= SHRT_MIN && value <= SHRT_MAX))
    return true;
  this->refuseMeasurement();
  return false;
}
void WebScenePlatformController::refuseMeasurement()
{
  this->projectionParents_.current().markShortRangeRefused();
}
bool WebScenePlatformController::project(const LayoutState &state, LayoutState &projected)
{
  if (this->projectionParents_.current().project(state, projected))
    return true;
  this->refuseMeasurement();
  return false;
}
WebScenePlatformController::ScrollResult WebScenePlatformController::layoutScroll(ScrollViewNode *node,
                                                                                  const LayoutState &state)
{
  const ScrollResult refused(state.y + state.height, ScrollViewVisit::refused());
  if (this->projectionParents_.insideViewport() || this->projectionParents_.insideUnit())
    return refused;
  const int top = node->props.contentInsets_.top();
  if (state.width < 0 || state.height < 0 || state.y + state.height > SHRT_MAX || state.y - top < SHRT_MIN)
    return refused;
  WebScrollContext *ctx = static_cast<WebScrollContext *>(ScrollHandler.ensureContext(node, this, state));
  if (!ctx)
    return refused;
  short knownWidth = 0;
  const bool known = loka::app::layout::queryContentOverflowWidth(node, WebLayoutSpacing, this, knownWidth);
  const int knownOverflowWidth = known ? knownWidth : 0;
  const WebScrollbarGeometry &gutter = this->session_.scrollbarGeometry();
  const int viewportWidth = std::max(0, state.width - gutter.vertical());
  const int documentWidth = std::max(viewportWidth, knownOverflowWidth);
  const int maximumOffsetX = std::max(0, knownOverflowWidth - viewportWidth);
  const bool horizontal = maximumOffsetX > 0 && state.height > 0;
  const int viewportHeight = std::max(0, state.height - (horizontal ? gutter.horizontal() : 0));
  LayoutState childState = state;
  childState.width = static_cast<short>(viewportWidth);
  childState.height = static_cast<short>(viewportHeight);
  ProjectionParentScope scope;
  if (!this->projectionParents_.current().deriveScrolled(reinterpret_cast<void *>(static_cast<uintptr_t>(ctx->token())),
                                                         state.x,
                                                         state.y - top,
                                                         loka::core::Frame(0, 0, viewportWidth, viewportHeight),
                                                         scope))
    return refused;
  int extent = 0;
  {
    ProjectionViewportScopeGuard guard(this->projectionParents_, scope);
    if (!guard.isActive())
      return refused;
    int currentY = state.y;
    loka::app::layout::StackGaps gaps(state.spacing);
    for (Node *child = node->childrenHead(); child; child = child->nextInComposition)
    {
      const int start = currentY + gaps.before(child);
      if (!this->checkCoordinate(start))
        break;
      childState.y = static_cast<short>(start);
      const int bottom = this->layoutNode(child, childState);
      if (this->projectionParents_.current().hasShortRangeRefusal())
        break;
      if (!this->checkCoordinate(bottom)
          || !this->projectionParents_.current().tryAccumulateContentHeight(currentY, bottom))
      {
        this->refuseMeasurement();
        break;
      }
      currentY = bottom;
    }
    extent = this->projectionParents_.current().contentHeight() + node->props.contentInsets_.extentExtra();
    if (!this->checkCoordinate(extent) || (state.height == 0 && !this->checkCoordinate(state.y + extent))
        || this->projectionParents_.current().hasShortRangeRefusal())
      return refused;
  }
  const ScrollViewVisit axes = ScrollViewVisit::succeededWithNativeAxes(horizontal, true);
  WebScrollMetrics *metrics =
      new (std::nothrow) WebScrollMetrics(state, documentWidth, extent, viewportWidth, viewportHeight, axes);
  const bool applied = ctx->commit(metrics);
  return ScrollResult(state.y + (state.height ? state.height : extent), applied ? axes : ScrollViewVisit::refused());
}
bool WebScenePlatformController::applyScroll(ScrollViewNode &node,
                                             const ScrollResult &,
                                             const loka::core::Frame &target)
{
  WebScrollContext *ctx = static_cast<WebScrollContext *>(node.getContext());
  return ctx && ctx->apply(target);
}
