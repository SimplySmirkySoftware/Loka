#include "app/nodes/controls/TextEditorRequests.hpp"
#include "Win32InputDoor.hpp"
#include "Win32TextEditorContext.hpp"
#include "Win32FocusParticipant.hpp"
#include "Win32EditTextBridge.hpp"
#include "app/nodes/controls/TextEditorDiff.hpp"
#include "app/nodes/controls/TextDocumentLine.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <cassert>

using namespace loka::app;
namespace
{
  const wchar_t kEditorContext[] = L"Loka.TextEditor.Context";
  const UINT_PTR kRestoreTimer = 853;
  class TextEditorHandler : public scene::RetainedNodeHandler<TextEditorHandler, TextEditorNode, Win32TextEditorContext>
  {
  public:
    static TextEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextEditorNode>() ? static_cast<TextEditorNode *>(node)
                                                                                   : 0;
    }
    static Win32TextEditorContext *
    create(TextEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32TextEditorContext(win32, win32->projectionParentHwnd(), state, node, seamKey());
    }
    static void refresh(Win32TextEditorContext *context, const scene::LayoutState &state)
    {
      context->onPropsApplied();
      context->relayout(state);
    }
  };
  TextEditorHandler editorHandler;
  bool isInputMessage(UINT message)
  {
    return message == WM_CHAR || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_LBUTTONDOWN
           || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK || message == WM_MOUSEMOVE || message == WM_CUT
           || message == WM_CLEAR || message == WM_PASTE || message == WM_UNDO || message == EM_UNDO
           || message == EM_REPLACESEL;
  }
} // namespace

Win32TextEditorContext::Projection::Projection()
    : owner(0),
      revision(),
      text(),
      wide()
{
}
bool Win32TextEditorContext::Projection::current(const TextEditorNode &node) const
{
  if (!this->owner || this->owner != node.props.buffer_ || this->revision != this->owner->lines().revision().get())
    return false;
  loka::core::StateTracker *tracker = 0;
  const loka::core::ListEditResult ready = this->owner->lines().queryMutationTracker(tracker);
  return (ready == loka::core::EDIT_OK || ready == loka::core::EDIT_REENTRANT) && node.props.cursorUsesTracker(tracker);
}
EditorResult Win32TextEditorContext::Projection::capture(TextEditorNode &node, const scene::SeamKey<TextEditorNode> &key)
{
  if (this->current(node))
    return EDITOR_OK;
  this->owner = 0;
  // project() reserves text for the measured document; the wide buffer follows
  // that length plus one UTF-16 unit per logical CR projected as CRLF.
  const EditorResult result = node.seam(key).project(this->text);
  if (result == EDITOR_OK)
  {
    const unsigned short rows = node.props.buffer_->lines().size();
    this->wide.reserve(this->text.size() + (rows ? rows - 1 : 0));
  }
  if (result != EDITOR_OK || !loka::win32::TextEditorToWide(this->text, this->wide))
  {
    this->text.clear();
    this->wide.clear();
    return result == EDITOR_OK ? EDITOR_NON_ASCII : result;
  }
  this->owner = node.props.buffer_;
  this->revision = this->owner->lines().revision().get();
  return EDITOR_OK;
}

Win32TextEditorContext::Win32TextEditorContext(Win32ScenePlatformController *controller,
                                               HWND parent,
                                               const scene::LayoutState &state,
                                               TextEditorNode *node,
                                               const scene::SeamKey<TextEditorNode> &key)
    : Win32RetirableContext(controller),
      key_(key),
      node_(node),
      previousProc_(0),
      phase_(IDLE),
      status_(EDITOR_UNAVAILABLE),
      selection_(),
      projection_(),
      restores_(0),
      delivery_(scene::PaintAnswer::refused(scene::PAINT_REFUSED_HISTORY_UNKNOWN))
{
  this->createNativeChildWindow(
      loka::win32::EditTextControlExStyle(),
      L"EDIT",
      L"",
      loka::win32::EditTextControlStyle() | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
      controller->displayScale().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height)),
      parent,
      NULL,
      GetModuleHandleW(NULL),
      NULL);
  if (this->hwnd())
  {
    if (!SetPropW(this->hwnd(), kEditorContext, this))
    {
      this->retireWindow();
      return;
    }
    this->previousProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(this->hwnd(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (!this->previousProc_)
    {
      RemovePropW(this->hwnd(), kEditorContext);
      this->retireWindow();
      return;
    }
    SendMessageW(
        this->hwnd(), WM_SETFONT, reinterpret_cast<WPARAM>(controller->fallbackFont(TextStyle() + Monospace)), FALSE);
    // The owner enforces the logical cap. Leave room for an over-cap native
    // action so it is refused atomically instead of accepting a truncated paste.
    // Zero has special multiline semantics; specify the native limit explicitly.
    SendMessageW(this->hwnd(), EM_SETLIMITTEXT, 0x7ffffffe, 0);
  }
}
Win32TextEditorContext::~Win32TextEditorContext()
{
  assert(!this->hwnd() && "terminal lifecycle must strip the native input route before reclaim");
}
Win32TextEditorContext *Win32TextEditorContext::fromWindow(HWND window)
{
  return static_cast<Win32TextEditorContext *>(GetPropW(window, kEditorContext));
}
/** Platform twin of Toolbox's stack policy; context access follows the driver's lifetime wall. */
class Win32TextEditorContext::RailOperation : public scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(Win32TextEditorContext &ownerContext)
      : loka::app::scene::RailOperation<LineCursor>(*ownerContext.controller(), ownerContext.node_, &ownerContext),
        binding_(),
        follow_()
#ifdef TEST_BUILD
        ,
        before_(ownerContext.node_ && ownerContext.node_->props.cursorState()
                    ? ownerContext.node_->props.cursorState()->get() : LineCursor::None())
#endif
  {
  }
  void project(scene::Node &base)
  {
    this->include(context(base).replaceProjection());
  }
  void restore(scene::Node &base)
  {
    this->include(context(base).restoreCommittedProjection());
  }
  void include(scene::FollowUp follow)
  {
    this->follow_ = this->follow_.including(follow);
  }
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
    Win32TextEditorContext &c = context(base);
    if (!c.node_ || (c.phase_ != IDLE && !(c.phase_ == RETRY && c.status_ == EDITOR_UNAVAILABLE)))
      return scene::ADMISSION_DEFERRED;
    if (!request.isValid() || request.state()->get().isNone())
      return scene::ADMISSION_EMPTY;
    if (priority && priority->isValid() && !priority->state()->get().isNone())
      return scene::ADMISSION_DEFERRED;
    // Preserve the settle-scoped retry while opening COMMIT for refusal delivery.
    if (c.phase_ == RETRY)
      this->include(scene::RESTORE_QUEUED);
    c.phase_ = COMMIT;
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
    Win32TextEditorContext &c = context(base);
    if (!this->current(base, request))
      return EDITOR_OWNER_MISMATCH;
    if (c.phase_ != COMMIT)
      return EDITOR_REENTRANT;
    if (!c.hwnd())
      return EDITOR_UNAVAILABLE;
    if (c.status_ != EDITOR_OK)
      return c.status_;
    loka::core::StateTracker *owner = 0;
    if (!this->binding_.buffer_ || this->binding_.buffer_->lines().queryMutationTracker(owner) != loka::core::EDIT_OK)
      return EDITOR_UNAVAILABLE;
    return request.usesTracker(owner) ? EDITOR_OK : EDITOR_OWNER_MISMATCH;
  }
  virtual EditorResult validate(scene::Node &base, const LineCursor &pending)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    const EditorResult ready = node.seam(context(base).key_).availability();
    if (ready != EDITOR_OK)
      return ready;
    return node.props.buffer_->lines().find(pending.line) < 0 ? EDITOR_STALE_ID : EDITOR_OK;
  }
  /** Taking can notify an owner edit. Repair before asking EDIT for offsets
      or geometry, so both seats measure the reconciled projection. */
  scene::FollowUp ensureProjection(scene::Node &base)
  {
    Win32TextEditorContext &c = context(base);
    return c.projection_.current(*c.node_) ? scene::FOLLOW_NONE : c.replaceProjection();
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &base, const LineCursor &pending)
  {
    Win32TextEditorContext &c = context(base);
    const scene::FollowUp follow = this->ensureProjection(base);
    if (c.phase_ != COMMIT || c.status_ != EDITOR_OK)
      return scene::RequestApplication<LineCursor, EditorResult>(pending, c.status_, follow);
    const int row = c.node_->props.buffer_->lines().find(pending.line);
    const int offset = static_cast<int>(SendMessageW(c.hwnd(), EM_LINEINDEX, row, 0));
    std::size_t start = 0, length = 0;
    if (offset < 0 || !c.projection_.owner || !loka::win32::TextEditorRowRange(c.projection_.text, row, start, length))
      return scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE, follow);
    const char *bytes = c.projection_.text.data() + start;
    const LineCursor::Column column =
        static_cast<LineCursor::Column>(Utf8ClampColumnToBoundary(bytes, length, pending.column));
    const int units = loka::win32::LogicalRow(bytes, length).unitsAt(static_cast<std::size_t>(column));
    SendMessageW(c.hwnd(), EM_SETSEL, offset + units, offset + units);
    return scene::RequestApplication<LineCursor, EditorResult>(
        LineCursor(pending.line, column), EDITOR_OK, scene::REPAINT);
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return static_cast<TextEditorNode &>(base).seam(context(base).key_).moveCaret(applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<LineCursor> &reply,
                                     const scene::RequestApplication<LineCursor, EditorResult> &application)
  {
    Win32TextEditorContext &c = context(base);
    scene::FollowUp follow = scene::FOLLOW_NONE;
    if (c.node_ && c.hwnd() && c.status_ == EDITOR_OK && c.node_->lifecycleFact() == scene::NODE_FACT_ATTACHED)
    {
      if (c.phase_ == REJECTED || !c.projection_.current(*c.node_))
      {
        c.phase_ = COMMIT;
        // replaceProjection also restores selection from the post-report fact.
        follow = c.replaceProjection();
      }
      else if (reply.kind() == scene::Reply<LineCursor>::REFUSED && application.result() == EDITOR_OK)
      {
        // Native apply succeeded, but the seam did not accept its caret fact.
        // Text is still current; repair selection without discarding native undo.
        c.restoreSelection();
        follow = scene::REPAINT;
      }
    }
    if (c.phase_ == COMMIT || c.phase_ == REJECTED)
      c.phase_ = IDLE;
    return follow;
  }
  virtual scene::FollowUpResult finishSettle(scene::Node &base, const scene::FollowUps &follow)
  {
    Win32TextEditorContext &c = context(base);
    // A reply subscriber can detach a retained context without retiring its identity.
    if (!c.hwnd() || base.lifecycleFact() != scene::NODE_FACT_ATTACHED)
      return scene::FOLLOW_UP_NONE;
    if (this->follow_.contains(scene::RESTORE_QUEUED) && c.phase_ == IDLE)
      c.phase_ = RETRY;
    if (follow.contains(scene::REPAINT) || this->follow_.contains(scene::REPAINT))
      c.delivery_ = scene::PaintAnswer::nativeScheduled();
    if (c.phase_ == RETRY
        && (follow.contains(scene::SCHEDULE_RESTORE) || this->follow_.contains(scene::SCHEDULE_RESTORE)))
    {
      // A failed arm refuses the deferred request in this same settlement.
      if (!SetTimer(c.hwnd(), kRestoreTimer, 1, NULL))
      {
        c.status_ = EDITOR_UNAVAILABLE;
        // Keep clear/reply subscribers inside the same native exclusion as a take.
        // settle's returned result releases it after the refusal-only tail.
        c.phase_ = COMMIT;
        return scene::FOLLOW_UP_FAILED;
      }
      return scene::FOLLOW_UP_ARMED;
    }
    return scene::FOLLOW_UP_NONE;
  }
#ifdef TEST_BUILD
  virtual LineCursor fact(scene::Node &base) const
  {
    loka::core::State<LineCursor> *state = static_cast<TextEditorNode &>(base).props.cursorState();
    return state ? state->get() : LineCursor::None();
  }
  LineCursor before() const
  {
    return this->before_;
  }
#endif
private:
  bool currentBinding(scene::Node &base) const
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    return node.lifecycleFact() == scene::NODE_FACT_ATTACHED && node.props.buffer_ == this->binding_.buffer_
           && node.props.cursorState() == this->binding_.cursorState();
  }
  static Win32TextEditorContext &context(scene::Node &node)
  {
    return *static_cast<Win32TextEditorContext *>(node.getContext());
  }
  TextEditorProps binding_;
  scene::FollowUps follow_;
#ifdef TEST_BUILD
  const LineCursor before_;
#endif
};
bool Win32TextEditorContext::queryVisibleLines(unsigned &lines) const
{
  if (!this->hwnd())
    return false;
  RECT rect = {0};
  SendMessageW(this->hwnd(), EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
  HDC dc = GetDC(this->hwnd());
  if (!dc)
    return false;
  const HFONT font = reinterpret_cast<HFONT>(SendMessageW(this->hwnd(), WM_GETFONT, 0, 0));
  const HGDIOBJ previous = font ? SelectObject(dc, font) : NULL;
  TEXTMETRICW metrics = {0};
  const BOOL measured = GetTextMetricsW(dc, &metrics);
  if (previous)
    SelectObject(dc, previous);
  ReleaseDC(this->hwnd(), dc);
  const LONG lineHeight = metrics.tmHeight + metrics.tmExternalLeading;
  if (!measured || lineHeight <= 0 || rect.bottom <= rect.top)
    return false;
  // ES_AUTOHSCROLL disables wrapping: each display row is one logical line.
  // EM_GETRECT is the control's own formatting frame, not ancestor clipping.
  const unsigned capacity = static_cast<unsigned>((rect.bottom - rect.top) / lineHeight);
  if (!capacity)
    return false;
  lines = capacity;
  return true;
}
/** The command seat borrows the entry's caret pipeline and completion owner. */
class Win32TextEditorContext::CommandOperation : public scene::SeatOperation<EditorCommand, LineCursor>
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
    Win32TextEditorContext &c = *static_cast<Win32TextEditorContext *>(base.getContext());
    return static_cast<TextEditorNode &>(base).seam(c.key_).availability();
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &base, const EditorCommand &pending)
  {
    Win32TextEditorContext &c = *static_cast<Win32TextEditorContext *>(base.getContext());
    unsigned visibleLines = 0;
    LineCursor target;
    const scene::FollowUp follow = this->rail_.ensureProjection(base);
    if (!c.queryVisibleLines(visibleLines))
      return scene::RequestApplication<LineCursor, EditorResult>(target, EDITOR_UNAVAILABLE, follow);
    const EditorResult result =
        static_cast<TextEditorNode &>(base).seam(c.key_).pageTarget(pending, visibleLines, target);
    if (result != EDITOR_OK)
      return scene::RequestApplication<LineCursor, EditorResult>(target, result, follow);
    const scene::RequestApplication<LineCursor, EditorResult> applied = this->rail_.apply(base, target);
    if (applied.result() == EDITOR_OK)
    {
      // Scroll by an explicit line delta: EM_SCROLLCARET does nothing for a
      // window that is not visible (hosted CI), while EM_LINESCROLL moves the
      // formatting frame regardless of visibility.
      const LRESULT row = SendMessageW(c.hwnd(), EM_LINEFROMCHAR, static_cast<WPARAM>(-1), 0);
      const LRESULT first = SendMessageW(c.hwnd(), EM_GETFIRSTVISIBLELINE, 0, 0);
      const LRESULT last = first + static_cast<LRESULT>(visibleLines) - 1;
      const LRESULT delta = row < first ? row - first : row > last ? row - last : 0;
      if (delta)
        SendMessageW(c.hwnd(), EM_LINESCROLL, 0, static_cast<LPARAM>(delta));
    }
    return applied;
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return this->rail_.report(base, applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<EditorCommand> &reply,
                                     const scene::RequestApplication<LineCursor, EditorResult> &applied)
  {
    Win32TextEditorContext &c = *static_cast<Win32TextEditorContext *>(base.getContext());
    // A report subscriber can require projection repair. Preserve the page's
    // viewport before the shared finishTake calls restoreSelection (#902), but
    // only for an accepted command: a refused report restores the pre-command
    // live view, so the snapshot must still describe it.
    if (reply.kind() == scene::Reply<EditorCommand>::GRANTED && applied.result() == EDITOR_OK && c.hwnd())
      c.captureSelection();
    const scene::Reply<LineCursor> caretReply = reply.kind() == scene::Reply<EditorCommand>::REFUSED
                                                    ? scene::Reply<LineCursor>::Refused(applied.value(), reply.reason())
                                                    : scene::Reply<LineCursor>::Granted(applied.value());
    return this->rail_.finishTake(base, caretReply, applied);
  }

private:
  RailOperation &rail_;
};
void Win32TextEditorContext::settle(scene::Settlement stimulus)
{
  RailOperation op(*this);
  this->settle(stimulus, op);
}
void Win32TextEditorContext::settle(scene::Settlement stimulus, RailOperation &op)
{
  CommandOperation command(op);
  const scene::FollowUpResult result = scene::RequestSettlement<LineCursor>::settle(op,
                                                                                    op,
                                                                                    command,
                                                                                    stimulus
#ifdef TEST_BUILD
                                                                                    ,
                                                                                    op.before()
#endif
  );
  if (op.hasSameContext() && result == scene::FOLLOW_UP_FAILED
      && (this->phase_ == COMMIT || this->phase_ == REJECTED))
    this->phase_ = op.node()->lifecycleFact() == scene::NODE_FACT_ATTACHED ? RETRY : IDLE;
}
void Win32TextEditorContext::readLifecycleFactOnAttach()
{
  RailOperation op(*this);
  if (this->hwnd() && this->node_ && this->node_->lifecycleFact() == scene::NODE_FACT_ATTACHED)
    Win32FocusParticipant::attach(this->hwnd(), this);
  this->syncFromNode(scene::SETTLE_ATTACH);
}
void Win32TextEditorContext::onPropsApplied()
{
  if (this->phase_ == IDLE || this->phase_ == RETRY)
    this->syncFromNode();
}
void Win32TextEditorContext::onNativeFact(scene::NodeLifecycleFact, scene::NodeLifecycleFact next)
{
  RailOperation op(*this);
  if (next != scene::NODE_FACT_ATTACHED)
    this->selection_.evidence.clear();
  if (!this->hwnd())
    return;
  if (next == scene::NODE_FACT_ATTACHED)
  {
    Win32FocusParticipant::attach(this->hwnd(), this);
    this->syncFromNode(scene::SETTLE_ATTACH);
    if (op.hasSameContext() && this->node_->lifecycleFact() == scene::NODE_FACT_ATTACHED)
      ShowWindow(this->hwnd(), SW_SHOW);
    return;
  }
  Win32FocusParticipant::detach(this->hwnd());
  KillTimer(this->hwnd(), kRestoreTimer);
  ShowWindow(this->hwnd(), SW_HIDE);
  SendMessageW(this->hwnd(), EM_SETREADONLY, TRUE, 0);
  this->status_ = EDITOR_UNAVAILABLE;
  this->phase_ = this->phase_ == INPUT || this->phase_ == PASTING || this->phase_ == COMMIT ? REJECTED : IDLE;
  this->delivery_ = scene::PaintAnswer::refused(scene::PAINT_REFUSED_HISTORY_UNKNOWN);
  if (next == scene::NODE_FACT_RETIRED)
  {
    RemovePropW(this->hwnd(), kEditorContext);
    SetWindowLongPtrW(this->hwnd(), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->previousProc_));
    this->node_ = 0;
  }
}
void Win32TextEditorContext::captureSelection(CaptureIntent intent)
{
  this->selection_.evidence = intent == PRE_INPUT && this->node_ && this->projection_.current(*this->node_)
                                  ? EditEvidence(this->projection_.revision)
                                  : EditEvidence();
  SendMessageW(this->hwnd(),
               EM_GETSEL,
               reinterpret_cast<WPARAM>(&this->selection_.start),
               reinterpret_cast<LPARAM>(&this->selection_.end));
  this->selection_.firstVisible = static_cast<int>(SendMessageW(this->hwnd(), EM_GETFIRSTVISIBLELINE, 0, 0));
  this->selection_.horizontal = GetScrollPos(this->hwnd(), SB_HORZ);
}
scene::FollowUp Win32TextEditorContext::deferRestore()
{
  if (!this->hwnd() || !this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED)
    return scene::FOLLOW_NONE;
  this->phase_ = RETRY;
  SendMessageW(this->hwnd(), EM_SETREADONLY, TRUE, 0);
  return scene::SCHEDULE_RESTORE;
}
scene::FollowUp Win32TextEditorContext::replaceProjection()
{
  if (!this->hwnd() || !this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED)
    return scene::FOLLOW_NONE;
  // A repair from the request consumer must keep its exclusion at completion.
  const Phase completion = this->phase_ == COMMIT ? COMMIT : IDLE;
  this->phase_ = RESTORING;
  const EditorResult projected = this->projection_.capture(*this->node_, this->key_);
  const bool available = projected == EDITOR_OK;
  // Deliberately bypass State equality and the ordinary projection cache.
  const bool complete = loka::win32::WriteTextEditorWide(this->hwnd(), this->projection_.wide);
  SendMessageW(this->hwnd(), EM_EMPTYUNDOBUFFER, 0, 0);
  if (!complete)
  {
    this->status_ = EDITOR_UNAVAILABLE;
    this->delivery_ = scene::PaintAnswer::refused(scene::PAINT_REFUSED_PROPS_UNRECONCILED);
    this->phase_ = completion;
    return this->deferRestore();
  }
  this->restoreSelection();
  this->status_ = projected;
  this->phase_ = completion;
  SendMessageW(this->hwnd(), EM_SETREADONLY, available ? FALSE : TRUE, 0);
  this->delivery_ = scene::PaintAnswer::nativeScheduled();
  if (projected == EDITOR_ALLOCATION)
    return this->deferRestore();
  return scene::REPAINT;
}
void Win32TextEditorContext::restoreSelection()
{
  DWORD start = this->selection_.start, end = this->selection_.end;
  if (this->node_->props.buffer_ && this->node_->props.cursorState())
  {
    const LineCursor cursor = this->node_->props.cursorState()->get();
    const int index = this->node_->props.buffer_->lines().find(cursor.line);
    if (index >= 0)
    {
      const int offset = static_cast<int>(SendMessageW(this->hwnd(), EM_LINEINDEX, index, 0));
      std::size_t rowStart = 0, length = 0;
      if (offset >= 0 && this->projection_.owner
          && loka::win32::TextEditorRowRange(this->projection_.text, index, rowStart, length))
      {
        const char *bytes = this->projection_.text.data() + rowStart;
        const std::size_t column = Utf8ClampColumnToBoundary(bytes, length, cursor.column);
        start = end = static_cast<DWORD>(offset + loka::win32::LogicalRow(bytes, length).unitsAt(column));
      }
    }
  }
  SendMessageW(this->hwnd(), EM_SETSEL, start, end);
  const int visible = static_cast<int>(SendMessageW(this->hwnd(), EM_GETFIRSTVISIBLELINE, 0, 0));
  SendMessageW(this->hwnd(), EM_LINESCROLL, 0, this->selection_.firstVisible - visible);
  SendMessageW(this->hwnd(), WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, this->selection_.horizontal), 0);
}
scene::FollowUp Win32TextEditorContext::restoreCommittedProjection()
{
  assert(this->phase_ != COMMIT && this->phase_ != RESTORING);
  ++this->restores_;
  return this->replaceProjection();
}
void Win32TextEditorContext::syncFromNode(scene::Settlement stimulus)
{
  if (this->phase_ != IDLE && this->phase_ != RETRY)
    return;
  RailOperation op(*this);
  if (!this->hwnd() || !this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED)
  {
    this->settle(stimulus, op);
    return;
  }
  if (this->phase_ == RETRY)
  {
    KillTimer(this->hwnd(), kRestoreTimer);
    op.restore(*this->node_);
  }
  else
  {
    this->captureSelection();
    if (this->status_ == EDITOR_OK && this->projection_.current(*this->node_))
    {
      // Reports are facts, never commands to collapse a native selection.
      const scene::PaintDamage empty = {paintScope(), 0, 0, 0, 0, scene::PAINT_COVERAGE_PAINT_ONLY};
      this->delivery_ = scene::PaintAnswer::exact(empty);
    }
    else
      op.project(*this->node_);
  }
  this->settle(stimulus, op);
}
RowCursor Win32TextEditorContext::nativeRowCaret(const std::string &logical) const
{
  DWORD start = 0, end = 0;
  SendMessageW(this->hwnd(), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
  const int index = static_cast<int>(SendMessageW(this->hwnd(), EM_LINEFROMCHAR, end, 0));
  const int offset = static_cast<int>(SendMessageW(this->hwnd(), EM_LINEINDEX, index, 0));
  std::size_t rowStart = 0, length = 0;
  if (offset < 0 || !loka::win32::TextEditorRowRange(logical, index, rowStart, length))
    return RowCursor::None();
  const std::size_t column =
      loka::win32::LogicalRow(logical.data() + rowStart, length).bytesFloor(static_cast<int>(end) - offset);
  return RowCursor(static_cast<unsigned short>(index), static_cast<LineCursor::Column>(column));
}
LineCursor Win32TextEditorContext::nativeCaret() const
{
  if (!this->node_ || !this->node_->props.buffer_)
    return LineCursor::None();
  const RowCursor caret = this->nativeRowCaret(this->projection_.text);
  if (caret.isNone() || caret.row >= this->node_->props.buffer_->lines().size())
    return LineCursor::None();
  return LineCursor(this->node_->props.buffer_->lines().at(caret.row).id, caret.column);
}
EditorResult Win32TextEditorContext::applyLines(int first, int oldCount, int newCount, const std::string &logical)
{
  loka::app::TextLines &lines = this->node_->props.buffer_->lines();
  if (first < 0 || oldCount <= 0 || newCount <= 0 || first + oldCount > lines.size())
    return EDITOR_INVALID_CURSOR;
  const unsigned short last = static_cast<unsigned short>(first + oldCount - 1);
  const LineCursor from(lines.at(static_cast<unsigned short>(first)).id, 0);
  const LineCursor to(
      lines.at(last).id,
      static_cast<LineCursor::Column>(loka::app::TextEditorLogicalLine(this->projection_.text, last).size()));
  // Slice the changed logical rows once, retaining only their interior CRs.
  std::size_t start = 0;
  for (int row = 0; row < first; ++row)
    start = logical.find('\r', start) + 1;
  std::size_t end = start;
  for (int row = 0; row < newCount; ++row)
  {
    end = logical.find('\r', end);
    if (end == std::string::npos)
    {
      end = logical.size();
      break;
    }
    if (row + 1 < newCount)
      ++end;
  }
  // Native rows already describe the post-state, including newly inserted rows.
  return this->node_->seam(this->key_)
      .applyReplace(from, to, logical.data() + start, end - start, this->nativeRowCaret(logical));
}
EditorResult Win32TextEditorContext::commitNativeChange()
{
  // Take before every refusal/no-op and before either model call can notify.
  const bool matchingEvidence = this->selection_.evidence.consumeMatching(this->projection_.revision);
  if (!this->node_ || !this->node_->props.buffer_)
    return EDITOR_UNAVAILABLE;
  if (!this->projection_.current(*this->node_))
    return EDITOR_STALE_ID;
  const int length = GetWindowTextLengthW(this->hwnd());
  const TextCapacity &capacity = this->node_->props.buffer_->capacity();
  std::size_t budget = 0;
  if (!text_document_detail::ExpandedSize(capacity.bytes, capacity.lines ? capacity.lines - 1 : 0, budget)
      || static_cast<std::size_t>(length) > budget)
    return EDITOR_CAPACITY;
  std::wstring wide;
  loka::win32::ReadEditTextWide(this->hwnd(), wide);
  if (wide.size() != static_cast<std::size_t>(length))
    return EDITOR_UNAVAILABLE;
  std::string logical;
  if (!loka::win32::TextEditorFromWide(wide.data(), wide.size(), logical))
    return EDITOR_NON_ASCII;
  if (!text_document_detail::PayloadAdmits(logical.data(), logical.size()))
    return EDITOR_NON_ASCII;
  if (logical.size() > this->node_->props.buffer_->capacity().bytes)
    return EDITOR_CAPACITY;
  // Walk the before snapshot once; the final unterminated row also has units.
  int caretLine = 0;
  int caretColumn = static_cast<int>(this->selection_.end);
  std::size_t start = 0;
  for (;;)
  {
    const std::size_t found = this->projection_.text.find('\r', start);
    const std::size_t end = found == std::string::npos ? this->projection_.text.size() : found;
    const loka::win32::LogicalRow row(this->projection_.text.data() + start, end - start);
    const int units = row.unitLength();
    if (found == std::string::npos || caretColumn < units + 2)
    {
      caretColumn = static_cast<int>(row.bytesFloor(caretColumn));
      break;
    }
    caretColumn -= units + 2;
    ++caretLine;
    start = end + 1;
  }
  // Only a collapsed selection captured before this revision's input is evidence.
  // Without it, repeated/empty edge rows deliberately use text-only identity.
  const loka::app::TextEditorLineDiff diff = loka::app::DiffTextEditorLines(
      this->projection_.text,
      logical,
      matchingEvidence && this->selection_.start == this->selection_.end ? caretLine : -1,
      caretColumn);
  if (diff.before() == 0 && diff.after() == 0)
    return this->node_->seam(this->key_).moveCaret(this->nativeCaret());
  if (this->node_->props.buffer_->lines().size() - diff.before() + diff.after()
      > this->node_->props.buffer_->capacity().lines)
    return EDITOR_CAPACITY;
  return this->applyLines(diff.first(), diff.before(), diff.after(), logical);
}
bool Win32TextEditorContext::handleCommand(WPARAM wParam, LPARAM)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (HIWORD(wParam) != EN_CHANGE)
    return false;
  if (this->phase_ == RESTORING)
    return true;
  if (this->phase_ != INPUT && this->phase_ != PASTING && this->phase_ != IDLE)
  {
    if (this->phase_ != RETRY)
      this->phase_ = REJECTED;
    return true;
  }
  RailOperation op(*this);
  const Phase inputPhase = this->phase_ == PASTING ? PASTING : INPUT;
  const bool outsideInput = this->phase_ == IDLE;
  if (outsideInput)
    this->captureSelection();
  this->phase_ = COMMIT;
  const EditorResult result = this->status_ == EDITOR_OK ? this->commitNativeChange() : this->status_;
  if (this->status_ != EDITOR_OK)
    this->selection_.evidence.clear();
  if (!op.hasSameContext())
    return true;
  if (result == EDITOR_OK && this->node_)
  {
    this->status_ = this->projection_.capture(*this->node_, this->key_);
    this->delivery_ = scene::PaintAnswer::nativeScheduled();
  }
  if (result != EDITOR_OK || this->status_ != EDITOR_OK || this->phase_ == REJECTED)
  {
    if (result != EDITOR_OK)
      this->status_ = result;
    this->phase_ = REJECTED;
  }
  else
    this->phase_ = inputPhase;
  if (outsideInput)
  {
    if (this->phase_ == REJECTED)
      op.include(this->deferRestore());
    else
      this->phase_ = IDLE;
    this->settle(scene::SETTLE_INPUT, op);
  }
  return true;
}
void Win32TextEditorContext::syncCaret()
{
  RailOperation op(*this);
  if (!this->node_ || (this->phase_ != INPUT && this->phase_ != PASTING) || this->status_ != EDITOR_OK)
    return;
  const LineCursor cursor = this->nativeCaret();
  if (cursor == this->node_->props.cursorState()->get())
    return;
  const Phase inputPhase = this->phase_;
  this->phase_ = COMMIT;
  const EditorResult result = this->node_->seam(this->key_).moveCaret(cursor);
  if (!op.hasSameContext())
    return;
  this->phase_ = result == EDITOR_OK && this->phase_ != REJECTED ? inputPhase : REJECTED;
}
LRESULT CALLBACK Win32TextEditorContext::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
  Win32TextEditorContext *self = fromWindow(window);
  if (!self)
    return DefWindowProcW(window, message, wParam, lParam);
  if (message == WM_TIMER && wParam == kRestoreTimer)
  {
    Win32InputDoor::textEditorRetry(*self);
    return 0;
  }
  if (!isInputMessage(message))
    return CallWindowProcW(self->previousProc_, window, message, wParam, lParam);
  return Win32InputDoor::textEditorInput(*self, message, wParam, lParam);
}
void Win32TextEditorContext::handleRestoreTimer()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  const HWND window = this->hwnd();
  RailOperation op(*this);
  KillTimer(window, kRestoreTimer);
  if (this->phase_ == RETRY)
  {
    if (this->node_)
      op.restore(*this->node_);
    this->settle(scene::SETTLE_DEFERRED, op);
  }
}
/** One native step; capture, repair and settlement belong to performInput. */
struct Win32TextEditorContext::NativeInput
{
  enum Kind
  {
    MESSAGE,
    SELECT_ALL
  };
  const Kind kind;
  const UINT message;
  const WPARAM wParam;
  const LPARAM lParam;
  NativeInput(UINT m, WPARAM w, LPARAM l)
      : kind(MESSAGE),
        message(m),
        wParam(w),
        lParam(l)
  {
  }
  NativeInput()
      : kind(SELECT_ALL),
        message(EM_SETSEL),
        wParam(0),
        lParam(-1)
  {
  }
  LRESULT invoke(HWND window, WNDPROC previous) const
  {
    switch (this->kind)
    {
    case MESSAGE:
      return CallWindowProcW(previous, window, this->message, this->wParam, this->lParam);
    case SELECT_ALL:
      return SendMessageW(window, EM_SETSEL, 0, -1);
    }
    return 0;
  }
};
LRESULT Win32TextEditorContext::handleInputMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  const HWND window = this->hwnd();
  if (this->phase_ == INPUT || this->phase_ == PASTING)
  {
    // EDIT can forward input messages while executing one native action. Its
    // EN_CHANGE belongs to that open action; only the outer call closes it.
    // Owner callbacks run in COMMIT, where a second action is rejected below.
    if (message == WM_PASTE)
      this->phase_ = PASTING;
    return CallWindowProcW(this->previousProc_, window, message, wParam, lParam);
  }
  return this->performInput(NativeInput(message, wParam, lParam));
}
void Win32TextEditorContext::selectAll()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  this->performInput(NativeInput());
}
LRESULT Win32TextEditorContext::performInput(const NativeInput &input)
{
  if (this->phase_ != IDLE)
  {
    if (this->phase_ == COMMIT)
      this->phase_ = REJECTED;
    return 0;
  }
  if (!this->node_ || this->node_->lifecycleFact() != scene::NODE_FACT_ATTACHED || this->status_ != EDITOR_OK)
  {
    this->settle(scene::SETTLE_INPUT);
    return 0;
  }
  RailOperation op(*this);
  this->captureSelection(PRE_INPUT);
  this->phase_ = input.message == WM_PASTE ? PASTING : INPUT;
  const LRESULT result = input.invoke(this->hwnd(), this->previousProc_);
  this->selection_.evidence.clear();
  if (!op.hasSameContext())
    return result;
  if (!this->hwnd() || !this->node_)
    return result;
  this->syncCaret();
  if (!op.hasSameContext())
    return result;
  const bool rejected = this->phase_ == REJECTED;
  this->phase_ = IDLE;
  if (rejected)
    op.restore(*this->node_);
  this->settle(scene::SETTLE_INPUT, op);
  return result;
}
short Win32TextEditorContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  this->relayout(state);
  return static_cast<short>(state.y + state.height);
}
void Win32TextEditorContext::relayout(const scene::LayoutState &state)
{
  if (this->hwnd())
    this->positionNativeWindow(this->hwnd(),
                               this->controller()->displayScale().projectFrame(
                                   loka::core::Frame(state.x, state.y, state.width, state.height)));
}
scene::PaintAnswer Win32TextEditorContext::queryPaintDamage(const scene::PaintQuery &query) const
{
  if (!this->hwnd())
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != scene::PLACEMENT_ELIGIBLE)
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (this->phase_ == INPUT || this->phase_ == PASTING || this->phase_ == COMMIT)
  {
    // The native edit already owns its repaint; synchronous model echoes can
    // precede the committed-cache refresh, just as in EditText::applyText.
    const scene::PaintDamage empty = {query.scope, 0, 0, 0, 0, scene::PAINT_COVERAGE_PAINT_ONLY};
    return scene::PaintAnswer::exact(empty);
  }
  if (this->phase_ == RETRY || (this->status_ == EDITOR_OK && this->node_ && !this->projection_.current(*this->node_)))
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_PROPS_UNRECONCILED);
  scene::PaintAnswer answer = this->delivery_;
  if (answer.kind == scene::PAINT_ANSWER_EXACT)
    answer.damage.scope = query.scope;
  return answer;
}
void RegisterWin32TextEditorNodeHandler(scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&editorHandler);
}
