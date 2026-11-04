#ifndef LOKA_TESTS_WINDOW_ADMISSION_TEST_APP_HPP
#define LOKA_TESTS_WINDOW_ADMISSION_TEST_APP_HPP

#include "app/core/App.hpp"
#include "core/Operation.hpp"
#include "app/core/Window.hpp"

/** Shared Null model of Win32's tail for borrowed and owning test Apps. */
template <class TestApp>
void RunWindowAdmissionOperation(TestApp &app, void (*collect)(void *) = 0, void *data = 0)
{
  loka::core::Operation turn;
  if (collect)
    collect(data);
  turn.settle();
  app.admitAndApplyWindows();
  app.reconcileAtCompletion();
  app.admitAndApplyWindows();
  turn.close();
  app.reclaimWindows();
}

/** Exercises the production App clock with fixture-owned windows. The fixture
    keeps them alive through flush(); destruction returns the borrowed rows. */
class WindowAdmissionTestApp : public App
{
public:
  explicit WindowAdmissionTestApp(Window &first, Window *second = 0) : App(0)
  {
    this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &first));
    if (second)
      this->group_->adopt(second);
  }
  virtual ~WindowAdmissionTestApp() { this->group_->build(); }
  virtual void quit() {}
  using App::flushWindowInvalidations;
  using App::admitAndApplyWindows;
  using App::reclaimWindows;
  using App::hasPendingWindowAdmission;
  void flush() { this->flushWindowInvalidations(); }
  /** Null model of Win32App::flushIterationTail(): two admissions, one reclaim. */
  void operationLoop(void (*collect)(void *) = 0, void *data = 0)
  {
    RunWindowAdmissionOperation(*this, collect, data);
  }
};

#endif
