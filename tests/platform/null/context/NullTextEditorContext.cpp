#include "app/nodes/controls/TextEditorRequests.hpp"
#include "platform/null/NullInputDoor.hpp"
#include "platform/null/context/NullTextEditorContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <algorithm>
using namespace loka::app;
namespace
{
  class NullTextEditorHandler
      : public scene::RetainedNodeHandler<NullTextEditorHandler, TextEditorNode, NullTextEditorContext>
  {
  public:
    static TextEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextEditorNode>() ? static_cast<TextEditorNode *>(node)
                                                                                   : 0;
    }
    static NullTextEditorContext *create(TextEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &)
    {
      return new NullTextEditorContext(*controller, node, seamKey());
    }
  };
  NullTextEditorHandler textEditorHandler;
} // namespace
NullTextEditorContext::NullTextEditorContext(scene::IPlatformController &controller, TextEditorNode *node, const scene::SeamKey<TextEditorNode> &key)
    : key_(key),
      controller_(controller),
      node_(node),
      buffer_(),
      caret_(),
      phase_(IDLE),
      status_(EDITOR_UNAVAILABLE),
      restores_(0)
{
  // Reserve cancellation capacity before accepting any input.
  this->buffer_.reserve(TextEditorProps::kMaxBytes);
}
void NullTextEditorContext::readLifecycleFactOnAttach()
{
  assert(this->node_ && this->node_->props.lines_);
  this->project(LineCursor::None());
  this->settle(scene::SETTLE_ATTACH,
               this->node_ && this->node_->props.cursorState() ? this->node_->props.cursorState()->get()
                                                               : LineCursor::None());
}
void NullTextEditorContext::project(LineCursor fallback)
{
  if (!this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED)
  {
    this->status_ = EDITOR_UNAVAILABLE;
    this->buffer_.clear();
    this->caret_ = LineCursor::None();
    return;
  }
  this->status_ = this->node_->seam(this->key_).availability();
  if (this->status_ != EDITOR_OK)
  {
    this->buffer_.clear();
    this->caret_ = LineCursor::None();
    return;
  }
  this->status_ = this->node_->seam(this->key_).project(this->buffer_);
  if (this->status_ != EDITOR_OK)
  {
    this->buffer_.clear();
    this->caret_ = LineCursor::None();
    return;
  }
  const loka::core::ObservableList<loka::core::String> &lines = *this->node_->props.lines_;
  this->caret_ = this->node_->props.cursorState()->get();
  if (lines.find(this->caret_.line) < 0)
  {
    this->caret_ = fallback;
    if (lines.find(this->caret_.line) < 0)
      this->caret_ = lines.size() ? LineCursor(lines.at(0).id, fallback.column) : LineCursor::None();
  }
  if (!this->caret_.isNone())
  {
    const int lineIndex = lines.find(this->caret_.line);
    std::size_t start = 0;
    for (int i = 0; i < lineIndex; ++i)
      start = this->buffer_.find('\r', start) + 1;
    const std::size_t end = this->buffer_.find('\r', start);
    const std::size_t length = (end == std::string::npos ? this->buffer_.size() : end) - start;
    this->caret_.column = std::max(0, std::min(this->caret_.column, static_cast<int>(length)));
  }
}
void NullTextEditorContext::restoreCommittedProjection(LineCursor snapshot)
{
  assert(this->phase_ == IDLE);
  ++this->restores_;
  this->project(snapshot);
}
std::size_t NullTextEditorContext::nativeOffset() const
{
  if (!this->node_)
    return 0;
  const int index = this->node_->props.lines_->find(this->caret_.line);
  std::size_t offset = 0;
  for (int i = 0; i < index; ++i)
  {
    const std::size_t end = this->buffer_.find('\r', offset);
    if (end == std::string::npos)
      break;
    offset = end + 1;
  }
  return std::min(this->buffer_.size(), offset + static_cast<std::size_t>(std::max(0, this->caret_.column)));
}
void NullTextEditorContext::syncFromNode()
{
  if (this->phase_ == IDLE)
  {
    this->project(this->caret_);
    this->settle(scene::SETTLE_PROPS,
                 this->node_ && this->node_->props.cursorState() ? this->node_->props.cursorState()->get()
                                                                 : LineCursor::None());
  }
}
/** Stack policy: context lookup occurs only after the driver's lifetime wall. */
class NullTextEditorContext::RailOperation : public scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(NullTextEditorContext &ownerContext)
      : scene::RailOperation<LineCursor>(ownerContext.controller_, ownerContext.node_, &ownerContext) {}
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<LineCursor> &request)
  {
    request = static_cast<TextEditorNode &>(base).props.moveCaretTo_;
    this->capture(base);
    return this->enterTake(base, request);
  }
  void capture(scene::Node &base)
  {
    this->binding_ = static_cast<TextEditorNode &>(base).props;
  }
  template <class Request>
  scene::Admission enterTake(scene::Node &base,
                             const scene::RequestBinding<Request> &request,
                             const scene::RequestBinding<LineCursor> *priority = 0)
  {
    NullTextEditorContext &c = context(base);
    if (c.phase_ != IDLE || !c.node_)
      return scene::ADMISSION_DEFERRED;
    if (!request.isValid() || request.state()->get().isNone())
      return scene::ADMISSION_EMPTY;
    if (priority && priority->isValid() && !priority->state()->get().isNone())
      return scene::ADMISSION_DEFERRED;
    c.phase_ = INPUT;
    return scene::ADMISSION_TAKE;
  }
  virtual bool current(scene::Node &base, const scene::RequestBinding<LineCursor> &request)
  {
    return this->currentBinding(base) && static_cast<TextEditorNode &>(base).props.moveCaretTo_.same(request);
  }
  bool current(scene::Node &base, const scene::RequestBinding<EditorCommand> &request)
  {
    return this->currentBinding(base) && static_cast<TextEditorNode &>(base).props.command_.same(request);
  }
  virtual EditorResult resolve(scene::Node &base, const scene::RequestBinding<LineCursor> &request)
  {
    return this->resolveBinding(base, request);
  }
  template <class Request> EditorResult resolveBinding(scene::Node &base, const scene::RequestBinding<Request> &request)
  {
    if (!this->current(base, request))
      return EDITOR_OWNER_MISMATCH;
    if (context(base).phase_ != INPUT)
      return EDITOR_REENTRANT;
    loka::core::StateTracker *owner = 0;
    if (!this->binding_.lines_ || this->binding_.lines_->queryMutationTracker(owner) != loka::core::EDIT_OK)
      return EDITOR_UNAVAILABLE;
    return request.usesTracker(owner) ? EDITOR_OK : EDITOR_OWNER_MISMATCH;
  }
  virtual EditorResult validate(scene::Node &base, const LineCursor &pending)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    const EditorResult ready = node.seam(context(base).key_).availability();
    if (ready != EDITOR_OK)
      return ready;
    return node.props.lines_->find(pending.line) < 0 ? EDITOR_STALE_ID : EDITOR_OK;
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &base, const LineCursor &pending)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    const int row = node.props.lines_->find(pending.line);
    std::size_t length = 0;
    if (row < 0)
      return scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_STALE_ID);
    if (!node.props.lines_->at(static_cast<unsigned short>(row))
             .value.requiredUnits(loka::core::StringEncodingUtf8, length))
      return scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE);
    const LineCursor applied(pending.line, std::max(0, std::min(pending.column, static_cast<int>(length))));
    context(base).caret_ = applied;
    return scene::RequestApplication<LineCursor, EditorResult>(applied, EDITOR_OK);
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return static_cast<TextEditorNode &>(base).seam(context(base).key_).moveCaret(applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<LineCursor> &,
                                     const scene::RequestApplication<LineCursor, EditorResult> &)
  {
    NullTextEditorContext &c = context(base);
    c.phase_ = IDLE;
    c.project(c.caret_);
    // Null writes its simulated projection here, including refused-take repair.
    return scene::REPAINT;
  }
  // Null controls are native-scheduled paint answers; there is no repaint sink.
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
private:
  bool currentBinding(scene::Node &base) const
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    return node.lifecycleFact() == scene::NODE_FACT_ATTACHED && node.props.lines_ == this->binding_.lines_
           && node.props.cursorState() == this->binding_.cursorState();
  }
  static NullTextEditorContext &context(scene::Node &node)
  {
    return *static_cast<NullTextEditorContext *>(node.getContext());
  }
  TextEditorProps binding_;
};
bool NullTextEditorContext::queryVisibleLines(unsigned &lines) const
{
#ifdef TEST_BUILD
  if (loka::testing::declineNullTextEditorVisibleLines())
    return false;
#endif
  lines = kNullVisibleLines;
  return true;
}
/** The command seat borrows the entry's caret pipeline and completion owner. */
class NullTextEditorContext::CommandOperation : public scene::SeatOperation<EditorCommand, LineCursor>
{
public:
  explicit CommandOperation(RailOperation &rail)
      : rail_(rail)
  {
  }
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<EditorCommand> &request)
  {
    const TextEditorProps &props = static_cast<TextEditorNode &>(base).props;
    request = props.command_;
    this->rail_.capture(base);
    if (!request.isValid() || request.state()->get().isNone())
      return scene::ADMISSION_EMPTY;
    return this->rail_.enterTake(base, request, &props.moveCaretTo_);
  }
  virtual bool current(scene::Node &base, const scene::RequestBinding<EditorCommand> &request)
  {
    return this->rail_.current(base, request);
  }
  virtual EditorResult resolve(scene::Node &base, const scene::RequestBinding<EditorCommand> &request)
  {
    return this->rail_.resolveBinding(base, request);
  }
  virtual EditorResult validate(scene::Node &base, const EditorCommand &)
  {
    NullTextEditorContext &c = *static_cast<NullTextEditorContext *>(base.getContext());
    return static_cast<TextEditorNode &>(base).seam(c.key_).availability();
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &base, const EditorCommand &pending)
  {
    NullTextEditorContext &c = *static_cast<NullTextEditorContext *>(base.getContext());
    unsigned visibleLines = 0;
    LineCursor target;
    if (!c.queryVisibleLines(visibleLines))
      return scene::RequestApplication<LineCursor, EditorResult>(target, EDITOR_UNAVAILABLE);
    const EditorResult result =
        static_cast<TextEditorNode &>(base).seam(c.key_).pageTarget(pending, visibleLines, target);
    if (result != EDITOR_OK)
      return scene::RequestApplication<LineCursor, EditorResult>(target, result);
    const scene::RequestApplication<LineCursor, EditorResult> applied = this->rail_.apply(base, target);
    return scene::RequestApplication<LineCursor, EditorResult>(applied.value(), applied.result(), scene::REPAINT);
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return this->rail_.report(base, applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<EditorCommand> &reply,
                                     const scene::RequestApplication<LineCursor, EditorResult> &applied)
  {
    const scene::Reply<LineCursor> caretReply = reply.kind() == scene::Reply<EditorCommand>::REFUSED
                                                    ? scene::Reply<LineCursor>::Refused(applied.value(), reply.reason())
                                                    : scene::Reply<LineCursor>::Granted(applied.value());
    return this->rail_.finishTake(base, caretReply, applied);
  }

private:
  RailOperation &rail_;
};
#ifdef TEST_BUILD
scene::Admission loka::testing::TextEditorInput::probeAdmission(
    NullTextEditorContext &c, bool command, bool busy, bool &supplied, bool &opened)
{
  const NullTextEditorContext::Phase before = c.phase_;
  c.phase_ = busy ? NullTextEditorContext::RECONCILE : NullTextEditorContext::IDLE;
  NullTextEditorContext::RailOperation rail(c);
  scene::Admission admission;
  if (command)
  {
    NullTextEditorContext::CommandOperation op(rail);
    scene::RequestBinding<EditorCommand> binding;
    admission = op.admit(*c.node_, binding);
    supplied = binding.same(c.node_->props.command_);
  }
  else
  {
    scene::RequestBinding<LineCursor> binding;
    admission = rail.admit(*c.node_, binding);
    supplied = binding.same(c.node_->props.moveCaretTo_);
  }
  opened = c.phase_ == NullTextEditorContext::INPUT;
  c.phase_ = before;
  return admission;
}
#endif
EditorResult NullTextEditorContext::input(const std::string &bytes, bool join, const LineCursor *move)
{
  RailOperation op(*this);
  if (!this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED)
  {
    this->settle(scene::SETTLE_INPUT,
                 this->node_ && this->node_->props.cursorState() ? this->node_->props.cursorState()->get()
                                                                 : LineCursor::None());
    return EDITOR_UNAVAILABLE;
  }
  if (this->status_ != EDITOR_OK)
  {
    const EditorResult status = this->status_;
    this->settle(scene::SETTLE_INPUT,
                 this->node_ && this->node_->props.cursorState() ? this->node_->props.cursorState()->get()
                                                                 : LineCursor::None());
    return status;
  }
  const LineCursor factBefore = this->node_->props.cursorState()->get();
  const LineCursor before = this->caret_;
  const std::size_t offset = this->nativeOffset();
  // A real native control has already changed before its owner is notified.
  if (move)
    this->caret_ = *move;
  else if (join)
  {
    if (offset)
      this->buffer_.erase(offset - 1, 1);
  }
  else
    this->buffer_.insert(offset, bytes);
  if (this->phase_ != IDLE)
  {
    this->phase_ = RECONCILE;
    return EDITOR_REENTRANT;
  }
  this->phase_ = INPUT;
  EditorResult result;
  if (move)
    result = this->node_->seam(this->key_).moveCaret(*move);
  else if (join)
  {
    const loka::core::ObservableList<loka::core::String> &lines = *this->node_->props.lines_;
    const int index = lines.find(before.line);
    if (before.column != 0)
      result = EDITOR_INVALID_CURSOR;
    else if (index < 0)
      result = EDITOR_STALE_ID;
    else if (!index)
      result = EDITOR_OK;
    else
    {
      const unsigned short previous = static_cast<unsigned short>(index - 1);
      const LineCursor from(lines.at(previous).id,
                            static_cast<LineCursor::Column>(
                                lines.at(previous).value.bufferWithEncoding(loka::core::StringEncodingUtf8).length()));
      result = this->node_->seam(this->key_).applyReplace(from, before, "", 0);
    }
  }
  else
    result = this->node_->seam(this->key_).applyReplace(before, before, bytes.data(), bytes.size());
  if (!op.hasSameContext())
    return result;
  const bool reconcile = result != EDITOR_OK || this->phase_ == RECONCILE;
  this->phase_ = IDLE;
  if (reconcile)
    this->restoreCommittedProjection(before);
  else
    this->project(before);
  this->settle(scene::SETTLE_INPUT, factBefore);
  return result;
}
void NullTextEditorContext::settle(scene::Settlement stimulus
#ifdef TEST_BUILD
                                   ,
                                   LineCursor before
#endif
)
{
  RailOperation op(*this);
  CommandOperation command(op);
  scene::RequestSettlement<LineCursor>::settle(op,
                                               op,
                                               command,
                                               stimulus
#ifdef TEST_BUILD
                                               ,
                                               before
#endif
  );
}

short NullTextEditorContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  this->syncFromNode();
  // Fixed 80-pixel multiline block, preserving room for a following sibling.
  state.height = 80;
  return static_cast<short>(state.y + state.height);
}
void NullTextEditorContext::onFactChanged(scene::NodeLifecycleFact, scene::NodeLifecycleFact next)
{
  if (next != scene::NODE_FACT_ATTACHED)
  {
    if (this->phase_ != IDLE)
      this->phase_ = RECONCILE;
    if (next == scene::NODE_FACT_RETIRED)
      this->node_ = 0;
    this->buffer_.clear();
    this->caret_ = LineCursor::None();
    this->status_ = EDITOR_UNAVAILABLE;
  }
  else
    this->syncFromNode();
}
void RegisterNullTextEditorNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&textEditorHandler);
}

const void *NullTextEditorNodeHandlerKey()
{
  return textEditorHandler.nodeTypeKey();
}
bool IsNullTextEditorNodeHandler(const loka::app::scene::IPlatformNodeHandler *candidate)
{
  return candidate == &textEditorHandler;
}

// These native-event adapters share the rail door; observation helpers stay local.
EditorResult loka::testing::TextEditorInput::type(NullTextEditorContext &c, char value)
{ return NullInputDoor::textEditorInput(c, std::string(1, value), false, 0); }
EditorResult loka::testing::TextEditorInput::enter(NullTextEditorContext &c)
{ return NullInputDoor::textEditorInput(c, "\r", false, 0); }
EditorResult loka::testing::TextEditorInput::backspace(NullTextEditorContext &c)
{ return NullInputDoor::textEditorInput(c, "", true, 0); }
EditorResult loka::testing::TextEditorInput::paste(NullTextEditorContext &c, const std::string &text)
{ return NullInputDoor::textEditorInput(c, text, false, 0); }
EditorResult loka::testing::TextEditorInput::move(NullTextEditorContext &c, app::LineCursor cursor)
{ return NullInputDoor::textEditorInput(c, "", false, &cursor); }
