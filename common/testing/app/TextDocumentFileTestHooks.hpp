#ifndef LOKA_TESTING_APP_TEXT_DOCUMENT_FILE_TEST_HOOKS_HPP
#define LOKA_TESTING_APP_TEXT_DOCUMENT_FILE_TEST_HOOKS_HPP
#ifdef TEST_BUILD
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Synchronous one-shot probe; clear before the test-owned data dies. */
      void observeTextDocumentSavePasses(void (*probe)(void *), void *data);
      void betweenTextDocumentSavePasses();
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
#endif
