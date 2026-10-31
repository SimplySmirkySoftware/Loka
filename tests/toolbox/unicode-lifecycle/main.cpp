#include "unicode/ToolboxUnicodeEditorOptions.hpp"
/** Own-authored lifetime observation vehicle. This proves nothing about production
 * handler selection, ensure, placement, clip, focus, input, or fatal-stack safety.
 * No object is focused: every event is offered to TXNTSMCheck before dispatch.
 * The ledger exposes bool install, not OSStatus: refusal is printed as an
 * unavailable status, never fabricated as a native error code.
 */
#include "ToolboxUnicodeStartup.hpp"
#include "ToolboxUnicodeEditorLedger.hpp"
#include "ToolboxOutOfMemory.hpp"
#include "ToolboxPlatformContext.hpp"
#include "ToolboxWindow.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "unicode/MacTextEditorSubset.h"
#include <Fonts.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Controls.h>
#include <cstdio>
#include <cstring>

namespace
{
  /** Probe-owned transcript; counts are derived from the production ledger. */
  class Transcript
  {
  public:
    Transcript()
        : file_(std::fopen("LIFE.TXT", "w")),
          length_(0)
    {
      this->text_[0] = 0;
      if (this->file_)
        std::setvbuf(this->file_, 0, _IONBF, 0);
    }
    ~Transcript()
    {
      if (this->file_)
        std::fclose(this->file_);
    }
    void record(const char *step, const char *status, unsigned live, unsigned retired)
    {
      char line[160];
      std::sprintf(line, "%s OSStatus=%s live=%u retired=%u\r", step, status, live, retired);
      if (this->file_)
        std::fputs(line, this->file_);
      const std::size_t size = std::strlen(line);
      if (this->length_ + size < sizeof(this->text_))
      {
        std::memcpy(this->text_ + this->length_, line, size + 1);
        this->length_ += size;
      }
    }
    void draw() const
    {
      const char *start = this->text_;
      short y = 26;
      for (const char *p = start; *p; ++p)
        if (*p == '\r')
        {
          MoveTo(12, y);
          DrawText(start, 0, static_cast<short>(p - start));
          start = p + 1;
          y += 14;
        }
    }

  private:
    Transcript(const Transcript &);
    Transcript &operator=(const Transcript &);
    std::FILE *file_;
    char text_[2048];
    std::size_t length_;
  };
  unsigned live(ToolboxUnicodeEditorLedger &ledger,
                loka::app::scene::NativeNodeContext &a,
                loka::app::scene::NativeNodeContext &b)
  {
    return (ledger.find(&a) ? 1u : 0u) + (ledger.find(&b) ? 1u : 0u);
  }
  bool install(ToolboxUnicodeEditorLedger &ledger,
               ToolboxWindow &window,
               loka::app::scene::NativeNodeContext &context,
               const loka::core::Frame &frame)
  {
    ToolboxUnicodeEditorLedger::Prepared p(&context);
    if (!p.resource())
      return false;
    const unsigned long options = ToolboxUnicodeEditorOptions();
    if (!ledger.install(p.resource(), &window, frame, options))
      return false;
    ledger.link(p.release());
    return true;
  }
} // namespace
int main()
{
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(0);
  loka::toolbox::RequireUnicodeSystem();
  Transcript log;
  {
    loka::toolbox::UnicodeText::Session session;
    loka::toolbox::ArmOutOfMemoryReserve();
    InitCursor();
    log.record("init", "0", 0, 0);
    ToolboxPlatformContext platform;
    {
      ToolboxWindow window(&platform, WindowProps().title("MLTE lifetime").frame(60, 60, 520, 420));
      window.open();
      if (!window.window())
      {
        log.record("window refused", "unavailable", 0, 0);
        return 1;
      }
      SetPort(window.window());
      loka::app::scene::NativeNodeContext a, b;
      ToolboxUnicodeEditorLedger ledger;
      const bool first = install(ledger, window, a, loka::core::Frame(12, 170, 490, 85));
      log.record("install top", first ? "0" : "unavailable", live(ledger, a, b), 0);
      const bool second = install(ledger, window, b, loka::core::Frame(12, 270, 490, 85));
      log.record("install bottom", second ? "0" : "unavailable", live(ledger, a, b), 0);
      Rect rect = {375, 12, 400, 105};
      const unsigned char oomTitle[] = {3, 'O', 'O', 'M'};
      ControlHandle oom =
          NewControl(window.window(), &rect, oomTitle, true, 0, 0, 1, 0 /* standard push button CDEF */, 0);
      rect.left = 115;
      rect.right = 310;
      const unsigned char terminateTitle[] = {
          16, 't', 'e', 'r', 'm', 'i', 'n', 'a', 't', 'e', ' ', '-', '>', ' ', 'O', 'O', 'M'};
      ControlHandle terminate =
          NewControl(window.window(), &rect, terminateTitle, true, 0, 0, 1, 0 /* standard push button CDEF */, 0);
      rect.left = 320;
      rect.right = 500;
      const unsigned char finishTitle[] = {12, 'R', 'e', 't', 'i', 'r', 'e', ' ', '/', ' ', 'Q', 'u', 'i', 't'};
      ControlHandle finish =
          NewControl(window.window(), &rect, finishTitle, true, 0, 0, 1, 0 /* standard push button CDEF */, 0);
      if (!first || !second)
      {
        if (oom)
          HiliteControl(oom, 255);
        if (terminate)
          HiliteControl(terminate, 255);
      }
      if (first)
        ledger.draw(ledger.find(&a));
      if (second)
        ledger.draw(ledger.find(&b));
      log.record("draw", "void", live(ledger, a, b), 0);
      bool running = true;
      while (running)
      {
        EventRecord event;
        WaitNextEvent(everyEvent, &event, 1, 0);
        if (TXNTSMCheck(0, &event))
          continue;
        SetPort(window.window());
        if (event.what == updateEvt)
        {
          BeginUpdate(window.window());
          EraseRect(&window.window()->portRect);
          log.draw();
          DrawControls(window.window());
          if (ledger.find(&a))
            ledger.draw(ledger.find(&a));
          if (ledger.find(&b))
            ledger.draw(ledger.find(&b));
          EndUpdate(window.window());
        }
        else if (event.what == mouseDown)
        {
          WindowPtr hit = 0;
          const short part = FindWindow(event.where, &hit);
          if (hit != window.window())
            continue;
          if (part == inGoAway)
            running = !TrackGoAway(hit, event.where);
          else if (part == inContent)
          {
            Point point = event.where;
            GlobalToLocal(&point);
            ControlHandle control = 0;
            if (FindControl(point, hit, &control) && TrackControl(control, point, 0))
            {
              if (first && second && (control == oom || control == terminate))
              {
                log.record(control == oom ? "OOM" : "terminate -> OOM", "void", live(ledger, a, b), 0);
                if (control == terminate)
                  TXNTerminateTextension();
                loka::toolbox::QuitForOutOfMemory();
              }
              else if (control == finish)
                running = false;
            }
          }
          // No grow dispatch: this probe's geometry is fixed.
        }
      }
      ledger.retire(ledger.take(&a));
      log.record("retire top", "void", live(ledger, a, b), ledger.hasPending() ? 1u : 0u);
      ledger.drain();
      log.record("drain top", "void", live(ledger, a, b), ledger.hasPending() ? 1u : 0u);
      log.draw();
      if (ledger.find(&b))
        ledger.draw(ledger.find(&b));
      ledger.retire(ledger.take(&b));
      ledger.drain();
      log.record("before window close", "void", live(ledger, a, b), ledger.hasPending() ? 1u : 0u);
      // Window owns the controls and disposes them along with its native port.
    }
    log.record("window closed", "void", 0, 0);
  }
  log.record("terminate / Quit", "void", 0, 0);
  return 0;
}
