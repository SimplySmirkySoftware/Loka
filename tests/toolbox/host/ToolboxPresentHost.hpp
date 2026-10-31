#ifndef LOKA_TEST_TOOLBOX_PRESENT_HOST_HPP
#define LOKA_TEST_TOOLBOX_PRESENT_HOST_HPP
#include "ToolboxInputDoor.hpp"
/** The real Toolbox completion with fixture-owned native neighbors. */
class ToolboxApp : public WindowAdmissionTestApp
{
public:
  struct Cursor
  {
    void reconcile() {}
  } cursorOwner_;
  bool running_;
  explicit ToolboxApp(Window &window, Window *second = 0)
      : WindowAdmissionTestApp(window, second),
        running_(true)
  {
  }
  bool syncNativeFocusAll(NativeFocusMode);
  void present(ActivationPhase, loka::core::Operation &turn);
};
#include "ToolboxNativeFocusAll.cpp"
#include "ToolboxPresent.cpp"
#endif
