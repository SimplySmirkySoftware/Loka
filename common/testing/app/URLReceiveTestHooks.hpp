#ifndef LOKA_TESTING_APP_URL_RECEIVE_TEST_HOOKS_HPP
#define LOKA_TESTING_APP_URL_RECEIVE_TEST_HOOKS_HPP
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    void tolerateReceivePending(unsigned count);
    bool consumeReceivePendingTolerance();
  } // namespace testing
} // namespace loka
#endif
#endif
