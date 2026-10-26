#ifndef LOKA_TESTS_PLATFORM_NULL_EDIT_TEXT_CONTEXT_HPP
#define LOKA_TESTS_PLATFORM_NULL_EDIT_TEXT_CONTEXT_HPP

#include "platform/null/NullEditCommandRecorder.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace loka
{
  namespace app
  {
    class EditTextNode;
  }
}

class NullEditTextContext : public loka::app::scene::NativeNodeContext
{
public:
  loka::testing::NullEditCommandRecorder editCommands;
  NullEditTextContext(loka::app::EditTextNode *node, NullScenePlatformController *controller);
  virtual ~NullEditTextContext();

  /** Attach-time read (late-subscriber rule): presentation from the
      current fact, called by the installing handler after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  virtual short layout(loka::app::scene::IPlatformController *controller,
                       loka::app::scene::LayoutState &state);


private:
  loka::app::EditTextNode *node_;
  NullScenePlatformController *controller_;
  NullScenePlatformController::FakeControlHandle *handle_;
};

bool IsNullEditTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler);
void RegisterNullEditTextNodeHandler(NullScenePlatformController &controller);

#endif // LOKA_TESTS_PLATFORM_NULL_EDIT_TEXT_CONTEXT_HPP
