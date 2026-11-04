#ifndef LOKA_TESTS_NULL_TEXT_STORE_HPP
#define LOKA_TESTS_NULL_TEXT_STORE_HPP
#include "app/text/TextChangeNote.hpp"
#include <string>
namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct TextDocumentAccess;
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

private:
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
