#include "ToolboxPaintGround.hpp"
#include "context/ToolboxTextEditorContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <Memory.h>
#include <algorithm>
#include <cstring>
#include "ToolboxPortScope.hpp"
using namespace loka::app;
ToolboxTextEditorRects ToolboxTextEditorContext::geometry() const
{
  Rect port = Rect(), sizeBox = Rect();
  const bool hasPort =
      this->controller() && this->controller()->window_ && this->controller()->window_->queryPortRect(port);
  if (this->controller() && this->controller()->window_)
    this->controller()->window_->querySizeBoxRect(sizeBox);
  return ToolboxTextEditorGeometry(this->rect_, port, hasPort, sizeBox);
}

namespace
{
  class Handler : public scene::RetainedNodeHandler<Handler, TextEditorNode, ToolboxTextEditorContext>
  {
  public:
    static TextEditorNode *cast(scene::Node *n)
    {
      return n && n->nodeTypeKey() == scene::NodeTypeToken<TextEditorNode>() ? static_cast<TextEditorNode *>(n) : 0;
    }
    static ToolboxTextEditorContext *
    create(TextEditorNode *n, scene::IPlatformController *c, const scene::LayoutState &)
    {
      return new ToolboxTextEditorContext(n, static_cast<ToolboxScenePlatformController *>(c), seamKey());
    }
  };
  Handler handler;
  /** TE visual lineStarts include wrapped rows. Logical lines are CR-delimited.
      Borrow and lock the handle while scanning the native CR-delimited bytes. */
  class TextLock
  {
  public:
    explicit TextLock(TEHandle te)
        : handle_(reinterpret_cast<Handle>(TEGetText(te)))
    {
      if (this->handle_)
        HLock(this->handle_);
    }
    ~TextLock()
    {
      if (this->handle_)
        HUnlock(this->handle_);
    }
    const char *bytes() const
    {
      return this->handle_ ? *this->handle_ : 0;
    }

  private:
    TextLock(const TextLock &);
    TextLock &operator=(const TextLock &);
    Handle handle_;
  };
} // namespace
ToolboxTextEditorContext::ToolboxTextEditorContext(TextEditorNode *n,
                                                   ToolboxScenePlatformController *c,
                                                   const scene::SeamKey<TextEditorNode> &key)
    : ToolboxProjectedNodeContext(c),
      key_(key),
      node_(n),
      te_(0),
      restore_(static_cast<char *>(loka::core::LokaAllocRaw(
          TextEditorProps::kMaxBytes, loka::core::LokaAllocationSite("ToolboxTextEditor", "Restore")))),
      rect_(),
      paintRect_(),
      source_(0),
      revision_(),
      phase_(IDLE),
      status_(EDITOR_UNAVAILABLE),
      restores_(0),
      scrollBarId_(0)
{
}
ToolboxTextEditorContext::~ToolboxTextEditorContext()
{
  assert(!this->te_ && "detach must revoke the borrowed TE before reclaim");
  loka::core::LokaFreeRaw(this->restore_, loka::core::LokaAllocationSite("ToolboxTextEditor", "Restore"));
}
void ToolboxTextEditorContext::invalidateNativePresentation()
{
  if (this->controller())
    this->controller()->revokeEditorScrollBar(this->scrollBarId_);
  this->te_ = 0;
  this->source_ = 0;
  this->status_ = EDITOR_UNAVAILABLE;
}
void ToolboxTextEditorContext::onFactChanged(scene::NodeLifecycleFact previous, scene::NodeLifecycleFact next)
{
  if (next != scene::NODE_FACT_ATTACHED && this->controller())
  {
    this->controller()->revokeEditorScrollBar(this->scrollBarId_);
    this->controller()->retireTextEditorControl(this, this->lifetimeHint());
  }
  if (next == scene::NODE_FACT_RETIRED)
    this->node_ = 0;
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}
void ToolboxTextEditorContext::retireNativeProjection()
{
  if (this->scrollBarId_ && this->controller())
    this->controller()->destroyScrollBarControl(this->scrollBarId_, this->lifetimeHint());
}
short ToolboxTextEditorContext::offsetOf(LineCursor cursor) const
{
  if (!this->node_ || !this->te_)
    return 0;
  const int index = this->node_->props.lines_->find(cursor.line);
  TextLock text(this->te_);
  if (!text.bytes())
    return 0;
  short start = 0;
  for (int row = 0; row < index && start < (**this->te_).teLength; ++start)
    if (text.bytes()[start] == '\r')
      ++row;
  short end = start;
  while (end < (**this->te_).teLength && text.bytes()[end] != '\r')
    ++end;
  return static_cast<short>(start + std::max(0, std::min(cursor.column, static_cast<int>(end - start))));
}
LineCursor ToolboxTextEditorContext::cursorAt(short offset) const
{
  if (!this->node_ || !this->te_ || !this->node_->props.lines_->size())
    return LineCursor::None();
  TextLock text(this->te_);
  if (!text.bytes())
    return LineCursor::None();
  unsigned short row = 0;
  short start = 0;
  offset = std::max(static_cast<short>(0), std::min(offset, (**this->te_).teLength));
  for (short i = 0; i < offset; ++i)
    if (text.bytes()[i] == '\r')
    {
      ++row;
      start = i + 1;
    }
  if (row >= this->node_->props.lines_->size())
    return LineCursor::None();
  return LineCursor(this->node_->props.lines_->at(row).id, offset - start);
}
scene::FollowUp ToolboxTextEditorContext::project()
{
  if (this->phase_ != IDLE && this->phase_ != RECONCILE)
    return scene::FOLLOW_NONE;
  if (!this->te_ || !this->node_)
    return scene::FOLLOW_NONE;
  // Repair within the consumer keeps its exclusion through projection.
  const Phase completion = this->phase_;
  this->phase_ = PROJECT;
  std::size_t length = 0;
  this->status_ = this->node_->seam(this->key_).project(this->restore_, TextEditorProps::kMaxBytes, length);
  if (this->status_ == EDITOR_OK)
  {
    const Rect dest = (**this->te_).destRect;
    TESetText(this->restore_, static_cast<long>(length), this->te_);
    (**this->te_).destRect = dest;
    // TESetText has no result and TE has no undo history. Do not advance the
    // projection fact until the native record confirms the complete payload.
    if ((**this->te_).teLength != static_cast<short>(length))
      this->status_ = EDITOR_UNAVAILABLE;
    else
    {
      LineCursor caret = this->node_->props.cursorState()->get();
      if (this->node_->props.lines_->find(caret.line) < 0)
        caret = this->node_->props.lines_->size() ? LineCursor(this->node_->props.lines_->at(0).id, caret.column)
                                                  : LineCursor::None();
      const short offset = this->offsetOf(caret);
      TESetSelect(offset, offset, this->te_);
      this->source_ = this->node_->props.lines_;
      this->revision_ = this->source_->revision().get();
    }
  }
  this->phase_ = completion;
  return scene::REPAINT;
}
scene::FollowUp ToolboxTextEditorContext::restoreCommittedProjection()
{
  assert(this->phase_ == IDLE);
  ++this->restores_;
  return this->project();
}
/** Stack policy. Snapshots are values; context lookup follows the driver's lifetime wall. */
class ToolboxTextEditorContext::RailOperation : public scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(ToolboxTextEditorContext &ownerContext)
      : loka::app::scene::RailOperation<LineCursor>(*ownerContext.controller(), ownerContext.node_, &ownerContext),
        binding_(),
        follow_(),
        scroll_()
#ifdef TEST_BUILD
        ,
        before_(ownerContext.node_ && ownerContext.node_->props.cursorState()
                    ? ownerContext.node_->props.cursorState()->get() : LineCursor::None())
#endif
  {
  }
  void project(scene::Node &base)
  {
    this->follow_ = this->follow_.including(context(base).project());
  }
  void restore(scene::Node &base)
  {
    this->follow_ = this->follow_.including(context(base).restoreCommittedProjection());
  }
  void restoreScroll(const Rect &scroll)
  {
    this->scroll_ = scroll;
    this->follow_ = this->follow_.including(scene::SCROLL_CLEANUP);
  }
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<LineCursor> &request)
  {
    const scene::Admission admission = this->enterTake(base, static_cast<TextEditorNode &>(base).props.moveCaretTo_);
    if (admission != scene::ADMISSION_DEFERRED)
      request = static_cast<TextEditorNode &>(base).props.moveCaretTo_;
    return admission;
  }
  template <class Request>
  scene::Admission enterTake(scene::Node &base,
                             const scene::RequestBinding<Request> &request,
                             const scene::RequestBinding<LineCursor> *priority = 0)
  {
    ToolboxTextEditorContext &c = context(base);
    if (c.phase_ != IDLE || !c.node_)
      return scene::ADMISSION_DEFERRED;
    if (!request.isValid() || request.state()->get().isNone())
      return scene::ADMISSION_EMPTY;
    if (priority && priority->isValid() && !priority->state()->get().isNone())
      return scene::ADMISSION_DEFERRED;
    // Missing TE/unavailable status still takes, then refuses in resolve.
    this->binding_ = c.node_->props;
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
    ToolboxTextEditorContext &c = context(base);
    if (!this->current(base, request))
      return EDITOR_OWNER_MISMATCH;
    if (c.phase_ != INPUT)
      return EDITOR_REENTRANT;
    if (!c.te_)
      return EDITOR_UNAVAILABLE;
    if (c.status_ != EDITOR_OK)
      return c.status_;
    loka::core::StateTracker *owner = 0;
    if (!this->binding_.lines_ || this->binding_.lines_->queryMutationTracker(owner) != loka::core::EDIT_OK)
      return EDITOR_UNAVAILABLE;
    return request.usesTracker(owner) ? EDITOR_OK : EDITOR_OWNER_MISMATCH;
  }
  /** Arm cleanup at the pre-command viewport unless a refused key already
      did: a refused report must leave the view where the entry found it. */
  void armCommandScroll(scene::Node &base)
  {
    ToolboxTextEditorContext &c = context(base);
    if (!this->follow_.contains(scene::SCROLL_CLEANUP) && c.te_)
      this->restoreScroll((**c.te_).destRect);
  }
  void keepCommandScroll(scene::Node &base)
  {
    ToolboxTextEditorContext &c = context(base);
    if (this->follow_.contains(scene::SCROLL_CLEANUP) && c.te_)
      this->scroll_ = (**c.te_).destRect;
  }
  virtual EditorResult validate(scene::Node &base, const LineCursor &pending)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    const EditorResult ready = node.seam(context(base).key_).availability();
    if (ready != EDITOR_OK)
      return ready;
    return node.props.lines_->find(pending.line) < 0 ? EDITOR_STALE_ID : EDITOR_OK;
  }
  scene::FollowUp prepareApply(scene::Node &base)
  {
    ToolboxTextEditorContext &c = context(base);
    scene::FollowUp follow = scene::FOLLOW_NONE;
    // A take subscriber may edit the owner. Repair offsets under exclusion.
    if (c.source_ != c.node_->props.lines_ || c.hasStaleCaret())
    {
      c.phase_ = RECONCILE;
      follow = c.project();
      if (c.phase_ == RECONCILE)
        c.phase_ = INPUT;
    }
    return follow;
  }
  virtual scene::RequestApplication<LineCursor> apply(scene::Node &base, const LineCursor &pending)
  {
    ToolboxTextEditorContext &c = context(base);
    const scene::FollowUp follow = this->prepareApply(base);
    if (c.status_ != EDITOR_OK)
      return scene::RequestApplication<LineCursor>(pending, c.status_, follow);
    const short offset = c.offsetOf(pending);
    const LineCursor clamped = c.cursorAt(offset);
    TESetSelect(offset, offset, c.te_);
    return scene::RequestApplication<LineCursor>(clamped, EDITOR_OK, scene::REPAINT);
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return static_cast<TextEditorNode &>(base).seam(context(base).key_).moveCaret(applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<LineCursor> &reply,
                                     const scene::RequestApplication<LineCursor> &application)
  {
    ToolboxTextEditorContext &c = context(base);
    scene::FollowUp follow = scene::FOLLOW_NONE;
    if (c.node_ && c.te_ && c.status_ == EDITOR_OK && c.node_->lifecycleFact() == scene::NODE_FACT_ATTACHED)
    {
      if (c.phase_ == RECONCILE || c.source_ != c.node_->props.lines_ || c.hasStaleCaret())
      {
        c.phase_ = RECONCILE;
        // Project also restores selection from the post-report fact.
        follow = c.project();
      }
      else if (reply.kind() == scene::Reply<LineCursor>::REFUSED && application.result() == EDITOR_OK)
      {
        // Native apply succeeded, but the seam did not accept its caret fact.
        // Platform twin of Win32 finishTake: current text needs selection-only repair.
        const short offset = c.offsetOf(c.node_->props.cursorState()->get());
        TESetSelect(offset, offset, c.te_);
        follow = scene::REPAINT;
      }
    }
    if (c.phase_ == INPUT || c.phase_ == RECONCILE)
      c.phase_ = IDLE;
    return follow;
  }
  virtual scene::FollowUpResult finishSettle(scene::Node &base, const scene::FollowUps &follow)
  {
    ToolboxTextEditorContext &c = context(base);
    if (this->follow_.contains(scene::SCROLL_CLEANUP) && c.te_)
      (**c.te_).destRect = this->scroll_;
    if ((follow.contains(scene::REPAINT) || this->follow_.contains(scene::REPAINT)) && c.controller()
        && c.controller()->window_)
      c.controller()->window_->requestInvalidateRect(c.paintRect_);
    c.syncPresentation();
    // No timer arm: foreground idle retries directly from status_/TE presence.
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
    return node.lifecycleFact() == scene::NODE_FACT_ATTACHED && node.props.lines_ == this->binding_.lines_
           && node.props.cursorState() == this->binding_.cursorState();
  }
  static ToolboxTextEditorContext &context(scene::Node &node)
  {
    return *static_cast<ToolboxTextEditorContext *>(node.getContext());
  }
  TextEditorProps binding_;
  scene::FollowUps follow_;
  Rect scroll_;
#ifdef TEST_BUILD
  const LineCursor before_;
#endif
};
/** Plain TE has fixed-height visual lines. Count CR-delimited groups only
    when every wrapped visual line is inside the editor's unclipped text rectangle. */
bool ToolboxTextEditorContext::queryVisibleLines(unsigned &lines) const
{
  lines = 0;
  if (!this->te_ || (**this->te_).lineHeight <= 0)
    return false;
  TextLock text(this->te_);
  if (!text.bytes())
    return false;
  const TERec &te = **this->te_;
  const Rect textRect = this->geometry().text;
  const long height = te.lineHeight;
  short first = 0;
  for (short end = 1; end <= te.nLines; ++end)
  {
    const short offset = te.lineStarts[end];
    if (end != te.nLines && (offset == 0 || text.bytes()[offset - 1] != '\r'))
      continue;
    if (te.destRect.top + first * height >= textRect.top && te.destRect.top + end * height <= textRect.bottom)
      ++lines;
    first = end;
  }
  // TE's nLines excludes the empty visual line after a final CR (and empty text).
  if ((te.teLength == 0 || text.bytes()[te.teLength - 1] == '\r')
      && (te.nLines == 0 || te.lineStarts[te.nLines - 1] < te.teLength)
      && te.destRect.top + te.nLines * height >= textRect.top
      && te.destRect.top + (te.nLines + 1L) * height <= textRect.bottom)
    ++lines;
  return lines != 0;
}
void ToolboxTextEditorContext::scrollTo(LineCursor target)
{
  const short offset = this->offsetOf(target);
  const TERec &te = **this->te_;
  const Rect textRect = this->geometry().text;
  short visual = 0;
  while (visual < te.nLines && te.lineStarts[visual + 1] <= offset)
    ++visual;
  // The sentinel is a visual line only for empty text or a trailing CR.
  if (visual == te.nLines && visual && offset && target.column != 0)
    --visual;
  const long top = te.destRect.top + static_cast<long>(visual) * te.lineHeight;
  const long bottom = top + te.lineHeight;
  const long delta = top < textRect.top           ? textRect.top - top
                     : bottom > textRect.bottom ? textRect.bottom - bottom
                                                : 0;
  if (delta)
    TEScroll(0, static_cast<short>(delta), this->te_);
}
/** The command seat borrows the entry's caret pipeline and completion owner. */
class ToolboxTextEditorContext::CommandOperation : public scene::SeatOperation<EditorCommand, LineCursor>
{
public:
  explicit CommandOperation(RailOperation &rail)
      : rail_(rail)
  {
  }
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<EditorCommand> &request)
  {
    const TextEditorProps &props = static_cast<TextEditorNode &>(base).props;
    const scene::Admission admission = this->rail_.enterTake(base, props.command_, &props.moveCaretTo_);
    if (admission != scene::ADMISSION_DEFERRED)
      request = props.command_;
    return admission;
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
    ToolboxTextEditorContext &c = *static_cast<ToolboxTextEditorContext *>(base.getContext());
    return static_cast<TextEditorNode &>(base).seam(c.key_).availability();
  }
  virtual scene::RequestApplication<LineCursor> apply(scene::Node &base, const EditorCommand &pending)
  {
    ToolboxTextEditorContext &c = *static_cast<ToolboxTextEditorContext *>(base.getContext());
    unsigned visibleLines = 0;
    LineCursor target;
    const scene::FollowUp follow = this->rail_.prepareApply(base);
    if (c.status_ != EDITOR_OK)
      return scene::RequestApplication<LineCursor>(target, c.status_, follow);
    if (!c.queryVisibleLines(visibleLines))
      return scene::RequestApplication<LineCursor>(target, EDITOR_UNAVAILABLE, follow);
    const EditorResult result =
        static_cast<TextEditorNode &>(base).seam(c.key_).pageTarget(pending, visibleLines, target);
    if (result != EDITOR_OK)
      return scene::RequestApplication<LineCursor>(target, result, follow);
    const scene::RequestApplication<LineCursor> applied = this->rail_.apply(base, target);
    if (applied.result() == EDITOR_OK)
    {
      this->rail_.armCommandScroll(base);
      c.scrollTo(applied.value());
    }
    return applied;
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &applied)
  {
    return this->rail_.report(base, applied);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<EditorCommand> &reply,
                                     const scene::RequestApplication<LineCursor> &applied)
  {
    const scene::Reply<LineCursor> caretReply = reply.kind() == scene::Reply<EditorCommand>::REFUSED
                                                    ? scene::Reply<LineCursor>::Refused(applied.value(), reply.reason())
                                                    : scene::Reply<LineCursor>::Granted(applied.value());
    const scene::FollowUp follow = this->rail_.finishTake(base, caretReply, applied);
    if (reply.kind() == scene::Reply<EditorCommand>::GRANTED)
      this->rail_.keepCommandScroll(base);
    return follow;
  }

private:
  RailOperation &rail_;
};
#ifdef TEST_BUILD
scene::Admission loka::testing::ToolboxTextEditorAdmission::probe(
    ToolboxTextEditorContext &c, bool command, bool busy, bool &supplied, bool &opened)
{
  const ToolboxTextEditorContext::Phase before = c.phase_;
  c.phase_ = busy ? ToolboxTextEditorContext::RECONCILE : ToolboxTextEditorContext::IDLE;
  ToolboxTextEditorContext::RailOperation rail(c);
  scene::Admission admission;
  if (command)
  {
    ToolboxTextEditorContext::CommandOperation op(rail);
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
  opened = c.phase_ == ToolboxTextEditorContext::INPUT;
  c.phase_ = before;
  return admission;
}
#endif
void ToolboxTextEditorContext::settle(scene::Settlement stimulus)
{
  RailOperation op(*this);
  this->settle(stimulus, op);
}
void ToolboxTextEditorContext::settle(scene::Settlement stimulus, RailOperation &op)
{
  CommandOperation command(op);
  scene::RequestSettlement<LineCursor>::settle(op,
                                               op,
                                               command,
                                               stimulus
#ifdef TEST_BUILD
                                               ,
                                               op.before()
#endif
  );
}
namespace
{
  bool EditorShort(long value)
  {
    return value >= SHRT_MIN && value <= SHRT_MAX;
  }
  /** Stack snapshot of this TE's visual line range; never stored on a context. */
  struct EditorScrollMetrics
  {
    long line, maximumPixels, maximum, offset, clamped, value, page;
    bool read(TEHandle handle, const Rect &textRect)
    {
      if (!handle || EmptyRect(&textRect))
        return false;
      const TERec &te = **handle;
      TextLock text(handle);
      const long height = static_cast<long>(textRect.bottom) - textRect.top;
      this->line = te.lineHeight;
      if (!text.bytes() || te.teLength < 0 || te.nLines < 0 || this->line <= 0 || height < this->line)
        return false;
      const long extra = (te.teLength == 0 || text.bytes()[te.teLength - 1] == '\r')
                                 && (te.nLines == 0 || te.lineStarts[te.nLines - 1] < te.teLength)
                             ? 1
                             : 0;
      const long total = (static_cast<long>(te.nLines) + extra) * this->line;
      // Conservative supported range, not a mathematical requirement of line-unit controls.
      if (!EditorShort(total))
        return false;
      this->maximumPixels = std::max(0L, total - height);
      this->maximum = (this->maximumPixels + this->line - 1) / this->line;
      this->offset = static_cast<long>(textRect.top) - te.destRect.top;
      this->clamped = std::max(0L, std::min(this->offset, this->maximumPixels));
      this->value =
          this->maximumPixels > 0 && this->clamped == this->maximumPixels ? this->maximum : this->clamped / this->line;
      this->page = std::max(1L, height / this->line - 1);
      return EditorShort(this->maximumPixels) && EditorShort(this->maximum) && EditorShort(this->value)
             && EditorShort(this->page);
    }
    bool move(TEHandle te, long target) const
    {
      const long delta = this->offset - target;
      if (!EditorShort(delta) || !EditorShort((**te).destRect.top + delta)
          || !EditorShort((**te).destRect.bottom + delta))
        return false;
      if (delta)
        TEScroll(0, static_cast<short>(delta), te);
      return true;
    }
  };
} // namespace
void ToolboxTextEditorContext::syncPresentation()
{
  if (this->phase_ != IDLE || !this->controller() || !this->controller()->window_)
    return;
  ToolboxPortScope port(this->controller()->window_->window());
  EditorScrollMetrics metrics;
  if (!this->te_ || this->status_ != EDITOR_OK || !metrics.read(this->te_, this->geometry().text)
      || !metrics.move(this->te_, metrics.clamped))
  {
    this->controller()->presentEditorScrollBar(this->scrollBarId_, ToolboxEditorScrollBarPresentation());
    return;
  }
  if (metrics.offset != metrics.clamped)
    this->controller()->window_->requestInvalidateRect(this->paintRect_);
  this->controller()->presentEditorScrollBar(this->scrollBarId_,
                                             ToolboxEditorScrollBarPresentation(static_cast<short>(metrics.maximum),
                                                                                static_cast<short>(metrics.value),
                                                                                static_cast<short>(metrics.page)));
}
void ToolboxTextEditorContext::idleTick()
{
  if (!this->te_ || !this->controller() || !this->controller()->window_)
    return;
  ToolboxPortScope port(this->controller()->window_->window());
  const Rect before = (**this->te_).destRect;
  TEIdle(this->te_);
  if (!EqualRect(&before, &(**this->te_).destRect))
  {
    this->controller()->window_->requestInvalidateRect(this->paintRect_);
    this->syncPresentation();
  }
}
EditorResult ToolboxTextEditorContext::scrollInput(int value)
{
  RailOperation op(*this);
  const EditorResult ready = this->beginInput();
  if (ready != EDITOR_OK)
    return ready;
  ToolboxPortScope port(this->controller()->window_->window());
  EditorScrollMetrics metrics;
  if (metrics.read(this->te_, this->geometry().text))
  {
    const long target = std::min(std::max(0L, std::min(static_cast<long>(value), metrics.maximum)) * metrics.line,
                                 metrics.maximumPixels);
    if (metrics.move(this->te_, target) && target != metrics.offset)
      this->controller()->window_->requestInvalidateRect(this->paintRect_);
  }
  // Operation-only refusal is a successful no-op, never a document restore.
  return this->finishInput(EDITOR_OK, CARET_CHANGE, op);
}

void ToolboxTextEditorContext::retryProjection()
{
  // Called once by the foreground idle pass, outside scheduler drain callbacks.
  if (this->status_ != EDITOR_OK && this->te_)
  {
    RailOperation op(*this);
    op.project(*this->node_);
    this->settle(scene::SETTLE_DEFERRED, op);
  }
}
void ToolboxTextEditorContext::onPropsApplied()
{
  if (this->phase_ != IDLE)
    return;
  RailOperation op(*this);
  if (this->node_ && this->te_ && (this->source_ != this->node_->props.lines_ || this->hasStaleCaret()))
    op.project(*this->node_);
  this->settle(scene::SETTLE_PROPS, op);
}
bool ToolboxTextEditorContext::hasStaleCaret() const
{
  // TE still shows the last projected revision. Any unprojected owner edit,
  // structural or not, makes its native offsets name the wrong rows, so the
  // whole revision is the wall, not the survival of one cached identity.
  return this->revision_ != this->node_->props.lines_->revision().get();
}
EditorResult ToolboxTextEditorContext::beginInput()
{
  if (this->phase_ == PROJECT)
    return EDITOR_REENTRANT;
  if (this->phase_ != IDLE)
  {
    this->phase_ = RECONCILE;
    return EDITOR_REENTRANT;
  }
  if (!this->node_ || !this->te_ || this->status_ != EDITOR_OK)
  {
    const EditorResult result = !this->node_ || !this->te_ ? EDITOR_UNAVAILABLE : this->status_;
    this->settle(scene::SETTLE_INPUT);
    return result;
  }
  this->phase_ = INPUT;
  return EDITOR_OK;
}
EditorResult ToolboxTextEditorContext::finishInput(EditorResult result, Change change, RailOperation &op)
{
  const bool restore = result != EDITOR_OK || this->phase_ == RECONCILE;
  this->phase_ = IDLE;
  if (!this->te_ || !this->node_)
  {
    this->settle(scene::SETTLE_INPUT, op);
    return result;
  }
  if (restore)
    op.restore(*this->node_);
  else
  {
    const loka::core::ListRevision after = this->node_->props.lines_->revision().get();
    // A commit publishes exactly one content revision (and one structure
    // revision for split/join). Extra owner writes are not our native edit.
    const bool ownerChanged = this->source_ != this->node_->props.lines_
                              || after.content != this->revision_.content + (change == CARET_CHANGE ? 0 : 1)
                              || after.structure != this->revision_.structure + (change == STRUCTURE_CHANGE ? 1 : 0);
    if (ownerChanged || this->cursorAt((**this->te_).selStart) != this->node_->props.cursorState()->get())
      op.project(*this->node_);
    else
      this->revision_ = after;
  }
  this->settle(scene::SETTLE_INPUT, op);
  return result;
}
EditorResult ToolboxTextEditorContext::key(char key)
{
  RailOperation op(*this);
  EditorResult result = this->beginInput();
  if (result != EDITOR_OK)
    return result;
  const short start = (**this->te_).selStart, end = (**this->te_).selEnd;
  const Rect scroll = (**this->te_).destRect;
  // One controller call per key: scan this TE's CR prefix for each endpoint,
  // sharing the scan for an empty selection. No native line copy is needed.
  LineCursor from = this->cursorAt(start);
  const LineCursor to = start == end ? from : this->cursorAt(end);
  if (key == '\b' && start == end && start > 0)
    from = from.column > 0 ? LineCursor(from.line, from.column - 1) : this->cursorAt(start - 1);
  Change change = from.line != to.line || key == '\r' ? STRUCTURE_CHANGE : LINE_CHANGE;
  TEKey(key, this->te_);
  if (key >= 28 && key <= 31)
  {
    change = CARET_CHANGE;
    result = this->node_->seam(this->key_).moveCaret(this->cursorAt((**this->te_).selStart));
  }
  else if (key == '\b' && start == 0 && start == end)
  {
    change = CARET_CHANGE;
    result = EDITOR_OK;
  }
  else if (this->hasStaleCaret())
    result = EDITOR_STALE_ID;
  else
    result = this->node_->seam(this->key_).applyReplace(from, to, key == '\b' ? "" : &key, key == '\b' ? 0 : 1);
  if (!op.hasSameContext())
    return result;
  const bool refused = result != EDITOR_OK || this->phase_ == RECONCILE;
  if (refused)
    op.restoreScroll(scroll);
  return this->finishInput(result, change, op);
}
EditorResult ToolboxTextEditorContext::click(const Point &point)
{
  RailOperation op(*this);
  EditorResult result = this->beginInput();
  if (result != EDITOR_OK)
    return result;
  TEClick(point, false, this->te_);
  result = this->node_->seam(this->key_).moveCaret(this->cursorAt((**this->te_).selStart));
  if (!op.hasSameContext())
    return result;
  return this->finishInput(result, CARET_CHANGE, op);
}
EditorResult ToolboxTextEditorContext::paste(const char *bytes, std::size_t length)
{
  RailOperation op(*this);
  EditorResult result = this->beginInput();
  if (result != EDITOR_OK)
    return result;
  // Refuse before TE's signed-short storage can overflow.
  if (length > TextEditorProps::kMaxBytes)
    result = EDITOR_CAPACITY;
  else if (this->hasStaleCaret())
    result = EDITOR_STALE_ID;
  else
  {
    const short start = (**this->te_).selStart, end = (**this->te_).selEnd;
    const LineCursor from = this->cursorAt(start);
    const LineCursor to = start == end ? from : this->cursorAt(end);
    result = this->node_->seam(this->key_).applyReplace(from, to, bytes, length);
  }
  if (!op.hasSameContext())
    return result;
  result = this->finishInput(result, CARET_CHANGE, op);
  return result;
}
void ToolboxTextEditorContext::updateRect(const Rect &rect)
{
  const ToolboxTextEditorRects before = this->geometry();
  const Rect oldPaint = this->paintRect_;
  this->rect_ = rect;
  const ToolboxTextEditorRects geometry = this->geometry();
  this->paintRect_ = geometry.frame;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(geometry.frame, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (this->te_)
  {
    const Rect textRect = geometry.text;
    if (!EqualRect(&geometry.text, &before.text))
    {
      const Rect previousTextRect = before.text;
      const long dx = static_cast<long>((**this->te_).destRect.left) - previousTextRect.left;
      const long dy = static_cast<long>((**this->te_).destRect.top) - previousTextRect.top;
      if (EditorShort(textRect.left + dx) && EditorShort(textRect.right + dx) && EditorShort(textRect.top + dy)
          && EditorShort(textRect.bottom + dy))
      {
        const Rect destination = {static_cast<short>(textRect.top + dy),
                                  static_cast<short>(textRect.left + dx),
                                  static_cast<short>(textRect.bottom + dy),
                                  static_cast<short>(textRect.right + dx)};
        (**this->te_).destRect = destination;
        TECalText(this->te_);
      }
    }
    Rect view = textRect;
    if (this->controller() && !this->controller()->intersectWithProjectionClip(textRect, view))
      SetRect(&view, 0, 0, 0, 0);
    (**this->te_).viewRect = view;
  }
  if (!EqualRect(&oldPaint, &this->paintRect_) && this->controller() && this->controller()->window_)
  {
    this->controller()->window_->requestInvalidateRect(oldPaint);
    this->controller()->window_->requestInvalidateRect(this->paintRect_);
  }
}
scene::PaintAnswer ToolboxTextEditorContext::queryPaintDamage(const scene::PaintQuery &query) const
{
  if (query.placement != scene::PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return scene::PaintAnswer::nativeScheduled();
}
short ToolboxTextEditorContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  Rect rect;
  const long right = static_cast<long>(state.x) + state.width;
  const long bottom = static_cast<long>(state.y) + (state.height > 0 ? state.height : 80);
  if (EditorShort(right) && EditorShort(bottom))
    SetRect(&rect, state.x, state.y, static_cast<short>(right), static_cast<short>(bottom));
  else
    SetRect(&rect, state.x, state.y, state.x, state.y);
  this->updateRect(rect);
  // Without a TextEdit record, render's attach settle later in this pass
  // delivers the request (or refuses it when creation fails); settling here
  // would refuse a request render can still apply (#1141).
  if (this->te_)
    this->onPropsApplied();
  state.y = rect.bottom;
  return state.width;
}
void ToolboxTextEditorContext::repaint(TEHandle te)
{
  if (EmptyRect(&this->paintRect_))
    return;
  ToolboxPaintClip clip(this->paintRect_);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const Rect frame = this->geometry().frame;
  FrameRect(&frame);
  if (te && this->status_ == EDITOR_OK)
  {
    // TEUpdate redraws only the lines the text has. The ground below the last
    // line is ours, or a projection that shortens the text leaves the old
    // lines and highlight there (#1429). Only that band, so a repaint of a
    // full view does not flash.
    const TERec &record = **te;
    Rect below = record.viewRect;
    const long textBottom =
        static_cast<long>(record.destRect.top) + static_cast<long>(record.nLines) * record.lineHeight;
    if (textBottom > below.top)
      below.top = static_cast<short>(std::min(textBottom, static_cast<long>(below.bottom)));
    if (!EmptyRect(&below))
      loka::toolbox::ToolboxPaintGround(loka::app::SURFACE_GROUND_DOCUMENT, below);
    TEUpdate(&(**te).viewRect, te);
  }
  else
  {
    // This fallback stands in for the native editor's document ground.
    loka::toolbox::ToolboxPaintGround(loka::app::SURFACE_GROUND_DOCUMENT, frame);
    FrameRect(&frame);
    MoveTo(this->rect_.left + 3, this->rect_.top + 14);
    const char label[] = "Editor unavailable";
    DrawText(label, 0, sizeof(label) - 1);
  }
}
void ToolboxTextEditorContext::render(scene::IPlatformController *)
{
  if (!this->controller() || !this->node_)
    return;
  RailOperation op(*this);
  TEHandle te = this->controller()->ensureTextEditorControl(this, this->rect_, this->lifetimeHint());
  if (te != this->te_)
  {
    this->te_ = te;
    op.project(*this->node_);
    this->settle(scene::SETTLE_ATTACH, op);
  }
  else if (!te)
    this->settle(scene::SETTLE_ATTACH, op);
  if (op.hasSameContext())
  {
    if (!this->scrollBarId_)
      this->scrollBarId_ = this->controller()->allocateControlId();
    EditorScrollMetrics metrics;
    const bool measured =
        this->phase_ == IDLE && this->status_ == EDITOR_OK && metrics.read(this->te_, this->geometry().text);
    const ToolboxEditorScrollBarPresentation presentation =
        measured ? ToolboxEditorScrollBarPresentation(static_cast<short>(metrics.maximum),
                                                      static_cast<short>(metrics.value),
                                                      static_cast<short>(metrics.page))
                 : ToolboxEditorScrollBarPresentation();
    if (this->controller()->ensureEditorScrollBar(
            this->scrollBarId_, this->geometry().bar, this->te_ ? this : 0, presentation, this->lifetimeHint()))
      this->syncPresentation();
    this->repaint(this->te_);
  }
}
bool RegisterToolboxTextEditorNodeHandler(scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&handler);
}
