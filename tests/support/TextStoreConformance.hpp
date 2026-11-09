#ifndef LOKA_TESTS_TEXT_STORE_CONFORMANCE_HPP
#define LOKA_TESTS_TEXT_STORE_CONFORMANCE_HPP
#include "app/text/TextDocumentRegistration.hpp"
#include "support/TestVerify.hpp"
#include "app/scene/projection/PlatformController.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct TextStoreTestAccess
      {
        static bool retired(const scene::TextStoreRow &row)
        {
          return row.phase_ == scene::TextStoreRow::RETIRED;
        }
        static unsigned retiredCount(const scene::TextStoreRow &row)
        {
          unsigned count = 0;
          for (const scene::TextStoreRow *p = row.owner_.retired_; p; p = p->next_)
            ++count;
          return count;
        }
      };
      /** Shared store-birth contract. Each later rail's store-row PR must run this
          helper with its controller fixture. The driver captures birthReading and fresh
          in its createTextStore override and completes through the ordinary App door. */
      template <class Controller, class App>
      void checkTextStoreConformance(Controller &controller,
                                     App &app,
                                     TextDocument &document,
                                     scene::TextStoreRow &old,
                                     const TextReplacement &replacement,
                                     unsigned long nativeEnd,
                                     unsigned long placement)
      {
        LOKA_VERIFY(document.replace(replacement) == scene::POST_ACCEPTED);
        app.reconcileAtCompletion();
        TextChangedRange range;
        LOKA_VERIFY(controller.birthReading.queryRange(range));
        LOKA_VERIFY(!controller.birthReading.changed() && !controller.birthReading.marked());
        LOKA_VERIFY(range.start() == 0 && range.insertedLength() == nativeEnd);
        LOKA_VERIFY(controller.birthReading.caret() == placement && controller.birthReading.anchor() == placement);
        LOKA_VERIFY(TextStoreTestAccess::retired(old));
        LOKA_VERIFY(TextStoreTestAccess::retiredCount(old) > 0);
        scene::TextStoreReading next;
        LOKA_VERIFY(controller.readTextStore(*controller.fresh, next));
        LOKA_VERIFY(!next.changed() && !next.marked());
        LOKA_VERIFY(next.caret() == placement && next.anchor() == placement);
        LOKA_VERIFY(!controller.peekTextStore(*controller.fresh));
        unsigned long length = 0;
        LOKA_VERIFY(document.queryLength(length));
        LOKA_VERIFY(length == replacement.logicalLength());
        unsigned long cursor = nativeEnd, written = 0;
        char scratch[4];
        LOKA_VERIFY(
            controller.readTextChunk(*controller.fresh, cursor, scene::kTextNativeEnd, scratch, sizeof scratch, written)
            == scene::TEXT_CHUNK_END);
      }
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
