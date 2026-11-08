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
/** PR 1 uses a fixed metric table: 8 CSS px per scalar, Text height 16,
    Button height 24 and horizontal inset 16. The INaturalSizeSource seam is
    real; page 3 owns native font metrics and wrapping. Cost: one own text run
    per natural-size query, O(scalars); no foreign rows. */
bool WebScenePlatformController::queryNaturalWidth(Node *node, short &out) const
{
  loka::core::State<loka::core::String> *state = 0;
  int inset = 0;
  if (TextNode *text = node ? node->asTextNode() : 0)
    state = text->props.text_;
  else if (ButtonNode *button = node ? node->asButtonNode() : 0)
  {
    state = button->props.text_;
    inset = 16;
  }
  else
    return false;
  if (!state)
    return false;
  loka::core::StringBuffer buffer(loka::core::StringEncodingUtf32);
  if (!state->get().assignToBuffer(buffer))
    return false;
  const std::size_t length = buffer.length();
  if (length > static_cast<std::size_t>((SHRT_MAX - inset) / 8))
    return false;
  if (length == 0 && inset == 0)
    return false;
  out = static_cast<short>(length * 8 + inset);
  return true;
}
bool WebScenePlatformController::queryNaturalHeight(Node *node, short &out) const
{
  if (node && node->asTextNode())
  {
    out = 16;
    return true;
  }
  if (node && node->asButtonNode())
  {
    out = 24;
    return true;
  }
  return false;
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
    this->controller_.retire(this->element_);
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
  short height = 0;
  if (!this->controller_.queryNaturalHeight(this->owner(), height))
  {
    this->controller_.refuseTextMeasurement(this->owner(), state);
    return state.y;
  }
  state.height = height;
  web_set_rect(this->element_.token(), state.x, state.y, state.width, height);
  return Narrow(state.y + height);
}
void WebElementContext::render(IPlatformController *)
{
  loka::core::State<loka::core::String> *text = 0;
  if (TextNode *node = this->owner()->asTextNode())
  {
    text = node->props.text_;
    const TextStyle style = node->props.resolvedTextStyle();
    int points = 0;
    TextSizeRole role = TEXT_SIZE_ROLE_SMALL_LABEL;
    const bool hasPoints = style.size_.queryPoints(points);
    const bool hasRole = style.size_.queryRole(role);
    TextWeight weight = TEXT_WEIGHT_NORMAL;
    bool italic = false, underline = false;
    const int descriptor[] = {1,
                              10,
                              hasPoints ? 1
                              : hasRole ? 2
                                        : 0,
                              hasPoints ? points
                              : hasRole ? static_cast<int>(role)
                                        : 0,
                              style.queryWeight(weight) ? static_cast<int>(weight) : -1,
                              style.queryItalic(italic) ? static_cast<int>(italic) : -1,
                              static_cast<int>(style.color_),
                              static_cast<int>(style.family_.family()),
                              style.queryUnderline(underline) ? static_cast<int>(underline) : -1,
                              16};
    web_set_text_style(this->element_.token(), descriptor, 10);
  }
  else if (ButtonNode *button = this->owner()->asButtonNode())
  {
    text = button->props.text_;
    web_set_enabled(this->element_.token(), !button->props.enabled_ || button->props.enabled_->get());
  }
  std::string bytes;
  if (text && loka::platform::CollectUtf8(text->get(), bytes))
    web_set_text(this->element_.token(), bytes.data(), static_cast<unsigned>(bytes.size()));
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
