#ifndef LOKA_TESTING_APP_TEXT_INPUT_COMMIT_TEST_HOOKS_HPP
#define LOKA_TESTING_APP_TEXT_INPUT_COMMIT_TEST_HOOKS_HPP
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    /** Reset the count and install a synchronous test-owned probe; clear it before its owner dies. */
    void observeTextInputCommits(void (*probe)(void *), void *data);
    unsigned textInputCommitCount();
    void noteTextInputCommit();
  } // namespace testing
} // namespace loka
#endif
#endif
