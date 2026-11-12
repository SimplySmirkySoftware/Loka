#ifndef LOKA_TESTS_NULL_TEXT_STORE_HPP
#define LOKA_TESTS_NULL_TEXT_STORE_HPP
#include "app/text/TextChangeNote.hpp"
#include <string>
#include "core/Utf8Decode.hpp"
#include "app/text/TextSeparators.hpp"
#include "app/scene/projection/PlatformController.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct TextDocumentAccess;
#ifdef TEST_BUILD
      void failTextSplices(TextSpliceResult reason, unsigned count);
      TextSpliceResult consumeTextSpliceFailure();
      void failTextSpliceAfterChange(unsigned count);
      bool consumeTextSpliceAfterChangeFailure();
      void failTextStoreReads(unsigned count);
      bool consumeTextStoreReadFailure();
      void failTextStoreBirths(scene::TextStoreBirth reason, unsigned count);
      scene::TextStoreBirth consumeTextStoreBirthFailure();
      void startTextDocumentRevision(TextDocument &, unsigned long revision);
      void failTextChunkReads(unsigned count);
      /** Allow k chunk calls (including END), then refuse n calls with RESOURCE. */
      void failTextChunkReadsAfter(unsigned successes, unsigned failures);
      bool consumeTextChunkReadFailure();
#endif
    }
  } // namespace app
} // namespace loka
/** Null owns the native body and selection, with changes independent of any view. */
class NullTextStore : public loka::app::scene::TextStoreRow
{
public:
  NullTextStore(loka::app::scene::TextStoreLedger &ledger, loka::app::scene::IPlatformController &controller)
      : TextStoreRow(ledger, controller),
        caret_(0),
        anchor_(0)
  {
  }
  /** Construct the native body in one source traversal, then consume construction marks. */
  loka::app::scene::TextStoreReading build(loka::app::TextStoreSource &source)
  {
    this->committedText_ = materialize(source);
    const loka::app::TextPlacement placement = source.appliedPlacement();
    this->apply(placement.kind() == loka::app::TextPlacement::START ? 0
                : placement.kind() == loka::app::TextPlacement::END ? this->length()
                                                                    : placement.utf8Offset());
    const loka::app::TextChangedRange range(0, 0, this->length());
    this->changes_.noteEdit(&range);
    this->take();
    return loka::app::scene::TextStoreReading(false, &range, this->caret_, this->anchor_, false);
  }
  /** Validate first, prepare the entire new body, then commit and consume marks. */
  loka::app::TextSpliceResult splice(unsigned long from,
                                     unsigned long to,
                                     loka::app::TextStoreSource &source,
                                     loka::app::scene::TextStoreReading &out)
  {
    using namespace loka::app;
    if (from > to || to > this->length() || !this->canonical(from) || !this->canonical(to))
      return TEXT_SPLICE_RANGE;
#ifdef TEST_BUILD
    const TextSpliceResult refusal = loka::app::testing::consumeTextSpliceFailure();
    if (refusal != TEXT_SPLICE_OK)
      return refusal;
#endif
    // The test-native store assumes std::string allocation succeeds.
    const std::string inserted = materialize(source);
    const unsigned long length = static_cast<unsigned long>(inserted.size());
    std::string prepared(this->committedText_);
    prepared.replace(from, to - from, inserted);
    const TextPlacement placement = source.appliedPlacement();
    const unsigned long caret = from
                                + (placement.kind() == TextPlacement::START ? 0
                                   : placement.kind() == TextPlacement::END ? length
                                                                            : placement.utf8Offset());
    this->committedText_.swap(prepared);
    this->apply(caret);
    const TextChangedRange range(from, to - from, length);
    this->changes_.noteEdit(&range);
#ifdef TEST_BUILD
    // Deliberate contract violation for the kernel's release recovery pins.
    if (loka::app::testing::consumeTextSpliceAfterChangeFailure())
      return TEXT_SPLICE_ALLOCATION;
#endif
    this->take();
    out = scene::TextStoreReading(false, &range, this->caret_, this->anchor_, false);
    return TEXT_SPLICE_OK;
  }
  loka::app::scene::TextStoreReading take()
  {
    const loka::app::scene::TextStoreReading changes = this->changes_.take(this->caret_, this->anchor_);
    loka::app::TextChangedRange range;
    return loka::app::scene::TextStoreReading(changes.changed(),
                                              changes.queryRange(range) ? &range : 0,
                                              this->caret_,
                                              this->anchor_,
                                              this->composition_.marked);
  }
  void commit()
  {
    ++this->commitProbe_.calls;
    // Snapshot the callback: a nested door must not recursively invoke this hook.
    void (*callback)(void *) = this->commitProbe_.callback;
    void *data = this->commitProbe_.data;
    this->commitProbe_.callback = 0;
    if (callback)
      callback(data);
    if (this->composition_.marked)
    {
      const unsigned long added = static_cast<unsigned long>(this->composition_.text.size());
      if (added)
      {
        const loka::app::TextChangedRange range(this->length(), 0, added);
        this->committedText_ += this->composition_.text;
        this->caret_ = this->anchor_ = this->length();
        this->changes_.noteEdit(&range);
      }
      this->composition_ = Composition();
    }
  }
  bool pending() const
  {
    return this->changes_.pending();
  }
  unsigned long length() const
  {
    return static_cast<unsigned long>(this->committedText_.size());
  }
  void apply(unsigned long caret)
  {
    this->caret_ = this->anchor_ = std::min(caret, this->length());
  }

  loka::app::scene::TextChunkResult
  readChunk(unsigned long &offset, unsigned long end, char *out, unsigned long capacity, unsigned long &written) const
  {
    using namespace loka::app::scene;
    written = 0;
    if (!capacity || !out)
      return TEXT_CHUNK_REFUSED_RANGE;
    if (end == kTextNativeEnd)
      end = this->length();
    if (offset > end || end > this->length() || !this->canonical(offset) || !this->canonical(end))
      return TEXT_CHUNK_REFUSED_RANGE;
#ifdef TEST_BUILD
    if (loka::app::testing::consumeTextChunkReadFailure())
      return TEXT_CHUNK_REFUSED_RESOURCE;
#endif
    if (offset == end)
      return TEXT_CHUNK_END;
    unsigned long cursor = offset, count = 0;
    while (cursor < end)
    {
      std::size_t next = cursor;
      unsigned int scalar = 0;
      if (!loka::core::detail::DecodeUtf8Codepoint(
              this->committedText_.data(), this->committedText_.size(), next, scalar))
        return TEXT_CHUNK_REFUSED_MALFORMED;
      const bool newline =
          loka::app::TextSeparators::Fold(scalar, this->committedText_.data(), this->committedText_.size(), next);
      const unsigned long bytes = newline ? 1 : static_cast<unsigned long>(next - cursor);
      if (bytes > capacity - count)
        break;
      if (newline)
        out[count] = '\n';
      else
        for (unsigned long i = 0; i < bytes; ++i)
          out[count + i] = this->committedText_[cursor + i];
      count += bytes;
      cursor = static_cast<unsigned long>(next);
    }
    if (!count)
      return TEXT_CHUNK_NO_ROOM;
    offset = cursor;
    written = count;
    return TEXT_CHUNK_PROGRESS;
  }

private:
  static std::string materialize(loka::app::TextStoreSource &source)
  {
    std::string out;
    unsigned int scalar;
    bool separator;
    while (source.next(scalar, separator))
    {
      if (scalar < 0x80)
        out += static_cast<char>(scalar);
      else
      {
        const unsigned count = scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
        out += static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | (scalar >> (6 * (count - 1))));
        for (unsigned i = count - 1; i; --i)
          out += static_cast<char>(0x80 | ((scalar >> (6 * (i - 1))) & 0x3f));
      }
    }
    return out;
  }
  bool canonical(unsigned long offset) const
  {
    if (offset == 0 || offset == this->length())
      return true;
    const unsigned char byte = static_cast<unsigned char>(this->committedText_[offset]);
    return (byte & 0xc0) != 0x80 && !(this->committedText_[offset - 1] == '\r' && byte == '\n');
  }
  /** Test-native composition, independent of committed length and edit notifications. */
  struct Composition
  {
    explicit Composition(const char *value = 0)
        : text(value ? value : ""),
          marked(value != 0)
    {
    }
    std::string text;
    bool marked;
  };
  /** Synchronous fixture hook; the fixture owns data until the call returns. */
  struct CommitProbe
  {
    CommitProbe()
        : calls(0),
          callback(0),
          data(0)
    {
    }
    unsigned calls;
    void (*callback)(void *);
    void *data;
  };
  std::string committedText_;
  Composition composition_;
  CommitProbe commitProbe_;
  unsigned long caret_, anchor_;
  loka::app::scene::TextChangeNote changes_;
  friend struct loka::app::testing::TextDocumentAccess;
};
#endif
