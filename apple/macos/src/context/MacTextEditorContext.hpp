#ifndef LOKA_MAC_TEXT_EDITOR_CONTEXT_HPP
#define LOKA_MAC_TEXT_EDITOR_CONTEXT_HPP

#include "app/nodes/controls/TextEditorRequests.hpp"
#include "MacRetirableContext.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/scene/state/RequestSettlement.hpp"

namespace loka
{
  namespace testing
  {
    class MacTextEditorAccess;
  }
} // namespace loka
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

/** Whole-document native projection. The node owns the mutation seam; the
    context owns native presentation and cancellable reconciliation only. */
class MacTextEditorContext : public MacRetirableContext
{
public:
  /** Borrow a context only from this rail's class-checked native participant. */
  static MacTextEditorContext *fromNativeFocus(void *responder);
  MacTextEditorContext(MacScenePlatformController *, void *parent, loka::app::TextEditorNode *,
                       const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key);
  virtual ~MacTextEditorContext();
  bool hasNativeView() const;
  /** Borrow the document text view used as the native focus destination, or 0. */
  void *nativeFocusView() const;
  void readLifecycleFactOnAttach();
  virtual void onPropsApplied();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  /** Native notification origin; only the view supplies a selection offset. */
  enum TextObservation
  {
    VIEW_CHANGE,
    STORAGE_EDIT
  };

private:
  friend class MacInputDoor;
  void handleTextDidChange(TextObservation source, std::size_t caretOffset);
  void handleSelectionDidChange();
  void captureSelection();
  void restoreCommittedProjection();
  void applyHighlights();
  friend class loka::testing::MacTextEditorAccess;
  struct Projection;
  Projection *projection_;
  unsigned restores_;
  const loka::app::scene::SeamKey<loka::app::TextEditorNode> key_;
  loka::app::TextEditorNode *node_;
  void *parent_;
  void *scroll_;
  void *delegate_;
  class RailOperation;
  /** Project only; return scheduling/paint intent to the owning operation. */
  loka::app::scene::FollowUp syncFromNode(RailOperation &op, bool force, bool nativeCommit = false);
  void projectHighlights(RailOperation &op);
  /** Restore the committed selection without replacing text or clearing undo. */
  void restoreSelectionFromFact(RailOperation &op);
  bool queryVisibleLines(unsigned &) const;
  class CommandOperation;
  void settle(loka::app::scene::Settlement, RailOperation &);
  loka::app::scene::FollowUp prepareRestore();
  loka::app::EditorResult applyNativeChange(TextObservation source, std::size_t &caretOffset);
};

void RegisterMacTextEditorNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
