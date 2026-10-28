#ifndef LOKA_TOOLBOX_SCENE_DEBUG_STATS_HPP
#define LOKA_TOOLBOX_SCENE_DEBUG_STATS_HPP

#include "app/scene/Node.hpp"
#include <string>
#include "ToolboxPresentObservation.hpp"

class ToolboxSceneDebugStats
{
public:
  ToolboxSceneDebugStats();
  /** Last completed full layout, including passes with no viewport movement. */
  ToolboxPresentObservation lastPresentObservation;
  /** Last completed full layout with movement; survives the #1513 republication pass.
      Cleared with the rest of the debug stats on explicit reset. */
  ToolboxPresentObservation lastMovedPresentObservation;

  static std::string flagsToString(loka::app::scene::NodeDirtyFlags flags);
  void begin(loka::app::scene::NodeDirtyFlags flags, bool fullRebuild);
  void refreshHitCounts(int buttonHits, int cellHits, int editHits, int textHits, int popupHits);
  void refreshNativePoolCounters(unsigned long buttonHits,
                                 unsigned long buttonMisses,
                                 unsigned long buttonEvicts,
                                 int buttonDepth,
                                 unsigned long editHits,
                                 unsigned long editMisses,
                                 unsigned long editEvicts,
                                 int editDepth,
                                 int scrollBarDepth,
                                 int intakeAuditFails);
  std::string summary() const;
  void reset();
#if LOKA_RETRO68_DIAGNOSTICS
  // Diagnostic profile only (#135): the fprintf/localtime chain this pulls
  // from newlib is compiled out of compact builds.
  // A null overrideName uses the timestamped scene-dump filename; a caller
  // that observes at a different phase (e.g. process teardown) passes a
  // distinct 8.3 name so its report cannot overwrite a scene dump.
  bool dumpToTimestampedFile(const char *overrideName = 0) const;
#endif

  /** Diagnostic copies populated only by the TEST_BUILD snapshot read. */
  unsigned long cursorNativeApplies;
  unsigned long cursorOuterEntries;
  unsigned long cursorOuterExits;
  short cursorDepth;
  int cursorLastApplied;

  unsigned long changeSequence;
  unsigned long totalChanges;
  loka::app::scene::NodeDirtyFlags lastFlags;
  bool lastFullRebuild;
  bool lastRootPresent;
  int fullInvalidateRequests;
  int rectInvalidateRequests;
  int drawCalls;
  int drawDirtyCalls;
  int renderCalls;
  int renderDirtyCalls;
  int buttonHitCount;
  int cellHitCount;
  int editHitCount;
  int textHitCount;
  int popupHitCount;
  int controlDrawCount;
  int relayoutTextCount;
  int textChangedCellCount;
  int textChangedTextCount;
  int textChangedEditHitCount;
  int textChangedEditControlCount;
  int textChangedPendingCount;
  int textChangedImmediateInvalidateCount;
  int batchOnChangeCount;
  int batchNullRootCount;
  int batchFullRebuildCount;
  int batchNonNoneFlagsCount;
  int batchAccumOnChangeCount;
  int batchAccumNullRootCount;
  bool batchAccumFullRebuild;
  loka::app::scene::NodeDirtyFlags batchAccumFlags;
  std::string batchAccumTrace;
  std::string relayoutTextPreview;
  bool fallbackRootIsBoundary;
  bool fallbackRootHasLayoutBounds;
  bool fallbackQueuedByChild;
  bool fallbackUsedFullInvalidate;
  int windowFullRequestCount;
  int windowRectRequestCount;
  int windowFlushFullCount;
  int windowFlushDirtyCount;
  int windowUpdateEvtDrawCount;
  const char *windowFullRequestSource;
  int requestInvalidateCallCount;
  bool requestInvalidateFirstRootPresent;
  bool requestInvalidateFirstFullRebuild;
  loka::app::scene::NodeDirtyFlags requestInvalidateFirstFlags;
  bool requestInvalidateRootPresent;
  bool requestInvalidateFullRebuild;
  loka::app::scene::NodeDirtyFlags requestInvalidateFlags;
  int totalFullInvalidateRequests;
  int totalRectInvalidateRequests;
  int totalDrawCalls;
  int totalDrawDirtyCalls;
  int totalRenderCalls;
  int totalRenderDirtyCalls;
  int totalControlDrawCount;
  // Cumulative only: boundary applies precede onChange (which calls begin) in a
  // cycle that also has global work, so a per-interval copy would be reset
  // right after it was incremented. Read these as deltas across a cycle.
  int totalCollectorVisitCount;
  int totalBoundaryApplyCount;
  void noteCollectorVisit();
  unsigned long buttonPoolHitCount;
  unsigned long buttonPoolMissCount;
  unsigned long buttonPoolEvictCount;
  int buttonPoolDepth;
  unsigned long editPoolHitCount;
  unsigned long editPoolMissCount;
  unsigned long editPoolEvictCount;
  int editPoolDepth;
  int scrollBarPoolDepth;
  int poolIntakeAuditFailCount;
};

#endif // LOKA_TOOLBOX_SCENE_DEBUG_STATS_HPP
