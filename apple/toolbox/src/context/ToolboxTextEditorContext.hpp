#ifndef LOKA_TOOLBOX_TEXT_EDITOR_CONTEXT_HPP
#define LOKA_TOOLBOX_TEXT_EDITOR_CONTEXT_HPP
#include "app/nodes/controls/TextEditorRequests.hpp"
#include "ToolboxScrollKey.hpp"
#include "ToolboxEditorSeat.hpp"
#include "context/ToolboxProjectedNodeContext.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include <TextEdit.h>
#include "ToolboxTextEditorGeometry.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
namespace loka
{
  namespace testing
  {
    class ToolboxTextEditorAccess;
    struct ToolboxTextEditorAdmission;
  } // namespace testing
} // namespace loka

/** Plain whole-document projection. The controller's edit ledger owns TE;
    this context owns cancellation storage and the synchronous input phase. */
class ToolboxTextEditorContext : public ToolboxProjectedNodeContext, public ToolboxEditorSeat
{
public:
  ToolboxTextEditorContext(loka::app::TextEditorNode *,
                           ToolboxScenePlatformController *,
                           const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key);
  virtual ~ToolboxTextEditorContext();
  virtual void onPropsApplied();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  virtual void render(loka::app::scene::IPlatformController *);
  void repaint(TEHandle);
  virtual void repaintNative()
  {
    this->repaint(this->te_);
  }
  virtual loka::app::EditorResult click(const Point &point, const EventRecord &)
  {
    return this->click(point);
  }
  virtual bool offerKeyEvent(const EventRecord &)
  {
    return false;
  }
  virtual bool editVerb(loka::app::EditVerb)
  {
    return false;
  }
  virtual bool hasNativeHandle() const
  {
    return this->te_ != 0;
  }
  ToolboxTextEditorRects geometry() const;
  Rect chromeRect() const
  {
    return this->geometry().frame;
  }
  void idleTick();
  loka::app::EditorResult scrollInput(int);
  loka::app::EditorResult scrollKey(ToolboxScrollKey);
  loka::app::EditorResult key(char);
  loka::app::EditorResult click(const Point &);
  /** Bounded selection replacement door, also used by the scenario's paste action. */
  loka::app::EditorResult paste(const char *, std::size_t);
  void retryProjection();
  /** Called by every controller retirement path before queueing TE. */
  void invalidateNativePresentation();

private:
  friend class loka::testing::ToolboxTextEditorAccess;
  friend struct loka::testing::ToolboxTextEditorAdmission;
  enum ScrollAim
  {
    SCROLL_AIM_LINE,
    SCROLL_AIM_PAGE_UP,
    SCROLL_AIM_PAGE_DOWN,
    SCROLL_AIM_TOP,
    SCROLL_AIM_BOTTOM
  };
  loka::app::EditorResult scrollView(ScrollAim aim, int line);
  enum Phase
  {
    IDLE,
    INPUT,
    RECONCILE,
    PROJECT
  };
  virtual void retireNativeProjection();
  void syncPresentation();
  bool hasStaleCaret() const;
  loka::app::EditorResult beginInput();
  enum Change
  {
    CARET_CHANGE,
    LINE_CHANGE,
    STRUCTURE_CHANGE
  };
  class RailOperation;
  class CommandOperation;
  bool queryVisibleLines(unsigned &) const;
  void scrollTo(loka::app::LineCursor);
  loka::app::EditorResult finishInput(loka::app::EditorResult, Change, RailOperation &);
  loka::app::scene::FollowUp restoreCommittedProjection();
  loka::app::scene::FollowUp project();
  void settle(loka::app::scene::Settlement);
  void settle(loka::app::scene::Settlement, RailOperation &);
  loka::app::LineCursor cursorAt(short) const;
  short offsetOf(loka::app::LineCursor) const;
  void updateRect(const Rect &);
  const loka::app::scene::SeamKey<loka::app::TextEditorNode> key_;
  loka::app::TextEditorNode *node_;
  TEHandle te_;
  char *restore_;
  Rect rect_;
  Rect paintRect_;
  const loka::app::TextBuffer *source_;
  loka::core::ListRevision revision_;
  Phase phase_;
  loka::app::EditorResult status_;
  unsigned restores_;
  short scrollBarId_;
};
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    /** Test-only observation of the private gate, including deferred extraction. */
    struct ToolboxTextEditorAdmission
    {
      static app::scene::Admission
      probe(ToolboxTextEditorContext &, bool command, bool busy, bool &supplied, bool &opened);
    };
  } // namespace testing
} // namespace loka
#endif
bool RegisterToolboxTextEditorNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
