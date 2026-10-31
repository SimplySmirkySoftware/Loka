#ifndef LOKA_TEST_TOOLBOX_PRESENT_HOST_HPP
#define LOKA_TEST_TOOLBOX_PRESENT_HOST_HPP
/** The real Toolbox completion with fixture-owned native neighbors. */
class ToolboxApp : public WindowAdmissionTestApp
{
public:
  struct Cursor
  {
    void reconcile() {}
  } cursorOwner_;
  bool running_;
  explicit ToolboxApp(Window &window)
      : WindowAdmissionTestApp(window),
        running_(true)
  {
  }
  bool syncNativeFocusAll(NativeFocusMode)
  {
    return false;
  }
  void present(ActivationPhase, loka::core::Operation &turn);
};
#include "ToolboxPresent.cpp"
#endif
