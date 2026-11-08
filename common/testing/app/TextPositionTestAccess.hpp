#ifndef LOKA_TESTING_APP_TEXT_POSITION_TEST_ACCESS_HPP
#define LOKA_TESTING_APP_TEXT_POSITION_TEST_ACCESS_HPP
#ifndef TEST_BUILD
#error TextPositionTestAccess is available only in TEST_BUILD
#endif
#include "app/text/TextDocumentValues.hpp"
#include "app/text/TextStore.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Test-only construction and inspection of opaque positions. */
      struct TextPositionTestAccess
      {
        static TextPosition make(unsigned long revision, unsigned long offset)
        {
          return TextPosition(revision, offset);
        }
        static unsigned long revision(const TextPosition &position)
        {
          return position.revision_;
        }
        static unsigned long offset(const TextPosition &position)
        {
          return position.offset_;
        }
      };
      /** Test-only construction of kernel-published facts. */
      struct TextDocumentFactTestAccess
      {
        static TextDocumentFact make(TextDocumentState state,
                                     unsigned long revision,
                                     const TextPosition &caret,
                                     const TextPosition &anchor,
                                     const TextChangedRange *range = 0)
        {
          return TextDocumentFact(state,
                                  revision,
                                  caret,
                                  anchor,
                                  range ? TextPositionTestAccess::make(revision, range->start()) : TextPosition::None(),
                                  range
                                      ? TextPositionTestAccess::make(revision, range->start() + range->insertedLength())
                                      : TextPosition::None());
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
