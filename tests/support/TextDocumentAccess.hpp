#ifndef LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#define LOKA_TESTS_TEXT_DOCUMENT_ACCESS_HPP
#include "app/text/TextDocumentRegistration.hpp"
#include "app/text/TextReader.hpp"
#include "app/text/TextDocumentPublisher.hpp"
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
        static void complete(scene::SceneTextDocuments &documents, scene::IPlatformController &controller)
        {
          detail::TextDocumentPublisher::complete(documents, controller);
        }
        static scene::TextDocumentRegistration *registration(TextDocument &document)
        {
          return document.registration_.remote();
        }
        static bool connected(const TextReader &reader)
        {
          return reader.membership_.owner() != 0;
        }
        static void body(TextDocument &document, const std::string &body)
        {
          NullTextStore *row = store(document);
          const TextChangedRange range(0, row->length(), static_cast<unsigned long>(body.size()));
          row->committedText_ = body;
          row->changes_.noteEdit(&range);
        }
        static void duringPublication(scene::SceneTextDocuments &documents, void (*action)(void *), void *data)
        {
          scene::SceneTextDocuments::Publication publication(documents);
          action(data);
        }
        static void withdraw(TextDocument &document)
        {
          document.registration_.remote()->withdraw();
        }
        static NullTextStore *store(TextDocument &document)
        {
          return document.registration_.remote()
                     ? static_cast<NullTextStore *>(document.registration_.remote()->store())
                     : 0;
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
        static scene::PostResult postSpliceDirect(TextDocument &document, const TextSplice &splice)
        {
          return document.splice_.post(splice);
        }
        static unsigned spliceRing(const TextDocument &document)
        {
          return document.splice_.pending();
        }
        static bool splicePending(const TextDocument &document)
        {
          return !document.splice_.state()->get().isNone();
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
