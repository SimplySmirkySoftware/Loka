#include "WebTextDescriptor.hpp"
#include "WebScenePlatformController.hpp"
#include "WebSession.hpp"
#include "WebTestHooks.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/layout/TextShaping.hpp"
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
  const loka::app::layout::RailSpacing Spacing(12, 4, 9);
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
  Handler<TextNode, WebTextContext, loka::web::TEXT> TextHandler;
  Handler<ButtonNode, WebButtonContext, loka::web::BUTTON> ButtonHandler;
  RefusedNodeHandler Refused[] = {RefusedNodeHandler(NodeTypeToken<EditTextNode>()),
                                  RefusedNodeHandler(NodeTypeToken<TextEditorNode>()),
                                  RefusedNodeHandler(NodeTypeToken<PopupMenuNode>()),
                                  RefusedNodeHandler(NodeTypeToken<ImageViewNode>()),
                                  RefusedNodeHandler(NodeTypeToken<RectSurfaceNode>()),
                                  RefusedNodeHandler(NodeTypeToken<ScrollBarNode>()),
                                  RefusedNodeHandler(NodeTypeToken<CellNode>()),
                                  RefusedNodeHandler(NodeTypeToken<CardBandNode>())};
  short Narrow(int n)
  {
    return static_cast<short>(n < SHRT_MIN ? SHRT_MIN : n > SHRT_MAX ? SHRT_MAX : n);
  }
  /** Unsupported ScrollView still settles one visited request; never publishes a viewport. */
  class RefusedScrollSeat : public ScrollViewSeat
  {
  public:
    RefusedScrollSeat()
        : ScrollViewSeat(ScrollViewVisit::refused())
    {
    }
    virtual RequestApplication<loka::core::Frame, ScrollRequestResult> apply(Node &, const ScrollTarget &)
    {
      return RequestApplication<loka::core::Frame, ScrollRequestResult>(loka::core::Frame(), SCROLL_UNAVAILABLE);
    }
  };
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
      retired_(0)
{
  this->layout_.width = Narrow(width);
  this->layout_.height = Narrow(height);
  this->layout_.lineHeight = 16;
  this->layout_.spacing = 4;
  loka::app::layout::RegisterBuiltinPlatformLayoutHandlers(this->layouts_, 0, 0, Spacing);
}
WebScenePlatformController::~WebScenePlatformController()
{
  this->destroy();
}
void WebScenePlatformController::onChange(Node *root, NodeDirtyFlags, bool)
{
  this->tree_ = root;
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
bool WebScenePlatformController::materialize(WebElement &element, loka::web::Kind kind, WebElementContext &receiver)
{
  int width = 0, height = 0;
  if (!element.create(this->session_.routes(), this->session_.tokens(), kind, this->root_, width, height))
    return false;
  this->session_.routes().find(element.token())->leaf = &receiver;
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
  out = row && row->leaf && &row->leaf->controller() == this ? row->leaf : 0;
  return true;
}
namespace
{
  /** Cost: layout/query once per leaf; snapshots only the leaf's own current props. */
  struct TextInput
  {
    explicit TextInput(Node *node)
        : button(node && node->asButtonNode()),
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
      if (state)
        text = state->get();
    }
    const bool button;
    TextStyle style;
    BlockStyle block;
    loka::core::String text;
  };
} // namespace
bool WebScenePlatformController::queryNaturalWidth(Node *node, short &out) const
{
  if (!node || (!node->asTextNode() && !node->asButtonNode()))
    return false;
  const TextInput input(node);
  return WebTextLayout::natural(
      input.text, WebTextConstraint(WebFontKey(input.style), input.block, 0), input.button, false, out);
}
bool WebScenePlatformController::queryNaturalHeight(Node *node, short &out) const
{
  if (!node || (!node->asTextNode() && !node->asButtonNode()))
    return false;
  const TextInput input(node);
  return WebTextLayout::natural(
      input.text, WebTextConstraint(WebFontKey(input.style), input.block, 0), input.button, true, out);
}
int WebScenePlatformController::layoutNode(Node *node, const LayoutState &received)
{
  if (!node || node->lifecycleFact() != NODE_FACT_ATTACHED)
    return received.y;
  LayoutState state = received;
  state.spacing = Narrow(loka::app::layout::resolveLayoutGap(node, received.spacing, Spacing));
  if (node->asScrollViewNode())
  {
    ScrollViewVisitOwner owner(*this, node);
    RefusedScrollSeat seat;
    RequestSettlement<loka::core::Frame>::settle(owner,
                                                 seat,
                                                 SETTLE_PROPS
#ifdef TEST_BUILD
                                                 ,
                                                 owner.fact(*node)
#endif
    );
    return state.y;
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
    state.y = Narrow(state.y + gaps.before(child));
    state.y = Narrow(this->layoutNode(child, state));
  }
  return state.y;
}
WebElementContext::WebElementContext(Node &node, WebScenePlatformController &controller)
    : controller_(controller)
{
  this->setOwner(&node);
}
WebElementContext::~WebElementContext()
{
  assert(!this->element_.token());
}
bool WebElementContext::materialize(loka::web::Kind kind)
{
  return this->controller_.materialize(this->element_, kind, *this);
}
void WebElementContext::readLifecycleFactOnAttach()
{
  web_set_hidden(this->element_.token(), this->owner()->lifecycleFact() != NODE_FACT_ATTACHED);
}
void WebElementContext::onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
{
  if (next == NODE_FACT_RETIRED)
  {
    this->projection_.clear();
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
  const ButtonNode *button = this->owner()->asButtonNode();
  return button && (!button->props.enabled_ || button->props.enabled_->get());
}
short WebElementContext::layout(IPlatformController *, LayoutState &state)
{
  const TextInput input(this->owner());
  const WebTextConstraint constraint(WebFontKey(input.style), input.block, state.width);
  bool reusable = false;
  int status = WEB_MEASURE_OK;
  if (this->projection_.get() && this->projection_.get()->constraint() == constraint)
    status = this->projection_.get()->matches(input.text, input.button, reusable);
  if (status == WEB_MEASURE_OK && !reusable)
  {
    WebTextSnapshot *built = 0;
    status = WebTextLayout::build(input.text, constraint, input.button, built);
    if (status == WEB_MEASURE_OK)
      this->projection_.replace(built);
  }
  if (status == WEB_MEASURE_REFUSED)
    this->controller_.refuseTextMeasurement(this->owner(), state);
  int height = this->projection_.get() ? this->projection_.get()->extent().height : 0;
  if (input.button && this->projection_.get())
    height += 2 * (kWebButtonInsetY + kWebButtonBorder);
  state.height = Narrow(height);
  web_set_rect(this->element_.token(), state.x, state.y, state.width, state.height);
  return Narrow(state.y + state.height);
}
void WebElementContext::render(IPlatformController *)
{
  const TextInput input(this->owner());
  if (ButtonNode *button = this->owner()->asButtonNode())
    web_set_enabled(this->element_.token(), !button->props.enabled_ || button->props.enabled_->get());
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
bool WebElementContext::acceptsInstallation() const
{
#ifdef TEST_BUILD
  return !loka::web::testing::refuseInstallation();
#else
  return true;
#endif
}
bool WebButtonContext::click()
{
#ifdef TEST_BUILD
  loka::web::testing::beforeClick(*this);
#endif
  if (!this->active())
    return false;
  ButtonNode *node = this->owner()->asButtonNode();
  if (node->props.onClick_)
  {
    node->props.onClick_->emit();
    return true;
  }
  return false;
}
