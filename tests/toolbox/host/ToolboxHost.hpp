#ifndef LOKA_TEST_TOOLBOX_HOST_HPP
#define LOKA_TEST_TOOLBOX_HOST_HPP
struct ToolboxScrollViewMetrics;
#include "app/layout/NaturalWidthSource.hpp"
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
#define LOKA_TOOLBOX_IMAGE_VIEW_CONTEXT_HPP
#define LOKA_TOOLBOX_OPEN_FILE_DIALOG_CONTEXT_HPP
#define LOKA_TOOLBOX_CONFIRM_DIALOG_CONTEXT_HPP
#ifndef LOKA_HOST_CONTROL_WIDTH
#define LOKA_TOOLBOX_POPUP_MENU_CONTEXT_HPP
#endif
#define LOKA_TOOLBOX_SCROLL_BAR_CONTEXT_HPP
class ToolboxTextContext;
class ToolboxTextFontDescriptor;
#include "Quickdraw.h"
#include "Controls.h"
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
#define LOKA_TOOLBOX_PENDING_DIALOGS_HPP
#include "app/OpenFileDialog.hpp"
#include "ToolboxDialogPresenter.hpp"
class ToolboxPendingDialogs
{
public:
  ToolboxDialogPresenter *take() { return 0; }
  bool empty() const { return true; }
};
class ToolboxScenePlatformController;
class ToolboxWindow
{
  loka::core::PushStateTracker tracker_;
  loka::core::MutableState<bool> visible_;
public:
  struct WindowPort : GrafPort { Rect portRect; } port;
  ToolboxWindowContext context_;
  void (*onFlush)(void *);
  void *flushData;
  ToolboxWindow() : visible_(true), onFlush(0), flushData(0)
  {
    this->tracker_.addState(&this->visible_);
    toolbox_host::frontWindow = &port;
    port.txFont = 3;
    port.txSize = 12;
    port.txFace = 0;
    port.fgColor = blackColor;
    SetRect(&port.portRect, 0, 0, 360, 260);
  }
  void requestInvalidate() { ++toolbox_host::invalidations; }
  void requestInvalidateWithReason(const char *) { ++toolbox_host::invalidations; }
  ToolboxScenePlatformController *scenePlatformController() { return 0; }
  void flushInvalidate() { if (this->onFlush) this->onFlush(this->flushData); }
  loka::core::StateTracker *getTracker() { return &this->tracker_; }
  void requestInvalidateRect(const Rect &) { ++toolbox_host::invalidations; }
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
  const Rect &chromeRect() const { static const Rect rect = {0, 0, 0, 0}; return rect; }
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
  void installHit(const ButtonHit &hit) { this->hitLedger_.buttonHits_.push_back(hit); }
  void installHit(const CellHit &hit) { this->hitLedger_.cellHits_.push_back(hit); }
  void installHit(const PopupHit &hit) { this->hitLedger_.popupHits_.push_back(hit); }
  loka::app::scene::FocusLink fallbackFocus_;
  ToolboxEditTextContext *fallbackFocusContext() const;
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
#include "ToolboxInputBodies.hpp"
public:
#include "ToolboxScrollViewProjection.hpp"
  unsigned leafLayouts;
  struct ScrollCall
  {
    short id;
    Rect rect;
    int maximum;
    int pageStep;
  };
  std::vector<ScrollCall> scrollCalls;
  int scrollMaximum(short id = 100) const
  {
    for (std::size_t i = this->scrollCalls.size(); i > 0; --i)
      if (this->scrollCalls[i - 1].id == id)
        return this->scrollCalls[i - 1].maximum;
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
  short layoutScrollView(loka::app::ScrollViewNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *);
  void requestStructurePresent();
  virtual void onBoundaryApply(loka::app::scene::Node *, loka::app::scene::BoundaryNode *,
      const loka::app::scene::BoundaryLocalApplyInfo &, const loka::app::scene::PlatformApplyPlan &);
  virtual void releaseNodeContexts(loka::app::scene::Node *);
  void refuseScrollViewShortRange()
  {
    if (this->projectionParentScopes_.activeDepth())
      this->projectionParentScopes_.current().markShortRangeRefused();
  }
  bool refuseNarrowingInScrollScope(int y)
  {
    if (!this->projectionParentScopes_.activeDepth()) return false;
    if (y < SHRT_MIN || y > SHRT_MAX) this->refuseScrollViewShortRange();
    return this->projectionParentScopes_.current().hasShortRangeRefusal();
  }
  int ensureViewportScrollBarControl(
      const Rect &, loka::app::ScrollViewNode *, int, int, int, const ToolboxScrollViewMetrics &, bool);
  void destroyViewportScrollBarControl(loka::app::ScrollViewNode *, loka::app::scene::NativeLifetimeHint);
  short allocateControlId() { return this->controlIds_.allocate(); }
  void destroyScrollBarControl(short id, loka::app::scene::NativeLifetimeHint) { this->controlIds_.release(id); }
  bool handleControlClick(const Point &);
  void emitHitEmitter(loka::core::EmitterState *);
  void applyPopupSelectionChange(const Rect &, loka::app::scene::BoundaryNode *,
      loka::core::State<int> *, const loka::app::scene::WriteSeat<int> &, loka::core::EmitterState *, int);
  typedef ToolboxScrollBarLedger::ScrollBarControlBinding ScrollBarControlBinding;
  typedef ToolboxScrollBarLedger::ViewportScrollBarBinding ViewportScrollBarBinding;
  bool ensureScrollBarBinding(short id,
                              const Rect &rect,
                              int,
                              int maximum,
                              int,
                              int pageStep,
                              loka::app::scene::NativeLifetimeHint,
                              ScrollBarControlBinding *&out)
  {
    const ScrollCall call = {id, rect, maximum, pageStep};
    this->scrollCalls.push_back(call);
    out = 0;
    for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
      if (this->scrollBarLedger_.scrollBarControls_[i].resourceId == id)
      {
        out = &this->scrollBarLedger_.scrollBarControls_[i];
        out->maximum = maximum;
        out->rect = rect;
        out->usedThisFrame = true;
        break;
      }
    return true;
  }
  loka::app::layout::StackSpans *scrollSpans()
  { return this->scrollBarLedger_.viewportScrollBars_.empty() ? 0 : this->scrollBarLedger_.viewportScrollBars_[0].spans.get(); }
  ToolboxScrollBarLedger scrollBarLedger_;
  struct ButtonControlBinding
  {
    Rect rect;
    ToolboxButtonContext *context;
    std::string label;
    ControlRef control;
    loka::core::EmitterState *emitter;
    loka::core::State<bool> *enabled;
    bool usedThisFrame;
  };
  std::vector<ButtonControlBinding> buttonControls_;
  void commitScrollBarValueAt(std::size_t);
  void
  commitViewportScrollBarValue(ViewportScrollBarBinding &, ScrollBarControlBinding &, loka::app::ScrollBarOrientation);
  void addPendingDirty(const Rect &rect) { this->pendingDirtyRects_.push_back(rect); }
  void installScroll(const ScrollBarControlBinding &row) { this->scrollBarLedger_.scrollBarControls_.push_back(row); }

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
  void refreshContextProps(loka::app::scene::Node *, short = 0) {}
  short measureTextWidth(const loka::core::String &, const ToolboxTextFontDescriptor &) const;
  short measureTextWidth(const loka::core::String &) const;
#if defined(LOKA_HOST_CELL_PAINT) || defined(LOKA_HOST_CONTROL_WIDTH)
  void recordCellHit(const Rect &, loka::core::EmitterState *, loka::app::scene::BoundaryNode *,
                     ToolboxCellContext *, loka::core::State<loka::core::String> *) {}
#endif
#ifdef LOKA_HOST_CONTROL_WIDTH
  bool ensureButtonControl(short, const Rect &rect, const loka::core::String &, loka::core::EmitterState *,
      loka::core::State<bool> *, loka::app::scene::NativeLifetimeHint, ToolboxButtonContext *) { toolbox_host::controlRect = rect; return true; }
  void destroyButtonControl(short, loka::app::scene::NativeLifetimeHint) {}
  void recordButtonHit(const Rect &, loka::core::EmitterState *, loka::core::State<bool> *,
      loka::app::scene::BoundaryNode *, ToolboxButtonContext *) {}
  void recordPopupHit(const Rect &, loka::core::State<bool> *, ToolboxPopupMenuContext *) {}
#endif
  void recordTextHit(const Rect &, short, short, loka::core::State<loka::core::String> *,
                     loka::app::scene::BoundaryNode *, bool, short, ToolboxTextContext *) { ++toolbox_host::textHits; }
  void bindTextState(loka::core::State<loka::core::String> *) {}
  void unbindTextState(loka::core::State<loka::core::String> *) {}
  bool hasLiveBinding(loka::core::State<loka::core::String> *s) const
  {
    for (std::size_t i = 0; i < editControls_.size(); ++i)
      if (editControls_[i].text == s) return true;
    return false;
  }
  void activateEditControl(std::size_t index);
  bool handleEditClick(const Point &point);
  TEHandle ensureTextEditorControl(ToolboxTextEditorContext *, const Rect &, loka::app::scene::NativeLifetimeHint);
  void retireTextEditorControl(loka::app::scene::NodeContext *, loka::app::scene::NativeLifetimeHint);
  void flushTE();
  ToolboxCompositionReplay compositionReplay;
  void registerCompositionReplay(ToolboxCompositionReplay::Registration &registration)
  {
    registration.attach(this->compositionReplay);
  }
  ToolboxWindow *window_;
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  mutable CursorOwner cursor;
  std::vector<loka::app::scene::NodeContext *> retired;
  Rect projectionClip;
  loka::app::scene::NodeContext *renderContext;
  explicit ToolboxScenePlatformController(ToolboxWindow *window)
      : leafLayouts(0),
        scrollCalls(),
        scrollViewClipRgn_(NewRgn()),
        paintSuppressClipRgn_(NewRgn()),
        scrollBarLedger_(0),
        poolIntakeAuditFailCount_(0),
        inBatchUpdate_(false),
        window_(window),
        renderContext(0),
        rootNode_(0),
        controlIds_(100)
  {
    SetRect(&projectionClip, -30000, -30000, 30000, 30000);
  }
  ~ToolboxScenePlatformController()
  {
    if (this->scrollViewClipRgn_) DisposeRgn(this->scrollViewClipRgn_);
    if (this->paintSuppressClipRgn_) DisposeRgn(this->paintSuppressClipRgn_);
    this->flushTE();
    this->textEditBucket_.drainWith(TEDispose);
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
  void retireNodeContext(loka::app::scene::NodeContext *context, loka::app::scene::NativeLifetimeHint hint)
  {
    // Mirrors production: detach strips the context's edit binding first.
    this->retireEditTextControl(context, hint);
    retired.push_back(context);
  }
  virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
  loka::app::scene::Node *rootNode_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  ToolboxControlIdAllocator controlIds_;
  std::vector<Rect> pendingDirtyRects_;
  std::vector<loka::core::State<loka::core::String> *> pendingTextStates_;
  struct RenderStats
  {
    unsigned renderCalls, totalRenderCalls, totalBoundaryApplyCount;
    unsigned controlDrawCount, totalControlDrawCount;
    RenderStats() : renderCalls(0), totalRenderCalls(0), totalBoundaryApplyCount(0), controlDrawCount(0), totalControlDrawCount(0) {}
    void refreshHitCounts(int, int, int, int, int) {}
  } debugStats_;
  void clearEnabledBindings() {}
  void drawControlsInRect(const Rect &);
  virtual void synchronize() {}
  virtual bool hasPendingSync() const
  {
    return false;
  }
  virtual void destroy() {}
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
LOKA_HOST_OTHER_HANDLER(ImageView)
LOKA_HOST_OTHER_HANDLER(OpenFileDialog)
LOKA_HOST_OTHER_HANDLER(ConfirmDialog)
#ifndef LOKA_HOST_CONTROL_WIDTH
LOKA_HOST_OTHER_HANDLER(PopupMenu)
#endif
LOKA_HOST_OTHER_HANDLER(ScrollBar)
#undef LOKA_HOST_OTHER_HANDLER

namespace toolbox_host
{
  struct Draw
  {
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
#endif
