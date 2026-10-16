#include "ScenarioRunAudit.hpp"

ScenarioRunAudit::ScenarioRunAudit(const loka::platform::file::FileHandle &file, const char *name)
    : ScenarioAuditFile(file, name),
      phase_(RUNNING)
{
}

bool ScenarioRunAudit::recordTerminal(loka::dsl::testing::ScenarioAuditTerminalStatus status)
{
  const bool written = ScenarioAuditFile::recordTerminal(status);
  this->phase_ = written && status == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED ? SUCCEEDED : FAILED;
  return written;
}

bool ScenarioRunAudit::finished() const
{
  return this->phase_ != RUNNING;
}

bool ScenarioRunAudit::failed() const
{
  return !this->isValid() || this->phase_ == FAILED;
}
