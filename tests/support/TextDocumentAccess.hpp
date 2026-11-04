#ifndef LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#define LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#include "app/text/TextDocumentRegistration.hpp"
#include "app/scene/SceneTextDocuments.hpp"
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
        static void duringPublication(scene::SceneTextDocuments &documents, void (*action)(void *), void *data)
        {
          scene::SceneTextDocuments::Publication publication(documents);
          action(data);
        }
        static void withdraw(TextDocument &document)
        {
          document.registration_->withdraw();
        }
        static NullTextStore *store(TextDocument &document)
        {
          return document.registration_ ? static_cast<NullTextStore *>(document.registration_->store()) : 0;
        }
        static const std::string &committedText(TextDocument &document)
        {
          return store(document)->committedText_;
        }
        static void uncommitted(TextDocument &document, const char *text)
        {
          store(document)->composition_ = NullTextStore::Composition(text);
        }
        static void onCommit(TextDocument &document, void (*callback)(void *), void *data)
        {
          store(document)->commitProbe_.callback = callback;
          store(document)->commitProbe_.data = data;
        }
        static unsigned commitCalls(TextDocument &document)
        {
          return store(document)->commitProbe_.calls;
        }
        static bool pending(const TextDocument &document)
        {
          return !document.request_.get().isNone();
        }
        static void edit(TextDocument &document, const TextChangedRange &range)
        {
          NullTextStore *row = store(document);
          assert(row);
          row->committedText_.replace(range.start(), range.length(), range.insertedLength(), '?');
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
