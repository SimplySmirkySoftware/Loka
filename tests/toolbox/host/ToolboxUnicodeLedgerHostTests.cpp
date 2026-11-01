#include <cstdio>
#include "ToolboxUnicodeEditorLedger.hpp"
#include "ToolboxEditorFrame.hpp"
#include "ToolboxTextensionFake.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "core/Frame.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/TestVerify.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

void SetRect(Rect *r, short l, short t, short right, short bottom)
{
  r->left = l;
  r->top = t;
  r->right = right;
  r->bottom = bottom;
}
static const unsigned long options = kTXNReadOnlyMask | kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask
                                     | kTXNDontDrawCaretWhenInactiveMask | kTXNDoNotInstallDragProcsMask;
static void install(ToolboxUnicodeEditorLedger &ledger, ToolboxWindow &window, loka::app::scene::NodeContext *context)
{
  ToolboxUnicodeEditorLedger::Prepared p(context);
  LOKA_VERIFY(p.resource());
  const std::size_t before = TextensionFake::calls().size();
  LOKA_VERIFY(ledger.install(p.resource(), &window, loka::core::Frame(10, 20, 100, 80), options));
  LOKA_VERIFY(TextensionFake::calls().size() == before + 3);
  LOKA_VERIFY(!ledger.find(context) && "install does not commit");
  const TextensionFake::Creation &creation = TextensionFake::creations().back();
  LOKA_VERIFY(creation.window == window.window() && creation.options == options);
  LOKA_VERIFY(creation.frame.left == 10 && creation.frame.top == 20 && creation.frame.right == 110
              && creation.frame.bottom == 100);
  const std::vector<TextensionFake::Control> &controls = TextensionFake::controls();
  LOKA_VERIFY(controls.size() >= 2);
  LOKA_VERIFY(controls[controls.size() - 2].tag == kTXNInlineStateTag
              && controls[controls.size() - 2].value == kTXNUseBottomline);
  LOKA_VERIFY(controls.back().tag == kTXNDisableDragAndDropTag && controls.back().value == 1);
  ledger.link(p.release());
  LOKA_VERIFY(!p.resource() && ledger.find(context));
}
static void atomicity()
{
  using namespace loka::core::testing;
  ToolboxWindow window;
  loka::app::scene::NativeNodeContext context;
  TextensionFake::reset();
  failLokaAllocRaw("ToolboxUnicodeEditorLedger", "Resource", 1);
  {
    ToolboxUnicodeEditorLedger::Prepared p(&context);
    LOKA_VERIFY(!p.resource());
  }
  LOKA_VERIFY(TextensionFake::calls().empty() && lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
  for (unsigned failure = 0; failure != 3; ++failure)
  {
    TextensionFake::reset();
    failLokaAllocRaw("ToolboxUnicodeEditorLedger", "Resource", 0);
    {
      ToolboxUnicodeEditorLedger ledger;
      ToolboxUnicodeEditorLedger::Prepared p(&context);
      LOKA_VERIFY(p.resource() && lokaAllocRawLive() == 1);
      if (!failure)
        TextensionFake::failNext("new", memFullErr);
      else
        TextensionFake::failControl(failure, memFullErr);
      LOKA_VERIFY(!ledger.install(p.resource(), &window, loka::core::Frame(10, 20, 100, 80), options));
      LOKA_VERIFY(!ledger.find(&context) && !ledger.hasPending());
      LOKA_VERIFY(TextensionFake::calls().size() == (failure ? failure + 2u : 1u));
      LOKA_VERIFY(TextensionFake::deletes() == (failure ? 1u : 0u));
      LOKA_VERIFY(TextensionFake::live() == 0 && lokaAllocRawLive() == 1);
    }
    LOKA_VERIFY(lokaAllocRawLive() == 0);
    LOKA_VERIFY(TextensionFake::deletes() == (failure ? 1u : 0u));
    allowLokaAllocRaw();
  }
}
static void lifecycle()
{
  TextensionFake::reset();
  ToolboxWindow window, secondWindow;
  ToolboxUnicodeEditorLedger ledger, second;
  loka::app::scene::NativeNodeContext a, b, c;
  install(ledger, window, &a);
  install(ledger, window, &b);
  install(second, secondWindow, &c);
  LOKA_VERIFY(TextensionFake::live() == 3 && TextensionFake::deletes() == 0);
  ledger.draw(ledger.find(&a));
  TextensionFake::beginUpdate();
  ledger.draw(ledger.find(&b));
  TextensionFake::endUpdate();
  LOKA_VERIFY(TextensionFake::draws().size() == 2);
  LOKA_VERIFY(TextensionFake::draws()[0].updateDepth == 0 && TextensionFake::draws()[1].updateDepth == 1);
  ledger.retire(ledger.take(&a));
  LOKA_VERIFY(!ledger.find(&a) && ledger.find(&b) && ledger.hasPending());
  ledger.drain();
  LOKA_VERIFY(!ledger.hasPending() && TextensionFake::live() == 2 && TextensionFake::deletes() == 1);
  ledger.drain();
  LOKA_VERIFY(TextensionFake::deletes() == 1);
  // Model the close/scene-exchange retirement door; native DisposeWindow ordering is rig-observed.
  second.retire(second.take(&c));
  second.drain();
  LOKA_VERIFY(!second.hasPending() && TextensionFake::live() == 1 && ledger.find(&b));
  ledger.retire(ledger.take(&b));
  ledger.drain();
  LOKA_VERIFY(TextensionFake::live() == 0 && TextensionFake::deletes() == 3);
  install(ledger, window, &a);
  ledger.retire(ledger.take(&a));
  ledger.drain();
  LOKA_VERIFY(TextensionFake::live() == 0);
}
static void destructorDeath(bool retired)
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (!child)
  {
    ToolboxWindow window;
    loka::app::scene::NativeNodeContext context;
    {
      ToolboxUnicodeEditorLedger ledger;
      install(ledger, window, &context);
      if (retired)
        ledger.retire(ledger.take(&context));
    }
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
  (void)retired;
  std::puts(
      "[skip] ledger destructor death pin requires Linux without ASan or NDEBUG (the list asserts are compiled out)");
#endif
}
void testUnicodeTeardown();
int main()
{
  const Rect high = ToolboxEditorFrame(1, 1, INT_MAX, INT_MAX);
  const Rect low = ToolboxEditorFrame(-1, -1, INT_MIN, INT_MIN);
  LOKA_VERIFY(high.left == high.right && high.top == high.bottom);
  LOKA_VERIFY(low.left == low.right && low.top == low.bottom);
  const Rect edge = ToolboxEditorFrame(1, 1, SHRT_MAX - 1, SHRT_MAX - 1);
  LOKA_VERIFY(edge.right == SHRT_MAX && edge.bottom == SHRT_MAX);
  testUnicodeTeardown();
  atomicity();
  lifecycle();
  destructorDeath(false);
  destructorDeath(true);
}
