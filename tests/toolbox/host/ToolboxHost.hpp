#ifndef LOKA_TEST_TOOLBOX_HOST_HPP
#define LOKA_TEST_TOOLBOX_HOST_HPP
#include "ToolboxFullDrawPresenter.hpp"
#include "ToolboxUnitLedger.hpp"
#include "ToolboxUnitHairline.hpp"
#include "ToolboxErasedPass.hpp"
#include "ToolboxBlurSnapshot.hpp"
#include "ToolboxBlurDirtyExtent.hpp"
#include "ToolboxScrollKey.hpp"
#include "ToolboxTabKey.hpp"
#include "ToolboxActivationPhase.hpp"
#include "support/TestVerify.hpp"
struct ToolboxScrollViewMetrics;
#include "app/layout/NaturalWidthSource.hpp"
#include "app/core/Window.hpp"
#include "ToolboxWindowGeometry.hpp"
#ifdef LOKA_HOST_GAP_DISPATCH
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "support/TestVerify.hpp"
#define LOKA_TOOLBOX_RECT_SURFACE_CONTEXT_HPP
#endif
#include "ToolboxEditInstalled.hpp"
// Replace OS/controller neighbors; compile the actual context, table, measure
// scope, lifecycle base and built-in registration source without alteration.
#define LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_TOOLBOX_WINDOW_HPP
#define LOKA_TOOLBOX_WINDOW_CONTEXT_HPP
#define LOKA_TOOLBOX_APP_HPP
#ifndef LOKA_HOST_CONTROL_WIDTH
#define LOKA_TOOLBOX_BUTTON_CONTEXT_HPP
#endif
#ifndef LOKA_HOST_CELL_PAINT
#define LOKA_TOOLBOX_CELL_CONTEXT_HPP
#endif
#ifndef LOKA_HOST_CONTROL_WIDTH
#define LOKA_TOOLBOX_EDIT_TEXT_CONTEXT_HPP
#endif
#ifndef LOKA_HOST_IMAGE_VIEW
#define LOKA_TOOLBOX_IMAGE_VIEW_CONTEXT_HPP
#endif
#define LOKA_TOOLBOX_OPEN_FILE_DIALOG_CONTEXT_HPP
#define LOKA_TOOLBOX_CONFIRM_DIALOG_CONTEXT_HPP
#ifndef LOKA_HOST_CONTROL_WIDTH
#define LOKA_TOOLBOX_POPUP_MENU_CONTEXT_HPP
#endif
#ifndef LOKA_HOST_CONTROL_ORDER
#define LOKA_TOOLBOX_SCROLL_BAR_CONTEXT_HPP
#endif
class ToolboxTextContext;
class ToolboxTextFontDescriptor;
#include "Quickdraw.h"
#include "Controls.h"
#include "ToolboxPressLedger.hpp"
#include "ToolboxScrollBarLedger.hpp"
#include "ToolboxControlIdAllocator.hpp"
#include "app/RectSurface.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "TextEdit.h"
#include "ToolboxEditControlLedger.hpp"
#include "ToolboxHitLedger.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/FocusParticipant.hpp"
class ToolboxTextEditorContext;
#include "ToolboxCompositionReplay.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "support/MeasurementRetryQueue.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/scene/projection/NativeHandlePool.hpp"
#include <vector>
#include <string>

struct CursorOwner
{
  int depth, entries;
  CursorOwner()
      : depth(0),
        entries(0)
  {
  }
};
class BusyScope
{
public:
  explicit BusyScope(CursorOwner *owner)
      : owner_(owner)
  {
    if (owner_)
    {
      ++owner_->depth;
      ++owner_->entries;
    }
  }
  ~BusyScope()
  {
    if (owner_)
      --owner_->depth;
  }

private:
  CursorOwner *owner_;
};
class ToolboxWindowContext
{
public:
  enum
  {
    CAP_CONTROL_MANAGER = 1,
    CAP_TEXT_EDIT = 2
  };
  int capabilities() const
  {
    return CAP_TEXT_EDIT;
  }
};
namespace toolbox_host { extern GrafPtr frontWindow; extern int textHits; }
inline GrafPtr FrontWindow() { return toolbox_host::frontWindow; }
#include "ToolboxPendingDialogs.hpp"
#include "ToolboxDialogPresenter.hpp"
namespace toolbox_host
{
  extern unsigned staticCaptures, staticCopies, staticReleases;
  extern unsigned bandCaptures, bandCopies, bandReleases, bandHairlines;
  extern bool staticEligible, staticCaptureSucceeds, staticCopySucceeds;
} // namespace toolbox_host
class ToolboxScenePlatformController;
class ToolboxWindow : public Window
{
  loka::core::PushStateTracker tracker_;
  loka::core::MutableState<bool> visible_;
public:
  struct WindowPort : GrafPort
  {
    Rect portRect;
    RgnHandle updateRgn;
  } port;
  ToolboxWindowContext context_;
  void (*onFlush)(void *);
  void *flushData;
  /** False models a window with no grow box: the size box query declines. */
  bool answersSizeBox;
  ToolboxScenePlatformController *scenePlatformController_;
  void idleControls(ActivationPhase);
  explicit ToolboxWindow(const WindowProps &props = WindowProps())
      : Window(0, props),
        visible_(true),
        onFlush(0),
        flushData(0),
        answersSizeBox(true),
        scenePlatformController_(0)
  {
#ifdef LOKA_HOST_ZSTACK_UNITS
    this->window_ = &this->port;
    this->needsInvalidate_ = false;
#endif
    this->tracker_.addState(&this->visible_);
    toolbox_host::frontWindow = &port;
    port.updateRgn = 0;
#ifdef LOKA_HOST_IMAGE_VIEW
    port.grafProcs = 0;
    port.portBits.baseAddr = 0;
#endif
    port.portBits.bounds = Rect();
    port.txFont = 3;
    port.txSize = 12;
    port.txFace = 0;
    port.fgColor = blackColor;
    SetRect(&port.portRect, 0, 0, 360, 260);
  }
  bool queryPortRect(Rect &out) const
  {
    out = this->port.portRect;
    return true;
  }
  bool querySizeBoxRect(Rect &out) const
  {
    if (!this->answersSizeBox)
      return false;
    out = ToolboxWindowSizeBoxRect(this->port.portRect);
    return true;
  }
#ifdef LOKA_HOST_ZSTACK_UNITS
  WindowPort *window_;
  bool needsInvalidate_;
  std::vector<Rect> pendingInvalidateRects_;
  void requestInvalidate();
  void requestInvalidateImpl();
  void requestInvalidateWithReason(const char *);
  void requestInvalidateWithReasonImpl(const char *);
  void requestInvalidateRect(const Rect &);
  void requestInvalidateRectImpl(const Rect &);
  void flushInvalidate();
  bool hasPendingInvalidate() const;
  /** Not re-entrant; does not pump events. Owns the complete OS update bracket. */
  void drawForUpdate();
  void drawForFlush();

private:
  void drawWhole(const Rect &evidence);

public:
  void drawDirty(const Rect &);
#else
  void requestInvalidate() { ++toolbox_host::invalidations; }
  void requestInvalidateWithReason(const char *) { ++toolbox_host::invalidations; }
#endif
  ToolboxScenePlatformController *scenePlatformController()
  {
    return this->scenePlatformController_;
  }
#ifndef LOKA_HOST_ZSTACK_UNITS
  void flushInvalidate() { if (this->onFlush) this->onFlush(this->flushData); }
#endif
  loka::core::StateTracker *getTracker() { return &this->tracker_; }
#ifndef LOKA_HOST_ZSTACK_UNITS
  void requestInvalidateRect(const Rect &rect)
  {
    ++toolbox_host::invalidations;
    toolbox_host::invalidatedRects.push_back(rect);
  }
#endif
  WindowPort *window()
  {
    return &port;
  }
  ToolboxWindowContext *context()
  {
    return &context_;
  }
};
/** Host neighbor: the production controller only needs the ordinary edit's write seat and invalidation. */
#ifndef LOKA_HOST_CONTROL_WIDTH
class ToolboxEditTextContext : public loka::app::scene::NativeNodeContext
{
public:
  explicit ToolboxEditTextContext(loka::app::EditTextNode *node = 0) { this->setOwner(node); }
  loka::app::scene::WriteSeat<loka::core::String> projectedWriteSeat() const
  { return this->owner() ? this->owner()->asEditTextNode()->props.text_ : loka::app::scene::WriteSeat<loka::core::String>(); }
  virtual loka::core::State<loka::core::String> *projectedTextState()
  { return this->projectedWriteSeat().state(); }
  void invalidateNativePresentation() {}
  void repaint(const ToolboxEditPresentation &)
  {
    LOKA_VERIFY(false);
  }
  const Rect &chromeRect() const
  {
    static const Rect rect = {-30000, -30000, 30000, 30000};
    return rect;
  }
};
#else
#include "context/ToolboxEditTextContext.hpp"
#endif
/** Host neighbors carry the fields consumed by the unmodified leaf input bodies. */
#ifndef LOKA_HOST_CONTROL_WIDTH
class ToolboxButtonContext : public loka::app::scene::NativeNodeContext
{
public:
  loka::core::EmitterState *emitter_;
  loka::core::State<bool> *enabled_;
  Rect rect_;
  ToolboxButtonContext() : emitter_(0), enabled_(0) {}
  void repaint(ControlRef, std::string &) {}
  void forgetPresentedControl() {}
  bool handleMouseDown(const Point &, ToolboxScenePlatformController *);
};
#else
#include "context/ToolboxButtonContext.hpp"
#endif
#ifndef LOKA_HOST_CELL_PAINT
class ToolboxCellContext : public loka::app::scene::NativeNodeContext
{
public:
  loka::app::CellNode *node_;
  Rect rect_;
  ToolboxCellContext() : node_(0) {}
  void draw(ToolboxScenePlatformController *)
  {
    LOKA_VERIFY(false);
  }
  bool handleMouseDown(const Point &, ToolboxScenePlatformController *);
};
#else
#include "context/ToolboxCellContext.hpp"
#endif
#ifndef LOKA_HOST_CONTROL_WIDTH
class ToolboxPopupMenuContext : public loka::app::scene::NativeNodeContext
{
public:
  const loka::Vector<loka::core::String> *items_;
  loka::core::State<int> *selectedIndex_;
  loka::app::scene::WriteSeat<int> selectedIndexSeat_;
  loka::core::EmitterState *onChange_;
  loka::core::State<bool> *enabled_;
  loka::app::scene::BoundaryNode *boundary_;
  Rect rect_;
  ToolboxPopupMenuContext() : items_(0), selectedIndex_(0), onChange_(0), enabled_(0), boundary_(0) {}
  short menuId() const { return 2000; }
  short clampIndex(int value) const { return static_cast<short>(value); }
  bool handleMouseDown(const Point &, ToolboxScenePlatformController *);
};
#else
#include "context/ToolboxPopupMenuContext.hpp"
#endif
class ToolboxScenePlatformController : public loka::app::scene::IPlatformController,
    public loka::app::layout::INaturalWidthSource
{
public:
#ifdef LOKA_HOST_GAP_DISPATCH
  loka::app::scene::PlatformLayoutHandlerRegistry gapLayoutRegistry;
  loka::app::scene::PlatformLayoutHandlerRegistry *layoutHandlerRegistry()
  {
    return &this->gapLayoutRegistry;
  }
  loka::app::scene::BoundaryNode *activeLayoutBoundary()
  {
    return 0;
  }
  void setActiveLayoutBoundary(loka::app::scene::BoundaryNode *) {}
#ifndef LOKA_HOST_CONTROL_ORDER
  void renderScrollView(loka::app::ScrollViewNode *) {}
#endif
  void recordRectSurfaceExtent(loka::app::RectSurfaceNode *, const loka::core::Frame &)
  {
    LOKA_VERIFY(false);
  }
#endif
  ToolboxPendingDialogs pendingDialogs_;
  virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const;
  /** Push-button measurement shared by allocation and standalone layout.
      The bool form refuses an unmeasurable label; the short form keeps the
      standalone layout's inset-only width for that case. */
  bool measurePushButtonNaturalWidth(const loka::core::String &label, short &width) const;
  short measurePushButtonNaturalWidth(const loka::core::String &label) const;
  ToolboxPendingDialogs &pendingDialogs() { return this->pendingDialogs_; }
  MeasurementRetryQueue relayoutRetries;
  virtual void requestRelayout();
  void requestSceneRelayout(loka::app::scene::Node *) { this->relayoutRetries.request(); }

  typedef ToolboxHitLedger::EditHit EditHit;
  typedef ToolboxHitLedger::ButtonHit ButtonHit;
  typedef ToolboxHitLedger::CellHit CellHit;
  typedef ToolboxHitLedger::PopupHit PopupHit;
  ToolboxHitLedger hitLedger_;
  const std::vector<ButtonHit> &buttonHits() const
  {
    return this->hitLedger_.buttonHits_;
  }
  const std::vector<CellHit> &cellHits() const
  {
    return this->hitLedger_.cellHits_;
  }
  const std::vector<ToolboxHitLedger::TextHit> &textHits() const
  {
    return this->hitLedger_.textHits_;
  }
  void installHit(const ButtonHit &hit) { this->hitLedger_.buttonHits_.push_back(hit); }
  void installHit(const CellHit &hit) { this->hitLedger_.cellHits_.push_back(hit); }
  void installHit(const PopupHit &hit) { this->hitLedger_.popupHits_.push_back(hit); }
  /** Synthetic render for legacy fixtures that install rows directly.
      Ground hits precede the native/edit faces those fixtures place above them.
      New ordering pins use the production render walk instead. */
  void publishFixturePressOrder()
  {
    this->pressLedger_.begin();
    for (std::size_t i = 0; i < this->hitLedger_.buttonHits_.size(); ++i)
      this->pressLedger_.append(PressRow::BUTTON_HIT, i);
    for (std::size_t i = 0; i < this->hitLedger_.cellHits_.size(); ++i)
      this->pressLedger_.append(PressRow::CELL_HIT, i);
    for (std::size_t i = 0; i < this->hitLedger_.popupHits_.size(); ++i)
      this->pressLedger_.append(PressRow::POPUP_HIT, i);
    for (std::size_t i = 0; i < this->hitLedger_.editHits_.size(); ++i)
      this->pressLedger_.append(PressRow::EDIT_HIT, i);
    for (std::size_t i = 0; i < this->editControls_.size(); ++i)
      this->pressLedger_.append(PressRow::EDIT_CONTROL, i);
    for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
      this->pressLedger_.append(PressRow::NATIVE_BAR, i);
    for (std::size_t i = 0; i < this->buttonControls_.size(); ++i)
      this->pressLedger_.append(PressRow::NATIVE_BUTTON, i);
    this->pressLedger_.commit();
  }
  loka::app::scene::FocusLink fallbackFocus_;
  ToolboxEditTextContext *fallbackFocusContext() const;
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
  bool isPointInEdit(const Point &point) const;
#include "ToolboxInputBodies.hpp"
public:
#include "ToolboxScrollViewProjection.hpp"
  unsigned leafLayouts;
  const std::vector<ToolboxScrollBarLedger::ScrollBarControlBinding> &scrollControls() const
  {
    return this->scrollBarLedger_.scrollBarControls_;
  }
  int scrollMaximum(short id = 0) const
  {
    if (!id && !this->scrollBarLedger_.viewportScrollBars_.empty())
      id = this->scrollBarLedger_.viewportScrollBars_[0].barId[loka::app::SCROLL_BAR_VERTICAL];
    for (std::size_t i = 0; i < this->scrollControls().size(); ++i)
      if (this->scrollControls()[i].resourceId == id)
        return this->scrollControls()[i].maximum;
    return 0;
  }
  ToolboxScrollBarLedger::ViewportScrollBarBinding &viewportScroll()
  {
    return this->scrollBarLedger_.viewportScrollBars_[0];
  }
  ToolboxScrollBarLedger::ScrollBarControlBinding &scrollControl(unsigned index)
  {
    return this->scrollBarLedger_.scrollBarControls_[index];
  }
  RgnHandle scrollViewClipRgn_;
  RgnHandle paintSuppressClipRgn_;
  void invalidateUnitOverlap(const Rect &);
  short layoutZStack(loka::app::ZStackNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *);
  void renderZStack(loka::app::ZStackNode *);
  short layoutScrollView(loka::app::ScrollViewNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *);
  void requestStructurePresent();
  virtual void onBoundaryApply(loka::app::scene::Node *, loka::app::scene::BoundaryNode *,
      const loka::app::scene::BoundaryLocalApplyInfo &, const loka::app::scene::PlatformApplyPlan &);
  virtual void releaseNodeContexts(loka::app::scene::Node *);
  int ensureViewportScrollBarControl(
      const Rect &, loka::app::ScrollViewNode *, int, int, int, const ToolboxScrollViewMetrics &, bool);
  void destroyViewportScrollBarControl(loka::app::ScrollViewNode *, loka::app::scene::NativeLifetimeHint);
  short allocateControlId() { return this->controlIds_.allocate(); }
  void destroyScrollBarControl(short, loka::app::scene::NativeLifetimeHint);
  bool presentEditorScrollBar(short, const ToolboxEditorScrollBarPresentation &);
  void revokeEditorScrollBar(short);
  bool ensureEditorScrollBar(short,
                             const Rect &,
                             ToolboxTextEditorContext *,
                             const ToolboxEditorScrollBarPresentation &,
                             loka::app::scene::NativeLifetimeHint);

  void emitHitEmitter(loka::core::EmitterState *);
  void applyPopupSelectionChange(const Rect &, loka::app::scene::BoundaryNode *,
      loka::core::State<int> *, const loka::app::scene::WriteSeat<int> &, loka::core::EmitterState *, int);
  typedef ToolboxScrollBarLedger::ScrollBarControlBinding ScrollBarControlBinding;
  typedef ToolboxScrollBarLedger::ViewportScrollBarBinding ViewportScrollBarBinding;
  bool ensureScrollBarBinding(short,
                              const Rect &,
                              int,
                              int,
                              int,
                              int,
                              loka::app::scene::NativeLifetimeHint,
                              ScrollBarControlBinding *&,
                              Rect *movedFrom = 0);
  bool ensureScrollBarControl(short,
                              const Rect &,
                              const loka::app::ScrollBarProps &,
                              loka::app::scene::NodeContext *,
                              loka::app::scene::NativeLifetimeHint);
  void bindEnabledState(loka::core::State<bool> *) {}
  void unbindEnabledState(loka::core::State<bool> *) {}
  bool hasLiveBinding(loka::core::State<bool> *) const;
  void clearControls();
  void retireScrollBarBinding(ScrollBarControlBinding &, loka::app::scene::NativeLifetimeHint);
  void queueRetiredScrollBarControl(ControlRef, loka::app::scene::NativeLifetimeHint);
  void queueRetiredControl(ControlRef, loka::app::scene::NativeLifetimeHint);
  bool hasLiveBinding(ControlRef) const;
  void disposeNativeHandle(ControlRef);
  void flushControls();
  void flushRetiredNativeHandles()
  {
    this->flushTE();
    this->flushControls();
  }
  loka::app::layout::StackSpans *scrollSpans()
  { return this->scrollBarLedger_.viewportScrollBars_.empty() ? 0 : this->scrollBarLedger_.viewportScrollBars_[0].spans.get(); }
  ToolboxScrollBarLedger scrollBarLedger_;
  struct ButtonControlBinding
  {
    Rect rect;
    short resourceId;
    ToolboxButtonContext *context;
    std::string label;
    ControlRef control;
    loka::core::EmitterState *emitter;
    loka::core::State<bool> *enabled;
    bool usedThisFrame;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };
  std::vector<ButtonControlBinding> buttonControls_;
#ifdef LOKA_HOST_CONTROL_ORDER
  loka::app::scene::ExactMatchHandleBucket<ControlRef> pushButtonBucket_;
  void renderScrollView(loka::app::ScrollViewNode *);
#endif
  void requestControlRepaint(const Rect &rect);
  void refreshButtonEnabled(ButtonControlBinding &);
  void refreshScrollBarEnabled(ScrollBarControlBinding &);
  void commitScrollBarValueAt(std::size_t);
  void
  commitViewportScrollBarValue(ViewportScrollBarBinding &, ScrollBarControlBinding &, loka::app::ScrollBarOrientation);
  void addPendingDirty(const Rect &rect)
  {
    this->pendingDirtyRects_.push_back(rect);
  }

  bool handleTextKey(char);
  void beginBatchUpdate() {}
  void endBatchUpdate() {}
  struct EditTextControlBinding;
  void updateStateFromEdit(EditTextControlBinding &, const loka::core::String &);
  void recordEditHit(const Rect &, loka::core::State<loka::core::String> *,
                     loka::app::scene::BoundaryNode *, ToolboxEditTextContext *);
  struct EditTextControlBinding
  {
    loka::app::scene::NodeContext *ownerContext;
    TEHandle te;
    loka::core::State<loka::core::String> *text;
    loka::app::scene::WriteSeat<loka::core::String> textSeat;
    ToolboxEditInstalled installed;
    ToolboxTextEditorContext *editor;
    Rect rect;
    bool usedThisFrame;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };
  ToolboxEditControlLedger<EditTextControlBinding, loka::app::scene::NodeContext> editControls_;
  template <typename T> struct RetiredNativeEntry
  {
    T handle;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };
  std::vector<RetiredNativeEntry<ControlRef> > retiredControls_, retiredScrollBarControls_;
  std::vector<RetiredNativeEntry<TEHandle> > retiredTextEdits_;
  loka::app::scene::ExactMatchHandleBucket<TEHandle> textEditBucket_;
  unsigned poolIntakeAuditFailCount_;
  bool inBatchUpdate_;
  template <typename T> void queueRetiredNativeHandle(std::vector<RetiredNativeEntry<T> > &, T,
                                                     loka::app::scene::NativeLifetimeHint);
  template <typename T> void flushRetiredEntriesInto(std::vector<RetiredNativeEntry<T> > &,
                                                    loka::app::scene::ExactMatchHandleBucket<T> &);
  void queueRetiredTextEdit(TEHandle, loka::app::scene::NativeLifetimeHint);
  bool hasLiveBinding(TEHandle) const;
  void disposeNativeHandle(TEHandle);
  ToolboxEditPresentation ensureEditTextControl(ToolboxEditTextContext *, const Rect &, loka::core::State<loka::core::String> *,
                                loka::app::scene::NativeLifetimeHint);
  void retireEditTextBinding(EditTextControlBinding &, loka::app::scene::NativeLifetimeHint);
  void retireEditTextControlAt(std::size_t, loka::app::scene::NativeLifetimeHint);
  void retireEditTextControl(loka::app::scene::NodeContext *, loka::app::scene::NativeLifetimeHint);
  void syncEditTextFromState(EditTextControlBinding &);
#ifdef LOKA_HOST_CONTROL_ORDER
  void refreshContextProps(loka::app::scene::Node *, short = 0);
#else
  void refreshContextProps(loka::app::scene::Node *, short = 0) {}
#endif
  short measureTextWidth(const loka::core::String &, const ToolboxTextFontDescriptor &) const;
  short measureTextWidth(const loka::core::String &) const;
#ifdef LOKA_HOST_CLIP_PAINT
  void recordCellHit(const Rect &,
                     loka::core::EmitterState *,
                     loka::app::scene::BoundaryNode *,
                     ToolboxCellContext *,
                     loka::core::State<loka::core::String> *);
#elif defined(LOKA_HOST_CELL_PAINT) || defined(LOKA_HOST_CONTROL_WIDTH)
  void recordCellHit(const Rect &, loka::core::EmitterState *, loka::app::scene::BoundaryNode *,
                     ToolboxCellContext *, loka::core::State<loka::core::String> *) {}
#endif
#ifdef LOKA_HOST_CONTROL_WIDTH
#ifdef LOKA_HOST_CONTROL_ORDER
  bool ensureButtonControl(short,
                           const Rect &,
                           const loka::core::String &,
                           loka::core::EmitterState *,
                           loka::core::State<bool> *,
                           loka::app::scene::NativeLifetimeHint,
                           ToolboxButtonContext *,
                           ButtonControlBinding *&,
                           bool *painted = 0);
#else
  bool ensureButtonControl(short,
                           const Rect &rect,
                           const loka::core::String &,
                           loka::core::EmitterState *,
                           loka::core::State<bool> *,
                           loka::app::scene::NativeLifetimeHint,
                           ToolboxButtonContext *,
                           ButtonControlBinding *&binding,
                           bool *painted = 0)
  {
    if (painted)
      *painted = false;
    binding = 0;
    toolbox_host::controlRect = rect;
    return true;
  }
#endif
#ifdef LOKA_HOST_CONTROL_ORDER
  void destroyButtonControl(short, loka::app::scene::NativeLifetimeHint);
#else
  void destroyButtonControl(short, loka::app::scene::NativeLifetimeHint) {}
#endif
#ifdef LOKA_HOST_CLIP_PAINT
  void recordButtonHit(const Rect &,
                       loka::core::EmitterState *,
                       loka::core::State<bool> *,
                       loka::app::scene::BoundaryNode *,
                       ToolboxButtonContext *);
  void recordPopupHit(const Rect &, loka::core::State<bool> *, ToolboxPopupMenuContext *);
#else
  void recordButtonHit(const Rect &, loka::core::EmitterState *, loka::core::State<bool> *,
      loka::app::scene::BoundaryNode *, ToolboxButtonContext *) {}
  void recordPopupHit(const Rect &, loka::core::State<bool> *, ToolboxPopupMenuContext *) {}
#endif
#endif
#ifdef LOKA_HOST_CLIP_PAINT
  void recordTextHit(const Rect &,
                     short,
                     short,
                     loka::core::State<loka::core::String> *,
                     loka::app::scene::BoundaryNode *,
                     bool,
                     short,
                     ToolboxTextContext *);
#elif defined(LOKA_HOST_ZSTACK_UNITS)
  void recordTextHit(const Rect &rect,
                     short x,
                     short y,
                     loka::core::State<loka::core::String> *text,
                     loka::app::scene::BoundaryNode *boundary,
                     bool relayout,
                     short width,
                     ToolboxTextContext *context)
  {
    const ToolboxHitLedger::TextHit hit = {context, rect, x, y, text, boundary, width, relayout};
    this->hitLedger_.textHits_.push_back(hit);
  }
#else
  void recordTextHit(const Rect &, short, short, loka::core::State<loka::core::String> *,
                     loka::app::scene::BoundaryNode *, bool, short, ToolboxTextContext *) { ++toolbox_host::textHits; }
#endif
  void bindTextState(loka::core::State<loka::core::String> *) {}
  void unbindTextState(loka::core::State<loka::core::String> *) {}
  bool hasLiveBinding(loka::core::State<loka::core::String> *s) const
  {
    for (std::size_t i = 0; i < editControls_.size(); ++i)
      if (editControls_[i].text == s) return true;
    return false;
  }
  void activateEditControl(std::size_t index);
  void reachNativeButton(short id);
  void reachNativeBar(short id);
  void reachEditControl(loka::app::scene::NodeContext *context);
  void reachPopupHit(ToolboxPopupMenuContext *context);
  TEHandle ensureTextEditorControl(ToolboxTextEditorContext *, const Rect &, loka::app::scene::NativeLifetimeHint);
  void retireTextEditorControl(loka::app::scene::NodeContext *, loka::app::scene::NativeLifetimeHint);
  void flushTE();
  ToolboxCompositionReplay compositionReplay;
  ToolboxCompositionReplay &compositionReplay_;
  bool forceFullRedraw_;
  void registerCompositionReplay(ToolboxCompositionReplay::Registration &registration)
  {
    registration.attach(this->compositionReplay);
  }
  ToolboxWindow *window_;
  /** Paint children under the latest Box/Card placement, or without an extra clip on refusal. */
  void renderClipBox(loka::app::scene::Node *node);
  /** One registry attempt per flagged Box/Card placement, before children. */
  void ensureClipBoxContext(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state);
  virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler);
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  mutable CursorOwner cursor;
  std::vector<loka::app::scene::NodeContext *> retired;
  Rect projectionClip;
  loka::app::scene::NodeContext *renderContext;
  explicit ToolboxScenePlatformController(ToolboxWindow *window)
      : leafLayouts(0),
        scrollViewClipRgn_(NewRgn()),
        paintSuppressClipRgn_(NewRgn()),
        scrollBarLedger_(4),
#ifdef LOKA_HOST_CONTROL_ORDER
        pushButtonBucket_(0),
#endif
        poolIntakeAuditFailCount_(0),
        inBatchUpdate_(false),
        compositionReplay_(compositionReplay),
        forceFullRedraw_(false),
        window_(window),
        renderContext(0),
        rootNode_(0),
        controlIds_(100)
  {
    SetRect(&projectionClip, -30000, -30000, 30000, 30000);
  }
  ~ToolboxScenePlatformController()
  {
    this->clearControls();
    this->flushControls();
    this->scrollBarLedger_.scrollBarBucket_.drainWith(DisposeControl);
    this->flushTE();
    this->textEditBucket_.drainWith(TEDispose);
    if (this->scrollViewClipRgn_)
      DisposeRgn(this->scrollViewClipRgn_);
    if (this->paintSuppressClipRgn_)
      DisposeRgn(this->paintSuppressClipRgn_);
  }
  loka::app::TextShaping textShaping() const
  {
    return loka::app::PER_RUN;
  }
  CursorOwner *cursorOwner() const
  {
    return &cursor;
  }
  bool intersectWithProjectionClip(const Rect &rect, Rect &out) const;
  void retireNodeContext(loka::app::scene::NodeContext *, loka::app::scene::NativeLifetimeHint);
  virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
  loka::app::scene::Node *rootNode_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  /** Per-pass projected, clipped opaque unit facts; layout writes, replay reads. */
  ToolboxUnitLedger unitLedger_;
  ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN> staticBlur_;
  ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE> bandBlur_;
  ToolboxPressLedger pressLedger_;
  ToolboxErasedPass erasedPass_;
  /** Host observes admission only; native pixels are pinned by FrostStandalone. */
  struct FrostCache
  {
    unsigned paints;
    FrostCache()
        : paints(0)
    {
    }
    bool paint(ToolboxWindow::WindowPort *, const Rect &, const Rect &)
    {
      ++paints;
      return false;
    }
    bool paintHairline(ToolboxWindow::WindowPort *, const Rect &rect, const Rect &unit)
    {
      ++toolbox_host::bandHairlines;
      Rect line;
      return ToolboxUnitHairlineRect(rect, unit, line);
    }
  } frostCache_;
  void noteWindowFullRequest(const char *) {}
  void noteWindowRectRequest() {}
  void noteWindowFlushFull() {}
  void noteWindowFlushDirty() {}
  /** Dirty delivery queries the previous layout key once; O(1), no rows. */
  Rect dirtyExtent(const Rect &rect) const
  {
    Rect key;
    return this->bandBlur_.queryKeyRect(key) ? ToolboxBlurDirtyExtent(rect, key) : rect;
  }
  /** Move/size callers: once per changed control per render; O(1), no rows.
      Requires the window port while the erased pass is open. */
  void validateOwnControlMove(const Rect &before, const Rect &after);
  Rect admitOwnControlMove(const Rect &before, const Rect &after) const;
  void validateOwnControlMove(const Rect &admittedArea);
  void noteControlDraw();
  ToolboxErasedPass &erasedPass()
  {
    return this->erasedPass_;
  }
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  ToolboxControlIdAllocator controlIds_;
  std::vector<Rect> pendingDirtyRects_;
  std::vector<loka::core::State<loka::core::String> *> pendingTextStates_;
  struct RenderStats
  {
    unsigned renderCalls, totalRenderCalls, totalBoundaryApplyCount;
    unsigned renderDirtyCalls, totalRenderDirtyCalls;
    unsigned controlDrawCount, totalControlDrawCount;
    RenderStats()
        : renderCalls(0),
          totalRenderCalls(0),
          totalBoundaryApplyCount(0),
          renderDirtyCalls(0),
          totalRenderDirtyCalls(0),
          controlDrawCount(0),
          totalControlDrawCount(0)
    {
    }
    void refreshHitCounts(int, int, int, int, int) {}
  } debugStats_;
  void clearEnabledBindings() {}
  void drawControlsInRect(const Rect &);
  void hideRetiredControl(ControlRef control);
  void drawScrollBarControl(short);
  void drawControl(ControlRef);
  typedef ToolboxHitLedger::TextHit TextHit;
  void redrawPopupHit(PopupHit &)
  {
    LOKA_VERIFY(false);
  }
  void redrawTextHit(TextHit &);
  virtual void synchronize() {}
  virtual bool hasPendingSync() const
  {
    return false;
  }
  virtual void destroy()
  {
    this->staticBlur_.clear();
    this->bandBlur_.clear();
    this->rootNode_ = 0;
  }
};
#define LOKA_HOST_OTHER_HANDLER(Name)                                                                                  \
  inline bool RegisterToolbox##Name##NodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &)                      \
  {                                                                                                                    \
    return true;                                                                                                       \
  }
#ifndef LOKA_HOST_CONTROL_WIDTH
LOKA_HOST_OTHER_HANDLER(Button)
#endif
#ifndef LOKA_HOST_CELL_PAINT
LOKA_HOST_OTHER_HANDLER(Cell)
#endif
#ifndef LOKA_HOST_CONTROL_WIDTH
LOKA_HOST_OTHER_HANDLER(EditText)
#endif
#ifndef LOKA_HOST_IMAGE_VIEW
LOKA_HOST_OTHER_HANDLER(ImageView)
#else
bool RegisterToolboxImageViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
LOKA_HOST_OTHER_HANDLER(OpenFileDialog)
LOKA_HOST_OTHER_HANDLER(ConfirmDialog)
#ifndef LOKA_HOST_CONTROL_WIDTH
LOKA_HOST_OTHER_HANDLER(PopupMenu)
#endif
#ifndef LOKA_HOST_CONTROL_ORDER
LOKA_HOST_OTHER_HANDLER(ScrollBar)
#else
bool RegisterToolboxScrollBarNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
#undef LOKA_HOST_OTHER_HANDLER

namespace toolbox_host
{
  extern std::vector<std::string> paintOperations;
  /** Every EraseRect's rect, in call order; paintOperations logs each as "erase". */
  extern std::vector<Rect> erasedRects;
  struct Draw
  {
    Rect clip;
    short x, y, size;
    Style face;
    int length;
    std::string bytes;
    long ink;
  };
  struct Fill
  {
    Rect rect;
    Pattern pattern;
    long ink;
  };
  struct StencilTransfer
  {
    Rect rect;
    short mode, rowBytes;
    std::vector<unsigned char> bytes;
  };
  extern std::vector<char> cardOperations;
  extern std::vector<StencilTransfer> stencilTransfers;
  extern std::vector<Fill> stencilRuns;
  extern std::vector<Fill> fills;
  extern std::vector<Rect> roundFrames;
  extern int shadowPaints, regionDifferences;
  extern short shadowMode;
  extern Pattern shadowPattern;
  extern PenState pen;
  extern std::vector<Draw> draws;
  extern std::vector<std::string> pascalDraws, windowTitles, widthPayloads, measurePayloads;
  extern int erases, widths, measures, fonts, metrics;
  extern std::vector<long> backgroundColors, foregroundColors;
  /** Remaining NewRgn calls to refuse with a null handle (Classic memory pressure). */
  extern int failRegions;
  /** NewRgn attempts, including refused ones. */
  extern int regions;
  extern int refuseRegionAttempt;
  void reset();
} // namespace toolbox_host
#ifdef LOKA_HOST_GAP_DISPATCH
// RectSurface painting is outside the gap fixture; accidental routing is a failure.
class ToolboxRectSurfaceContext : public loka::app::scene::NodeContext
{
public:
  void setBoundary(loka::app::scene::BoundaryNode *)
  {
    LOKA_VERIFY(false);
  }
};
inline void EnsureToolboxRectSurfaceContext(loka::app::RectSurfaceNode *, ToolboxScenePlatformController *)
{
  LOKA_VERIFY(false);
}
#endif
#endif // LOKA_TEST_TOOLBOX_HOST_HPP
