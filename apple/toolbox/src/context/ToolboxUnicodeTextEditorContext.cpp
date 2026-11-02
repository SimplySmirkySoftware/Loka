#include "ToolboxUnicodeTextEditorContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "ToolboxWindow.hpp"
#include "ToolboxUnicodeStartup.hpp"
#include "ToolboxArrivalProbe.hpp"
#include "app/nodes/controls/TextEditorDiff.hpp"
#include "app/nodes/controls/TextDocumentLine.hpp"
#include <Memory.h>
#include <Sound.h>
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
#include <cstdio>
#endif
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
      paintRect_(),
      phase_(IDLE),
      status_(EDITOR_UNAVAILABLE),
      projection_()
{
}
ToolboxUnicodeTextEditorContext::~ToolboxUnicodeTextEditorContext()
{
  assert(!this->unicode_ && "detach must revoke the borrowed Unicode presentation before reclaim");
}

namespace
{
  /** Temporary UTF-16 projection, owned inside one native call. */
  class UnicodeUnits
  {
  public:
    explicit UnicodeUnits(std::size_t length)
        : units_(static_cast<unsigned short *>(loka::core::LokaAllocRaw(length ? length * 2 : 2, site())))
    {
    }
    ~UnicodeUnits()
    {
      loka::core::LokaFreeRaw(this->units_, site());
    }
    unsigned short *get() const
    {
      return this->units_;
    }

  private:
    UnicodeUnits(const UnicodeUnits &);
    UnicodeUnits &operator=(const UnicodeUnits &);
    static loka::core::LokaAllocationSite site()
    {
      return loka::core::LokaAllocationSite("ToolboxUnicodeTextEditor", "Units");
    }
    unsigned short *units_;
  };
  /** Movable native storage never escapes the locked conversion interval. */
  class NativeText
  {
  public:
    NativeText()
        : handle_(0)
    {
    }
    ~NativeText()
    {
      if (this->handle_)
        DisposeHandle(this->handle_);
    }
    bool capture(TXNObject object, ByteCount size)
    {
      if (TXNGetData(object, kTXNStartOffset, kTXNEndOffset, &this->handle_) != noErr)
        return false;
      if (!this->handle_)
        return size == 0;
      if (GetHandleSize(this->handle_) != static_cast<long>(size))
        return false;
      HLock(this->handle_);
      return true;
    }
    const unsigned short *units() const
    {
      return this->handle_ ? reinterpret_cast<const unsigned short *>(*this->handle_) : 0;
    }

  private:
    NativeText(const NativeText &);
    NativeText &operator=(const NativeText &);
    Handle handle_;
  };
  std::size_t rowStart(const UnicodeLogicalText &text, int row)
  {
    std::size_t start = 0;
    for (int i = 0; i < row && start < text.length(); ++i)
      start = loka::app::detail::TextEditorLineEnd(text.bytes(), text.length(), start) + 1;
    return std::min(start, text.length());
  }
  RowCursor rowAt(const UnicodeLogicalText &text, unsigned long offset)
  {
    unsigned short row = 0;
    std::size_t start = 0;
    for (;;)
    {
      const std::size_t end = loka::app::detail::TextEditorLineEnd(text.bytes(), text.length(), start);
      const int units = loka::core::Utf16UnitsAt(text.bytes() + start, end - start, end - start);
      if (end == text.length() || offset <= static_cast<unsigned long>(units))
        return RowCursor(
            row,
            static_cast<int>(loka::core::Utf8BytesFloor(text.bytes() + start, end - start, static_cast<int>(offset))));
      offset -= units + 1; // MLTE CR is one unit; Win32's platform twin uses two.
      ++row;
      start = end + 1;
    }
  }
} // namespace
ToolboxUnicodeTextEditorContext::Selection ToolboxUnicodeTextEditorContext::selection() const
{
  Selection selected;
  TXNOffset start = 0, end = 0;
  TXNGetSelection(static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_)), &start, &end);
  selected.start = start;
  selected.end = end;
  return selected;
}
unsigned long ToolboxUnicodeTextEditorContext::offsetOf(LineCursor cursor, const UnicodeLogicalText &text) const
{
  const int row = this->node_->props.buffer_->lines().find(cursor.line);
  if (row < 0)
    return 0;
  const std::size_t start = rowStart(text, row);
  const std::size_t end = detail::TextEditorLineEnd(text.bytes(), text.length(), start);
  const std::size_t column = Utf8ClampColumnToBoundary(text.bytes() + start, end - start, cursor.column);
  return loka::core::Utf16UnitsAt(text.bytes(), text.length(), start + column);
}
LineCursor ToolboxUnicodeTextEditorContext::cursorAt(unsigned long offset) const
{
  const RowCursor row = rowAt(this->projection_.committed(), offset);
  const TextLines &lines = this->node_->props.buffer_->lines();
  return row.row < lines.size() ? LineCursor(lines.at(row.row).id, row.column) : LineCursor::None();
}
EditorResult ToolboxUnicodeTextEditorContext::project()
{
  if (this->phase_ != IDLE && this->phase_ != RECONCILE)
    return EDITOR_REENTRANT;
  if (!this->unicode_ || !this->node_ || !this->node_->props.buffer_)
    return this->status_ = EDITOR_UNAVAILABLE;
  const Phase completion = this->phase_;
  this->phase_ = PROJECT;
  ToolboxUnicodeProjection::Stamp staged;
  ToolboxUnicodeProjection::Preparation preparation(this->projection_, this->node_->props.buffer_->capacity().bytes);
  EditorResult result = preparation.stage(*this->node_, this->key_, staged);
  if (result == EDITOR_OK)
  {
    const UnicodeLogicalText &text = preparation.candidate();
    UnicodeUnits units(text.length());
    std::size_t count = 0;
    if (!units.get())
      result = EDITOR_ALLOCATION;
    else if (!loka::core::Utf8ToUtf16(text.bytes(), text.length(), units.get(), text.length(), count))
      result = EDITOR_NON_ASCII;
    else
    {
      TXNObject object = static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_));
      const OSStatus status =
          TXNSetData(object, kTXNUnicodeTextData, units.get(), count * 2, kTXNStartOffset, kTXNEndOffset);
      if (status != noErr || TXNDataSize(object) != count * 2)
        result = EDITOR_UNAVAILABLE;
      else
      {
        const LineCursor caret = this->node_->props.cursorState()->get();
        const unsigned long offset = this->offsetOf(caret, text);
        if (TXNSetSelection(object, offset, offset) != noErr)
          result = EDITOR_UNAVAILABLE;
        else
        {
          staged.changes = TXNGetChangeCount(object);
          preparation.publish(staged);
        }
      }
    }
  }
  this->phase_ = completion;
  return this->status_ = result;
}
void ToolboxUnicodeTextEditorContext::unavailable()
{
  this->status_ = EDITOR_UNAVAILABLE;
  this->controller()->retireTextEditorControl(this, this->lifetimeHint());
}
void ToolboxUnicodeTextEditorContext::repair(const Selection &before)
{
  this->phase_ = RECONCILE;
  const bool same = this->projection_.current(*this->node_);
  ToolboxUnicodeEditorLedger::Prepared fresh(this);
  if (!this->controller()->unicodeEditors_.replace(this->unicode_,
                                                   fresh,
                                                   this->controller()->window_,
                                                   ToolboxUnicodeLogicalFrame(this->chromeRect()),
                                                   ToolboxUnicodeEditorOptions())
      || this->project() != EDITOR_OK)
  {
    this->unavailable();
    return;
  }
  TXNObject object = static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_));
  const UnicodeLogicalText &text = this->projection_.committed();
  const unsigned long limit = loka::core::Utf16UnitsAt(text.bytes(), text.length(), text.length());
  const unsigned long start =
      same ? std::min(before.start, limit) : this->offsetOf(this->node_->props.cursorState()->get(), text);
  const unsigned long end = same ? std::min(before.end, limit) : start;
  if (TXNSetSelection(object, start, end) != noErr)
  {
    this->unavailable();
    return;
  }
  ToolboxInputDoor::syncNativeFocus(*this->controller(), NATIVE_FOCUS_NORMAL);
  this->controller()->window_->requestInvalidateRect(this->paintRect_);
}

/** Platform twin of TE RailOperation: take subscribers run before prepareApply. */
class ToolboxUnicodeTextEditorContext::RailOperation : public scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(ToolboxUnicodeTextEditorContext &context)
      : scene::RailOperation<LineCursor>(*context.controller(), context.node_, &context),
        binding_()
  {
  }
  static ToolboxUnicodeTextEditorContext &context(scene::Node &node)
  {
    return *static_cast<ToolboxUnicodeTextEditorContext *>(node.getContext());
  }
  virtual scene::Admission admit(scene::Node &base, scene::RequestBinding<LineCursor> &request)
  {
    ToolboxUnicodeTextEditorContext &c = context(base);
    request = static_cast<TextEditorNode &>(base).props.moveCaretTo_;
    if (c.phase_ != IDLE)
      return scene::ADMISSION_DEFERRED;
    if (!request.isValid() || request.state()->get().isNone())
      return scene::ADMISSION_EMPTY;
    this->binding_ = c.node_->props;
    c.phase_ = INPUT;
    return scene::ADMISSION_TAKE;
  }
  virtual bool current(scene::Node &base, const scene::RequestBinding<LineCursor> &request)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    return node.props.buffer_ == this->binding_.buffer_ && node.props.cursorState() == this->binding_.cursorState()
           && node.props.moveCaretTo_.same(request);
  }
  virtual EditorResult resolve(scene::Node &base, const scene::RequestBinding<LineCursor> &request)
  {
    ToolboxUnicodeTextEditorContext &c = context(base);
    if (!this->current(base, request))
      return EDITOR_OWNER_MISMATCH;
    if (c.phase_ != INPUT)
      return EDITOR_REENTRANT;
    if (!c.unicode_)
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
  void prepareApply(scene::Node &base)
  {
    ToolboxUnicodeTextEditorContext &c = context(base);
    if (!c.projection_.current(*c.node_))
    {
      c.phase_ = RECONCILE;
      const Selection selected = c.selection();
      if (c.project() != EDITOR_OK)
        c.repair(selected);
      c.phase_ = INPUT;
    }
  }
  virtual scene::RequestApplication<LineCursor, EditorResult> apply(scene::Node &base, const LineCursor &pending)
  {
    ToolboxUnicodeTextEditorContext &c = context(base);
    this->prepareApply(base);
    if (c.status_ != EDITOR_OK)
      return scene::RequestApplication<LineCursor, EditorResult>(pending, c.status_);
    const unsigned long offset = c.offsetOf(pending, c.projection_.committed());
    TXNObject object = static_cast<TXNObject>(c.controller()->unicodeEditors_.object(c.unicode_));
    const EditorResult result = TXNSetSelection(object, offset, offset) == noErr ? EDITOR_OK : EDITOR_UNAVAILABLE;
    if (!this->hasSameContext())
      return scene::RequestApplication<LineCursor, EditorResult>(pending, result);
    TXNShowSelection(object, false);
    return scene::RequestApplication<LineCursor, EditorResult>(c.cursorAt(offset), result, scene::REPAINT);
  }
  virtual EditorResult report(scene::Node &base, const LineCursor &after)
  {
    return static_cast<TextEditorNode &>(base).seam(context(base).key_).moveCaret(after);
  }
  virtual scene::FollowUp finishTake(scene::Node &base,
                                     const scene::Reply<LineCursor> &reply,
                                     const scene::RequestApplication<LineCursor, EditorResult> &application)
  {
    if (!this->hasSameContext())
      return scene::FOLLOW_NONE;
    ToolboxUnicodeTextEditorContext &c = context(base);
    c.phase_ = RECONCILE;
    if (c.node_ && c.unicode_ && c.node_->lifecycleFact() == scene::NODE_FACT_ATTACHED)
    {
      if (!c.projection_.current(*c.node_))
      {
        const Selection selected = c.selection();
        if (c.project() != EDITOR_OK)
          c.repair(selected);
      }
      else if (reply.kind() == scene::Reply<LineCursor>::REFUSED && application.result() == EDITOR_OK)
      {
        const unsigned long offset = c.offsetOf(c.node_->props.cursorState()->get(), c.projection_.committed());
        if (TXNSetSelection(static_cast<TXNObject>(c.controller()->unicodeEditors_.object(c.unicode_)), offset, offset)
            != noErr)
          c.unavailable();
      }
    }
    c.phase_ = IDLE;
    return scene::REPAINT;
  }
  virtual scene::FollowUpResult finishSettle(scene::Node &base, const scene::FollowUps &follow)
  {
    if (follow.contains(scene::REPAINT))
      context(base).controller()->window_->requestInvalidateRect(context(base).paintRect_);
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
  TextEditorProps binding_;
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
/** Platform twin of Win32 commitNativeChange/applyLines; whole-document capture. */
EditorResult ToolboxUnicodeTextEditorContext::readBack(const Selection &before, Captured &captured, RailOperation &op)
{
  if (!this->projection_.current(*this->node_))
    return EDITOR_STALE_ID;
  TXNObject object = static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_));
  const ByteCount size = TXNDataSize(object);
  if (size & 1)
    return EDITOR_UNAVAILABLE;
  const TextCapacity &capacity = this->node_->props.buffer_->capacity();
  std::size_t budget = 0;
  if (!text_document_detail::ExpandedSize(capacity.bytes, capacity.lines ? capacity.lines - 1 : 0, budget)
      || size / 2 > budget)
    return EDITOR_CAPACITY;
  NativeText native;
  if (!native.capture(object, size))
    return EDITOR_UNAVAILABLE;
  const Selection after = this->selection();
  // Before offsets can exceed the new length after deletion. Conversion's map
  // requires bounded offsets; rowAt also floors offsets in surrogate pairs.
  std::size_t offsets[4] = {std::min<std::size_t>(before.start, size / 2),
                            std::min<std::size_t>(before.end, size / 2),
                            std::min<std::size_t>(after.start, size / 2),
                            std::min<std::size_t>(after.end, size / 2)};
  {
    // No dereferenced Handle pointer survives into the allocating seam calls.
    const unsigned short *units = native.units();
    for (std::size_t i = 0; i < size / 2; ++i)
      if (units[i] == '\n')
        captured.normalized = true;
    const EditorResult conversion = this->projection_.convert(units, size / 2, offsets);
    if (conversion != EDITOR_OK)
      return conversion;
  }
  const UnicodeLogicalText &candidate = this->projection_.candidate();
  const UnicodeLogicalText &committed = this->projection_.committed();
  if (!text_document_detail::PayloadAdmits(candidate.bytes(), candidate.length()))
    return EDITOR_NON_ASCII;
  if (candidate.length() > capacity.bytes)
    return EDITOR_CAPACITY;
  // The pre-edit selection belongs to committed, even after a deletion shortens
  // native text. Only normalization rewrites it, never the new body length.
  const unsigned long hintOffset = captured.normalized ? offsets[1] : before.end;
  const RowCursor hint = rowAt(committed, hintOffset);
  const TextEditorLineDiff diff = DiffTextEditorLines(committed.bytes(),
                                                      committed.length(),
                                                      candidate.bytes(),
                                                      candidate.length(),
                                                      before.start == before.end ? hint.row : -1,
                                                      hint.column);
  const RowCursor caret = rowAt(candidate, offsets[2]);
  TextLines &lines = this->node_->props.buffer_->lines();
  if (!lines.size())
    return EDITOR_INVALID_CURSOR;
  if (!diff.before() && !diff.after())
  {
    const LineCursor cursor(lines.at(caret.row).id, caret.column);
    return cursor == this->node_->props.cursorState()->get() ? EDITOR_OK
                                                             : this->node_->seam(this->key_).moveCaret(cursor);
  }
  if (lines.size() - diff.before() + diff.after() > capacity.lines)
    return EDITOR_CAPACITY;
  const unsigned short last = static_cast<unsigned short>(diff.first() + diff.before() - 1);
  const std::size_t oldStart = rowStart(committed, last);
  const LineCursor from(lines.at(static_cast<unsigned short>(diff.first())).id, 0);
  const LineCursor to(
      lines.at(last).id,
      static_cast<int>(detail::TextEditorLineEnd(committed.bytes(), committed.length(), oldStart) - oldStart));
  const std::size_t start = rowStart(candidate, diff.first());
  const std::size_t end = detail::TextEditorLineEnd(
      candidate.bytes(), candidate.length(), rowStart(candidate, diff.first() + diff.after() - 1));
  const EditorResult result =
      this->node_->seam(this->key_).applyReplace(from, to, candidate.bytes() + start, end - start, caret);
  if (!op.hasSameContext())
    return result;
  captured.accepted = result == EDITOR_OK;
  // The document's replace is UPDATE(first) + REMOVE x (before - 1) + INSERT x
  // (after - 1) (TextEditorDocument.cpp ReplaceOpCursor), so the list's structure
  // revision advances whenever more than one row is touched on either side,
  // including a same-count replacement; the content revision always advances.
  captured.structure = diff.before() > 1 || diff.after() > 1;
  return result;
}
EditorResult ToolboxUnicodeTextEditorContext::finishInput(EditorResult result,
                                                          const Selection &before,
                                                          const Captured &captured,
                                                          RailOperation &op)
{
  if (!op.hasSameContext())
    return result;
  const bool refused = result != EDITOR_OK || this->phase_ == RECONCILE;
  this->phase_ = IDLE;
  if (this->unicode_ && this->node_)
  {
    if (refused)
      this->repair(before);
    else
    {
      const loka::core::ListRevision after = this->node_->props.buffer_->lines().revision().get();
      // One accepted applyReplace is one list batch: content +1, structure +1 only
      // when the line count changed (ObservableList batch revision, ObservableList.hpp
      // ~:444-458). Anything else between the pre-input stamp and now is an owner write,
      // including one made by an observer during the apply itself.
      const bool ownerChanged =
          captured.before.owner != this->node_->props.buffer_
          || after.content != captured.before.revision.content + (captured.accepted ? 1 : 0)
          || after.structure != captured.before.revision.structure + (captured.accepted && captured.structure ? 1 : 0);
      if (ownerChanged)
      {
        if (this->project() != EDITOR_OK)
          this->repair(before);
      }
      else
      {
        if (captured.accepted)
        {
          ToolboxUnicodeProjection::Stamp stamp(*this->node_);
          stamp.changes =
              TXNGetChangeCount(static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_)));
          this->projection_.publish(stamp);
        }
        if (captured.normalized && this->project() != EDITOR_OK)
        {
          // The edit is already committed and published: recover to its caret.
          // The pre-edit selection would now span the new text.
          Selection caret;
          caret.start = caret.end =
              this->offsetOf(this->node_->props.cursorState()->get(), this->projection_.committed());
          this->repair(caret);
        }
      }
    }
    this->phase_ = IDLE;
  }
  if (result == EDITOR_NON_ASCII)
    SysBeep(1);
  this->settle(scene::SETTLE_INPUT, op);
  return result;
}
EditorResult ToolboxUnicodeTextEditorContext::beginInput()
{
  if (this->phase_ == PROJECT)
    return EDITOR_REENTRANT;
  if (this->phase_ != IDLE)
  {
    this->phase_ = RECONCILE;
    return EDITOR_REENTRANT;
  }
  if (!this->node_ || !this->unicode_ || this->status_ != EDITOR_OK)
    return !this->node_ || !this->unicode_ ? EDITOR_UNAVAILABLE : this->status_;
  this->phase_ = INPUT;
  return EDITOR_OK;
}
EditorResult ToolboxUnicodeTextEditorContext::refuseInput()
{
  if (!this->controller() || !this->node_)
    return EDITOR_UNAVAILABLE;
  RailOperation operation(*this);
  this->settle(scene::SETTLE_INPUT, operation);
  return EDITOR_UNAVAILABLE;
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
  return EDITOR_OK;
}
EditorResult ToolboxUnicodeTextEditorContext::click(const Point &, const EventRecord &record)
{
  RailOperation op(*this);
  const EditorResult ready = this->beginInput();
  if (ready != EDITOR_OK)
  {
    if (this->phase_ == IDLE)
      this->settle(scene::SETTLE_INPUT, op);
    return ready;
  }
  const Selection before = this->selection();
  Captured captured(this->projection_.stamp());
  loka::toolbox::probe::ArrivalSample("click-before", 0);
  TXNClick(static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_)), &record);
  loka::toolbox::probe::ArrivalSample("click-after", 0);
  if (!op.hasSameContext())
    return EDITOR_UNAVAILABLE;
  const EditorResult result = this->readBack(before, captured, op);
  return this->finishInput(result, before, captured, op);
}
bool ToolboxUnicodeTextEditorContext::offerKeyEvent(const EventRecord &record)
{
  RailOperation op(*this);
  const EditorResult ready = this->beginInput();
  if (ready != EDITOR_OK)
  {
    if (this->phase_ == IDLE)
      this->settle(scene::SETTLE_INPUT, op);
    return true;
  }
  const Selection before = this->selection();
  Captured captured(this->projection_.stamp());
  loka::toolbox::probe::ArrivalSample("key-before", 0);
  TXNKeyDown(static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_)), &record);
  loka::toolbox::probe::ArrivalSample("key-after", 0);
  if (!op.hasSameContext())
    return true;
  const EditorResult result = this->readBack(before, captured, op);
  loka::toolbox::probe::ArrivalNote("key-readback result", result);
  this->finishInput(result, before, captured, op);
  loka::toolbox::probe::ArrivalSample("key-finished", 0);
  return true;
}
bool ToolboxUnicodeTextEditorContext::editVerb(EditVerb verb)
{
  RailOperation op(*this);
  const EditorResult ready = this->beginInput();
  if (ready != EDITOR_OK)
  {
    if (this->phase_ == IDLE)
      this->settle(scene::SETTLE_INPUT, op);
    return true;
  }
  const Selection before = this->selection();
  Captured captured(this->projection_.stamp());
  TXNObject object = static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_));
  EditorResult result = EDITOR_OK;
  switch (verb)
  {
  case EDIT_UNDO:
    TXNUndo(object);
    if (!op.hasSameContext())
      return true;
    result = this->readBack(before, captured, op);
    break;
  case EDIT_CUT:
    loka::toolbox::UnicodeText::noteNativeScrap(TXNCut(object));
    if (!op.hasSameContext())
      return true;
    result = this->readBack(before, captured, op); // even a failed Cut may have changed the body
    break;
  case EDIT_COPY:
    loka::toolbox::UnicodeText::noteNativeScrap(TXNCopy(object));
    break;
  case EDIT_PASTE:
    if (!loka::toolbox::UnicodeText::scrapReady() || !TXNIsScrapPastable())
    {
      SysBeep(1);
      break;
    }
    TXNPaste(object);
    if (!op.hasSameContext())
      return true;
    result = this->readBack(before, captured, op);
    break;
  case EDIT_SELECT_ALL:
    TXNSelectAll(object);
    if (!op.hasSameContext())
      return true;
    {
      const LineCursor cursor = this->cursorAt(this->selection().start);
      if (cursor != this->node_->props.cursorState()->get())
        result = this->node_->seam(this->key_).moveCaret(cursor);
    }
    break;
  }
  this->finishInput(result, before, captured, op);
  return true;
}
void ToolboxUnicodeTextEditorContext::onPropsApplied()
{
  if (this->phase_ == IDLE && this->controller() && this->node_)
  {
    RailOperation operation(*this);
    if (this->unicode_ && !this->projection_.current(*this->node_))
      if (this->project() != EDITOR_OK)
        this->repair(Selection());
    this->phase_ = IDLE;
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
  this->projection_.clear();
}
void ToolboxUnicodeTextEditorContext::retryProjection() {}
void ToolboxUnicodeTextEditorContext::idleTick()
{
  if (this->unicode_)
  {
    this->controller()->unicodeEditors_.idle(this->unicode_);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    // Best-effort focused-row sample only; this is not an input/settle door.
    static unsigned long outsideDoorSamples = 0;
    TXNObject object = static_cast<TXNObject>(this->controller()->unicodeEditors_.object(this->unicode_));
    const ItemCount changes = TXNGetChangeCount(object);
    if (this->phase_ == IDLE && changes != this->projection_.stamp().changes)
      std::fprintf(stderr,
                   "[unicode-outside-door] object=%p delta=%lu samples=%lu\n",
                   static_cast<void *>(object),
                   static_cast<unsigned long>(changes - this->projection_.stamp().changes),
                   ++outsideDoorSamples);
#endif
  }
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
  if (resource && (changed || this->status_ != EDITOR_OK || !this->projection_.current(*this->node_)))
  {
    if (this->project() != EDITOR_OK)
      this->repair(Selection());
    this->phase_ = IDLE;
  }
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
