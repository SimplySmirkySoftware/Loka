#ifndef SMIRKYCARD_SCENARIO_RUN_AUDIT_HPP
#define SMIRKYCARD_SCENARIO_RUN_AUDIT_HPP
#include "testing/scene/ScenarioAudit.hpp"
/** Remembers only the terminal phase; shutdown happens after the tick unwinds. */
class ScenarioRunAudit : public loka::dsl::testing::ScenarioAuditFile
{
public:
  ScenarioRunAudit(const loka::platform::file::FileHandle &file, const char *name);
  virtual bool recordTerminal(loka::dsl::testing::ScenarioAuditTerminalStatus status);
  bool finished() const;
  bool failed() const;

private:
  enum Phase
  {
    RUNNING,
    SUCCEEDED,
    FAILED
  };
  Phase phase_;
};

#endif
