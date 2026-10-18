#ifndef LOKA_TOOLBOX_TEXT_EDITOR_KEYS_SCENARIO_HPP
#define LOKA_TOOLBOX_TEXT_EDITOR_KEYS_SCENARIO_HPP
#include "ToolboxTextEditorScrollScenario.hpp"
#include <Script.h>

namespace loka
{
  namespace scenario_tests
  {
    /** Real ADB input comes from mame-launch.lua; this owner only observes it.
        The quiet intervals also cover Command input and an unfocused editor. */
    class ToolboxTextEditorKeysScenario
    {
    public:
      ToolboxTextEditorKeysScenario()
          : phase_(OPEN),
            elapsed_(0),
            snapshot_()
      {
      }
      ScenarioAdvance advance(double elapsed,
                              app::scene::Scene *scene,
                              ToolboxWindow &window,
                              ToolboxScenePlatformController &controller,
                              dsl::testing::ScenarioAuditSink &audit,
                              dsl::SnapRecord &out)
      {
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
          const ToolboxTextEditorRects geometry = editor->geometry();
          Point point = {static_cast<short>(geometry.bar.bottom - 24), static_cast<short>(geometry.bar.left + 8)};
          ControlRef bar = 0;
          (void)FindControl(point, window.window(), &bar);
          app::TextEditorNode &logical = *static_cast<app::TextEditorNode *>(node);
          this->elapsed_ += elapsed;
          if (this->phase_ == OPEN)
          {
            SelectWindow(window.window());
            TESetSelect(0, 32767, te);
            ok = editor->paste(kTextEditorOverflow, sizeof(kTextEditorOverflow) - 1) == app::EDITOR_OK;
            if (ok)
              ok = editor->scrollInput(0) == app::EDITOR_OK;
            Point first = {(**te).viewRect.top, (**te).viewRect.left};
            if (ok)
              ok = ToolboxInputDoor::mouseDown(controller, first);
            if (ok)
              ok = editor->click(first) == app::EDITOR_OK;
            TESetSelect(0, 2, te);
            this->snapshot_ = Snapshot(logical, te);
            // The bar found above was inactive before the paste; FindControl
            // answers no control for hilite 255, so look again now.
            bar = 0;
            (void)FindControl(point, window.window(), &bar);
            ok = ok && (**te).active && bar && GetControlMaximum(bar) > 0;
            this->phase_ = DOWN;
            this->elapsed_ = 0;
            if (ok)
              ok = audit.recordLog("ready: focused long document");
          }
          else
          {
            const long offset = static_cast<long>(geometry.text.top) - (**te).destRect.top;
            const long maximumPixels =
                static_cast<long>((**te).nLines) * (**te).lineHeight - (geometry.text.bottom - geometry.text.top);
            const long page =
                std::max(1L, static_cast<long>(geometry.text.bottom - geometry.text.top) / (**te).lineHeight - 1);
            ok = bar && this->snapshot_.matches(logical, te);
            bool arrived = false;
            const char *label = "";
            switch (this->phase_)
            {
            case OPEN:
              break;
            case DOWN:
              arrived = bar && GetControlValue(bar) == page && offset == page * (**te).lineHeight;
              label = "Page Down";
              break;
            case UP:
              arrived = offset == 0;
              label = "Page Up";
              break;
            case END:
              arrived = offset == maximumPixels;
              label = "End";
              break;
            case HOME:
              arrived = offset == 0;
              label = "Home";
              break;
            case COMMAND:
              ok = ok && offset == 0;
              arrived = this->elapsed_ > 3;
              label = "Command Page Down";
              break;
            case UNFOCUSED:
              ok = ok && offset == (**te).lineHeight && !(**te).active;
              arrived = this->elapsed_ > 5;
              label = "unfocused Page Down";
              break;
            }
            if (ok && arrived)
            {
              dsl::SnapRecord record = SmirkBenchEditorRecord("text-editor-keys", this->phase_, true);
              record.set("key", label);
              record.setInt("bar.value", GetControlValue(bar));
              record.setInt("first.visible.line", offset / (**te).lineHeight);
              record.setInt("offset.pixels", offset);
              record.set("caret.selection.document.revision", "unchanged");
              // The click above synchronizes the keyboard to the editor's Roman
              // font; Lua switches back to Japanese, and this records it.
              record.setInt("key.script", GetScriptManagerVariable(smKeyScript));
              ok = audit.recordVerdict(record);
              if (this->phase_ == UNFOCUSED)
              {
                out = SmirkBenchEditorRecord("text-editor-keys", 7, ok);
                return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
              }
              if (this->phase_ == COMMAND)
              {
                // A visible marker for Lua: the next key must arrive only
                // after the controller has dropped editor focus.
                ok = editor->scrollInput(1) == app::EDITOR_OK;
                Point outside = {0, 0};
                (void)ToolboxInputDoor::mouseDown(controller, outside);
              }
              this->phase_ = static_cast<Phase>(this->phase_ + 1);
              this->elapsed_ = 0;
            }
            if (this->elapsed_ > 30)
              ok = false;
          }
        }
        if (ok)
          return SCENARIO_ADVANCE_PENDING;
        out = SmirkBenchEditorRecord("text-editor-keys", this->phase_, false);
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }

    private:
      enum Phase
      {
        OPEN,
        DOWN,
        UP,
        END,
        HOME,
        COMMAND,
        UNFOCUSED
      };
      struct Snapshot
      {
        short start, end;
        app::LineCursor caret;
        core::ListRevision revision;
        std::string document;
        Snapshot()
            : start(0),
              end(0),
              caret(),
              revision(),
              document()
        {
        }
        Snapshot(app::TextEditorNode &node, TEHandle te)
            : start((**te).selStart),
              end((**te).selEnd),
              caret(node.props.cursorState()->get()),
              revision(node.props.lines_->revision().get()),
              document()
        {
          (void)app::testing::TextEditorAccess::document(node).project(this->document);
        }
        bool matches(app::TextEditorNode &node, TEHandle te) const
        {
          std::string current;
          return app::testing::TextEditorAccess::document(node).project(current) == app::EDITOR_OK
                 && current == this->document && !(node.props.lines_->revision().get() != this->revision)
                 && node.props.cursorState()->get() == this->caret && (**te).selStart == this->start
                 && (**te).selEnd == this->end;
        }
      };
      Phase phase_;
      double elapsed_;
      Snapshot snapshot_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
