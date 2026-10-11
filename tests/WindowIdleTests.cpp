#include "WindowIdleTests.hpp"

#include <cstdio>
#include "app/core/Window.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/TestVerify.hpp"

#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  static void countIdle(Window *, double, void *userData)
  {
    ++(*static_cast<int *>(userData));
  }
} // namespace

void testWindowIdleHandlerRequiresCadence()
{
  // Positive control: a handler declared with a cadence is accepted and runs.
  NullPlatformContext context;
  int calls = 0;
  Window window(&context,
                WindowProps().onIdle(&countIdle, &calls).idlePolicy(loka::app::IdlePolicy::everyTick()));
  LOKA_VERIFY(window.handleIdle(0.5));
  LOKA_VERIFY(calls == 1);

#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  // A handler without a cadence would never be called (#663); constructing
  // the Window refuses it.
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (!child)
  {
    NullPlatformContext childContext;
    int childCalls = 0;
    Window refused(&childContext, WindowProps().onIdle(&countIdle, &childCalls));
    (void)refused;
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::printf("[skip] idle cadence death pin requires Linux debug without ASan.\n");
#endif
}
