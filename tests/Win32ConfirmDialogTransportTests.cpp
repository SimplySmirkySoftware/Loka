#include "Win32ConfirmDialogTransportTests.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "Win32Window.hpp"
#include "context/Win32ConfirmDialogContext.hpp"
#include "context/Win32OpenFileDialogContext.hpp"
#include <cstdio>

/** Same native-equivalent capture seam as Win32DialogResultTestAccess. */
class Win32ConfirmDialogResultTestAccess
{
public:
  static Win32ConfirmDialogContext *create(Win32Window &window, loka::app::ConfirmDialogNode *node)
  {
    return new Win32ConfirmDialogContext(window.hwnd(), node, &window);
  }
  static void queue(Win32ConfirmDialogContext &context, const loka::app::ConfirmResult &result)
  {
    context.registration_ = context.transport_->reserve(context.node_->props);
    LOKA_VERIFY(context.registration_ != 0);
    loka::app::DialogResultTransport::ConfirmReturnPort port(context.registration_);
    context.queueDeferredResult(port, result);
  }
};

void testWin32ConfirmDialogMapsEachButtonToResult()
{
  using loka::app::ConfirmResult;
  LOKA_VERIFY(Win32ConfirmDialogContext::mapNativeResult(IDYES, 665) == ConfirmResult::Save());
  LOKA_VERIFY(Win32ConfirmDialogContext::mapNativeResult(IDNO, 665) == ConfirmResult::Discard());
  LOKA_VERIFY(Win32ConfirmDialogContext::mapNativeResult(IDCANCEL, 665) == ConfirmResult::Cancel());
  LOKA_VERIFY(Win32ConfirmDialogContext::mapNativeResult(0, 665) == ConfirmResult::Error(665));
}

namespace
{
  void countResult(void *data)
  {
    ++*static_cast<int *>(data);
    std::fprintf(stderr, "Confirm result write/emitter observed\n");
  }
  void dispatchWake(HWND window)
  {
    MSG message;
    const UINT wake = Win32OpenFileDialogContext::deferredResultMessage();
    LOKA_VERIFY(PeekMessage(&message, window, wake, wake, PM_REMOVE));
    LOKA_VERIFY(message.wParam == 0 && message.lParam == 0);
    std::fprintf(stderr, "Confirm dispatch: production Window WndProc\n");
    DispatchMessage(&message);
  }
  void retiredBeforeWake(bool reclaimOwner)
  {
    using namespace loka::app;
    Win32Window window(0, WindowProps());
    WindowAdmissionTestApp app(window);
    app.flush();
    assert(window.hwnd() != 0);
    loka::core::MutableState<ConfirmResult> *result = new loka::core::MutableState<ConfirmResult>();
    loka::core::PushStateTracker *tracker = new loka::core::PushStateTracker();
    tracker->addState(result);
    loka::core::EmitterState emitter;
    int writes = 0, emits = 0;
    result->bind(&countResult, &writes, false);
    emitter.bind(&countResult, &emits, false);
    ConfirmDialogNode *node = new ConfirmDialogNode(
        ConfirmDialogProps().result(scene::NodeState<ConfirmResult>(result, tracker)).onResult(&emitter));
    Win32ConfirmDialogContext *context = Win32ConfirmDialogResultTestAccess::create(window, node);
    node->setContext(context);
    Win32ConfirmDialogResultTestAccess::queue(*context, ConfirmResult::Error(665));
    std::fprintf(stderr, "Confirm retire: terminal node deletion, context destructor follows\n");
    delete node;
    if (reclaimOwner)
    {
      result->unbind(&countResult, &writes);
      delete tracker;
      delete result;
      tracker = 0;
      result = 0;
      std::fprintf(stderr, "Confirm owner: result and tracker reclaimed\n");
    }
    dispatchWake(window.hwnd());
    LOKA_VERIFY(writes == 0 && emits == 0);
    app.flush();
    app.flush();
    LOKA_VERIFY(writes == 0 && emits == 0);
    if (result)
      result->unbind(&countResult, &writes);
    delete tracker;
    delete result;
    emitter.unbind(&countResult, &emits);
  }
} // namespace

void testWin32ConfirmDialogRetiredBeforePostedWake()
{
  retiredBeforeWake(false);
}
void testWin32ConfirmDialogOwnerReclaimedBeforePostedWake()
{
  retiredBeforeWake(true);
}

void testWin32ConfirmDialogLiveAdmissionDelivers()
{
  using namespace loka::app;
  Win32Window window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  app.flush();
  loka::core::MutableState<ConfirmResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  loka::core::EmitterState emitter;
  int writes = 0, emits = 0;
  result.bind(&countResult, &writes, false);
  emitter.bind(&countResult, &emits, false);
  ConfirmDialogNode *node = new ConfirmDialogNode(ConfirmDialogProps()
      .result(scene::NodeState<ConfirmResult>(&result, &tracker)).onResult(&emitter));
  Win32ConfirmDialogContext *context = Win32ConfirmDialogResultTestAccess::create(window, node);
  node->setContext(context);
  Win32ConfirmDialogResultTestAccess::queue(*context, ConfirmResult::Save());
  LOKA_VERIFY(writes == 0 && emits == 0 && result.get() == ConfirmResult::none());
  dispatchWake(window.hwnd());
  LOKA_VERIFY(writes == 0 && emits == 0 && result.get() == ConfirmResult::none());
  app.flush();
  LOKA_VERIFY(writes == 1 && emits == 1 && result.get() == ConfirmResult::Save());
  app.flush();
  LOKA_VERIFY(writes == 1 && emits == 1);
  delete node;
  app.flush();
  result.unbind(&countResult, &writes);
  emitter.unbind(&countResult, &emits);
  tracker.removeState(&result);
}

void testWin32ConfirmDialogPropsChangeRevokesPostedResult()
{
  using namespace loka::app;
  Win32Window window(0, WindowProps());
  WindowAdmissionTestApp app(window);
  app.flush();
  loka::core::MutableState<ConfirmResult> result;
  loka::core::PushStateTracker tracker;
  tracker.addState(&result);
  ConfirmDialogNode *node = new ConfirmDialogNode(ConfirmDialogProps()
      .result(scene::NodeState<ConfirmResult>(&result, &tracker)));
  Win32ConfirmDialogContext *context = Win32ConfirmDialogResultTestAccess::create(window, node);
  node->setContext(context);
  Win32ConfirmDialogResultTestAccess::queue(*context, ConfirmResult::Discard());
  node->props.options_ = ConfirmDialogOptions(loka::core::String::Literal("Changed message"));
  context->onPropsApplied();
  dispatchWake(window.hwnd());
  app.flush();
  LOKA_VERIFY(result.get() == ConfirmResult::none());
  delete node;
  app.flush();
  tracker.removeState(&result);
}
