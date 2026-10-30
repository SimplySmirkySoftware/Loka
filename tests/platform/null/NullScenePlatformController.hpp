#ifndef LOKA_TESTS_PLATFORM_NULL_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_TESTS_PLATFORM_NULL_SCENE_PLATFORM_CONTROLLER_HPP

#include "app/layout/NaturalSizeSource.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include <cstddef>
#include "platform/null/NullMenuAttachment.hpp"
#include "app/layout/TextShaping.hpp"

namespace loka { namespace app { class TextWidthSource; } }
#include <vector>

#include "app/RectSurface.hpp"
#include "app/FocusParticipant.hpp"
#include "app/scene/projection/NativeHandlePool.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"

namespace loka
{
  namespace testing
  {
    class LazyViewAccess;
    class NullClipAccess;
  } // namespace testing
} // namespace loka

class NullButtonContext;
class NullEditTextContext;
class NullScrollBarContext;
class NullWindow;

class NullScenePlatformController : public loka::app::scene::IPlatformController,
                                    public loka::app::layout::INaturalSizeSource
{
public:
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
  virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const;
  /** Retire door for a RectSurface context: takes back that surface's pending
      seat rows so a surface reclaimed during delivery publishes nothing. */
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *surface)
  {
    this->rectSurfaceExtentLedger_.cancel(surface);
  }
  enum ControlRecipe
  {
    CONTROL_RECIPE_BUTTON,
    CONTROL_RECIPE_EDIT_TEXT,
    CONTROL_RECIPE_SCROLL_BAR
  };

  struct FakeControlHandle
  {
    FakeControlHandle(int identity, loka::app::scene::NodeContext *contextOwner)
        : id(identity),
          owner(contextOwner),
          hitOwner(contextOwner),
          disposed(false),
          leakedDeliberately(false)
    {
    }

    int id;
    loka::app::scene::NodeContext *owner;
    loka::app::scene::NodeContext *hitOwner;
    bool disposed;
    bool leakedDeliberately;
  };

  struct LedgerRow
  {
    LedgerRow(FakeControlHandle *nativeHandle,
              ControlRecipe controlRecipe,
              loka::app::scene::NativeLifetimeHint lifetimeHint)
        : handle(nativeHandle),
          visible(false),
          hint(lifetimeHint),
          recipe(controlRecipe)
    {
    }

    FakeControlHandle *handle;
    bool visible;
    loka::app::scene::NativeLifetimeHint hint;
    ControlRecipe recipe;
  };

  enum EventKind
  {
    EVENT_CONTROL_CREATED,
    EVENT_CONTROL_SHOWN,
    EVENT_CONTROL_HIDDEN,
    EVENT_CONTROL_DISPOSED,
    EVENT_FLUSH_BEGIN,
    EVENT_FLUSH_END,
    EVENT_DRAIN_BEGIN,
    EVENT_DRAIN_END,
    EVENT_WINDOW_DISPOSED
  };

  struct EventRecord
  {
    EventRecord(unsigned long eventSequence, EventKind eventKind, int handleIdentity)
        : sequence(eventSequence),
          kind(eventKind),
          handleId(handleIdentity)
    {
    }

    unsigned long sequence;
    EventKind kind;
    int handleId;
  };

  struct TeardownCounters
  {
    TeardownCounters()
        : backPointerCleared(0),
          hitRouteRemoved(0),
          queuedForNativeRetirement(0),
          ledgerRowRemovedAtSafePoint(0)
    {
    }

    unsigned long backPointerCleared;
    unsigned long hitRouteRemoved;
    unsigned long queuedForNativeRetirement;
    unsigned long ledgerRowRemovedAtSafePoint;
  };

  struct BucketStats
  {
    BucketStats(unsigned long hits, unsigned long misses, unsigned long evicts, std::size_t liveDepth)
        : hitCount(hits),
          missCount(misses),
          evictCount(evicts),
          depth(liveDepth)
    {
    }

    unsigned long hitCount;
    unsigned long missCount;
    unsigned long evictCount;
    std::size_t depth;
  };

  explicit NullScenePlatformController(std::size_t bucketDepthCap = 8,
                                       loka::app::TextShaping shaping = loka::app::PER_RUN);
  loka::app::TextShaping textShaping() const
  {
    return this->textShaping_;
  }
  /** Select the width provider for a projection kind. Null uses the supplied
      synthetic table for both; a test rail can supply discriminating widths.
      The returned source is borrowed only for the synchronous measure. */
  virtual const loka::app::TextWidthSource &textWidthSource(loka::app::TextShaping,
                                                            const loka::app::TextWidthSource &synthetic) const
  {
    return synthetic;
  }
  virtual ~NullScenePlatformController();
  bool handleEditCommand(loka::app::EditVerb verb);
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out)
  {
    loka::app::FocusParticipant *row =
        loka::app::FocusParticipant::from(this->focusSource_.peerRow());
    out = row ? row->context() : 0;
    return true;
  }
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
#ifdef TEST_BUILD
  void simulateNativeFocus(loka::app::scene::NodeContext *context)
  {
    this->focusSource_.cut();
    loka::app::scene::Node *node = context ? context->owner() : 0;
    loka::app::FocusParticipant *row = node
        ? loka::app::FocusParticipant::from(node->asFocusParticipant()) : 0;
    if (row)
      row->connectSource(this->focusSource_);
  }
  void clearSimulatedFocus() { this->focusSource_.cut(); }
#endif

  virtual void onChange(loka::app::scene::Node *rootNode,
                        loka::app::scene::NodeDirtyFlags flags,
                        bool fullRebuild);
  /** Real platforms (Win32, Toolbox, macOS) opt into skipping the global
      onChange for applies that claim to be boundary-local paint-only. The
      wall contract must exercise the same skip, or an apply that lies about
      its structure work presents correctly here while every real platform
      goes dark (#277). */
  virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
  {
    return true;
  }
  virtual void requestRelayout();
  virtual void synchronize();
  /** The null arm's paint channel. Toolbox re-applies scroll-bar props at
      draw (ensureScrollBarControl inside ToolboxScrollBarContext::draw), so
      a paint-only apply legitimately refreshes displayed values there. The
      wall contract needs the same channel or value/range flows that ride
      paint on real platforms are only observable through the projection
      sweep here. */
  virtual void onBoundaryApply(loka::app::scene::Node *rootNode,
                               loka::app::scene::BoundaryNode *boundary,
                               const loka::app::scene::BoundaryLocalApplyInfo &info,
                               const loka::app::scene::PlatformApplyPlan &plan);
  virtual bool hasPendingSync() const;
  virtual void drainNativeRetirements();
  virtual void destroy();
  virtual void releaseMenu() { this->menuAttachment_.disconnect(); }
  NullMenuAttachment &menuAttachment() { return this->menuAttachment_; }
  virtual bool prepareProjectedLayout(loka::app::scene::Node *node,
                                      loka::app::scene::LayoutState &state);
  /** RectSurface, Text and AttributedText handlers cannot be replaced (registerNodeHandler refuses):
      the presenter addresses their contexts by concrete Null type. Every other kind
      may be replaced; the paint walk answers native controls by kind and never
      casts a context it did not install. */
  virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler);

  /** Runs the same deterministic projection traversal as onChange with
      caller-supplied bounds, retaining them for subsequent onChange calls. */
  int projectLayoutForTesting(loka::app::scene::Node *node,
                              const loka::app::scene::LayoutState &state,
                    const loka::app::layout::LazyWindow *range = 0,
                    loka::app::layout::StackSpans *spans = 0);

  unsigned long leafLayoutVisitsForTesting() const { return this->leafLayoutVisits_; }

  const std::vector<LedgerRow> &ledger() const;
  loka::app::scene::NodeDirtyFlags lastOnChangeFlags() const
  {
    return lastOnChangeFlags_;
  }
  unsigned long onChangeCallCount() const
  {
    return onChangeCallCount_;
  }
  const std::vector<FakeControlHandle *> &allHandles() const;
  const std::vector<EventRecord> &eventLog() const;
  std::size_t retiredCount() const;
  const TeardownCounters &teardownCounters() const;
  unsigned long intakeCheckFailCount() const;
  unsigned long createdCount() const;
  unsigned long disposedCount() const;
  unsigned cellRefusalCount() const;
  unsigned scrollViewShortRangeRefusalCount() const;
  unsigned nestedScrollViewRefusalCount() const;
  unsigned projectionParentScopeDepthForTesting() const;
  BucketStats bucketStats(ControlRecipe recipe) const;
  unsigned long eventCount(EventKind kind) const;
  const LedgerRow *findLedgerRow(ControlRecipe recipe) const;
  bool hasHitTarget(ControlRecipe recipe) const;
  bool injectNotification(ControlRecipe recipe);
  unsigned long injectedDeliveryCount() const;

  /** Test injection: leaves the next retiring handle's owner set so the
      intake consistency check can exercise its deliberate-release arm. */
  void preserveNextRetiredOwnerForTesting();

  /** Test injection: materializes the logical tree without projecting native
      contexts during the next apply. */
  void skipNextProjectionForTesting();

  loka::app::scene::PaintScope paintScope() const;
  bool queryPaintProjectionScope(loka::app::scene::PaintScope &scope) const;
  void presentPaintPlan(loka::app::scene::BoundaryNode *root,
                        const loka::app::scene::ApplyPaintPlan &plan,
                        loka::app::scene::PaintPlacementEligibility eligibility = loka::app::scene::PLACEMENT_ELIGIBLE);
  virtual void onPaintPlanSubmitted(loka::app::scene::BoundaryNode *, const loka::app::scene::ApplyPaintPlan &) {}
  /** Rail policy for the common collector; false excludes logical non-drawers. */
  bool queryPaintAnswer(loka::app::scene::Node *,
                        loka::app::scene::NodeContext *,
                        const loka::app::scene::PaintQuery &,
                        loka::app::scene::PaintAnswer &);
  /** Null-only synchronous observation seam; borrowed rows must not escape. */
  virtual void onPaintAnswersCollected(loka::app::scene::BoundaryNode *,
                                       const loka::app::scene::BoundaryLocalApplyInfo &,
                                       const loka::app::scene::PaintAnswerBuffer<> &,
                                       const loka::app::scene::PaintApplyVerdict &)
  {
  }
  virtual void onPaintQueried() {}
  virtual void onPaintCommitted() {}

private:
  NullMenuAttachment menuAttachment_;
  friend class loka::testing::LazyViewAccess;
  friend class loka::testing::NullClipAccess;
  unsigned long leafLayoutVisits_;
  class PaintCompletionVisitor;
  class PaintInvalidationVisitor;
  class LayoutTraversal;

  class RefusedProjectedNodeHandlers
  {
  public:
    RefusedProjectedNodeHandlers();
    void registerWith(NullScenePlatformController &controller);
    unsigned cellCount() const;

  private:
    loka::app::scene::RefusedNodeHandler cardBand_;
    loka::app::scene::RefusedNodeHandler ribbonGroupEdge_;
    loka::app::scene::RefusedNodeHandler cell_;
    loka::app::scene::RefusedNodeHandler popupMenu_;
    loka::app::scene::RefusedNodeHandler imageView_;
    loka::app::scene::RefusedNodeHandler openFileDialog_;
    loka::app::scene::RefusedNodeHandler confirmDialog_;
  };

  struct RetiredEntry
  {
    RetiredEntry(FakeControlHandle *nativeHandle,
                 ControlRecipe controlRecipe,
                 loka::app::scene::NativeLifetimeHint lifetimeHint)
        : handle(nativeHandle),
          recipe(controlRecipe),
          hint(lifetimeHint)
    {
    }

    FakeControlHandle *handle;
    ControlRecipe recipe;
    loka::app::scene::NativeLifetimeHint hint;
  };

  class DisposePooledHandle
  {
  public:
    explicit DisposePooledHandle(NullScenePlatformController *controller)
        : controller_(controller)
    {
    }

    void operator()(FakeControlHandle *handle) const;

  private:
    NullScenePlatformController *controller_;
  };

  /** The single place that maps a recipe to its pool bucket. Adding a
      recipe without a bucket here is a compile-visible omission instead of
      a control that silently pools with the wrong kind. */
  loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> &bucketFor(ControlRecipe recipe);
  const loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> &bucketFor(ControlRecipe recipe) const;

  FakeControlHandle *createLedgerRow(ControlRecipe recipe,
                                     loka::app::scene::NodeContext *owner,
                                     loka::app::scene::NativeLifetimeHint hint);
  void completeContextTeardown(FakeControlHandle *handle);
  void setVisible(FakeControlHandle *handle, bool visible);
  void observeHint(FakeControlHandle *handle, loka::app::scene::NativeLifetimeHint hint);
  LedgerRow *findLedgerRow(FakeControlHandle *handle);
  int layoutNode(loka::app::scene::Node *node,
                 const loka::app::scene::LayoutState &state,
                    const loka::app::layout::LazyWindow *range = 0,
                    loka::app::layout::StackSpans *spans = 0);
  struct ScrollViewLayoutResult;
  ScrollViewLayoutResult layoutScrollView(loka::app::scene::Node *node,
                                          const loka::app::scene::LayoutState &state,
                                          const loka::app::layout::LazyWindow *range = 0,
                                          loka::app::layout::StackSpans *spans = 0);
  bool applyScrollViewRequest(loka::app::ScrollViewNode &node,
                              const ScrollViewLayoutResult &result,
                              const loka::core::Frame &target);
  int projectLayout(loka::app::scene::Node *node,
                    const loka::app::scene::LayoutState &state,
                    const loka::app::layout::LazyWindow *range = 0,
                    loka::app::layout::StackSpans *spans = 0);
  void refuseScrollViewShortRange();
  void refuseNestedScrollView();
  bool refuseNarrowingInScrollScope(int resultY);
  void flushRetired();
  void drainBuckets();
  void disposeHandle(FakeControlHandle *handle);
  void appendEvent(EventKind kind, int handleId);
  void recordWindowDisposed();

  loka::app::scene::FocusLink focusSource_;
  const loka::app::TextShaping textShaping_;

  /** Synthetic viewport input, replaced by an explicit fixture projection. */
  loka::app::scene::LayoutState layoutState_;
  loka::app::scene::PaintScope paintScope_;
  loka::app::scene::PlatformLayoutHandlerRegistry layoutHandlers_;
  RefusedProjectedNodeHandlers refusedProjectedNodeHandlers_;
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlers_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  loka::app::scene::Node *rootNode_;
  std::vector<LedgerRow> ledger_;
  loka::app::scene::NodeDirtyFlags lastOnChangeFlags_;
  unsigned long onChangeCallCount_;
  std::vector<RetiredEntry> retired_;
  std::vector<FakeControlHandle *> allHandles_;
  loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> buttonBucket_;
  loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> editTextBucket_;
  loka::app::scene::ExactMatchHandleBucket<FakeControlHandle *> scrollBarBucket_;
  TeardownCounters teardownCounters_;
  unsigned long intakeCheckFailCount_;
  unsigned long createdCount_;
  unsigned long disposedCount_;
  unsigned long injectedDeliveryCount_;
  unsigned long nextEventSequence_;
  int nextHandleId_;
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  unsigned scrollViewShortRangeRefusalCount_;
  unsigned nestedScrollViewRefusalCount_;
  bool preserveNextRetiredOwner_;
  bool skipNextProjection_;
  bool destroyed_;
  std::vector<EventRecord> eventLog_;

  NullScenePlatformController(const NullScenePlatformController &);
  NullScenePlatformController &operator=(const NullScenePlatformController &);

  friend class NullButtonContext;
  friend class NullEditTextContext;
  friend class NullScrollBarContext;
  friend class NullWindow;
};

#endif // LOKA_TESTS_PLATFORM_NULL_SCENE_PLATFORM_CONTROLLER_HPP
