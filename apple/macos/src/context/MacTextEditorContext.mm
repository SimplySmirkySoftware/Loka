#include "app/nodes/controls/TextEditorRequests.hpp"
#include "MacTextEditorContext.hpp"
#include "../MacInputDoor.hpp"
#include "app/nodes/controls/TextChangeSpan.hpp"
#include "app/nodes/controls/TextEditorDiff.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include "app/nodes/controls/TextDocumentLine.hpp"
#include "core/LokaAlloc.hpp"
#include <algorithm>
#include <new>
#include <cstdio>

using namespace loka::app;
using namespace loka::core;

namespace
{
  /** Checked UTF-16 scalar read, bounded by the borrowed snapshot or row. */
  bool ReadNativeScalar(NSString *text, NSUInteger end, NSUInteger &offset, unsigned int &scalar)
  {
    if (offset >= end)
      return false;
    const unichar first = [text characterAtIndex:offset++];
    if (!first || (first >= 0xDC00 && first <= 0xDFFF))
      return false;
    scalar = first;
    if (first >= 0xD800 && first <= 0xDBFF)
    {
      if (offset == end)
        return false;
      const unichar second = [text characterAtIndex:offset++];
      if (second < 0xDC00 || second > 0xDFFF)
        return false;
      scalar = 0x10000 + ((first - 0xD800) << 10) + second - 0xDC00;
    }
    return true;
  }

  std::size_t ScalarBytes(unsigned int scalar)
  {
    return scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
  }

  /** Projection-owned row storage, admitted once per capacity before native input.
      Replacement is atomic; callbacks only borrow the installed buffers. */
  class RowBuffers
  {
  public:
    RowBuffers()
        : before_(0),
          after_(0),
          ids_(0),
          capacity_(0)
    {
    }
    ~RowBuffers()
    {
      this->clear();
    }
    bool matches(unsigned short capacity) const
    {
      return capacity != 0 && this->capacity_ == capacity;
    }
    bool admit(unsigned short capacity)
    {
      if (this->capacity_ == capacity)
        return capacity != 0;
      if (!capacity)
        return false;
      NSRange *before = static_cast<NSRange *>(LokaAllocRaw(sizeof(NSRange) * capacity, Site()));
      NSRange *after = before ? static_cast<NSRange *>(LokaAllocRaw(sizeof(NSRange) * capacity, Site())) : 0;
      ItemId *ids = after ? static_cast<ItemId *>(LokaAllocRaw(sizeof(ItemId) * capacity, Site())) : 0;
      if (!ids)
      {
        LokaFreeRaw(after, Site());
        LokaFreeRaw(before, Site());
        return false;
      }
      for (unsigned short i = 0; i < capacity; ++i)
        new (ids + i) ItemId(ItemId::none());
      this->clear();
      this->before_ = before;
      this->after_ = after;
      this->ids_ = ids;
      this->capacity_ = capacity;
      return true;
    }
    void clear()
    {
      for (unsigned short i = 0; i < this->capacity_; ++i)
        this->ids_[i].~ItemId();
      LokaFreeRaw(this->ids_, Site());
      LokaFreeRaw(this->after_, Site());
      LokaFreeRaw(this->before_, Site());
      this->ids_ = 0;
      this->after_ = this->before_ = 0;
      this->capacity_ = 0;
    }
    NSRange *before() const
    {
      return this->before_;
    }
    NSRange *after() const
    {
      return this->after_;
    }
    ItemId &id(unsigned short index)
    {
      assert(index < this->capacity_);
      return this->ids_[index];
    }

  private:
    static LokaAllocationSite Site()
    {
      return LokaAllocationSite("TextEditor", "Rows");
    }
    RowBuffers(const RowBuffers &);
    RowBuffers &operator=(const RowBuffers &);
    NSRange *before_;
    NSRange *after_;
    ItemId *ids_;
    unsigned short capacity_;
  };

  /** Logical CR/LF/CRLF rows, including a final empty row; never Cocoa lines.
      Main-thread scans borrow Projection storage sequentially. INPUT/APPLYING
      excludes reentrant notifications; consumers finish reading before seam
      publication or native setters can invoke other work. */
  struct NativeLines
  {
    NSRange *const ranges;
    unsigned short count;
    EditorResult result;
    NativeLines(NSString *text, const TextCapacity &capacity, NSRange *buffer)
        : ranges(buffer),
          count(0),
          result(buffer ? EDITOR_OK : EDITOR_UNAVAILABLE)
    {
      // No installed row storage (cleared projection): refuse before any write.
      if (!buffer)
        return;
      const NSUInteger length = [text length];
      std::size_t budget;
      if (!text_document_detail::ExpandedSize(capacity.bytes, capacity.lines ? capacity.lines - 1 : 0, budget)
          || length > budget)
      {
        this->result = EDITOR_CAPACITY;
        return;
      }
      NSUInteger offset = 0;
      for (;;)
      {
        if (this->count == capacity.lines)
        {
          this->result = EDITOR_CAPACITY;
          return;
        }
        const NSUInteger start = offset;
        NSUInteger contentsEnd = offset;
        while (offset < length)
        {
          const NSUInteger scalarStart = offset;
          unsigned int scalar;
          if (!ReadNativeScalar(text, length, offset, scalar))
          {
            this->result = EDITOR_NON_ASCII;
            return;
          }
          if (scalar == '\r' || scalar == '\n')
          {
            contentsEnd = scalarStart;
            if (scalar == '\r' && offset < length && [text characterAtIndex:offset] == '\n')
              ++offset;
            break;
          }
          contentsEnd = offset;
        }
        this->ranges[this->count++] = NSMakeRange(start, contentsEnd - start);
        if (contentsEnd == length)
          return;
        // A separator at EOF starts one more iteration for the empty row.
      }
    }
    unsigned short lineAt(NSUInteger offset) const
    {
      unsigned short i = 0;
      while (i + 1 < this->count && offset >= this->ranges[i + 1].location)
        ++i;
      return i;
    }
  };

  /** Immutable borrowed row from a successful NativeLines scan; no retained mapping or encoding buffer. */
  class NativeRow
  {
  public:
    NativeRow(NSString *text, NSRange range)
        : text_(text),
          range_(range)
    {
    }
    NSUInteger unitsAt(std::size_t bytes) const
    {
      NSUInteger offset = this->range_.location;
      std::size_t consumed = 0;
      while (offset < NSMaxRange(this->range_) && consumed < bytes)
      {
        const NSUInteger start = offset;
        unsigned int scalar;
        if (!ReadNativeScalar(this->text_, NSMaxRange(this->range_), offset, scalar)
            || ScalarBytes(scalar) > bytes - consumed)
          return start - this->range_.location;
        consumed += ScalarBytes(scalar);
      }
      return offset - this->range_.location;
    }
    std::size_t bytesFloor(NSUInteger units) const
    {
      return this->bytesAt(units, false);
    }
    std::size_t bytesCeil(NSUInteger units) const
    {
      return this->bytesAt(units, true);
    }
    std::size_t byteLength() const
    {
      return this->bytesFloor(this->range_.length);
    }
    bool appendUtf8(std::string &out) const
    {
      NSUInteger offset = this->range_.location;
      while (offset < NSMaxRange(this->range_))
      {
        unsigned int scalar;
        if (!ReadNativeScalar(this->text_, NSMaxRange(this->range_), offset, scalar))
          return false;
        const std::size_t width = ScalarBytes(scalar);
        char bytes[4];
        unsigned int remaining = scalar;
        for (std::size_t i = width - 1; i > 0; --i)
        {
          bytes[i] = static_cast<char>(0x80 | (remaining & 0x3F));
          remaining >>= 6;
        }
        bytes[0] = static_cast<char>(remaining | (width == 1 ? 0 : width == 2 ? 0xC0 : width == 3 ? 0xE0 : 0xF0));
        out.append(bytes, width);
      }
      return true;
    }

  private:
    std::size_t bytesAt(NSUInteger units, bool ceil) const
    {
      const NSUInteger end = this->range_.location + std::min(units, this->range_.length);
      NSUInteger offset = this->range_.location;
      std::size_t bytes = 0;
      while (offset < end)
      {
        unsigned int scalar;
        if (!ReadNativeScalar(this->text_, NSMaxRange(this->range_), offset, scalar))
          break;
        if (offset > end && !ceil)
          break;
        bytes += ScalarBytes(scalar);
      }
      return bytes;
    }
    NSString *const text_;
    const NSRange range_;
  };

  bool SameAsModel(NSString *native, const std::string &scratch)
  {
    if (!native)
      return false;
    NSUInteger units = 0;
    std::size_t bytes = 0;
    const NSUInteger length = [native length];
    while (units < length && bytes < scratch.size())
    {
      unsigned int actual, expected;
      if (!ReadNativeScalar(native, length, units, actual)
          || !loka::core::detail::DecodeUtf8Codepoint(scratch.data(), scratch.size(), bytes, expected)
          || actual != (expected == '\r' ? '\n' : expected))
        return false;
    }
    return units == length && bytes == scratch.size();
  }

  /** One changed projection's refusing bytes; NSString copies before destruction. */
  class NativeTextBytes
  {
  public:
    explicit NativeTextBytes(const std::string &scratch)
        : length_(scratch.size()),
          bytes_(scratch.empty() ? 0 : static_cast<char *>(LokaAllocRaw(scratch.size(), Site())))
    {
      if (this->bytes_)
        for (std::size_t i = 0; i < scratch.size(); ++i)
          this->bytes_[i] = scratch[i] == '\r' ? '\n' : scratch[i];
    }
    ~NativeTextBytes()
    {
      LokaFreeRaw(this->bytes_, Site());
    }
    NSString *copyString() const
    {
      if (this->length_ && !this->bytes_)
        return nil;
      return [[NSString alloc] initWithBytes:this->length_ ? this->bytes_ : ""
                                      length:this->length_
                                    encoding:NSUTF8StringEncoding];
    }

  private:
    static LokaAllocationSite Site()
    {
      return LokaAllocationSite("MacTextEditor", "NativeTextBytes");
    }
    NativeTextBytes(const NativeTextBytes &);
    NativeTextBytes &operator=(const NativeTextBytes &);
    const std::size_t length_;
    char *const bytes_;
  };

  /** Storage infers the insertion end; view notifications report a selection. */
  std::size_t NativeCaretBytes(const NativeRow &row, NSUInteger units, MacTextEditorContext::TextObservation origin)
  {
    return origin == MacTextEditorContext::STORAGE_EDIT ? row.bytesCeil(units) : row.bytesFloor(units);
  }

  std::size_t ClampedColumn(const TextLines &lines, unsigned short index, int column)
  {
    const text_document_detail::LineBytes row(lines.at(index).value, lines.capacity());
    const std::size_t bytes = std::min(row.size(), static_cast<std::size_t>(std::max(0, column)));
    return Utf8RetreatToBoundary(row.data(), row.size(), bytes);
  }

  bool EqualLine(NSString *a, NSRange ar, NSString *b, NSRange br)
  {
    if (ar.length != br.length)
      return false;
    for (NSUInteger i = 0; i < ar.length; ++i)
      if ([a characterAtIndex:ar.location + i] != [b characterAtIndex:br.location + i])
        return false;
    return true;
  }

  /** Borrowed indexed characters for the rail-neutral column diff. */
  class NativeCharacters
  {
  public:
    NativeCharacters(NSString *text, NSUInteger offset)
        : text_(text),
          offset_(offset)
    {
    }
    unichar operator[](std::size_t index) const
    {
      return [this->text_ characterAtIndex:this->offset_ + index];
    }
  private:
    NSString *text_;
    NSUInteger offset_;
  };

  // The attributes every unstyled editor character carries: the default font
  // and the system text color. Typing, full replacement and the per-line reset
  // all start from this one dictionary; a font-only dictionary drops the color
  // and draws black on the dark appearance's background (#1182).
  NSDictionary *EditorBaseAttributes(NSFont *font)
  {
    NSColor *color = [NSColor textColor];
    if (!font)
      return [NSDictionary dictionaryWithObject:color forKey:NSForegroundColorAttributeName];
    return [NSDictionary dictionaryWithObjectsAndKeys:font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
  }

  void InstallEditorFont(NSTextView *view, MacScenePlatformController &controller)
  {
    NSFont *font = (NSFont *)controller.textFont(TextStyle() + Monospace);
    if (!font)
      return;
    [view setFont:font];
    NSDictionary *attributes = EditorBaseAttributes(font);
    NSTextStorage *storage = [view textStorage];
    [storage beginEditing];
    [storage addAttributes:attributes range:NSMakeRange(0, [storage length])];
    [storage endEditing];
    [view setTypingAttributes:attributes];
  }

  void ReplaceEditorString(NSTextView *view, NSString *text, MacScenePlatformController &controller)
  {
    [view setString:text];
    // Full replacement resets attributes, including those on line separators.
    InstallEditorFont(view, controller);
  }

  class MacTextEditorHandler
      : public loka::app::scene::RetainedNodeHandler<MacTextEditorHandler,
                                                     loka::app::TextEditorNode,
                                                     MacTextEditorContext>
  {
  public:
    static loka::app::TextEditorNode *cast(loka::app::scene::Node *node)
    {
      return node && node->nodeTypeKey() == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>()
                 ? static_cast<loka::app::TextEditorNode *>(node)
                 : 0;
    }
    static MacTextEditorContext *create(loka::app::TextEditorNode *node,
                                        loka::app::scene::IPlatformController *controller,
                                        const loka::app::scene::LayoutState &)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      MacTextEditorContext *context = new (std::nothrow) MacTextEditorContext(mac, mac->projectionParentView(), node, seamKey());
      if (context && !context->hasNativeView())
      {
        delete context;
        return 0;
      }
      return context;
    }
  };
  MacTextEditorHandler handler;
} // namespace

/** Native participant mark; behavior remains NSTextView's, except that the
    editor accepts no input-method composition (#862). A refused composition
    would leave marked text in a view the refusal makes read-only, and AppKit
    commits stale marked text through insertText:, which a read-only view
    declines, so the commit retries until the stack overflows (#1264). The
    composition is abandoned before it marks anything; text the input method
    commits still arrives through insertText: and is refused like a keystroke. */
@interface LokaTextEditorView : NSTextView
@end
@implementation LokaTextEditorView
- (void)abandonComposition
{
  // NSTextInputContext arrived in 10.6. Earlier systems only see the marking refused.
  if ([self respondsToSelector:@selector(inputContext)])
    [[self inputContext] discardMarkedText];
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selection replacementRange:(NSRange)replacement
{
  (void)string;
  (void)selection;
  (void)replacement;
  [self abandonComposition];
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selection
{
  (void)string;
  (void)selection;
  [self abandonComposition];
}
@end

/** Informal text view/storage delegate selectors also work on ObjC1 SDKs. */
@interface LokaTextEditorDelegate : NSObject
{
  MacTextEditorContext *owner_;
}
@property(nonatomic, assign) MacTextEditorContext *owner;
- (void)restoreProjection;
- (void)applyHighlights;
@end
@implementation LokaTextEditorDelegate
@synthesize owner = owner_;
- (void)textDidChange:(NSNotification *)notification
{
  NSTextView *view = (NSTextView *)[notification object];
  if ([self owner])
    MacInputDoor::textEditorChange(*[self owner], MacTextEditorContext::VIEW_CHANGE, [view selectedRange].location);
}
- (void)textStorageDidProcessEditing:(NSNotification *)notification
{
  NSTextStorage *storage = (NSTextStorage *)[notification object];
  if (([storage editedMask] & NSTextStorageEditedCharacters) && [self owner])
  {
    // Highlighting is deferred: this context applies no attributes inside
    // processEditing, so our styling no longer widens this character-edit range.
    // It still unions all character edits in the pass: evidence of where,
    // not of how much changed. Selection is not final here.
    MacInputDoor::textEditorChange(*[self owner], MacTextEditorContext::STORAGE_EDIT, [storage editedRange].location);
  }
}
- (void)textViewDidChangeSelection:(NSNotification *)notification
{
  (void)notification;
  if ([self owner])
    MacInputDoor::textEditorSelection(*[self owner]);
}
- (BOOL)textView:(NSTextView *)view shouldChangeTextInRange:(NSRange)range replacementString:(NSString *)text
{
  (void)view;
  (void)range;
  (void)text;
  if ([self owner])
    MacInputDoor::textEditorCapture(*[self owner]);
  return YES;
}
- (void)applyHighlights
{
  if ([self owner])
    MacInputDoor::textEditorHighlights(*[self owner]);
}
- (void)restoreProjection
{
  if ([self owner])
    MacInputDoor::textEditorRestore(*[self owner]);
}
@end

/** All tentative state shares one phase. The retained payload exists before
    native input; refusing an unchanged document needs no new NSString. */
struct MacTextEditorContext::Projection
{
  enum Phase
  {
    IDLE,
    INPUT,
    RECONCILE,
    STORAGE_PENDING,
    QUEUED,
    APPLYING,
    UNAVAILABLE
  };
  struct Styles
  {
    ItemId id;
    LineHighlight highlight;
    Styles *next;
    Styles(ItemId key, const String &text, const LineHighlighter *policy)
        : id(key),
          highlight(text, policy),
          next(0)
    {
    }
  };
  Phase phase;
  NSString *committed;
  std::string scratch;
  RowBuffers rows;
  NSRange selection;
  Styles *styles;
  Projection()
      : phase(UNAVAILABLE),
        committed(nil),
        selection(NSMakeRange(0, 0)),
        styles(0)
  {
    this->scratch.reserve(8192);
  }
  ~Projection()
  {
    this->clear();
  }
  void clearStyles()
  {
    while (this->styles)
    {
      Styles *row = this->styles;
      this->styles = row->next;
      delete row;
    }
  }
  void clear()
  {
    [this->committed release];
    this->committed = nil;
    this->clearStyles();
    this->rows.clear();
  }
  /** Validate the snapshot before consuming native text or deferred styling. */
  EditorResult validateDocument(TextEditorNode &node, const loka::app::scene::SeamKey<TextEditorNode> &key)
  {
    const EditorResult available = node.seam(key).project(this->scratch);
    if (available != EDITOR_OK)
      return available;
    if (!SameAsModel(this->committed, this->scratch))
      return EDITOR_STALE_ID;
    if (!this->rows.matches(node.props.buffer_->capacity().lines))
      return EDITOR_STALE_ID;
    const TextLines &committedLines = node.props.buffer_->lines();
    for (unsigned short i = 0; i < committedLines.size(); ++i)
      if (this->rows.id(i) != committedLines.at(i).id)
        return EDITOR_STALE_ID;
    return EDITOR_OK;
  }
  void style(NSTextView *view, loka::app::TextEditorNode &node, MacScenePlatformController &controller, bool force)
  {
    // APPLYING excludes native notifications throughout this before-buffer borrow.
    NativeLines lines([view string], node.props.buffer_->capacity(), this->rows.before());
    if (lines.result != EDITOR_OK)
      return;
    Styles *old = this->styles;
    this->styles = 0;
    Styles **tail = &this->styles;
    NSTextStorage *storage = [view textStorage];
    [storage beginEditing];
    for (unsigned short i = 0; i < node.props.buffer_->lines().size(); ++i)
    {
      const TextLines::Entry &entry = node.props.buffer_->lines().at(i);
      Styles **slot = &old;
      while (*slot && (*slot)->id != entry.id)
        slot = &(*slot)->next;
      Styles *row = *slot;
      bool changed = true;
      if (row)
      {
        *slot = row->next;
        const LineHighlight next(entry.value, node.props.highlighter_, &row->highlight);
        changed = next.value() != row->highlight.value();
        row->highlight = next;
      }
      else
        row = new (std::nothrow) Styles(entry.id, entry.value, node.props.highlighter_);
      if (force || changed || !row)
      {
        NSDictionary *defaults = EditorBaseAttributes((NSFont *)controller.textFont(TextStyle() + Monospace));
        [storage setAttributes:defaults range:lines.ranges[i]];
        if (row && row->highlight.value().valid())
        {
          const AttributedString &value = row->highlight.value();
          std::string joined;
          bool valid = true;
          for (std::size_t run = 0; run < value.segmentCount(); ++run)
          {
            std::string bytes;
            if (!loka::platform::CollectUtf8(value.segment(run).text, bytes))
              valid = false;
            joined += bytes;
          }
          NSString *styled = [[[NSString alloc] initWithBytes:joined.data()
                                                       length:joined.size()
                                                     encoding:NSUTF8StringEncoding] autorelease];
          if (valid && styled && EqualLine(styled, NSMakeRange(0, [styled length]), [view string], lines.ranges[i]))
          {
            NSUInteger offset = lines.ranges[i].location;
            for (std::size_t run = 0; run < value.segmentCount(); ++run)
            {
              std::string bytes;
              NSFont *runFont = (NSFont *)controller.textFont(value.segment(run).style + Monospace);
              if (!runFont || !loka::platform::CollectUtf8(value.segment(run).text, bytes))
              {
                valid = false;
                break;
              }
              NSString *runText = [[[NSString alloc] initWithBytes:bytes.data()
                                                            length:bytes.size()
                                                          encoding:NSUTF8StringEncoding] autorelease];
              const NSUInteger rowEnd = NSMaxRange(lines.ranges[i]);
              const NSUInteger runLength = [runText length];
              if (!runText || offset < lines.ranges[i].location || offset > rowEnd || runLength > rowEnd - offset)
              {
                valid = false;
                break;
              }
              [storage addAttribute:NSFontAttributeName value:runFont range:NSMakeRange(offset, runLength)];
              offset += runLength;
            }
            if (!valid)
              [storage setAttributes:defaults range:lines.ranges[i]];
          }
        }
      }
      if (row)
      {
        row->next = 0;
        *tail = row;
        tail = &row->next;
      }
    }
    [storage endEditing];
    while (old)
    {
      Styles *next = old->next;
      delete old;
      old = next;
    }
  }
};

/** Stack policy, parallel to Toolbox/Win32. Never retains a context borrow. */
class MacTextEditorContext::RailOperation : public loka::app::scene::RailOperation<LineCursor>
{
public:
  explicit RailOperation(MacTextEditorContext &ownerContext)
      : loka::app::scene::RailOperation<LineCursor>(*ownerContext.controller(), ownerContext.node_, &ownerContext),
        binding_(),
        completion_(Projection::UNAVAILABLE),
        follow_()
#ifdef TEST_BUILD
        ,
        before_(ownerContext.node_ && ownerContext.node_->props.cursorState()
                    ? ownerContext.node_->props.cursorState()->get() : LineCursor::None())
#endif
  {
  }
  void sync(bool force, bool nativeCommit = false)
  {
    this->follow_ = this->follow_.including(context(*this->node()).syncFromNode(*this, force, nativeCommit));
  }
  void restore()
  {
    this->follow_ = this->follow_.including(context(*this->node()).prepareRestore());
  }
  void highlights()
  {
    context(*this->node()).projectHighlights(*this);
    // Keep the highlight operation's follow-up intent through both takes:
    // an unavailable completion needs restoration instead of another style pass.
    this->follow_ = this->follow_.including(loka::app::scene::SCHEDULE_HIGHLIGHTS);
  }
  void deferHighlights()
  {
    context(*this->node()).projection_->phase = Projection::STORAGE_PENDING;
    this->follow_ = this->follow_.including(loka::app::scene::SCHEDULE_HIGHLIGHTS);
  }
  virtual loka::app::scene::Admission admit(loka::app::scene::Node &base,
                                            loka::app::scene::RequestBinding<LineCursor> &request)
  {
    return this->enterTake(base, request, &TextEditorProps::moveCaretTo_);
  }
  template <class Request>
  loka::app::scene::Admission enterTake(
      loka::app::scene::Node &base, loka::app::scene::RequestBinding<Request> &request,
      loka::app::scene::RequestBinding<Request> TextEditorProps::*seat,
      const loka::app::scene::RequestBinding<LineCursor> *priority = 0)
  {
    MacTextEditorContext &c = context(base);
    Projection &p = *c.projection_;
    if (!c.node_ || (p.phase != Projection::IDLE && p.phase != Projection::UNAVAILABLE))
      return loka::app::scene::ADMISSION_DEFERRED;
    request = c.node_->props.*seat;
    if (!request.isValid() || request.state()->get().isNone())
      return loka::app::scene::ADMISSION_EMPTY;
    if (priority && priority->isValid() && !priority->state()->get().isNone())
      return loka::app::scene::ADMISSION_DEFERRED;
    this->binding_ = c.node_->props;
    this->completion_ = p.phase;
    p.phase = Projection::INPUT;
    return loka::app::scene::ADMISSION_TAKE;
  }
  virtual bool current(loka::app::scene::Node &base, const loka::app::scene::RequestBinding<LineCursor> &request)
  {
    return this->currentBinding(base) && static_cast<TextEditorNode &>(base).props.moveCaretTo_.same(request);
  }
  bool current(loka::app::scene::Node &base, const loka::app::scene::RequestBinding<EditorCommand> &request)
  {
    return this->currentBinding(base) && static_cast<TextEditorNode &>(base).props.command_.same(request);
  }
  virtual EditorResult resolve(loka::app::scene::Node &base,
                               const loka::app::scene::RequestBinding<LineCursor> &request)
  {
    return this->resolveBinding(base, request);
  }
  template <class Request>
  EditorResult resolveBinding(loka::app::scene::Node &base, const loka::app::scene::RequestBinding<Request> &request)
  {
    MacTextEditorContext &c = context(base);
    if (!this->current(base, request))
      return EDITOR_OWNER_MISMATCH;
    if (c.projection_->phase != Projection::INPUT)
      return EDITOR_REENTRANT;
    if (this->completion_ != Projection::IDLE || !c.scroll_)
      return EDITOR_UNAVAILABLE;
    StateTracker *owner = 0;
    if (!this->binding_.buffer_ || this->binding_.buffer_->lines().queryMutationTracker(owner) != EDIT_OK)
      return EDITOR_UNAVAILABLE;
    return request.usesTracker(owner) ? EDITOR_OK : EDITOR_OWNER_MISMATCH;
  }
  virtual EditorResult validate(loka::app::scene::Node &base, const LineCursor &pending)
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    const EditorResult ready = node.seam(context(base).key_).availability();
    if (ready != EDITOR_OK)
      return ready;
    return node.props.buffer_->lines().find(pending.line) < 0 ? EDITOR_STALE_ID : EDITOR_OK;
  }
  virtual loka::app::scene::RequestApplication<LineCursor, EditorResult> apply(loka::app::scene::Node &base,
                                                                               const LineCursor &pending)
  {
    return this->applySelection<LineCursor>(base, pending, *this, this->binding_.moveCaretTo_, false);
  }
  loka::app::scene::RequestApplication<LineCursor, EditorResult>
  applyCommand(loka::app::scene::Node &base,
               const LineCursor &target,
               loka::app::scene::SeatOperation<EditorCommand, LineCursor> &seat)
  {
    return this->applySelection<EditorCommand>(base, target, seat, this->binding_.command_, true);
  }
  /** Reconcile a projection a take subscriber made stale, under exclusion.
      Both seats measure and apply against the reconciled native string. */
  bool ensureProjection(loka::app::scene::Node &base, loka::app::scene::FollowUp &follow)
  {
    MacTextEditorContext &c = context(base);
    Projection &p = *c.projection_;
    NSTextView *view = (NSTextView *)[(NSScrollView *)c.scroll_ documentView];
    if (p.validateDocument(*c.node_, c.key_) != EDITOR_OK || ![[view string] isEqualToString:p.committed])
    {
      p.phase = Projection::RECONCILE;
      follow = c.syncFromNode(*this, false);
      if (!this->hasSameContext())
        return false;
      // Only a completed reconciliation keeps the take's RECONCILE phase. A
      // failed one released the row buffers, refused the new capacity or
      // could not replace the native text, so the take stops here instead of
      // scanning an absent snapshot, and the phase it leaves (UNAVAILABLE,
      // which the next take's resolve refuses, or QUEUED, which defers it)
      // is what the next admission reads. The restore it asks for travels
      // in the follow-up to finishSettle, which queues it; queuing it here
      // first would hide the unavailable phase behind the queued one (#1617).
      if (p.phase != Projection::RECONCILE)
        return false;
      p.phase = Projection::INPUT;
    }
    return true;
  }
  template <class Request>
  loka::app::scene::RequestApplication<LineCursor, EditorResult>
  applySelection(loka::app::scene::Node &base,
                 const LineCursor &pending,
                 loka::app::scene::SeatOperation<Request, LineCursor> &seat,
                 const loka::app::scene::RequestBinding<Request> &request,
                 bool scroll)
  {
    MacTextEditorContext &c = context(base);
    Projection &p = *c.projection_;
    NSTextView *view = (NSTextView *)[(NSScrollView *)c.scroll_ documentView];
    loka::app::scene::FollowUp follow = loka::app::scene::FOLLOW_NONE;
    if (!this->ensureProjection(base, follow))
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE, follow);
    if (!seat.current(base, request))
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_OWNER_MISMATCH, follow);
    if (p.phase != Projection::INPUT)
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE, follow);
    // INPUT excludes nested input; copy the row before invoking native setters.
    const NativeLines native(p.committed, this->binding_.buffer_->capacity(), p.rows.before());
    const int index = this->binding_.buffer_->lines().find(pending.line);
    if (native.result != EDITOR_OK || index < 0 || index >= native.count)
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_STALE_ID, follow);
    const NSRange row = native.ranges[index];
    const std::size_t column =
        ClampedColumn(this->binding_.buffer_->lines(), static_cast<unsigned short>(index), pending.column);
    const NSUInteger units = NativeRow(p.committed, row).unitsAt(column);
    const LineCursor clamped(pending.line, static_cast<int>(column));
    p.phase = Projection::APPLYING;
    [view setSelectedRange:NSMakeRange(row.location + units, 0)];
    if (!this->hasSameContext())
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE);
    if (!seat.current(base, request))
    {
      if (p.phase == Projection::APPLYING)
        p.phase = Projection::RECONCILE;
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(
          pending, EDITOR_OWNER_MISMATCH, loka::app::scene::REPAINT);
    }
    if (p.phase != Projection::APPLYING)
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(
          pending, EDITOR_REENTRANT, loka::app::scene::REPAINT);
    if (scroll)
    {
      [view scrollRangeToVisible:NSMakeRange(row.location + units, 0)];
      if (!this->hasSameContext())
        return loka::app::scene::RequestApplication<LineCursor, EditorResult>(pending, EDITOR_UNAVAILABLE);
      if (!seat.current(base, request))
      {
        if (p.phase == Projection::APPLYING)
          p.phase = Projection::RECONCILE;
        return loka::app::scene::RequestApplication<LineCursor, EditorResult>(
            pending, EDITOR_OWNER_MISMATCH, loka::app::scene::REPAINT);
      }
      if (p.phase != Projection::APPLYING)
        return loka::app::scene::RequestApplication<LineCursor, EditorResult>(
            pending, EDITOR_REENTRANT, loka::app::scene::REPAINT);
    }
    p.selection = [view selectedRange];
    p.phase = Projection::INPUT;
    return loka::app::scene::RequestApplication<LineCursor, EditorResult>(
        clamped, EDITOR_OK, loka::app::scene::REPAINT);
  }
  virtual EditorResult report(loka::app::scene::Node &base, const LineCursor &applied)
  {
    return static_cast<TextEditorNode &>(base).seam(context(base).key_).moveCaret(applied);
  }
  virtual loka::app::scene::FollowUp
  finishTake(loka::app::scene::Node &base,
             const loka::app::scene::Reply<LineCursor> &reply,
             const loka::app::scene::RequestApplication<LineCursor, EditorResult> &applied)
  {
    MacTextEditorContext &c = context(base);
    Projection &p = *c.projection_;
    loka::app::scene::FollowUp follow = loka::app::scene::FOLLOW_NONE;
    if (base.lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
      return follow;
    const bool refusedReport =
        applied.result() == EDITOR_OK && reply.kind() == loka::app::scene::Reply<LineCursor>::REFUSED;
    if (this->completion_ == Projection::IDLE && (p.phase == Projection::INPUT || p.phase == Projection::RECONCILE))
    {
      NSTextView *view = (NSTextView *)[(NSScrollView *)c.scroll_ documentView];
      if (refusedReport || p.phase == Projection::RECONCILE || p.validateDocument(*c.node_, c.key_) != EDITOR_OK
          || ![[view string] isEqualToString:p.committed])
      {
        p.phase = Projection::RECONCILE;
        follow = c.syncFromNode(*this, false);
        if (!this->hasSameContext())
          return follow;
        if (follow == loka::app::scene::SCHEDULE_RESTORE)
          c.prepareRestore();
        else if (refusedReport && p.phase == Projection::RECONCILE)
        {
          // Apply wrote the caret but the seam did not commit it. Repair only
          // selection: unchanged text and its native undo history stay intact.
          c.restoreSelectionFromFact(*this);
          if (!this->hasSameContext())
            return follow;
          follow = loka::app::scene::REPAINT;
        }
      }
    }
    if (p.phase == Projection::INPUT || p.phase == Projection::RECONCILE)
      p.phase = this->completion_;
    return follow;
  }
  virtual loka::app::scene::FollowUpResult finishSettle(loka::app::scene::Node &base,
                                                      const loka::app::scene::FollowUps &follow)
  {
    MacTextEditorContext &c = context(base);
    if (base.lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
      return loka::app::scene::FOLLOW_UP_NONE;
    Projection &p = *c.projection_;
    LokaTextEditorDelegate *delegate = (LokaTextEditorDelegate *)c.delegate_;
    const bool highlights = this->follow_.contains(loka::app::scene::SCHEDULE_HIGHLIGHTS);
    if (follow.contains(loka::app::scene::SCHEDULE_RESTORE)
        || this->follow_.contains(loka::app::scene::SCHEDULE_RESTORE)
        || (highlights && p.phase == Projection::UNAVAILABLE))
    {
      c.prepareRestore();
      [NSObject cancelPreviousPerformRequestsWithTarget:delegate selector:@selector(applyHighlights) object:nil];
      [NSObject cancelPreviousPerformRequestsWithTarget:delegate selector:@selector(restoreProjection) object:nil];
      // NSObject's delayed selector arm is void: there is no observable failed-arm result.
      [delegate performSelector:@selector(restoreProjection) withObject:nil afterDelay:0];
      return loka::app::scene::FOLLOW_UP_ARMED;
    }
    else if (highlights && p.phase == Projection::STORAGE_PENDING)
    {
      [NSObject cancelPreviousPerformRequestsWithTarget:delegate selector:@selector(applyHighlights) object:nil];
      [delegate performSelector:@selector(applyHighlights) withObject:nil afterDelay:0];
      return loka::app::scene::FOLLOW_UP_ARMED;
    }
    // AppKit schedules paint for its text/selection writes; no extra repaint sink.
    return loka::app::scene::FOLLOW_UP_NONE;
  }
#ifdef TEST_BUILD
  virtual LineCursor fact(loka::app::scene::Node &base) const
  {
    State<LineCursor> *state = static_cast<TextEditorNode &>(base).props.cursorState();
    return state ? state->get() : LineCursor::None();
  }
  LineCursor before() const
  {
    return this->before_;
  }
#endif
private:
  bool currentBinding(loka::app::scene::Node &base) const
  {
    TextEditorNode &node = static_cast<TextEditorNode &>(base);
    return node.lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED && node.props.buffer_ == this->binding_.buffer_
           && node.props.cursorState() == this->binding_.cursorState();
  }
  static MacTextEditorContext &context(loka::app::scene::Node &node)
  {
    return *static_cast<MacTextEditorContext *>(node.getContext());
  }
  TextEditorProps binding_;
  Projection::Phase completion_;
  loka::app::scene::FollowUps follow_;
#ifdef TEST_BUILD
  const LineCursor before_;
#endif
};

bool MacTextEditorContext::queryVisibleLines(unsigned &lines) const
{
  lines = 0;
  NSScrollView *scroll = (NSScrollView *)this->scroll_;
  NSTextView *view = (NSTextView *)[scroll documentView];
  NSLayoutManager *layout = [view layoutManager];
  NSTextContainer *container = [view textContainer];
  if (!scroll || !layout || !container)
    return false;
  // Force layout, including the extra fragment for a final empty logical line.
  (void)[layout glyphRangeForTextContainer:container];
  // Request measurement finishes before applySelection reuses before, on the main thread.
  const NativeLines native([view string], this->node_->props.buffer_->capacity(), this->projection_->rows.before());
  if (native.result != EDITOR_OK)
    return false;
  const NSRect visible = [scroll documentVisibleRect];
  const NSPoint origin = [view textContainerOrigin];
  for (unsigned short i = 0; i < native.count; ++i)
  {
    NSRange row = native.ranges[i];
    NSRect rect;
    if (!row.length && row.location == [[view string] length])
    {
      if ([layout extraLineFragmentTextContainer] != container)
        continue;
      rect = [layout extraLineFragmentUsedRect];
    }
    else
    {
      // An interior empty row uses its newline glyph's fragment, not a
      // zero-length glyph range (which has no measurable bounding rect).
      if (!row.length)
        row.length = 1;
      const NSRange glyphs = [layout glyphRangeForCharacterRange:row actualCharacterRange:0];
      if (!glyphs.length)
        continue;
      rect = [layout boundingRectForGlyphRange:glyphs inTextContainer:container];
      for (NSUInteger glyph = glyphs.location; glyph < NSMaxRange(glyphs);)
      {
        NSRange fragment;
        const NSRect fragmentRect = [layout lineFragmentUsedRectForGlyphAtIndex:glyph effectiveRange:&fragment];
        rect = NSUnionRect(rect, fragmentRect);
        glyph = NSMaxRange(fragment);
      }
    }
    rect.origin.x += origin.x;
    rect.origin.y += origin.y;
    // Empty lines can have zero used width; compare edges instead of
    // NSContainsRect, whose empty-rectangle semantics would discard them.
    if (rect.size.height > 0 && NSMinX(rect) >= NSMinX(visible) && NSMaxX(rect) <= NSMaxX(visible)
        && NSMinY(rect) >= NSMinY(visible) && NSMaxY(rect) <= NSMaxY(visible))
      ++lines;
  }
  return lines != 0;
}

/** The command seat borrows the entry's caret pipeline and completion owner. */
class MacTextEditorContext::CommandOperation : public loka::app::scene::SeatOperation<EditorCommand, LineCursor>
{
public:
  explicit CommandOperation(RailOperation &rail)
      : rail_(rail)
  {
  }
  virtual loka::app::scene::Admission admit(
      loka::app::scene::Node &base, loka::app::scene::RequestBinding<EditorCommand> &request)
  {
    const TextEditorProps &props = static_cast<TextEditorNode &>(base).props;
    return this->rail_.enterTake(base, request, &TextEditorProps::command_, &props.moveCaretTo_);
  }

  virtual bool current(loka::app::scene::Node &base, const loka::app::scene::RequestBinding<EditorCommand> &request)
  {
    return this->rail_.current(base, request);
  }
  virtual EditorResult resolve(loka::app::scene::Node &base, const loka::app::scene::RequestBinding<EditorCommand> &request)
  {
    return this->rail_.resolveBinding(base, request);
  }
  virtual EditorResult validate(loka::app::scene::Node &base, const EditorCommand &)
  {
    MacTextEditorContext &c = *static_cast<MacTextEditorContext *>(base.getContext());
    return static_cast<TextEditorNode &>(base).seam(c.key_).availability();
  }
  virtual loka::app::scene::RequestApplication<LineCursor, EditorResult> apply(loka::app::scene::Node &base,
                                                                               const EditorCommand &pending)
  {
    MacTextEditorContext &c = *static_cast<MacTextEditorContext *>(base.getContext());
    unsigned visibleLines = 0;
    LineCursor target;
    // Geometry and the page target must come from the same snapshot: a caret
    // seat subscriber may have edited the list in this settle (bot P2, #908).
    loka::app::scene::FollowUp follow = loka::app::scene::FOLLOW_NONE;
    if (!this->rail_.ensureProjection(base, follow))
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(target, EDITOR_UNAVAILABLE, follow);
    if (!c.queryVisibleLines(visibleLines))
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(target, EDITOR_UNAVAILABLE, follow);
    const EditorResult result =
        static_cast<TextEditorNode &>(base).seam(c.key_).pageTarget(pending, visibleLines, target);
    if (result != EDITOR_OK)
      return loka::app::scene::RequestApplication<LineCursor, EditorResult>(target, result);
    return this->rail_.applyCommand(base, target, *this);
  }
  virtual EditorResult report(loka::app::scene::Node &base, const LineCursor &applied)
  {
    return this->rail_.report(base, applied);
  }
  virtual loka::app::scene::FollowUp
  finishTake(loka::app::scene::Node &base,
             const loka::app::scene::Reply<EditorCommand> &reply,
             const loka::app::scene::RequestApplication<LineCursor, EditorResult> &applied)
  {
    const loka::app::scene::Reply<LineCursor> caretReply =
        reply.kind() == loka::app::scene::Reply<EditorCommand>::REFUSED
            ? loka::app::scene::Reply<LineCursor>::Refused(applied.value(), reply.reason())
            : loka::app::scene::Reply<LineCursor>::Granted(applied.value());
    return this->rail_.finishTake(base, caretReply, applied);
  }

private:
  RailOperation &rail_;
};

void MacTextEditorContext::settle(loka::app::scene::Settlement stimulus, RailOperation &op)
{
  CommandOperation command(op);
  loka::app::scene::RequestSettlement<LineCursor>::settle(op,
                                                          op,
                                                          command,
                                                          stimulus
#ifdef TEST_BUILD
                                                          ,
                                                          op.before()
#endif
  );
}

void *MacTextEditorContext::nativeFocusView() const
{
  return [(NSScrollView *)this->scroll_ documentView];
}

MacTextEditorContext *MacTextEditorContext::fromNativeFocus(void *responder)
{
  id view = (id)responder;
  if (![view isKindOfClass:[LokaTextEditorView class]])
    return 0;
  id delegate = [(LokaTextEditorView *)view delegate];
  return [delegate isKindOfClass:[LokaTextEditorDelegate class]]
             ? [(LokaTextEditorDelegate *)delegate owner] : 0;
}

MacTextEditorContext::MacTextEditorContext(MacScenePlatformController *controller,
                                         void *parent,
                                         loka::app::TextEditorNode *node,
                                         const loka::app::scene::SeamKey<TextEditorNode> &key)
    : MacRetirableContext(controller),
      projection_(new(std::nothrow) Projection()),
      restores_(0),
      key_(key),
      node_(node),
      parent_(parent),
      scroll_(0),
      delegate_(0)
{
  NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  NSTextView *view = [[LokaTextEditorView alloc] initWithFrame:NSZeroRect];
  LokaTextEditorDelegate *delegate = [[LokaTextEditorDelegate alloc] init];
  if (!this->projection_ || !scroll || !view || !delegate)
  {
    [delegate release];
    [view release];
    [scroll release];
    return;
  }
  loka::macos::SetMacFrame(view, controller->projection().projectFrame(loka::core::Frame(0, 0, 200, 80)));
  [view setRichText:NO];
  [view setAllowsUndo:YES];
  InstallEditorFont(view, *controller);
  [view setVerticallyResizable:YES];
  [view setMaxSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
  [scroll setHasVerticalScroller:YES];
  [scroll setDocumentView:view];
  [view release];
  [delegate setOwner:this];
  this->scroll_ = scroll;
  this->delegate_ = delegate;
}

MacTextEditorContext::~MacTextEditorContext()
{
  assert(!this->scroll_ && !this->delegate_ && "detach must queue native objects before reclaim");
  delete this->projection_;
}
bool MacTextEditorContext::hasNativeView() const
{
  return this->scroll_ != 0;
}

void MacTextEditorContext::readLifecycleFactOnAttach()
{
  this->onFactChanged(loka::app::scene::NODE_FACT_DETACHED_RETAINED, this->node_->lifecycleFact());
}
void MacTextEditorContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  RailOperation op(*this);
  NSScrollView *scroll = (NSScrollView *)this->scroll_;
  NSTextView *view = (NSTextView *)[scroll documentView];
  LokaTextEditorDelegate *delegate = (LokaTextEditorDelegate *)this->delegate_;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    if (![scroll superview])
      [(NSView *)this->parent_ addSubview:scroll];
    [scroll setHidden:NO];
    [delegate setOwner:this];
    [view setDelegate:(id)delegate];
    [[view textStorage] setDelegate:(id)delegate];
    this->projection_->phase = Projection::IDLE;
    op.sync(true);
    if (!op.hasSameContext())
      return;
    this->settle(loka::app::scene::SETTLE_ATTACH, op);
  }
  else
  {
    [NSObject cancelPreviousPerformRequestsWithTarget:delegate];
    [delegate setOwner:0];
    [view setDelegate:nil];
    [[view textStorage] setDelegate:nil];
    [view setEditable:NO];
    [scroll setHidden:YES];
    this->projection_->phase = Projection::UNAVAILABLE;
    this->projection_->clear();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      [scroll removeFromSuperview];
      this->retireNativeObjects(this->scroll_, this->delegate_);
      this->node_ = 0;
      this->parent_ = 0;
    }
  }
}
void MacTextEditorContext::onPropsApplied()
{
  if (this->node_
      && (this->projection_->phase == Projection::IDLE || this->projection_->phase == Projection::UNAVAILABLE))
  {
    RailOperation op(*this);
    op.sync(false);
    if (!op.hasSameContext())
      return;
    this->settle(loka::app::scene::SETTLE_PROPS, op);
  }
}
short MacTextEditorContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  if (state.height <= 0)
    state.height = 80;
  NSScrollView *scroll = (NSScrollView *)this->scroll_;
  loka::macos::SetMacFrame(
      scroll,
      this->controller()->projection().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height)));
  NSTextView *view = (NSTextView *)[scroll documentView];
  const NSSize size = [scroll contentSize];
  [view setMinSize:size];
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const int documentHeight = projection.measurementToLu(std::max(size.height, [view frame].size.height));
  loka::macos::SetMacDocumentSize(scroll, [scroll contentSize].width, projection.projectLength(0, documentHeight));
  this->onPropsApplied();
  return static_cast<short>(state.y + state.height);
}

loka::app::scene::FollowUp MacTextEditorContext::prepareRestore()
{
  Projection &p = *this->projection_;
  if (!this->node_ || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED || p.phase == Projection::QUEUED)
    return loka::app::scene::FOLLOW_NONE;
  p.phase = Projection::QUEUED;
  [(NSTextView *)[(NSScrollView *)this->scroll_ documentView] setEditable:NO];
  return loka::app::scene::SCHEDULE_RESTORE;
}
void MacTextEditorContext::restoreCommittedProjection()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (!this->node_ || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  ++this->restores_;
  // This method is only the delegate entry; shared repairs use syncFromNode.
  RailOperation op(*this);
  op.sync(true);
  if (!op.hasSameContext())
    return;
  this->settle(loka::app::scene::SETTLE_RESTORE, op);
}

loka::app::scene::FollowUp MacTextEditorContext::syncFromNode(RailOperation &op, bool force, bool nativeCommit)
{
  if (!this->node_ || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return loka::app::scene::FOLLOW_NONE;
  Projection &p = *this->projection_;
  NSScrollView *scroll = (NSScrollView *)this->scroll_;
  NSTextView *view = (NSTextView *)[scroll documentView];
  const NSRect visible = [scroll documentVisibleRect];
  // A repair inside the consumer must not reopen request delivery.
  const Projection::Phase completion = p.phase == Projection::RECONCILE ? Projection::RECONCILE : Projection::IDLE;
  p.phase = Projection::APPLYING;
  const EditorResult result = this->node_->seam(this->key_).project(p.scratch);
  if (result != EDITOR_OK)
  {
    if (nativeCommit)
    {
      return this->prepareRestore();
    }
    ReplaceEditorString(view, @"", *this->controller());
    if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
      return loka::app::scene::FOLLOW_NONE;
    [view setEditable:NO];
    [[view undoManager] removeAllActions];
    p.clear();
    p.phase = Projection::UNAVAILABLE;
    // The entry refuses pending requests before scheduling an allocation retry.
    return result == EDITOR_ALLOCATION ? loka::app::scene::SCHEDULE_RESTORE : loka::app::scene::REPAINT;
  }
  const unsigned short capacity = this->node_->props.buffer_->capacity().lines;
  if (!(nativeCommit ? p.rows.matches(capacity) : p.rows.admit(capacity)))
  {
    // A refused row admission is an allocation failure like a refused payload:
    // keep the committed snapshot and native text, and retry at the next flush.
    [view setEditable:NO];
    p.phase = Projection::UNAVAILABLE;
    return loka::app::scene::SCHEDULE_RESTORE;
  }
  NSString *desired = p.committed;
  const bool same = SameAsModel(desired, p.scratch);
  if (!same)
  {
    const NativeTextBytes bytes(p.scratch);
    desired = bytes.copyString();
  }
  if (!desired)
  {
    p.clear();
    return this->prepareRestore();
  }
  const bool replace = force || ![[view string] isEqualToString:desired];
  if (replace && nativeCommit)
  {
    if (!same)
      [desired release];
    return this->prepareRestore();
  }
  if (replace)
  {
    ReplaceEditorString(view, desired, *this->controller());
    if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    {
      if (!same)
        [desired release];
      return loka::app::scene::FOLLOW_NONE;
    }
    [[view undoManager] removeAllActions];
  }
  if (![[view string] isEqualToString:desired])
  {
    if (!same)
      [desired release];
    p.clearStyles();
    return this->prepareRestore();
  }
  if (!same)
  {
    [p.committed release];
    p.committed = desired;
  }
  const TextLines &lines = this->node_->props.buffer_->lines();
  for (unsigned short i = 0; i < lines.size(); ++i)
    p.rows.id(i) = lines.at(i).id;
  if (nativeCommit)
  {
    // Accept the snapshot without writing selection or attributes during input.
    p.phase = Projection::IDLE;
    return loka::app::scene::FOLLOW_NONE;
  }
  if (replace)
  {
    this->restoreSelectionFromFact(op);
    if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
      return loka::app::scene::FOLLOW_NONE;
  }
  p.style(view, *this->node_, *this->controller(), replace);
  if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return loka::app::scene::FOLLOW_NONE;
  const loka::macos::MacProjection &projection = this->controller()->projection();
  loka::macos::ScrollMacDocument(view,
                                 projection.scrollOffsetToNative(0),
                                 projection.scrollOffsetToNative(projection.scrollPositionToLu(visible.origin.y)));
  [scroll reflectScrolledClipView:[scroll contentView]];
  [view setEditable:YES];
  p.phase = completion;
  return loka::app::scene::REPAINT;
}

void MacTextEditorContext::restoreSelectionFromFact(RailOperation &op)
{
  Projection &p = *this->projection_;
  const Projection::Phase completion = p.phase;
  NSTextView *view = (NSTextView *)[(NSScrollView *)this->scroll_ documentView];
  // The main-thread repair reads before completely before setting native selection.
  const NativeLines native(p.committed, this->node_->props.buffer_->capacity(), p.rows.before());
  const LineCursor cursor = this->node_->props.cursorState()->get();
  const int index = this->node_->props.buffer_->lines().find(cursor.line);
  NSRange selection = p.selection;
  if (index >= 0)
  {
    const NSUInteger location =
        native.ranges[index].location
        + NativeRow(p.committed, native.ranges[index])
              .unitsAt(ClampedColumn(
                  this->node_->props.buffer_->lines(), static_cast<unsigned short>(index), cursor.column));
    selection = NSMakeRange(location, 0);
  }
  selection.location = std::min(selection.location, [p.committed length]);
  selection.length = std::min(selection.length, [p.committed length] - selection.location);
  p.phase = Projection::APPLYING;
  [view setSelectedRange:selection];
  if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  p.selection = selection;
  if (p.phase == Projection::APPLYING)
    p.phase = completion;
}

void MacTextEditorContext::captureSelection()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (this->projection_->phase == Projection::IDLE || this->projection_->phase == Projection::STORAGE_PENDING)
    this->projection_->selection = [(NSTextView *)[(NSScrollView *)this->scroll_ documentView] selectedRange];
}

void MacTextEditorContext::handleSelectionDidChange()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  RailOperation op(*this);
  Projection &p = *this->projection_;
  if (p.phase == Projection::INPUT || p.phase == Projection::RECONCILE)
  {
    p.phase = Projection::RECONCILE;
    return;
  }
  if ((p.phase != Projection::IDLE && p.phase != Projection::STORAGE_PENDING) || !this->node_)
    return;
  NSTextView *view = (NSTextView *)[(NSScrollView *)this->scroll_ documentView];
  // NSTextView can announce selection before textDidChange. Only a selection
  // over committed text is a caret-only event.
  if (![[view string] isEqualToString:p.committed])
    return;
  // Read before into the cursor before publishing; nested input is then excluded.
  const NativeLines lines(p.committed, this->node_->props.buffer_->capacity(), p.rows.before());
  const NSRange selection = [view selectedRange];
  const unsigned short index = lines.lineAt(selection.location);
  const LineCursor cursor(
      p.rows.id(index),
      static_cast<int>(
          NativeRow(p.committed, lines.ranges[index]).bytesFloor(selection.location - lines.ranges[index].location)));
  const Projection::Phase completion = p.phase;
  p.phase = Projection::INPUT;
  const EditorResult result = this->node_->seam(this->key_).moveCaret(cursor);
  if (!op.hasSameContext())
    return;
  if (op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  if (result != EDITOR_OK || p.phase == Projection::RECONCILE)
    op.restore();
  else
  {
    p.selection = selection;
    p.phase = completion;
  }
  this->settle(loka::app::scene::SETTLE_INPUT, op);
}

EditorResult MacTextEditorContext::applyNativeChange(TextObservation source, std::size_t &caretOffset)
{
  Projection &p = *this->projection_;
  NSTextView *view = (NSTextView *)[(NSScrollView *)this->scroll_ documentView];
  NSString *text = [view string];
  const EditorResult available = p.validateDocument(*this->node_, this->key_);
  if (available != EDITOR_OK)
    return available;
  // INPUT excludes nested scans; both buffers are read before applyReplace publishes.
  const NativeLines before(p.committed, this->node_->props.buffer_->capacity(), p.rows.before()),
      after(text, this->node_->props.buffer_->capacity(), p.rows.after());
  if (after.result != EDITOR_OK)
    return after.result;
  // validateDocument left the committed CR-separated snapshot in scratch.
  // Normalize through native line ranges so CRLF is also one logical separator.
  std::string logical;
  logical.reserve([text length]);
  for (unsigned short i = 0; i < after.count; ++i)
  {
    if (i)
      logical += '\r';
    if (!NativeRow(text, after.ranges[i]).appendUtf8(logical))
      return EDITOR_NON_ASCII;
  }
  int hintLine = -1, hintColumn = -1;
  if (source == STORAGE_EDIT)
  {
    // Interpret mutation evidence in the before snapshot, as for view input.
    // Missing evidence leaves the hint absent; append-only passes clamp to EOF.
    if (before.result == EDITOR_OK && before.count && caretOffset != NSNotFound)
    {
      const NSUInteger location = std::min(static_cast<NSUInteger>(caretOffset), [p.committed length]);
      hintLine = before.lineAt(location);
      const NSRange range = before.ranges[hintLine];
      hintColumn = static_cast<int>(NativeRow(p.committed, range).bytesFloor(location - range.location));
    }
  }
  else if (!p.selection.length && p.selection.location <= [p.committed length])
  {
    // Like Win32, interpret the captured native selection in the before snapshot.
    hintLine = before.lineAt(p.selection.location);
    hintColumn = static_cast<int>(NativeRow(p.committed, before.ranges[hintLine])
                                      .bytesFloor(p.selection.location - before.ranges[hintLine].location));
  }
  const TextEditorLineDiff diff = DiffTextEditorLines(p.scratch, logical, hintLine, hintColumn);
  const unsigned short first = static_cast<unsigned short>(diff.first());
  if (!diff.before() && !diff.after())
  {
    if (source == STORAGE_EDIT)
      return EDITOR_OK;
    const NSUInteger caret = std::min(static_cast<NSUInteger>(caretOffset), [text length]);
    const unsigned short caretLine = after.lineAt(caret);
    // A later view notification can carry a new caret over committed text.
    const LineCursor cursor(p.rows.id(caretLine),
                            static_cast<int>(NativeCaretBytes(NativeRow(text, after.ranges[caretLine]),
                                                              caret - after.ranges[caretLine].location,
                                                              source)));
    return cursor == this->node_->props.cursorState()->get() ? EDITOR_OK
                                                             : this->node_->seam(this->key_).moveCaret(cursor);
  }
  const unsigned short oldEnd = static_cast<unsigned short>(first + diff.before());
  const unsigned short newEnd = static_cast<unsigned short>(first + diff.after());
  const NSRange oldRange = NSMakeRange(before.ranges[first].location,
                                     NSMaxRange(before.ranges[oldEnd - 1]) - before.ranges[first].location);
  const NSRange newRange = NSMakeRange(after.ranges[first].location,
                                     NSMaxRange(after.ranges[newEnd - 1]) - after.ranges[first].location);
  if (source == STORAGE_EDIT)
  {
    // Storage announces text before the view's selection is final. Compare the
    // complete changed span, including separators, to find the insertion end.
    const loka::app::detail::TextChangeSpan span(NativeCharacters(p.committed, oldRange.location),
                                                 oldRange.length,
                                                 NativeCharacters(text, newRange.location),
                                                 newRange.length);
    const NSUInteger end = newRange.location + span.afterEnd();
    const NSRange endRow = after.ranges[after.lineAt(end)];
    const NativeRow row(text, endRow);
    caretOffset = endRow.location + row.unitsAt(row.bytesCeil(end - endRow.location));
  }
  const NSUInteger caret = std::min(static_cast<NSUInteger>(caretOffset), [text length]);
  const unsigned short caretLine = after.lineAt(caret);
  std::string replacement;
  for (unsigned short i = first; i < newEnd; ++i)
  {
    if (i != first)
      replacement += '\r';
    if (!NativeRow(text, after.ranges[i]).appendUtf8(replacement))
      return EDITOR_NON_ASCII;
  }
  return this->node_->seam(this->key_)
      .applyReplace(
          LineCursor(p.rows.id(first), 0),
          LineCursor(p.rows.id(oldEnd - 1),
                     static_cast<int>(NativeRow(p.committed, before.ranges[oldEnd - 1]).byteLength())),
          replacement.data(),
          replacement.size(),
          RowCursor(caretLine,
                    static_cast<int>(NativeCaretBytes(
                        NativeRow(text, after.ranges[caretLine]), caret - after.ranges[caretLine].location, source))));
}

void MacTextEditorContext::handleTextDidChange(TextObservation source, std::size_t caretOffset)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  RailOperation op(*this);
  Projection &p = *this->projection_;
  if (p.phase == Projection::APPLYING || p.phase == Projection::UNAVAILABLE)
    return;
  if (p.phase == Projection::INPUT || p.phase == Projection::RECONCILE)
  {
    p.phase = Projection::RECONCILE;
    return;
  }
  if (p.phase == Projection::QUEUED || !this->node_)
    return;
  p.phase = Projection::INPUT;
#ifndef NDEBUG
  NSTextView *view = (NSTextView *)[(NSScrollView *)this->scroll_ documentView];
  const bool textChanged = ![[view string] isEqualToString:p.committed];
#endif
  const EditorResult result = this->applyNativeChange(source, caretOffset);
  if (!op.hasSameContext())
    return;
#ifndef NDEBUG
  // Scalar-only diagnostics identify the committing callback without user text.
  if (result != EDITOR_OK || p.phase == Projection::RECONCILE)
    fprintf(stderr, "[MacTextEditor %s] text-diff=%d caret-offset=%lu result=%d phase=%d\n",
            source == STORAGE_EDIT ? "textStorageDidProcessEditing:" : "textDidChange:",
            static_cast<int>(textChanged), static_cast<unsigned long>(caretOffset),
            static_cast<int>(result), static_cast<int>(p.phase));
#endif
  if (op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  if (result != EDITOR_OK || p.phase == Projection::RECONCILE)
    op.restore();
  else
  {
    op.sync(false, true);
    if (!op.hasSameContext())
      return;
    if (p.phase == Projection::IDLE)
    {
      LokaTextEditorDelegate *delegate = (LokaTextEditorDelegate *)this->delegate_;
      [NSObject cancelPreviousPerformRequestsWithTarget:delegate selector:@selector(applyHighlights) object:nil];
      if (source == VIEW_CHANGE)
      {
        p.selection = [(NSTextView *)[(NSScrollView *)this->scroll_ documentView] selectedRange];
        op.highlights();
        if (!op.hasSameContext())
          return;
      }
      else
        op.deferHighlights();
    }
  }
  if (source == VIEW_CHANGE)
    this->settle(loka::app::scene::SETTLE_INPUT, op);
  else
  {
    // processEditing is an observation, never a request-delivery operation.
    // Only arm its continuation here; that deferred entry owns settlement.
    // Delayed selector arms have no observable failure result to handle here.
    (void)op.finishSettle(*op.node(), loka::app::scene::FollowUps());
  }
}

void MacTextEditorContext::applyHighlights()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(this->controller()->borrowPhase().open());
#endif
  if (!this->node_ || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  RailOperation op(*this);
  op.highlights();
  if (!op.hasSameContext())
    return;
  this->settle(loka::app::scene::SETTLE_DEFERRED, op);
}

void MacTextEditorContext::projectHighlights(RailOperation &op)
{
  Projection &p = *this->projection_;
  if ((p.phase != Projection::IDLE && p.phase != Projection::STORAGE_PENDING) || !this->node_
      || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  NSTextView *view = (NSTextView *)[(NSScrollView *)this->scroll_ documentView];
  if (![[view string] isEqualToString:p.committed] || p.validateDocument(*this->node_, this->key_) != EDITOR_OK)
  {
    p.phase = Projection::UNAVAILABLE;
    return;
  }
  p.phase = Projection::APPLYING;
  p.style(view, *this->node_, *this->controller(), false);
  if (!op.hasSameContext() || op.node()->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  p.phase = Projection::IDLE;
}

void RegisterMacTextEditorNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}

#ifdef TEST_BUILD
#include "../../../../tests/support/MacTextEditorConversionPins.inc"
#endif
