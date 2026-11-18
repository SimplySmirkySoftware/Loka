#include "MacTextStore.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "app/style/Style.hpp"
#include "app/text/TextStoreSource.hpp"
#include <climits>
#include <cstdlib>
#include <new>

using namespace loka::app;
using namespace loka::app::scene;
#ifdef TEST_BUILD
using namespace loka::macos::testing;
#define MAC_STORE_ALLOC(stage, expression) (refuseStoreAllocation(stage) ? 0 : (expression))
#else
#define MAC_STORE_ALLOC(stage, expression) (expression)
#endif

@interface LokaTextDocumentView : NSTextView
@end
@implementation LokaTextDocumentView
@end

@interface LokaTextStoreDelegate : NSObject
{
@private
  MacTextStore *owner_;
}
- (MacTextStore *)owner;
- (void)setOwner:(MacTextStore *)row;
@end
@implementation LokaTextStoreDelegate
- (MacTextStore *)owner
{
  return owner_;
}
- (void)setOwner:(MacTextStore *)row
{
  owner_ = row;
}
- (NSUndoManager *)undoManagerForTextView:(NSTextView *)view
{
  (void)view;
  MacTextStore *row = [self owner];
  return row ? row->undoManager() : nil;
}
- (void)textStorageDidProcessEditing:(NSNotification *)notification
{
  MacTextStore *row = [self owner];
  NSTextStorage *storage = [notification object];
  if (!row || !([storage editedMask] & NSTextStorageEditedCharacters))
    return;
  const NSRange newRange = [storage editedRange];
  const NSInteger delta = [storage changeInLength];
  // Subtract a signed delta without negating the most negative signed value.
  const unsigned long magnitude =
      delta < 0 ? static_cast<unsigned long>(-(delta + 1)) + 1 : static_cast<unsigned long>(delta);
  if (newRange.location == NSNotFound || newRange.length > ULONG_MAX - newRange.location
      || (delta >= 0 && magnitude > newRange.length)
      || (delta < 0 && magnitude > ULONG_MAX - newRange.location - newRange.length))
  {
    row->noteEdit(0);
    return;
  }
  const unsigned long oldLength = delta < 0 ? newRange.length + magnitude : newRange.length - magnitude;
  const TextChangedRange range(newRange.location, oldLength, newRange.length);
  row->noteEdit(&range);
}
@end

namespace
{
  /** Transient UTF-16 units, including embedded NUL. Only the storage survives birth. */
  class BirthBuffer
  {
  public:
    BirthBuffer()
        : units_(0),
          length_(0),
          capacity_(0)
    {
    }
    ~BirthBuffer()
    {
      std::free(this->units_);
    }
    unsigned long length() const
    {
      return this->length_;
    }
    NSString *newString() const
    {
      const unichar empty = 0;
      return MAC_STORE_ALLOC(
          STRING, [[NSString alloc] initWithCharacters:(this->units_ ? this->units_ : &empty) length:this->length_]);
    }
    TextStoreBirth append(unsigned int scalar)
    {
      const unsigned long limit = ULONG_MAX / sizeof(unichar);
      const unsigned count = scalar > 0xffff ? 2 : 1;
      if (this->length_ > limit - count)
        return TEXT_STORE_REFUSED_CAPACITY;
      const unsigned long needed = this->length_ + count;
      if (needed > this->capacity_)
      {
        const unsigned long capacity = needed < 256 ? 256 : needed <= limit / 2 ? needed * 2 : needed;
        void *buffer = MAC_STORE_ALLOC(BUFFER, std::realloc(this->units_, capacity * sizeof(unichar)));
        if (!buffer)
          return TEXT_STORE_REFUSED_ALLOCATION;
        this->units_ = static_cast<unichar *>(buffer);
        this->capacity_ = capacity;
      }
      if (count == 2)
      {
        scalar -= 0x10000;
        this->units_[this->length_++] = static_cast<unichar>(0xd800 + (scalar >> 10));
        scalar = 0xdc00 + (scalar & 0x3ff);
      }
      this->units_[this->length_++] = static_cast<unichar>(scalar);
      return TEXT_STORE_BORN;
    }

  private:
    BirthBuffer(const BirthBuffer &);
    BirthBuffer &operator=(const BirthBuffer &);
    unichar *units_;
    unsigned long length_, capacity_;
  };
  bool high(unichar c)
  {
    return c >= 0xd800 && c <= 0xdbff;
  }
  bool low(unichar c)
  {
    return c >= 0xdc00 && c <= 0xdfff;
  }
  /** Bounded forward read from the native string, including lookahead across refills. */
  class NativeUnits
  {
  public:
    NativeUnits(NSString *text, unsigned long end)
        : text_(text),
          end_(end),
          block_(NSMakeRange(0, 0))
    {
    }
    unichar at(unsigned long offset)
    {
      if (!NSLocationInRange(offset, this->block_))
      {
        this->block_ =
            NSMakeRange(offset,
                        (std::min)(this->end_ - offset,
                                   static_cast<unsigned long>(sizeof this->scratch_ / sizeof *this->scratch_)));
        [this->text_ getCharacters:this->scratch_ range:this->block_];
      }
      return this->scratch_[offset - this->block_.location];
    }

  private:
    NSString *const text_;
    const unsigned long end_;
    NSRange block_;
    unichar scratch_[128];
  };
  bool canonical(NSString *text, unsigned long length, unsigned long position)
  {
    if (!position || position == length)
      return true;
    unichar pair[2];
    [text getCharacters:pair range:NSMakeRange(position - 1, 2)];
    return !(high(pair[0]) && low(pair[1])) && !(pair[0] == '\r' && pair[1] == '\n');
  }
  // Like the Win32 locked read, the caller commits these local counters only on progress.
  TextChunkResult readNativeChunk(
      NSString *text, unsigned long &cursor, unsigned long end, char *out, unsigned long capacity, unsigned long &count)
  {
    const unsigned long length = [text length];
    if (end == kTextNativeEnd)
      end = length;
    if (cursor > end || end > length || !canonical(text, length, cursor) || !canonical(text, length, end))
      return TEXT_CHUNK_REFUSED_RANGE;
    NativeUnits units(text, end);
    while (cursor < end && count < capacity)
    {
      unsigned int scalar = units.at(cursor);
      unsigned consumed = 1;
      if (high(scalar))
      {
        if (cursor + 1 == end)
          return TEXT_CHUNK_REFUSED_MALFORMED;
        const unichar trail = units.at(cursor + 1);
        if (!low(trail))
          return TEXT_CHUNK_REFUSED_MALFORMED;
        scalar = 0x10000 + ((scalar - 0xd800) << 10) + trail - 0xdc00;
        consumed = 2;
      }
      else if (low(scalar))
        return TEXT_CHUNK_REFUSED_MALFORMED;
      else if (scalar == '\r')
      {
        scalar = '\n';
        if (cursor + 1 < end && units.at(cursor + 1) == '\n')
          consumed = 2;
      }
      const unsigned bytes = scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
      if (bytes > capacity - count)
        break;
      if (bytes == 1)
        out[count] = static_cast<char>(scalar);
      else
      {
        const unsigned prefix = bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0;
        out[count] = static_cast<char>(prefix | (scalar >> (6 * (bytes - 1))));
        for (unsigned b = 1; b < bytes; ++b)
          out[count + b] = static_cast<char>(0x80 | ((scalar >> (6 * (bytes - b - 1))) & 0x3f));
      }
      count += bytes;
      cursor += consumed;
    }
    if (!count)
      return cursor == end ? TEXT_CHUNK_END : TEXT_CHUNK_NO_ROOM;
    return TEXT_CHUNK_PROGRESS;
  }
} // namespace

MacTextStore::MacTextStore(TextStoreLedger &ledger, MacScenePlatformController &controller)
    : TextStoreRow(ledger, controller),
      storage_(nil),
      layout_(nil),
      container_(nil),
      view_(nil),
      undo_(nil),
      delegate_(nil)
{
}

bool MacTextStore::buildNetwork()
{
  this->storage_ = MAC_STORE_ALLOC(STORAGE, [[NSTextStorage alloc] initWithString:@""]);
  if (!this->storage_)
    return false;
  this->layout_ = MAC_STORE_ALLOC(LAYOUT, [[NSLayoutManager alloc] init]);
  if (!this->layout_)
    return false;
  [this->storage_ addLayoutManager:this->layout_];
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  // Tiger's NSTextContainer initializer.
  this->container_ =
      MAC_STORE_ALLOC(CONTAINER, [[NSTextContainer alloc] initWithContainerSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)]);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  if (!this->container_)
    return false;
  [this->layout_ addTextContainer:this->container_];
  this->view_ =
      MAC_STORE_ALLOC(VIEW, [[LokaTextDocumentView alloc] initWithFrame:NSZeroRect textContainer:this->container_]);
  if (!this->view_)
    return false;
  this->undo_ = MAC_STORE_ALLOC(UNDO, [[NSUndoManager alloc] init]);
  if (!this->undo_)
    return false;
  this->delegate_ = MAC_STORE_ALLOC(DELEGATE, [[LokaTextStoreDelegate alloc] init]);
  return this->delegate_ != nil;
}

TextStoreBirth MacTextStore::create(TextStoreLedger &ledger,
                                    MacScenePlatformController &controller,
                                    TextStoreSource &source,
                                    TextStoreRow *&fresh,
                                    TextStoreReading &reading)
{
  fresh = 0;
  BirthBuffer buffer;
  const TextPlacement placement = source.requestedPlacement();
  unsigned long caret = 0;
  unsigned int scalar;
  bool separator;
  while (source.next(scalar, separator))
  {
    const TextStoreBirth result = buffer.append(scalar);
    if (result != TEXT_STORE_BORN)
      return result;
    if (placement.kind() == TextPlacement::END
        || (placement.kind() == TextPlacement::AT_UTF8_OFFSET && source.logicalOffset() <= placement.utf8Offset()))
      caret = buffer.length();
  }
  NSString *body = buffer.newString();
  if (!body)
    return TEXT_STORE_REFUSED_ALLOCATION;
  MacTextStore *row = MAC_STORE_ALLOC(ROW, new (std::nothrow) MacTextStore(ledger, controller));
  if (!row || !row->buildNetwork())
  {
    [body release];
    if (row)
      row->retire();
    return TEXT_STORE_REFUSED_ALLOCATION;
  }
  NSTextView *view = row->view_;
  [view setRichText:NO];
  [view setAllowsUndo:YES];
  [view setFieldEditor:NO];
  [view setUsesFontPanel:NO];
  [view setVerticallyResizable:YES];
  [view setMaxSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
  [view setFont:(NSFont *)controller.textFont(TextStyle())];
  [row->delegate_ setOwner:row];
  [row->storage_ setDelegate:(id)row->delegate_];
  [view setDelegate:(id)row->delegate_];
  NSTextStorage *storage = row->storage_;
  [storage beginEditing];
  [storage replaceCharactersInRange:NSMakeRange(0, 0) withString:body];
  [storage endEditing];
  [body release];
  if (row->matches(view))
    [view setSelectedRange:NSMakeRange(caret, 0)];
  if (!row->matches(view))
  {
    row->retire();
    return TEXT_STORE_REFUSED_UNSUPPORTED;
  }
  row->changes_.take();
  const TextChangedRange range(0, 0, buffer.length());
  reading = TextStoreReading(false, &range, caret, caret, false);
  fresh = row;
  return TEXT_STORE_BORN;
}

void MacTextStore::stopMarks()
{
  [this->delegate_ setOwner:0];
  [this->view_ setDelegate:nil];
  [this->storage_ setDelegate:nil];
}
MacTextStore::~MacTextStore()
{
  [this->undo_ removeAllActions];
  if (this->layout_)
    [this->storage_ removeLayoutManager:this->layout_];
  [this->view_ release];
  [this->container_ release];
  [this->layout_ release];
  [this->storage_ release];
  [this->undo_ release];
  [this->delegate_ release];
}
bool MacTextStore::matches(NSTextView *view) const
{
  return view && view == this->view_ && [this->delegate_ owner] == this;
}
bool MacTextStore::read(TextStoreReading &out)
{
  NSTextView *view = this->view_;
  if (!this->matches(view))
    return false;
  const NSRange selection = [view selectedRange];
  const bool marked = [view hasMarkedText];
  if (!this->matches(view))
    return false;
  const TextStoreReading note = this->changes_.take();
  TextChangedRange range;
  out = TextStoreReading(
      note.changed(), note.queryRange(range) ? &range : 0, NSMaxRange(selection), selection.location, marked);
  return true;
}
bool MacTextStore::pending() const
{
  return !this->matches(this->view_) || this->changes_.pending();
}
bool MacTextStore::apply(unsigned long target)
{
  NSTextView *view = this->view_;
  if (!this->matches(view))
    return false;
  const unsigned long length = [this->storage_ length];
  if (!this->matches(view) || target > length)
    return false;
  [view setSelectedRange:NSMakeRange(target, 0)];
  return this->matches(view);
}
void MacTextStore::commitMarkedText()
{
  NSTextView *view = this->view_;
  if (!this->matches(view))
    return;
  const bool marked = [view hasMarkedText];
  if (!this->matches(view) || !marked)
    return;
  [view unmarkText];
  if (!this->matches(view))
    return;
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(![view hasMarkedText]);
#endif
}

TextChunkResult
MacTextStore::chunk(unsigned long &offset, unsigned long end, char *out, unsigned long capacity, unsigned long &written)
{
  written = 0;
  if (!out || !capacity)
    return TEXT_CHUNK_REFUSED_RANGE;
  NSTextView *view = this->view_;
  if (!this->matches(view))
    return TEXT_CHUNK_REFUSED_RESOURCE;
  unsigned long next = offset, count = 0;
  const TextChunkResult result = readNativeChunk([this->storage_ string], next, end, out, capacity, count);
  if (!this->matches(view))
    return TEXT_CHUNK_REFUSED_RESOURCE;
  if (result == TEXT_CHUNK_PROGRESS)
  {
    written = count;
    offset = next;
  }
  return result;
}
