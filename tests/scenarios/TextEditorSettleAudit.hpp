#ifndef LOKA_TESTS_TEXT_EDITOR_SETTLE_AUDIT_HPP
#define LOKA_TESTS_TEXT_EDITOR_SETTLE_AUDIT_HPP
#include "app/nodes/controls/TextEditorRequests.hpp"
#include <cstdio>
#include "app/scene/state/RequestSettlement.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/snap/SnapFormat.hpp"
namespace loka
{
  namespace scenario_tests
  {
    inline const char *SettleStimulusName(app::scene::Settlement stimulus)
    {
      switch (stimulus)
      {
      case app::scene::SETTLE_ATTACH:
        return "attach";
      case app::scene::SETTLE_PROPS:
        return "props";
      case app::scene::SETTLE_INPUT:
        return "input";
      case app::scene::SETTLE_RESTORE:
        return "restore";
      case app::scene::SETTLE_DEFERRED:
        return "deferred";
      }
      return "unknown";
    }
    inline const char *SettleReplyName(app::scene::Reply<app::LineCursor>::Kind kind)
    {
      typedef app::scene::Reply<app::LineCursor> Reply;
      switch (kind)
      {
      case Reply::NO_REPLY:
        return "none";
      case Reply::GRANTED:
        return "granted";
      case Reply::CLAMPED:
        return "clamped";
      case Reply::REFUSED:
        return "refused";
      }
      return "unknown";
    }
    inline std::string SettleCursorText(const app::LineCursor &cursor)
    {
      char text[64];
      std::snprintf(text, sizeof text,
                   "%u:%u:%d",
                   static_cast<unsigned>(cursor.line.generation),
                   static_cast<unsigned>(cursor.line.seq),
                   cursor.column);
      return text;
    }
    inline const char *SettleReplyName(app::scene::Reply<app::EditorCommand>::Kind kind)
    {
      typedef app::scene::Reply<app::EditorCommand> Reply;
      switch (kind)
      {
      case Reply::NO_REPLY:
        return "none";
      case Reply::GRANTED:
        return "granted";
      case Reply::CLAMPED:
        return "clamped";
      case Reply::REFUSED:
        return "refused";
      }
      return "unknown";
    }
    inline std::string SettleRequestedText(const app::LineCursor &cursor)
    {
      return SettleCursorText(cursor);
    }
    inline std::string SettleRequestedText(const app::EditorCommand &command)
    {
      switch (command.kind())
      {
      case app::EditorCommand::NONE:
        return "NONE";
      case app::EditorCommand::PAGE_UP:
        return "PAGE_UP";
      case app::EditorCommand::PAGE_DOWN:
        return "PAGE_DOWN";
      }
      return "unknown";
    }
    inline const char *SettleSeatText(const app::LineCursor &)
    {
      return "";
    }
    inline const char *SettleSeatText(const app::EditorCommand &)
    {
      return " seat=command";
    }
    template <class Request>
    inline std::string SettleRowText(const app::testing::SettleTraceRow<Request, app::LineCursor> &row)
    {
      typedef app::scene::Reply<Request> Reply;
      char text[128];
      std::snprintf(text, sizeof text, "stimulus=%s takes=%u before=", SettleStimulusName(row.stimulus), row.count);
      std::string value = text + SettleCursorText(row.before) + " after=" + SettleCursorText(row.after);
      for (unsigned take = 0; take < row.count; ++take)
      {
        const Reply &reply = row.takes[take];
        std::snprintf(text,
                      sizeof text,
                      " take%u=%s seam=%d reason=%d%s requested=",
                      take,
                      SettleReplyName(reply.kind()),
                      static_cast<int>(row.seam[take]),
                      static_cast<int>(reply.kind() == Reply::REFUSED ? reply.reason() : app::EDITOR_OK),
                      SettleSeatText(Request()));
        value += text + SettleRequestedText(reply.requested()) + " applied=";
        value += reply.kind() == Reply::REFUSED ? "none" : SettleRequestedText(reply.applied());
      }
      return value;
    }
    /** Merge the two request histories by append sequence. Row keys use the
        merged position; caret-only records retain their original format. */
    inline bool RecordTextEditorSettleAudit(long step,
                                            const app::testing::SettleTrace<app::LineCursor> &trace,
                                            dsl::testing::ScenarioAuditSink &audit)
    {
      const app::testing::SettleTrace<app::EditorCommand, app::LineCursor> &commands =
          app::testing::SettleTrace<app::EditorCommand, app::LineCursor>::instance();
      if (app::testing::SettleTraceCapture<app::LineCursor>::overwritten())
        return false;
      dsl::SnapRecord record;
      unsigned caret = 0, command = 0, position = 0;
      while (caret < trace.size() || command < commands.size())
      {
        const bool takeCaret =
            command == commands.size() || (caret < trace.size() && trace.at(caret).seq < commands.at(command).seq);
        const std::string value = takeCaret ? SettleRowText(trace.at(caret++)) : SettleRowText(commands.at(command++));
        char key[64];
        std::snprintf(key, sizeof key, "settle.%ld.%u", step, position++);
        record.set(key, value.c_str());
      }
      return audit.recordVerdict(record);
    }
  } // namespace scenario_tests
} // namespace loka
#endif
