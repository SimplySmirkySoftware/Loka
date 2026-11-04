#ifndef LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#define LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#include "app/text/TextDocumentRegistration.hpp"
#include "platform/null/NullTextStore.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      void failTextStoreCreations(unsigned count);
      bool consumeTextStoreCreationFailure();
      struct TextDocumentAccess
      {
        static NullTextStore *store(TextDocument &document)
        {
          return document.registration_ ? static_cast<NullTextStore *>(document.registration_->store()) : 0;
        }
        static bool pending(const TextDocument &document)
        {
          return !document.request_.get().isNone();
        }
        static void edit(TextDocument &document, const TextChangedRange &range)
        {
          NullTextStore *row = store(document);
          assert(row);
          row->length_ = row->length_ - range.length() + range.insertedLength();
          row->changes_.noteEdit(&range);
        }
        static void selection(TextDocument &document, unsigned long caret, unsigned long anchor)
        {
          NullTextStore *row = store(document);
          assert(row);
          row->caret_ = caret;
          row->anchor_ = anchor;
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
