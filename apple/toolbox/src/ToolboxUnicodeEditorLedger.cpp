#include "ToolboxUnicodeEditorLedger.hpp"
#if LOKA_TOOLBOX_UNICODE
// page 2c: the TXN implementations. Page 2b keeps both branches inert.
ToolboxUnicodeEditorLedger::ToolboxUnicodeEditorLedger()
    : live_(0),
      retired_(0)
{
}
ToolboxUnicodeEditorLedger::~ToolboxUnicodeEditorLedger() {}
bool ToolboxUnicodeEditorLedger::install(ToolboxUnicodeEditorResource *,
                                         ToolboxWindow *,
                                         const loka::core::Frame &,
                                         unsigned long)
{
  return false;
}
void ToolboxUnicodeEditorLedger::link(ToolboxUnicodeEditorResource *) {}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::find(loka::app::scene::NodeContext *)
{
  return 0;
}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::take(loka::app::scene::NodeContext *)
{
  return 0;
}
void ToolboxUnicodeEditorLedger::retire(ToolboxUnicodeEditorResource *) {}
bool ToolboxUnicodeEditorLedger::hasPending() const
{
  return false;
}
void ToolboxUnicodeEditorLedger::drain() {}
#else
ToolboxUnicodeEditorLedger::ToolboxUnicodeEditorLedger()
    : live_(0),
      retired_(0)
{
}
ToolboxUnicodeEditorLedger::~ToolboxUnicodeEditorLedger() {}
bool ToolboxUnicodeEditorLedger::install(ToolboxUnicodeEditorResource *,
                                         ToolboxWindow *,
                                         const loka::core::Frame &,
                                         unsigned long)
{
  return false;
}
void ToolboxUnicodeEditorLedger::link(ToolboxUnicodeEditorResource *) {}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::find(loka::app::scene::NodeContext *)
{
  return 0;
}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::take(loka::app::scene::NodeContext *)
{
  return 0;
}
void ToolboxUnicodeEditorLedger::retire(ToolboxUnicodeEditorResource *) {}
bool ToolboxUnicodeEditorLedger::hasPending() const
{
  return false;
}
void ToolboxUnicodeEditorLedger::drain() {}
#endif
