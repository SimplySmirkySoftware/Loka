#ifndef LOKA_WIN32_TEXT_EDITOR_CONTEXT_HPP
#define LOKA_WIN32_TEXT_EDITOR_CONTEXT_HPP
#include "Win32RetirableContext.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/scene/state/RequestSettlement.hpp"

namespace loka
{
  namespace testing
  {
    class Win32TextEditorAccess;
  }
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

/** Plain whole-document EDIT projection. The app owns the lines and cursor;
    this context owns tentative native input and cancellation, never a draft. */
class Win32TextEditorContext : public Win32RetirableContext
{
public:
  Win32TextEditorContext(Win32ScenePlatformController *controller,
                         HWND parent,
                         const loka::app::scene::LayoutState &state,
                         loka::app::TextEditorNode *node,
                         const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key);
  virtual ~Win32TextEditorContext();
  void readLifecycleFactOnAttach();
  virtual void onPropsApplied();
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  void relayout(const loka::app::scene::LayoutState &state);
  /** Named property separates multiline EDIT notifications from EditText's
      GWLP_USERDATA without scanning controller-owned rows. */
  static Win32TextEditorContext *fromWindow(HWND window);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  friend class Win32InputDoor;
  bool handleCommand(WPARAM wParam, LPARAM lParam);
  friend class loka::testing::Win32TextEditorAccess;
  enum Phase
  {
    IDLE,
    INPUT,
    PASTING,
    COMMIT,
    REJECTED,
    RESTORING,
    RETRY
  };
  /** A pre-input revision fact, consumed before the first model callback.
      Restore coordinates have a longer lifetime than this evidence. */
  class EditEvidence
  {
  public:
    EditEvidence()
        : present_(false),
          revision_()
    {
    }
    explicit EditEvidence(const loka::core::ListRevision &revision)
        : present_(true),
          revision_(revision)
    {
    }
    bool consumeMatching(const loka::core::ListRevision &revision)
    {
      const bool matches = this->present_ && this->revision_ == revision;
      this->clear();
      return matches;
    }
    void clear()
    {
      this->present_ = false;
    }

  private:
    bool present_;
    loka::core::ListRevision revision_;
  };
  enum CaptureIntent
  {
    RESTORE_COORDINATES,
    PRE_INPUT
  };
  struct Selection
  {
    EditEvidence evidence;
    DWORD start, end;
    int firstVisible, horizontal;
    Selection()
        : start(0),
          end(0),
          firstVisible(0),
          horizontal(0)
    {
    }
  };
  /** Reserved serialization of a committed owner revision, never native draft
      text. Native delivery success is recorded separately in status/delivery. */
  struct Projection
  {
    const loka::app::TextBuffer *owner;
    loka::core::ListRevision revision;
    std::string text;
    std::wstring wide;
    Projection();
    bool current(const loka::app::TextEditorNode &node) const;
    loka::app::EditorResult capture(loka::app::TextEditorNode &node,
                                    const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key);
  };
  LRESULT handleInputMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void handleRestoreTimer();
  static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
  void captureSelection(CaptureIntent intent = RESTORE_COORDINATES);
  class RailOperation;
  class CommandOperation;
  bool queryVisibleLines(unsigned &lines) const;
  void syncFromNode(loka::app::scene::Settlement stimulus = loka::app::scene::SETTLE_PROPS);
  void settle(loka::app::scene::Settlement stimulus);
  void settle(loka::app::scene::Settlement stimulus, RailOperation &op);
  loka::app::scene::FollowUp restoreCommittedProjection();
  loka::app::scene::FollowUp replaceProjection();
  loka::app::scene::FollowUp deferRestore();
  void syncCaret();
  void restoreSelection();
  loka::app::EditorResult commitNativeChange();
  loka::app::EditorResult applyLines(int first, int oldCount, int newCount, const std::string &logical);
  loka::app::RowCursor nativeRowCaret(const std::string &logical) const;
  loka::app::LineCursor nativeCaret() const;

  const loka::app::scene::SeamKey<loka::app::TextEditorNode> key_;
  loka::app::TextEditorNode *node_;
  WNDPROC previousProc_;
  Phase phase_;
  loka::app::EditorResult status_;
  Selection selection_;
  Projection projection_;
  unsigned restores_;
  loka::app::scene::PaintAnswer delivery_;
};
void RegisterWin32TextEditorNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
