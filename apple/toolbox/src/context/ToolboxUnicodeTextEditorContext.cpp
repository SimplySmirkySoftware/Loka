#include "ToolboxUnicodeTextEditorContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "ToolboxWindow.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxEditorFrame.hpp"
#include "ToolboxUnicodeTextEditorGeometry.hpp"
#include "unicode/ToolboxUnicodeEditorOptions.hpp"
#include "ToolboxPaintGround.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
using namespace loka::app;
namespace
{
  class Handler : public scene::RetainedNodeHandler<Handler, TextEditorNode, ToolboxUnicodeTextEditorContext>
  {
  public:
    static TextEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextEditorNode>() ? static_cast<TextEditorNode *>(node)
                                                                                   : 0;
    }
    static ToolboxUnicodeTextEditorContext *
    create(TextEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &)
    {
      return new ToolboxUnicodeTextEditorContext(
          node, static_cast<ToolboxScenePlatformController *>(controller), seamKey());
    }
  };
  Handler handler;
}
ToolboxUnicodeTextEditorContext::ToolboxUnicodeTextEditorContext(TextEditorNode *node,
                                                                 ToolboxScenePlatformController *controller,
                                                                 const scene::SeamKey<TextEditorNode> &key)
    : ToolboxProjectedNodeContext(controller),
      key_(key),
      node_(node),
      unicode_(0),
      rect_(),
      paintRect_()
{
}
ToolboxUnicodeTextEditorContext::~ToolboxUnicodeTextEditorContext()
{
  assert(!this->unicode_ && "detach must revoke the borrowed Unicode presentation before reclaim");
}

/** Staging refuses requests without projection, extraction, native input or publication. */
class ToolboxUnicodeTextEditorContext::RailOperation : public scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(ToolboxUnicodeTextEditorContext &context)
      : scene::RailOperation<LineCursor>(*context.controller(), context.node_, &context)
  {
  }
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<LineCursor> &request)
  {
    request = static_cast<TextEditorNode &>(base).props.moveCaretTo_;
    return request.isValid() && !request.state()->get().isNone() ? scene::ADMISSION_TAKE : scene::ADMISSION_EMPTY;
  }
  virtual bool current(scene::Node &base, const scene::RequestBinding<LineCursor> &request)
  {
    return static_cast<TextEditorNode &>(base).props.moveCaretTo_.same(request);
  }
  virtual EditorResult resolve(scene::Node &, const scene::RequestBinding<LineCursor> &)
  {
    return EDITOR_UNAVAILABLE;
  }
  virtual EditorResult validate(scene::Node &base, const LineCursor &)
  {
    ToolboxUnicodeTextEditorContext &context = *static_cast<ToolboxUnicodeTextEditorContext *>(base.getContext());
    return static_cast<TextEditorNode &>(base).seam(context.key_).availability();
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &, const LineCursor &pending)
  {
    return scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE);
  }
  virtual EditorResult report(scene::Node &, const LineCursor &)
  {
    return EDITOR_UNAVAILABLE;
  }
  virtual scene::FollowUp finishTake(scene::Node &,
                                     const scene::Reply<LineCursor> &,
                                     const scene::RequestApplication<LineCursor, EditorResult> &)
  {
    return scene::FOLLOW_NONE;
  }
  virtual scene::FollowUpResult finishSettle(scene::Node &, const scene::FollowUps &)
  {
    return scene::FOLLOW_UP_NONE;
  }
#ifdef TEST_BUILD
  virtual LineCursor fact(scene::Node &base) const
  {
    loka::core::State<LineCursor> *state = static_cast<TextEditorNode &>(base).props.cursorState();
    return state ? state->get() : LineCursor::None();
  }
#endif
};
class ToolboxUnicodeTextEditorContext::CommandOperation : public scene::SeatOperation<EditorCommand, LineCursor>
{
public:
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<EditorCommand> &request)
  {
    request = static_cast<TextEditorNode &>(base).props.command_;
    return request.isValid() && !request.state()->get().isNone() ? scene::ADMISSION_TAKE : scene::ADMISSION_EMPTY;
  }
  virtual bool current(scene::Node &base, const scene::RequestBinding<EditorCommand> &request)
  {
    return static_cast<TextEditorNode &>(base).props.command_.same(request);
  }
  virtual EditorResult resolve(scene::Node &, const scene::RequestBinding<EditorCommand> &)
  {
    return EDITOR_UNAVAILABLE;
  }
  virtual EditorResult validate(scene::Node &, const EditorCommand &)
  {
    return EDITOR_UNAVAILABLE;
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &, const EditorCommand &)
  {
    return scene::RequestApplication<LineCursor, EditorResult>(LineCursor::None(), EDITOR_UNAVAILABLE);
  }
  virtual EditorResult report(scene::Node &, const LineCursor &)
  {
    return EDITOR_UNAVAILABLE;
  }
  virtual scene::FollowUp finishTake(scene::Node &,
                                     const scene::Reply<EditorCommand> &,
                                     const scene::RequestApplication<LineCursor, EditorResult> &)
  {
    return scene::FOLLOW_NONE;
  }
};
void ToolboxUnicodeTextEditorContext::settle(scene::Settlement stimulus, RailOperation &operation)
{
  CommandOperation command;
  scene::RequestSettlement<LineCursor>::settle(operation,
                                               operation,
                                               command,
                                               stimulus
#ifdef TEST_BUILD
                                               ,
                                               this->node_ ? operation.fact(*this->node_) : LineCursor::None()
#endif
  );
}
EditorResult ToolboxUnicodeTextEditorContext::beginInput()
{
  return EDITOR_UNAVAILABLE;
}
EditorResult ToolboxUnicodeTextEditorContext::refuseInput()
{
  if (!this->controller() || !this->node_)
    return EDITOR_UNAVAILABLE;
  RailOperation operation(*this);
  const EditorResult result = this->beginInput();
  this->settle(scene::SETTLE_INPUT, operation);
  return result;
}
EditorResult ToolboxUnicodeTextEditorContext::key(char)
{
  return this->refuseInput();
}
EditorResult ToolboxUnicodeTextEditorContext::paste(const char *, std::size_t)
{
  return this->refuseInput();
}
EditorResult ToolboxUnicodeTextEditorContext::scrollKey(ToolboxScrollKey)
{
  return this->refuseInput();
}
EditorResult ToolboxUnicodeTextEditorContext::click(const Point &, const EventRecord &)
{
  return this->refuseInput();
}
bool ToolboxUnicodeTextEditorContext::offerKeyEvent(const EventRecord &)
{
  this->refuseInput();
  return true;
}
bool ToolboxUnicodeTextEditorContext::editVerb(EditVerb)
{
  this->refuseInput();
  return true;
}
void ToolboxUnicodeTextEditorContext::onPropsApplied()
{
  if (this->controller() && this->node_)
  {
    RailOperation operation(*this);
    this->settle(scene::SETTLE_PROPS, operation);
  }
}
void ToolboxUnicodeTextEditorContext::onFactChanged(scene::NodeLifecycleFact previous, scene::NodeLifecycleFact next)
{
  if (next != scene::NODE_FACT_ATTACHED && this->controller())
  {
    ToolboxInputDoor::releaseNativeFocus(*this->controller(), this);
    this->controller()->retireTextEditorControl(this, this->lifetimeHint());
  }
  if (next == scene::NODE_FACT_RETIRED)
    this->node_ = 0;
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}
void ToolboxUnicodeTextEditorContext::invalidateNativePresentation()
{
  this->unicode_ = 0;
}
void ToolboxUnicodeTextEditorContext::retryProjection() {}
void ToolboxUnicodeTextEditorContext::idleTick()
{
  if (this->unicode_)
    this->controller()->unicodeEditors_.idle(this->unicode_);
}
Rect ToolboxUnicodeTextEditorContext::chromeRect() const
{
  Rect port = Rect(), sizeBox = Rect();
  ToolboxWindow *window = this->controller() ? this->controller()->window_ : 0;
  const bool hasPort = window && window->queryPortRect(port);
  if (window)
    window->querySizeBoxRect(sizeBox);
  return ToolboxUnicodeTextEditorFrame(this->rect_, port, hasPort, sizeBox);
}
void ToolboxUnicodeTextEditorContext::updateRect(const Rect &rect)
{
  const Rect oldPaint = this->paintRect_;
  this->rect_ = rect;
  const Rect frame = this->chromeRect();
  this->paintRect_ = frame;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(frame, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (!EqualRect(&oldPaint, &this->paintRect_) && this->controller() && this->controller()->window_)
  {
    this->controller()->window_->requestInvalidateRect(oldPaint);
    this->controller()->window_->requestInvalidateRect(this->paintRect_);
  }
}
short ToolboxUnicodeTextEditorContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  const Rect rect = ToolboxEditorFrame(state.x, state.y, state.width, state.height > 0 ? state.height : 80);
  this->updateRect(rect);
  state.y = rect.bottom;
  return state.width;
}
scene::PaintAnswer ToolboxUnicodeTextEditorContext::queryPaintDamage(const scene::PaintQuery &query) const
{
  if (query.placement != scene::PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return scene::PaintAnswer::nativeScheduled();
}
void ToolboxUnicodeTextEditorContext::repaintNative()
{
  if (EmptyRect(&this->paintRect_))
    return;
  if (this->unicode_)
  {
    this->controller()->unicodeEditors_.draw(this->unicode_);
    return;
  }
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const Rect frame = this->chromeRect();
  loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_DOCUMENT, frame);
  FrameRect(&frame);
  MoveTo(this->rect_.left + 3, this->rect_.top + 14);
  const char label[] = "Editor unavailable";
  DrawText(label, 0, sizeof(label) - 1);
}
void ToolboxUnicodeTextEditorContext::render(scene::IPlatformController *)
{
  if (!this->controller() || !this->node_)
    return;
  RailOperation operation(*this);
  ToolboxUnicodeEditorResource *resource =
      this->controller()->ensureUnicodeTextEditorControl(this, this->rect_, this->lifetimeHint());
  if (resource)
    this->controller()->reachEditControl(this);
  const bool changed = resource != this->unicode_;
  if (!resource)
  {
    this->invalidateNativePresentation();
    ToolboxInputDoor::releaseNativeFocus(*this->controller(), this);
  }
  this->unicode_ = resource;
  if (changed || !resource)
    this->settle(scene::SETTLE_ATTACH, operation);
  if (operation.hasSameContext())
    this->repaintNative();
}
bool RegisterToolboxUnicodeTextEditorNodeHandler(scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&handler);
}
#include "ToolboxUnicodeTextEditorBinding.cpp"
#else
ToolboxUnicodeEditorResource *ToolboxScenePlatformController::ensureUnicodeTextEditorControl(
    ToolboxUnicodeTextEditorContext *, const Rect &, loka::app::scene::NativeLifetimeHint)
{
  return 0;
}
#endif
