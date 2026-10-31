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
      static void scrollTo(ToolboxTextEditorContext &c, const app::LineCursor &cursor)
      {
        c.scrollTo(cursor);
      }
      static void repaint(ToolboxTextEditorContext &c, TEHandle te)
      {
        c.repaint(te);
      }
      static void forgetTE(ToolboxTextEditorContext &c)
      {
        c.te_ = 0;
      }
      static void renderMissingNode(ToolboxTextEditorContext &c, app::scene::IPlatformController *p)
      {
        app::TextEditorNode *saved = c.node_;
        c.node_ = 0;
        c.render(p);
        c.node_ = saved;
      }
      static void projecting(ToolboxTextEditorContext &c, bool value)
      {
        c.phase_ = value ? ToolboxTextEditorContext::PROJECT : ToolboxTextEditorContext::IDLE;
      }
      static bool isProjecting(ToolboxTextEditorContext &c)
      {
        return c.phase_ == ToolboxTextEditorContext::PROJECT;
      }
      static bool reconciling(ToolboxTextEditorContext &c)
      {
        return c.phase_ == ToolboxTextEditorContext::RECONCILE;
      }
      static void unavailable(ToolboxTextEditorContext &c)
      {
        c.status_ = app::EDITOR_UNAVAILABLE;
      }
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
      static bool idle(const ToolboxTextEditorContext &c)
      {
        return c.phase_ == ToolboxTextEditorContext::IDLE;
      }
      static const app::TextBuffer *source(const ToolboxTextEditorContext &c)
      {
        return c.source_;
      }
      static core::ListRevision revision(const ToolboxTextEditorContext &c)
      {
        return c.revision_;
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
