#ifndef LOKA_TESTS_STANDALONE_LOOP_LAUNCH_TESTS_HPP
#define LOKA_TESTS_STANDALONE_LOOP_LAUNCH_TESTS_HPP

void testStandaloneLoopDeclarationRefusalIsWindowless();
void testStandaloneLoopFactoryRefusalIsWindowless();
void testHelloWorldStandaloneLoopCloseBeforeFirstIdleSucceeds();
void testMineSweeperStandaloneLoopCloseBeforeFirstIdleSucceeds();
void testScrapbookStandaloneLoopCloseBeforeFirstIdleSucceeds();
void testTutorialStandaloneLoopCloseBeforeFirstIdleSucceeds();
void testFloppyBirdStandaloneLoopCloseBeforeFirstIdleSucceeds();

#include "WindowRootRefusalTests.hpp"
#include "standalone/StandaloneScenarioSupport.hpp"
#include "support/WindowAdmissionTestApp.hpp"

namespace StandaloneLoopLaunchTestSupport
{
  class LoopCloseApp : public NullApp
  {
  public:
    explicit LoopCloseApp(AppConfigurable *config)
        : NullApp(config)
    {
    }
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
  };

  template <class Config> void verifyCloseBeforeFirstIdle(const char *path)
  {
    std::remove(path);
    {
      loka::platform::file::FileHandle file;
      file.displayPath = loka::core::String::Literal(path);
      WindowRootRefusalTestSupport::CountingContext context;
      Config config(&context, &file, 0, loka::standalone_tests::StandaloneRunControl::REARM_COMPLETED_SCENE);
      LoopCloseApp app(&config);
      LOKA_VERIFY(config.exitCode() == 0);
      config.setApp(&app);
      app.run();
      LOKA_VERIFY(context.creates == 1 && app.adoptedWindows().size() == 1);
      Window *window = app.adoptedWindows()[0];
      LOKA_VERIFY(window && window->scene() && window->scene()->liveNodeCount() > 1);
      LOKA_VERIFY(window->scene()->getAttachedState()->get() && !window->scene()->composeRefusedForMemory());
      app.requestWindowClose(window);
      RunWindowAdmissionOperation(app);
      const int exitAfterClose = config.exitCode();
      LOKA_VERIFY(exitAfterClose == 0);
    }
    std::remove(path);
  }
} // namespace StandaloneLoopLaunchTestSupport

#endif
