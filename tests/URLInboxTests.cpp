#include "URLInboxTests.hpp"
#include "testing/app/URLInboxAccess.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  using namespace loka::app;
  using loka::core::String;
  typedef loka::app::testing::URLInboxAccess Access;
} // namespace

void testURLInboxOfferResults()
{
  URLInbox<2> owner("myapp");
  LOKA_VERIFY(Access::offer(owner, String("myapp:x")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::count(owner) == 1);
  const char *invalid[] = {"other:x", "myapp:", "myapp:%G0"};
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(Access::offer(owner, String(invalid[i])) == RECEIVE_INVALID);
    LOKA_VERIFY(Access::count(owner) == 1);
  }
}

void testURLInboxFull()
{
  URLInbox<2> owner("myapp");
  LOKA_VERIFY(Access::offer(owner, String("myapp:a")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:b")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:c")) == RECEIVE_FULL);
  LOKA_VERIFY(Access::count(owner) == 2);
  LOKA_VERIFY(Access::offer(owner, String("other:x")) == RECEIVE_INVALID);
  LOKA_VERIFY(Access::count(owner) == 2);
  IncomingURL out;
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:a")));
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:b")));
  LOKA_VERIFY(Access::count(owner) == 0);
}

void testURLInboxFIFO()
{
  URLInbox<3> owner("myapp");
  LOKA_VERIFY(Access::offer(owner, String("MyApp:a")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:b")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:c")) == RECEIVE_QUEUED);
  const char *expected[] = {"myapp:a", "myapp:b", "myapp:c"};
  IncomingURL out;
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(Access::take(owner, out));
    LOKA_VERIFY(out.text().equals(String(expected[i])));
    LOKA_VERIFY(Access::count(owner) == 2 - i);
  }
  const IncomingURL seeded = out;
  LOKA_VERIFY(!Access::take(owner, out));
  LOKA_VERIFY(out == seeded);
}

void testURLInboxWrapAround()
{
  URLInbox<2> owner("myapp");
  LOKA_VERIFY(Access::offer(owner, String("myapp:a")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:b")) == RECEIVE_QUEUED);
  IncomingURL out;
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:a")));
  LOKA_VERIFY(Access::offer(owner, String("myapp:c")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:b")));
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:c")));
  LOKA_VERIFY(Access::count(owner) == 0);
}

void testURLInboxDiscardAll()
{
  URLInbox<3> owner("myapp");
  Access::discardAll(owner);
  LOKA_VERIFY(Access::count(owner) == 0);
  LOKA_VERIFY(Access::offer(owner, String("myapp:a")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:b")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::offer(owner, String("myapp:c")) == RECEIVE_QUEUED);
  Access::discardAll(owner);
  LOKA_VERIFY(Access::count(owner) == 0);
  IncomingURL out;
  LOKA_VERIFY(!Access::take(owner, out));
  LOKA_VERIFY(Access::offer(owner, String("myapp:d")) == RECEIVE_QUEUED);
  LOKA_VERIFY(Access::take(owner, out));
  LOKA_VERIFY(out.text().equals(String("myapp:d")));
  Access::discardAll(owner);
  LOKA_VERIFY(Access::count(owner) == 0);
}

void testURLInboxDeclaredScheme()
{
  URLInbox<1> owner("MyApp");
  LOKA_VERIFY(Access::offer(owner, String("myapp:x")) == RECEIVE_QUEUED);
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  const char *malformed[] = {"", "1abc"};
  for (unsigned i = 0; i < 2; ++i)
  {
    pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      URLInbox<1> invalid(malformed[i]);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#elif defined(NDEBUG)
  const char *malformed[] = {"", "1abc"};
  for (unsigned i = 0; i < 2; ++i)
  {
    URLInbox<1> invalid(malformed[i]);
    LOKA_VERIFY(Access::offer(invalid, String("myapp:x")) == RECEIVE_INVALID);
    LOKA_VERIFY(Access::offer(invalid, String("1abc:x")) == RECEIVE_INVALID);
    LOKA_VERIFY(Access::count(invalid) == 0);
  }
  std::printf("[pass] malformed inbox scheme NDEBUG refusal branch.\n");
#else
  std::printf(
      "[skip] malformed inbox scheme death pin requires Linux debug without ASan; release tests the refusal.\n");
#endif
}
