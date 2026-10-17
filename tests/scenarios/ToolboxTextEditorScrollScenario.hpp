#ifndef LOKA_TOOLBOX_TEXT_EDITOR_SCROLL_SCENARIO_HPP
#define LOKA_TOOLBOX_TEXT_EDITOR_SCROLL_SCENARIO_HPP
#include "fixtures/TextEditorOverflow.hpp"
#include "SmirkBenchPlainEditorScenario.hpp"
#include <cstddef>
#include <cstdio>
#include "ToolboxPortScope.hpp"

namespace loka
{
  namespace scenario_tests
  {
    /** Owns the staged native gesture's phase and completed selection snapshot.
        Mouse input arrives from mame-launch.lua, through the normal event loop. */
    class ToolboxTextEditorScrollScenario
    {
    public:
      ToolboxTextEditorScrollScenario()
          : phase_(OPEN),
            elapsed_(0),
            selection_()
      {
      }
      ScenarioAdvance advance(double elapsed,
                              app::scene::Scene *scene,
                              ToolboxWindow &window,
                              ToolboxScenePlatformController &controller,
                              dsl::testing::ScenarioAuditSink &audit,
                              dsl::SnapRecord &out)
      {
        // The external rail reads only these documented System 7 records. Fail
        // the build if the selected native headers do not match that transport.
        typedef char WindowControlOffset[(offsetof(WindowRecord, controlList) == 140) ? 1 : -1];
        typedef char ControlRectOffset[(offsetof(ControlRecord, contrlRect) == 8) ? 1 : -1];
        (void)sizeof(WindowControlOffset);
        (void)sizeof(ControlRectOffset);
        app::scene::Node *node = 0;
        dsl::FlowError error;
        bool ok = dsl::testing::LookupNodeById<app::scene::Node>(scene, "SmirkBench.PlainEditor", node, error)
                  == dsl::FLOW_STEP_SUCCEEDED;
        ToolboxTextEditorContext *editor = ok && node ? static_cast<ToolboxTextEditorContext *>(node->getContext()) : 0;
        TEHandle te = editor ? testing::ToolboxTextEditorAccess::te(*editor) : 0;
        ok = te && testing::ToolboxTextEditorAccess::status(*editor) == app::EDITOR_OK;
        if (ok)
        {
          ToolboxPortScope port(window.window());
          SelectWindow(window.window());
          const ToolboxTextEditorRects geometry = editor->geometry();
          Point point = {static_cast<short>(geometry.bar.bottom - 24), static_cast<short>(geometry.bar.left + 8)};
          ControlRef bar = 0;
          const short part = FindControl(point, window.window(), &bar);
          const Rect idleBefore = (**te).destRect;
          ToolboxInputDoor::idleTextEdits(controller);
          if (!EqualRect(&idleBefore, &(**te).destRect))
            audit.recordLog("TEIdle moved destRect");
          switch (this->phase_)
          {
          case OPEN:
          {
            TESetSelect(0, 32767, te);
            ok = editor->paste(kTextEditorOverflow, sizeof(kTextEditorOverflow) - 1) == app::EDITOR_OK;
            // Pasting leaves the caret at the end and TextEdit's auto-view
            // scrolls there; start the gestures from the top of the document.
            if (ok)
              ok = editor->scrollInput(0) == app::EDITOR_OK;
            // Leave the caret at the first byte; the gesture must preserve it.
            Point first = {(**te).viewRect.top, (**te).viewRect.left};
            if (ok)
              ok = editor->click(first) == app::EDITOR_OK;
            this->selection_ = Selection((**te).selStart, (**te).selEnd);
            if (ok)
            {
              char line[96];
              std::snprintf(line,
                            sizeof(line),
                            "open: nLines=%d lineHeight=%d sel=%d..%d",
                            (**te).nLines,
                            (**te).lineHeight,
                            (**te).selStart,
                            (**te).selEnd);
              ok = audit.recordLog(line);
            }
            this->phase_ = SELECT;
            this->elapsed_ = 0;
            break;
          }
          case SELECT:
          {
            this->elapsed_ += elapsed;
            if ((**te).selStart != (**te).selEnd && (**te).destRect.top < geometry.text.top)
            {
              ok = audit.recordLog("selection drag auto-scrolled native TextEdit");
              Point outside = {0, 0};
              (void)ToolboxInputDoor::mouseDown(controller, outside);
              if (ok)
                ok = editor->scrollInput(0) == app::EDITOR_OK;
              Point first = {(**te).viewRect.top, (**te).viewRect.left};
              if (ok)
                ok = editor->click(first) == app::EDITOR_OK;
              this->selection_ = Selection((**te).selStart, (**te).selEnd);
              this->phase_ = PAGE;
              this->elapsed_ = 0;
            }
            else if (this->elapsed_ > 30)
            {
              char line[128];
              std::snprintf(line,
                            sizeof(line),
                            "select timeout: sel=%d..%d dest.top=%d text.top=%d bar=%d",
                            (**te).selStart,
                            (**te).selEnd,
                            (**te).destRect.top,
                            geometry.text.top,
                            bar ? GetControlValue(bar) : -1);
              (void)audit.recordLog(line);
              ok = false;
            }
            break;
          }
          case PAGE:
            this->elapsed_ += elapsed;
            if (bar && GetControlValue(bar) > 0)
            {
              const long offset = static_cast<long>(geometry.text.top) - (**te).destRect.top;
              const long maximumPixels =
                  static_cast<long>((**te).nLines) * (**te).lineHeight - (geometry.text.bottom - geometry.text.top);
              const long expected =
                  std::min(static_cast<long>(GetControlValue(bar)) * (**te).lineHeight, maximumPixels);
              ok = offset > 0 && offset == expected && this->selection_.matches(te);
              dsl::SnapRecord metrics = SmirkBenchEditorRecord("text-editor-scroll", 1, ok);
              metrics.setInt("lineHeight", (**te).lineHeight);
              metrics.setInt("U.left", geometry.text.left);
              metrics.setInt("U.right", geometry.text.right);
              metrics.setInt("U.top", geometry.text.top);
              metrics.setInt("U.bottom", geometry.text.bottom);
              // A held page press may repeat, so the audit records what every
              // run shares: the press moved the text, and the bar agrees with it.
              metrics.set("page.press", offset > 0 && GetControlValue(bar) > 0 ? "moved down" : "did not move");
              metrics.set("bar.agrees.with.dest", offset == expected ? "yes" : "no");
              metrics.setInt(
                  "page.step.lines",
                  std::max(1L, static_cast<long>(geometry.text.bottom - geometry.text.top) / (**te).lineHeight - 1));
              metrics.setInt("bar.left", geometry.bar.left);
              metrics.setInt("bar.top", geometry.bar.top);
              metrics.setInt("bar.right", geometry.bar.right);
              metrics.setInt("bar.bottom", geometry.bar.bottom);
              metrics.setInt("window.global.left", -window.window()->portBits.bounds.left);
              metrics.setInt("window.global.top", -window.window()->portBits.bounds.top);
              short thumbTop = -1, thumbBottom = -1;
              for (short y = geometry.bar.top; y < geometry.bar.bottom; ++y)
              {
                Point probe = {y, point.h};
                ControlRef hit = 0;
                if (FindControl(probe, window.window(), &hit) == 129 && hit == bar)
                {
                  if (thumbTop < 0)
                    thumbTop = y;
                  thumbBottom = y + 1;
                }
              }
              short thumbLeft = -1, thumbRight = -1;
              if (thumbTop >= 0)
                for (short x = geometry.bar.left; x < geometry.bar.right; ++x)
                {
                  Point probe = {static_cast<short>((thumbTop + thumbBottom) / 2), x};
                  ControlRef hit = 0;
                  if (FindControl(probe, window.window(), &hit) == 129 && hit == bar)
                  {
                    if (thumbLeft < 0)
                      thumbLeft = x;
                    thumbRight = x + 1;
                  }
                }
              metrics.setInt("thumb.left", thumbLeft);
              metrics.setInt("thumb.right", thumbRight);
              metrics.setInt("window.width", window.window()->portRect.right - window.window()->portRect.left);
              metrics.setInt("window.height", window.window()->portRect.bottom - window.window()->portRect.top);
              metrics.setInt("thumb.height", thumbTop >= 0 ? thumbBottom - thumbTop : -1);
              metrics.set("selection", this->selection_.matches(te) ? "unchanged" : "changed");
              ok = audit.recordVerdict(metrics) && ok;
              TESetSelect(0, 32767, te);
              if (ok)
                // Typing over the selection lets TextEdit redraw the shorter
                // document itself; a projected replacement would leave the old
                // lines on screen (a separate, pre-existing repaint gap, #1429).
                ok = editor->key('F') == app::EDITOR_OK;
              this->selection_ = Selection((**te).selStart, (**te).selEnd);
              this->phase_ = INACTIVE;
              this->elapsed_ = 0;
            }
            else if (this->elapsed_ > 30)
            {
              char line[128];
              std::snprintf(line,
                            sizeof(line),
                            "page timeout: sel=%d..%d dest.top=%d text.top=%d bar=%d",
                            (**te).selStart,
                            (**te).selEnd,
                            (**te).destRect.top,
                            geometry.text.top,
                            bar ? GetControlValue(bar) : -1);
              (void)audit.recordLog(line);
              ok = false;
            }
            break;
          case INACTIVE:
            this->elapsed_ += elapsed;
            ok = part == 0 && this->selection_.matches(te) && !(**te).active;
            if (ok && this->elapsed_ > 5)
            {
              audit.recordLog("inactive bar: FindControl part 0; selection and TE activation unchanged");
              audit.recordLog("TEIdle comparison completed; movement, if any, logged separately");
              out = SmirkBenchEditorRecord("text-editor-scroll", 2, true);
              return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
            }
            break;
          }
        }
        if (ok)
          return SCENARIO_ADVANCE_PENDING;
        out = SmirkBenchEditorRecord("text-editor-scroll", 2, false);
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }

    private:
      enum Phase
      {
        OPEN,
        SELECT,
        PAGE,
        INACTIVE
      };
      struct Selection
      {
        short start, end;
        Selection(short a = 0, short b = 0)
            : start(a),
              end(b)
        {
        }
        bool matches(TEHandle te) const
        {
          return (**te).selStart == this->start && (**te).selEnd == this->end;
        }
      };
      Phase phase_;
      double elapsed_;
      Selection selection_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
