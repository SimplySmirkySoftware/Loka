#ifndef LOKA_TOOLBOX_TEXT_EDITOR_JAPANESE_SCENARIO_HPP
#define LOKA_TOOLBOX_TEXT_EDITOR_JAPANESE_SCENARIO_HPP
#include "ToolboxTextEditorScrollScenario.hpp"
#include <Script.h>

namespace loka
{
  namespace scenario_tests
  {
    /** Observes real Japanese commits and a Roman positive control from Lua.
        Like the key cell, the driver owns the timer and a completed snapshot. */
    class ToolboxTextEditorJapaneseScenario
    {
    public:
      ToolboxTextEditorJapaneseScenario()
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
          app::TextEditorNode &logical = *static_cast<app::TextEditorNode *>(node);
          this->elapsed_ += elapsed;
          if (this->phase_ == OPEN)
          {
            SelectWindow(window.window());
            TESetSelect(0, 32767, te);
            ok = editor->paste("abc", 3) == app::EDITOR_OK;
            // Click after the short line so native and logical caret agree.
            Point first = {(**te).viewRect.top, static_cast<short>((**te).viewRect.right - 1)};
            // One click through the controller focuses the editor and places
            // the caret; a second click at the same point would be a
            // double-click and select the word.
            if (ok)
              ok = ToolboxInputDoor::mouseDown(controller, first);
            this->snapshot_ = Snapshot(logical, te);
            ok = ok && (**te).active && this->snapshot_.document == "abc" && (**te).selStart == 3 && (**te).selEnd == 3;
            this->phase_ = JAPANESE;
            this->elapsed_ = 0;
            if (ok)
              ok = audit.recordLog("ready: abc; caret at end");
          }
          else
          {
            std::string current;
            ok = app::testing::TextEditorAccess::document(logical).project(current) == app::EDITOR_OK;
            if (this->phase_ == JAPANESE)
            {
              ok = ok && this->snapshot_.matches(logical, te, current);
              // Lua commits both strings in the first few seconds, then waits
              // twenty seconds before Roman x. Keep this interval paired with it.
              if (!ok || this->elapsed_ > 15)
              {
                dsl::SnapRecord record = SmirkBenchEditorRecord("text-editor-japanese", JAPANESE, ok);
                record.set("document", current.c_str());
                record.set("caret.selection.document.revision", ok ? "unchanged" : "changed");
                // Clicking the editor synchronizes the keyboard to its Roman
                // font; Lua switches back before typing, and this records it.
                record.setInt("key.script", GetScriptManagerVariable(smKeyScript));
                ok = audit.recordVerdict(record) && ok;
                if (ok)
                  this->phase_ = ROMAN;
              }
            }
            else if (current == this->snapshot_.document + "x")
            {
              ok = ok && (**te).selStart == 4 && (**te).selEnd == 4
                   && logical.props.lines_->revision().get() != this->snapshot_.revision
                   && !(logical.props.cursorState()->get() == this->snapshot_.caret);
              out = SmirkBenchEditorRecord("text-editor-japanese", ROMAN, ok);
              out.set("document", current.c_str());
              out.set("roman.positive.control", ok ? "inserted" : "failed");
              out.setInt("key.script", GetScriptManagerVariable(smKeyScript));
              // Drop focus before completion so a blinking caret cannot keep
              // the capture from settling (text-editor-keys does the same).
              Point outside = {0, 0};
              (void)ToolboxInputDoor::mouseDown(controller, outside);
              return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
            }
            else
              ok = ok && this->snapshot_.matches(logical, te, current);
            if (this->elapsed_ > 60)
              ok = false;
          }
        }
        if (ok)
          return SCENARIO_ADVANCE_PENDING;
        out = SmirkBenchEditorRecord("text-editor-japanese", this->phase_, false);
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }

    private:
      enum Phase
      {
        OPEN,
        JAPANESE,
        ROMAN
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
        bool matches(app::TextEditorNode &node, TEHandle te, const std::string &current) const
        {
          return current == this->document && !(node.props.lines_->revision().get() != this->revision)
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
