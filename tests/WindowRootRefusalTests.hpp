#ifndef LOKA_TESTS_WINDOW_ROOT_REFUSAL_TESTS_HPP
#define LOKA_TESTS_WINDOW_ROOT_REFUSAL_TESTS_HPP

void testWindowRootCaptureRefusal();
void testWindowRootCopyRefusal();
void testWindowRootAssignmentRefusal();
void testWindowRootConstructionRefusal();
void testWindowRootCloneRefusal();
void testWindowRootResolutionRefusal();
void testWindowRootRefusalRecovery();
void testWindowSceneHandoffRefusal();
void testWindowMixedRootDeclarations();
void testWindowRootSuccessfulCopies();

#include <cstdio>
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "support/TestVerify.hpp"
#include "platform/file/FileHandle.hpp"

namespace WindowRootRefusalTestSupport
{
  class CountingContext : public NullPlatformContext
  {
  public:
    CountingContext()
        : creates(0)
    {
    }
    virtual Window *createWindow(const WindowProps &props)
    {
      ++this->creates;
      return NullPlatformContext::createWindow(props);
    }
    int creates;
  };

  template <class Config> void verifyDeclarationRefusal(const char *auditPath)
  {
    std::remove(auditPath);
    {
      loka::platform::file::FileHandle file;
      file.displayPath = loka::core::String::Literal(auditPath);
      CountingContext context;
      Config config(&context, &file);
      NullApp app(&config);
      LOKA_VERIFY(config.exitCode() == 0);
      config.setApp(&app);
      loka::scenario_tests::testing::failObservedMainDefinitionClones(1);
      app.run();
      loka::scenario_tests::testing::allowObservedMainDefinitionClones();
      LOKA_VERIFY(context.creates == 0);
      LOKA_VERIFY(app.adoptedWindows().empty() && app.bootstrapWindowlessReported());
      LOKA_VERIFY(config.exitCode() != 0);
    }
    std::remove(auditPath);
  }

  inline Window *launchWithRefusedMount(NullApp &app)
  {
    loka::scenario_tests::testing::refuseObservedMainCreation(true);
    app.run();
    loka::scenario_tests::testing::refuseObservedMainCreation(false);
    LOKA_VERIFY(app.adoptedWindows().size() == 1);
    Window *window = app.adoptedWindows()[0];
    LOKA_VERIFY(window && window->scene() && window->scene()->getRootDefinition());
    LOKA_VERIFY(window->scene()->composeRefusedForMemory());
    return window;
  }
} // namespace WindowRootRefusalTestSupport

#endif
