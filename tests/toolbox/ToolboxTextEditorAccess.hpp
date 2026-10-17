#ifndef LOKA_TEST_TOOLBOX_TEXT_EDITOR_ACCESS_HPP
#define LOKA_TEST_TOOLBOX_TEXT_EDITOR_ACCESS_HPP
#include "context/ToolboxTextEditorContext.hpp"
namespace loka
{
  namespace testing
  {
    /** Scenario/host observations only; no production getters or stored callbacks. */
    class ToolboxTextEditorAccess
    {
    public:
      static void sync(ToolboxTextEditorContext &c)
      {
        c.syncPresentation();
      }
      static short barId(ToolboxTextEditorContext &c)
      {
        return c.scrollBarId_;
      }
      static void busy(ToolboxTextEditorContext &c, bool value)
      {
        c.phase_ = value ? ToolboxTextEditorContext::INPUT : ToolboxTextEditorContext::IDLE;
      }
      static bool visibleLines(ToolboxTextEditorContext &c, unsigned &lines)
      {
        return c.queryVisibleLines(lines);
      }
      static TEHandle te(ToolboxTextEditorContext &c)
      {
        return c.te_;
      }
      static unsigned restores(ToolboxTextEditorContext &c)
      {
        return c.restores_;
      }
      static app::EditorResult status(ToolboxTextEditorContext &c)
      {
        return c.status_;
      }
    };
  } // namespace testing
} // namespace loka
#endif
