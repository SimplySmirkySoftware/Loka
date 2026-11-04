#ifndef LOKA_TESTS_NULL_TEXT_STORE_HPP
#define LOKA_TESTS_NULL_TEXT_STORE_HPP
#include "app/text/TextChangeNote.hpp"
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
/** Null owns only native length and selection, with changes independent of any view. */
class NullTextStore : public loka::app::scene::TextStoreRow
{
public:
  NullTextStore(loka::app::scene::TextStoreLedger &ledger, loka::app::scene::IPlatformController &controller)
      : TextStoreRow(ledger, controller),
        length_(0),
        caret_(0),
        anchor_(0)
  {
  }
  loka::app::scene::TextStoreReading take()
  {
    return this->changes_.take(this->caret_, this->anchor_);
  }
  bool pending() const
  {
    return this->changes_.pending();
  }
  unsigned long length() const
  {
    return this->length_;
  }
  void apply(unsigned long caret)
  {
    this->caret_ = this->anchor_ = std::min(caret, this->length_);
  }

private:
  unsigned long length_, caret_, anchor_;
  loka::app::scene::TextChangeNote changes_;
  friend struct loka::app::testing::TextDocumentAccess;
};
#endif
