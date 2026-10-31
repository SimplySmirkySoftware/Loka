#include "app/nodes/boundary/StdComposition.hpp"
#include "app/core/App.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "core/Operation.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif
namespace
{
  loka::core::MutableState<int> *result;
  loka::core::PushStateTracker *resultTracker;
  class Root : public loka::app::scene::BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const loka::app::scene::BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::FragmentDefinition());
    }
    virtual void detachNode(loka::app::scene::NodeComposition &)
    {
      loka::core::StateTrackerGuard guard(resultTracker);
      result->set(1);
    }
  };
  class RunApp : public App
  {
  public:
    explicit RunApp(Window *window)
        : App(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
    }
    virtual void quit() {}
    void runSequence(bool inside)
    {
      {
        loka::core::Operation::Regime regime;
        if (inside)
          this->retireComponents();
      }
      this->retireComponents();
    }
  };
  void sequence(bool inside)
  {
    loka::core::MutableState<int> value(0);
    loka::core::PushStateTracker tracker;
    tracker.addState(&value);
    result = &value;
    resultTracker = &tracker;
    NullPlatformContext platform;
    RunApp app(new NullWindow(&platform,
                              WindowProps().scene(new loka::app::scene::Scene(loka::app::scene::Boundary<Root>()))));
    app.runSequence(inside);
    LOKA_VERIFY(value.get() == 1);
  }
} // namespace
void testUnicodeTeardown()
{
  sequence(false);
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (!child)
  {
    sequence(true);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  std::puts("[skip] teardown death pin requires Linux without ASan");
#endif
}
