#ifndef LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
#include "ToolboxPresentObservation.hpp"
#include "ToolboxScrollViewVisit.hpp"
#include "ToolboxFullDrawPresenter.hpp"
#include "ToolboxUnitLedger.hpp"
#include "ToolboxPressLedger.hpp"
#include "ToolboxErasedPass.hpp"
#include "ToolboxOwnControlMoveFootprint.hpp"
#include "ToolboxBlurSnapshot.hpp"
#include "ToolboxBlurDirtyExtent.hpp"
#include "ToolboxFrostCache.hpp"
#include "ToolboxScrollKey.hpp"
#include "ToolboxTabKey.hpp"
#include "app/layout/NaturalWidthSource.hpp"
#include "ToolboxEditInstalled.hpp"
#include "ToolboxPendingDialogs.hpp"

#include "app/RectSurface.hpp"
#include "app/FocusParticipant.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "ToolboxControlIdAllocator.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxEditControlLedger.hpp"
#include "ToolboxEditorSeat.hpp"
#include "ToolboxUnicodeEditorLedger.hpp"
#include "ToolboxEnabledChangeDispatch.hpp"
#include "ToolboxHitLedger.hpp"
#include "ToolboxCompositionReplay.hpp"
#include "ToolboxScrollBarLedger.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/NativeHandlePool.hpp"
#include "core/State.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "debug/ToolboxSceneDebugStats.hpp"
#include <Quickdraw.h>
#include <Controls.h>
#include <TextEdit.h>

struct ToolboxScrollViewMetrics;
class ToolboxWindow;
class ToolboxButtonContext;
class ToolboxPopupMenuContext;
class ToolboxCellContext;
class ToolboxEditTextContext;
class ToolboxTextEditorContext;

namespace loka
{
  namespace app
  {
    struct ScrollBarProps;
    class ScrollViewNode;
  }
} // namespace loka

class ToolboxScenePlatformController : public loka::app::scene::IPlatformController,
    public loka::app::layout::INaturalWidthSource
{
private:
  struct ButtonControlBinding;
  ToolboxPendingDialogs pendingDialogs_;

public:
  /** Dirty delivery queries the previous layout key once; O(1), no rows. */
  Rect dirtyExtent(const Rect &rect) const
  {
    Rect key;
    return this->bandBlur_.queryKeyRect(key) ? ToolboxBlurDirtyExtent(rect, key) : rect;
  }
  /** Move/size callers: once per changed control per render; O(1), no rows.
      Requires the window port while the erased pass is open. */
  void validateOwnControlMove(const Rect &before, const Rect &after);
  ToolboxErasedPass &erasedPass()
  {
    return this->erasedPass_;
  }
  virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const;
  /** Push-button measurement shared by allocation and standalone layout.
      The bool form refuses an unmeasurable label; the short form keeps the
      standalone layout's inset-only width for that case. */
  bool measurePushButtonNaturalWidth(const loka::core::String &label, short &width) const;
  short measurePushButtonNaturalWidth(const loka::core::String &label) const;
  loka::app::DialogResultTransport *dialogResults()
  { return this->window_ ? &this->window_->dialogResults() : 0; }
  ToolboxPendingDialogs &pendingDialogs() { return this->pendingDialogs_; }
  void registerCompositionReplay(ToolboxCompositionReplay::Registration &registration)
  {
    registration.attach(this->compositionReplay_);
  }
  loka::app::TextShaping textShaping() const { return this->textShaping_; }
  explicit ToolboxScenePlatformController(ToolboxWindow *window);
  virtual ~ToolboxScenePlatformController();

  virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild);
  virtual void onBoundaryApply(loka::app::scene::Node *rootNode,
                               loka::app::scene::BoundaryNode *boundary,
                               const loka::app::scene::BoundaryLocalApplyInfo &info,
                               const loka::app::scene::PlatformApplyPlan &plan);
  virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
  {
    return true;
  }
  virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state);
  /** Paint children under the latest Box/Card placement, or without an extra clip on refusal. */
  void renderClipBox(loka::app::scene::Node *node);
  /** One registry attempt per flagged Box/Card placement, before children. */
  void ensureClipBoxContext(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state);
  virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler);
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
  virtual void requestRelayout();
  virtual void synchronize();
  virtual bool hasPendingSync() const;
  virtual void drainNativeRetirements();
  virtual void destroy();
  virtual void releaseMenu();
  virtual void releaseNodeContexts(loka::app::scene::Node *node);

  /** Refresh existing projection ledgers after a retained props apply. */
  void refreshContextProps(loka::app::scene::Node *node, short buttonResourceId = 0);
  void emitHitEmitter(loka::core::EmitterState *emitter);
  void recordButtonHit(const Rect &rect,
                       loka::core::EmitterState *emitter,
                       loka::core::State<bool> *enabled,
                       loka::app::scene::BoundaryNode *boundary,
                       ToolboxButtonContext *context);
  void recordCellHit(const Rect &rect,
                     loka::core::EmitterState *emitter,
                     loka::app::scene::BoundaryNode *boundary,
                     ToolboxCellContext *context,
                     loka::core::State<loka::core::String> *text);
  void recordEditHit(const Rect &rect,
                     loka::core::State<loka::core::String> *text,
                     loka::app::scene::BoundaryNode *boundary,
                     ToolboxEditTextContext *context);
  void recordTextHit(const Rect &rect,
                     short x,
                     short y,
                     loka::core::State<loka::core::String> *text,
                     loka::app::scene::BoundaryNode *boundary,
                     bool needsRelayoutOnChange,
                     short visibleWidth,
                     ToolboxTextContext *context);
  void recordPopupHit(const Rect &rect,
                      loka::core::State<bool> *enabled,
                      ToolboxPopupMenuContext *context);
  void applyPopupSelectionChange(const Rect &rect,
                                 loka::app::scene::BoundaryNode *boundary,
                                 loka::core::State<int> *selectedIndex,
                                 const loka::app::scene::WriteSeat<int> &selectedIndexSeat,
                                 loka::core::EmitterState *onChange,
                                 int newIndex);
  void drawControlsInRect(const Rect &rect);
  /** Hides a retiring native control under an empty clip; see ToolboxButtonControl.cpp. */
  void hideRetiredControl(ControlRef control);
  void drawScrollBarControl(short id);
  void drawControl(ControlRef control);
  /** Reconcile one Button; painted reports a ShowControl paint independently
      of label submission success. The caller supplies the composition clip. */
  bool ensureButtonControl(short resourceId,
                           const Rect &rect,
                           const loka::core::String &label,
                           loka::core::EmitterState *emitter,
                           loka::core::State<bool> *enabled,
                           loka::app::scene::NativeLifetimeHint lifetimeHint,
                           ToolboxButtonContext *context,
                           ButtonControlBinding *&binding,
                           bool *painted = 0);
  void destroyButtonControl(short resourceId, loka::app::scene::NativeLifetimeHint lifetimeHint);
  /** Creates or refreshes the scrollBarProc control for one ScrollBar node.
      The declared range and the bound value are pushed on every projection,
      which is how a recomposed range reaches the CDEF. */
  bool
  ensureScrollBarControl(short resourceId,
                         const Rect &rect,
                         const loka::app::ScrollBarProps &props,
                         loka::app::scene::NodeContext *ownerContext,
                         loka::app::scene::NativeLifetimeHint lifetimeHint = loka::app::scene::NATIVE_HINT_DEFAULT);
  void destroyScrollBarControl(short resourceId, loka::app::scene::NativeLifetimeHint lifetimeHint);
  bool presentEditorScrollBar(short, const ToolboxEditorScrollBarPresentation &);
  void revokeEditorScrollBar(short);
  bool ensureEditorScrollBar(short,
                             const Rect &,
                             ToolboxTextEditorContext *,
                             const ToolboxEditorScrollBarPresentation &,
                             loka::app::scene::NativeLifetimeHint);

  /** Projects the ScrollView viewport's own scrollBarProc without borrowing
      the DSL ScrollBar binding or its window-tracker commit clock. */
  ToolboxViewportRows ensureViewportScrollBarControl(const Rect &viewportRect,
                                                     loka::app::ScrollViewNode *scrollView,
                                                     int contentHeight,
                                                     int viewportHeight,
                                                     int requestedOffset,
                                                     const ToolboxScrollViewMetrics &horizontal,
                                                     bool horizontalBar);
  void destroyViewportScrollBarControl(
      loka::app::ScrollViewNode *scrollView,
      loka::app::scene::NativeLifetimeHint lifetimeHint);
  void retireNodeContext(loka::app::scene::NodeContext *context, loka::app::scene::NativeLifetimeHint lifetimeHint);
  /** Escalates recorded dirty rectangles because the projected structure
      changed since the last present. */
  void requestStructurePresent();
  void drawFallbackControl(const Rect &rect);
  TEHandle ensureTextEditorControl(ToolboxTextEditorContext *, const Rect &, loka::app::scene::NativeLifetimeHint);
  void retireTextEditorControl(loka::app::scene::NodeContext *context, loka::app::scene::NativeLifetimeHint hint)
  { this->retireEditTextControl(context, hint); }
  ToolboxEditPresentation ensureEditTextControl(ToolboxEditTextContext *ownerContext,
                                 const Rect &rect,
                                 loka::core::State<loka::core::String> *text,
                                 loka::app::scene::NativeLifetimeHint lifetimeHint = loka::app::scene::NATIVE_HINT_DEFAULT);
  bool isPointInEdit(const Point &point) const;
  short allocateControlId();
  /** Measures against this controller's window port and restores ambient QuickDraw state. */
  short measureTextWidth(
      const loka::core::String &value,
      const ToolboxTextFontDescriptor &descriptor = ToolboxTextFontDescriptor()) const;
  void beginClip(const Rect &rect);
  void endClip();
  loka::app::scene::PlatformLayoutHandlerRegistry *layoutHandlerRegistry()
  {
    return &layoutHandlerRegistry_;
  }
  void setActiveLayoutBoundary(loka::app::scene::BoundaryNode *boundary)
  {
    activeLayoutBoundary_ = boundary;
  }
  loka::app::scene::BoundaryNode *activeLayoutBoundary() const
  {
    return activeLayoutBoundary_;
  }
  void invalidateUnitOverlap(const Rect &);
  short layoutZStack(loka::app::ZStackNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *);
  void renderZStack(loka::app::ZStackNode *);
  ToolboxScrollViewLayoutResult layoutScrollView(loka::app::ScrollViewNode *scrollView,
                                                 loka::app::scene::LayoutState &state,
                                                 loka::app::scene::BoundaryNode *currentBoundary);
  /** Native step for SettleScrollViewVisit only; no other caller should use it.
      Public so the free LayoutNode dispatch can form its member pointer. */
  bool applyScrollViewRequest(loka::app::ScrollViewNode &node,
                              const ToolboxScrollViewLayoutResult &result,
                              const loka::core::Frame &target);
  void recordRectSurfaceExtent(loka::app::RectSurfaceNode *surface,
                               const loka::core::Frame &extent)
  {
    this->rectSurfaceExtentLedger_.record(surface, extent);
  }
  /** Retire door for a RectSurface context: takes back that surface's pending
      seat rows so a surface reclaimed during delivery publishes nothing. */
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *surface)
  {
    this->rectSurfaceExtentLedger_.cancel(surface);
  }
  void renderScrollView(loka::app::ScrollViewNode *scrollView);
#include "ToolboxScrollViewProjection.hpp"

#include "ToolboxInputBodies.hpp"

private:
  void noteControlDraw();
  /** Capture each move rectangle once; refused rectangles remain empty. */
  ToolboxOwnControlMoveFootprint admitOwnControlMove(const Rect &before, const Rect &after) const;
  void validateOwnControlMove(const ToolboxOwnControlMoveFootprint &footprint);
  void activateEditControl(std::size_t index);
  void reachNativeButton(short id);
  void reachNativeBar(short id);
  void reachEditControl(loka::app::scene::NodeContext *context);
  void reachPopupHit(ToolboxPopupMenuContext *context);
  friend class ToolboxTextMeasureScope;
  template <typename Controller>
  friend void ReconcileToolboxTextSubscription(Controller &,
      loka::core::State<loka::core::String> *, loka::core::State<loka::core::String> *);
  friend bool RegisterToolboxBuiltInSupport(ToolboxScenePlatformController &controller);
  template <typename Sink, typename StateType>
  friend class ToolboxEnabledStateBindingPath;
  friend class ToolboxTextContext;
  friend class ToolboxCellContext;
  friend class ToolboxButtonContext;
  friend class ToolboxScrollBarContext;
  friend class ToolboxPopupMenuContext;
  friend class ToolboxEditTextContext;
  friend class ToolboxTextEditorContext;
  friend class ToolboxRectSurfaceContext;

  typedef ToolboxHitLedger::ButtonHit ButtonHit;
  typedef ToolboxHitLedger::CellHit CellHit;
  typedef ToolboxHitLedger::EditHit EditHit;
  typedef ToolboxHitLedger::TextHit TextHit;
  typedef ToolboxHitLedger::PopupHit PopupHit;
  struct TextBinding
  {
    loka::core::State<loka::core::String> *state;
    ToolboxScenePlatformController *controller;
  };

  struct ButtonControlBinding
  {
    ToolboxButtonContext *context;
    short resourceId;
    ControlRef control;
    loka::core::EmitterState *emitter;
    loka::core::State<bool> *enabled;
    /** The render pass's mark-and-sweep bit, one contract for every control
        binding in this file: render() clears it, the ensure* call that
        projects the node sets it, and whatever is still false at the end of
        the frame gets hidden -- the scene no longer declares that control.
        Three sites in one dance, which is why the bit gets a sentence the
        name alone cannot carry. */
    bool usedThisFrame;
    ToolboxNativePlacement placement;
    std::string label;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };

  typedef ToolboxScrollBarLedger::ScrollBarControlBinding ScrollBarControlBinding;
  typedef ToolboxScrollBarLedger::ViewportScrollBarBinding ViewportScrollBarBinding;

#include "ToolboxEditControlBinding.inc"

  /** A native handle waiting for the platform safe point. The hint rides
      along so the flush can route it: EAGER_RELEASE handles are disposed
      at the clock (previous behavior for everything); the rest enter the
      exact-match pool bucket for reuse. */
  template <typename HandleT> struct RetiredNativeEntry
  {
    HandleT handle;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };

private:
  ToolboxEditTextContext *fallbackFocusContext() const;
  loka::app::scene::FocusLink fallbackFocus_;
  const loka::app::TextShaping textShaping_;
public:
  ToolboxWindow *window_;

private:
  /** Per-pass projected, clipped opaque unit facts; layout writes, replay reads. */
  ToolboxUnitLedger unitLedger_;
  ToolboxPresentObservation observation_;
  friend class ToolboxBlurSnapshotInspection;
  ToolboxPressLedger pressLedger_;
  ToolboxFrostCache frostCache_;
  ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN> staticBlur_;
  ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE> bandBlur_;
  ToolboxErasedPass erasedPass_;

public:
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  loka::app::scene::Node *rootNode_;
  loka::app::scene::Node *pendingRootNode_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  ToolboxHitLedger hitLedger_;
  ToolboxCompositionReplay compositionReplay_;
  std::vector<ButtonControlBinding> buttonControls_;
  ToolboxScrollBarLedger scrollBarLedger_;
  ToolboxEditControlLedger<EditTextControlBinding, loka::app::scene::NodeContext> editControls_;
  ToolboxUnicodeEditorLedger unicodeEditors_;
  std::vector<loka::core::State<loka::core::String> *> boundTextStates_;
  std::vector<TextBinding *> textBindings_;
  ToolboxEnabledStateBindingPath<ToolboxScenePlatformController,
                                 loka::core::State<bool> > enabledStateBindingPath_;
  bool inBatchUpdate_;
  bool pendingFullInvalidate_;
  loka::app::scene::NodeDirtyFlags pendingInvalidateFlags_;
  bool forceFullRedraw_;
  std::vector<Rect> pendingDirtyRects_;
  std::vector<loka::core::State<loka::core::String> *> pendingTextStates_;
  std::vector<RetiredNativeEntry<ControlRef> > retiredControls_;
  // A retired scroll bar keeps its own queue and bucket: NewControl bakes the
  // CDEF into the handle, so paying one back out as a push button would hand
  // the caller a control that draws and tracks as the wrong kind.
  std::vector<RetiredNativeEntry<ControlRef> > retiredScrollBarControls_;
  std::vector<RetiredNativeEntry<TEHandle> > retiredTextEdits_;
  loka::app::scene::ExactMatchHandleBucket<ControlRef> pushButtonBucket_;
  loka::app::scene::ExactMatchHandleBucket<TEHandle> textEditBucket_;
  int poolIntakeAuditFailCount_;
  RgnHandle clipRgn_;
  RgnHandle scrollViewClipRgn_;
  RgnHandle paintSuppressClipRgn_;
  bool hasClip_;
  ToolboxControlIdAllocator controlIds_;
  mutable ToolboxSceneDebugStats debugStats_;
  loka::app::scene::PlatformLayoutHandlerRegistry layoutHandlerRegistry_;
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  loka::app::scene::BoundaryNode *activeLayoutBoundary_;

  bool handleTextKey(char key);
  void bindTextState(loka::core::State<loka::core::String> *text);
  void bindEnabledState(loka::core::State<bool> *enabled);
  void unbindTextState(loka::core::State<loka::core::String> *text);
  void unbindEnabledState(loka::core::State<bool> *enabled);
  bool hasLiveBinding(loka::core::State<loka::core::String> *text) const;
  bool hasLiveBinding(loka::core::State<bool> *enabled) const;
  void handleTextChanged(loka::core::State<loka::core::String> *text);
  bool applyEnabledChangeForKind(ToolboxEnabledControlKind kind,
                                 loka::core::State<bool> *enabled);
  void refreshPopupEnabled(PopupHit &binding);
  void requestControlRepaint(const Rect &rect);
  void refreshButtonEnabled(ButtonControlBinding &binding);
  void refreshScrollBarEnabled(ScrollBarControlBinding &binding);
  void refreshButtonHitEnabled(ButtonHit &binding);
  bool enabledChangeDispatchReady() const
  {
    return this->window_ != 0;
  }
  void beginBatchUpdate();
  void endBatchUpdate();
  void addPendingDirty(const Rect &rect);
  void addPendingText(loka::core::State<loka::core::String> *text);
  void requestInvalidateForChange(loka::app::scene::Node *rootNodeForChange,
                                  loka::app::scene::NodeDirtyFlags flags,
                                  bool fullRebuild);
  void redrawTextHit(TextHit &hit);
  void redrawPopupHit(const PopupHit &hit);
  void requestInvalidateForText(loka::core::State<loka::core::String> *text);
  void clearTextBindings();
  void clearEnabledBindings();
  void clearControls();
  void queueRetiredControl(ControlRef control, loka::app::scene::NativeLifetimeHint lifetimeHint);
  void retireScrollBarBinding(ScrollBarControlBinding &, loka::app::scene::NativeLifetimeHint);
  void queueRetiredScrollBarControl(ControlRef control, loka::app::scene::NativeLifetimeHint lifetimeHint);
  /** Publishes the value a finished tracking loop settled on, in the order
      the ruling fixes: binding first, then onChange. */
  void commitScrollBarValueAt(std::size_t index);
  void
  commitViewportScrollBarValue(loka::app::ScrollViewNode &node, const ToolboxViewportRows &rows, const Rect *dirty);
  void commitViewportScrollBarValue(ViewportScrollBarBinding &binding,
                                    ScrollBarControlBinding &native,
                                    loka::app::ScrollBarOrientation axis);
  /** Request side of the same commit: sets each row this visit committed,
      without painting, then commits with no pending dirty. */
  void applyViewportScrollRequest(loka::app::ScrollViewNode &node,
                                  const ToolboxViewportRows &rows,
                                  const loka::core::Frame &target);
  /** Optional move output is empty when unchanged/refused. A caller requesting it
      validates only after its final native setter/show, while the window port is current.
      Editor presentation is separate and conservatively leaves OS damage pending. */
  bool ensureScrollBarBinding(short resourceId,
                              const Rect &rect,
                              int minimum,
                              int maximum,
                              int lineStep,
                              int pageStep,
                              loka::app::scene::NativeLifetimeHint lifetimeHint,
                              ScrollBarControlBinding *&binding,
                              Rect *movedFrom = 0);
  void queueRetiredTextEdit(TEHandle te, loka::app::scene::NativeLifetimeHint lifetimeHint);
  bool hasLiveBinding(ControlRef control) const;
  bool hasLiveBinding(TEHandle te) const;
  void disposeNativeHandle(ControlRef control);
  void disposeNativeHandle(TEHandle te);
  template <typename HandleT>
  void queueRetiredNativeHandle(std::vector<RetiredNativeEntry<HandleT> > &retired,
                                HandleT handle,
                                loka::app::scene::NativeLifetimeHint lifetimeHint);
  /** The one flush policy for every retired native handle type:
      EAGER_RELEASE dies at the clock; a handle still referenced by a live
      binding is deliberately leaked (counted — safer than a double life);
      the rest enter the bucket, and refusals (depth cap) die at the clock. */
  template <typename HandleT>
  void flushRetiredEntriesInto(std::vector<RetiredNativeEntry<HandleT> > &retired,
                               loka::app::scene::ExactMatchHandleBucket<HandleT> &bucket);
  void drainNativeHandleBuckets();
  void syncNativePoolStats();
  void refreshEditTextBindingForStateChange(EditTextControlBinding &binding);
  void syncEditTextFromState(EditTextControlBinding &binding);
  void updateStateFromEdit(EditTextControlBinding &binding, const loka::core::String &before);
  /** Revoke presentation and queue the TE on every native retirement path. */
  void retireEditTextBinding(EditTextControlBinding &binding, loka::app::scene::NativeLifetimeHint lifetimeHint);
  void retireEditTextControlAt(std::size_t index, loka::app::scene::NativeLifetimeHint lifetimeHint);
  void retireEditTextControl(loka::app::scene::NodeContext *ownerContext,
                             loka::app::scene::NativeLifetimeHint lifetimeHint);
  bool intersectWithProjectionClip(const Rect &rect, Rect &clipped) const;
  static void TextStateChangedThunk(void *userData);

public:
  /** Borrow the window's app-wide cursor owner; no controller registry walk. */
  CursorOwner *cursorOwner() const
  {
    ToolboxApp *app = this->window_ ? this->window_->toolboxApp() : 0;
    return app ? &app->cursorOwner() : 0;
  }
  void flushRetiredNativeHandles();
  std::string debugStatsSummary() const;
#ifdef TEST_BUILD
  /** Borrowed snapshot; cursor fields are diagnostic copies of the app owner. */
  const ToolboxSceneDebugStats &debugStatsForTesting() const
  {
    CursorOwner *owner = this->cursorOwner();
    if (owner)
      owner->copyDiagnostics(this->debugStats_);
    return this->debugStats_;
  }

  /** Reads counted native bytes (not UTF-8) without synchronizing TextEdit first.

      Scenario probes use this const door to distinguish the native record from
      its bound State; calling ensureEditTextControl would repair the value and
      make the probe non-discriminating. */
  bool queryEditTextValueForTesting(ToolboxEditTextContext *ownerContext, std::string &out) const;
  /** Native TE geometry and last applied interaction placement, without re-ensuring. */
  struct EditTextGeometry
  {
    Rect destination;
    Rect view;
    ToolboxNativePlacement placement;
  };
  bool queryEditTextGeometryForTesting(ToolboxEditTextContext *ownerContext, EditTextGeometry &out) const;
#endif
  void noteWindowDraw()
  {
    ++debugStats_.drawCalls;
    ++debugStats_.totalDrawCalls;
  }
  void noteWindowDirtyDraw()
  {
    ++debugStats_.drawDirtyCalls;
    ++debugStats_.totalDrawDirtyCalls;
  }
  /** Window requests: O(1) per request, before request coalescing; no rows. */
  void notePresentReason(ToolboxFullDrawReason reason)
  {
    this->observation_.noteReason(reason);
  }
  /** Window full-draw entries: O(1) bbox snapshot before OS validation; no rows. */
  void notePresentDamage(const Rect &damage)
  {
    this->observation_.noteDamage(damage);
  }
  void noteWindowFullRequest(const char *reason)
  {
    ++debugStats_.windowFullRequestCount;
    debugStats_.windowFullRequestSource = reason;
  }
  void noteWindowRectRequest()
  {
    ++debugStats_.windowRectRequestCount;
  }
  void noteWindowFlushFull()
  {
    ++debugStats_.windowFlushFullCount;
  }
  void noteWindowFlushDirty()
  {
    ++debugStats_.windowFlushDirtyCount;
  }
  void noteWindowUpdateEvtDraw()
  {
    ++debugStats_.windowUpdateEvtDrawCount;
  }
  void resetDebugStats();
  bool dumpDebugStatsToTimestampedFile() const;
};

#endif // LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
