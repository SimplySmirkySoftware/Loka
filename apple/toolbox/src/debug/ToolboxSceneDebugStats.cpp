#include "debug/ToolboxSceneDebugStats.hpp"
#include "core/LokaAlloc.hpp"
#include <cstdio>
#include <cstring>
#if LOKA_RETRO68_DIAGNOSTICS
#include <ctime>
#include "ToolboxSmallObjectPool.hpp"
#endif

namespace
{
  void AppendInt(std::string &out, int value)
  {
    char buffer[32];
    std::sprintf(buffer, "%d", value);
    out += buffer;
  }

  void AppendDirtyFlagToken(std::string &out,
                            loka::app::scene::NodeDirtyFlags flags,
                            loka::app::scene::NodeDirtyFlags mask,
                            char token)
  {
    if ((flags & mask) == 0)
    {
      return;
    }
    if (!out.empty())
    {
      out.push_back('+');
    }
    out.push_back(token);
  }

  std::string DirtyFlagsToStringImpl(loka::app::scene::NodeDirtyFlags flags)
  {
    if (flags == loka::app::scene::NODE_DIRTY_NONE)
    {
      return std::string("0");
    }
    std::string out;
    AppendDirtyFlagToken(out, flags, loka::app::scene::NODE_DIRTY_INITIAL, 'I');
    AppendDirtyFlagToken(out, flags, loka::app::scene::NODE_DIRTY_PROPS, 'P');
    AppendDirtyFlagToken(out, flags, loka::app::scene::NODE_DIRTY_LAYOUT, 'L');
    AppendDirtyFlagToken(out, flags, loka::app::scene::NODE_DIRTY_CHILD, 'C');
    return out;
  }

#if LOKA_RETRO68_DIAGNOSTICS
  void BuildRedrawDumpFileName(char out[13], const std::tm &local)
  {
    const unsigned int month = (static_cast<unsigned int>(local.tm_mon) + 1U) % 100U;
    const unsigned int day = static_cast<unsigned int>(local.tm_mday) % 100U;
    const unsigned int hour = static_cast<unsigned int>(local.tm_hour) % 100U;
    const unsigned int second = static_cast<unsigned int>(local.tm_sec) % 100U;
    std::sprintf(out, "%02u%02u%02u%02u.TXT", month, day, hour, second);
  }
#endif
} // namespace

ToolboxSceneDebugStats::ToolboxSceneDebugStats()
    : lastPresentEntryEligibility(TOOLBOX_PAINT_REFUSED),
      lastMovedEntryEligibility(TOOLBOX_PAINT_REFUSED),
      cursorNativeApplies(0),
      cursorOuterEntries(0),
      cursorOuterExits(0),
      cursorDepth(0),
      cursorLastApplied(0),
      changeSequence(0),
      totalChanges(0),
      lastFlags(loka::app::scene::NODE_DIRTY_NONE),
      lastFullRebuild(false),
      lastRootPresent(false),
      fullInvalidateRequests(0),
      rectInvalidateRequests(0),
      drawCalls(0),
      drawDirtyCalls(0),
      renderCalls(0),
      renderDirtyCalls(0),
      buttonHitCount(0),
      cellHitCount(0),
      editHitCount(0),
      textHitCount(0),
      popupHitCount(0),
      controlDrawCount(0),
      relayoutTextCount(0),
      textChangedCellCount(0),
      textChangedTextCount(0),
      textChangedEditHitCount(0),
      textChangedEditControlCount(0),
      textChangedPendingCount(0),
      textChangedImmediateInvalidateCount(0),
      batchOnChangeCount(0),
      batchNullRootCount(0),
      batchFullRebuildCount(0),
      batchNonNoneFlagsCount(0),
      batchAccumOnChangeCount(0),
      batchAccumNullRootCount(0),
      batchAccumFullRebuild(false),
      batchAccumFlags(loka::app::scene::NODE_DIRTY_NONE),
      batchAccumTrace(),
      relayoutTextPreview(),
      fallbackRootIsBoundary(false),
      fallbackRootHasLayoutBounds(false),
      fallbackQueuedByChild(false),
      fallbackUsedFullInvalidate(false),
      windowFullRequestCount(0),
      windowRectRequestCount(0),
      windowFlushFullCount(0),
      windowFlushDirtyCount(0),
      windowUpdateEvtDrawCount(0),
      windowFullRequestSource(0),
      requestInvalidateCallCount(0),
      requestInvalidateFirstRootPresent(false),
      requestInvalidateFirstFullRebuild(false),
      requestInvalidateFirstFlags(loka::app::scene::NODE_DIRTY_NONE),
      requestInvalidateRootPresent(false),
      requestInvalidateFullRebuild(false),
      requestInvalidateFlags(loka::app::scene::NODE_DIRTY_NONE),
      totalFullInvalidateRequests(0),
      totalRectInvalidateRequests(0),
      totalDrawCalls(0),
      totalDrawDirtyCalls(0),
      totalRenderCalls(0),
      totalRenderDirtyCalls(0),
      totalControlDrawCount(0),
      totalCollectorVisitCount(0),
      totalBoundaryApplyCount(0),
      buttonPoolHitCount(0),
      buttonPoolMissCount(0),
      buttonPoolEvictCount(0),
      buttonPoolDepth(0),
      editPoolHitCount(0),
      editPoolMissCount(0),
      editPoolEvictCount(0),
      editPoolDepth(0),
      scrollBarPoolDepth(0),
      poolIntakeAuditFailCount(0)
{
}

std::string ToolboxSceneDebugStats::flagsToString(loka::app::scene::NodeDirtyFlags flags)
{
  return DirtyFlagsToStringImpl(flags);
}

void ToolboxSceneDebugStats::noteCollectorVisit()
{
  ++this->totalCollectorVisitCount;
}

void ToolboxSceneDebugStats::begin(loka::app::scene::NodeDirtyFlags flags, bool fullRebuild)
{
  this->changeSequence += 1;
  this->totalChanges += 1;
  this->lastFlags = flags;
  this->lastFullRebuild = fullRebuild;
  this->fullInvalidateRequests = 0;
  this->rectInvalidateRequests = 0;
  this->drawCalls = 0;
  this->drawDirtyCalls = 0;
  this->renderCalls = 0;
  this->renderDirtyCalls = 0;
  this->buttonHitCount = 0;
  this->cellHitCount = 0;
  this->editHitCount = 0;
  this->textHitCount = 0;
  this->popupHitCount = 0;
  this->controlDrawCount = 0;
  this->relayoutTextCount = 0;
  this->textChangedCellCount = 0;
  this->textChangedTextCount = 0;
  this->textChangedEditHitCount = 0;
  this->textChangedEditControlCount = 0;
  this->textChangedPendingCount = 0;
  this->textChangedImmediateInvalidateCount = 0;
  this->batchOnChangeCount = 0;
  this->batchNullRootCount = 0;
  this->batchFullRebuildCount = 0;
  this->batchNonNoneFlagsCount = 0;
  this->relayoutTextPreview.clear();
  this->fallbackRootIsBoundary = false;
  this->fallbackRootHasLayoutBounds = false;
  this->fallbackQueuedByChild = false;
  this->fallbackUsedFullInvalidate = false;
  this->windowFullRequestCount = 0;
  this->windowRectRequestCount = 0;
  this->windowFlushFullCount = 0;
  this->windowFlushDirtyCount = 0;
  this->windowUpdateEvtDrawCount = 0;
  this->windowFullRequestSource = 0;
  this->requestInvalidateCallCount = 0;
  this->requestInvalidateFirstRootPresent = false;
  this->requestInvalidateFirstFullRebuild = false;
  this->requestInvalidateFirstFlags = loka::app::scene::NODE_DIRTY_NONE;
  this->requestInvalidateRootPresent = false;
  this->requestInvalidateFullRebuild = false;
  this->requestInvalidateFlags = loka::app::scene::NODE_DIRTY_NONE;
}

void ToolboxSceneDebugStats::refreshHitCounts(int buttonHits, int cellHits, int editHits, int textHits, int popupHits)
{
  this->buttonHitCount = buttonHits;
  this->cellHitCount = cellHits;
  this->editHitCount = editHits;
  this->textHitCount = textHits;
  this->popupHitCount = popupHits;
}

void ToolboxSceneDebugStats::refreshNativePoolCounters(unsigned long buttonHits,
                                                       unsigned long buttonMisses,
                                                       unsigned long buttonEvicts,
                                                       int buttonDepth,
                                                       unsigned long editHits,
                                                       unsigned long editMisses,
                                                       unsigned long editEvicts,
                                                       int editDepth,
                                                       int scrollBarDepth,
                                                       int intakeAuditFails)
{
  this->buttonPoolHitCount = buttonHits;
  this->buttonPoolMissCount = buttonMisses;
  this->buttonPoolEvictCount = buttonEvicts;
  this->buttonPoolDepth = buttonDepth;
  this->editPoolHitCount = editHits;
  this->editPoolMissCount = editMisses;
  this->editPoolEvictCount = editEvicts;
  this->editPoolDepth = editDepth;
  this->scrollBarPoolDepth = scrollBarDepth;
  this->poolIntakeAuditFailCount = intakeAuditFails;
}

std::string ToolboxSceneDebugStats::summary() const
{
  std::string out(" seq:");
  AppendInt(out, static_cast<int>(this->changeSequence));
  out += " f:";
  out += flagsToString(this->lastFlags);
  out += this->lastFullRebuild ? "/full1" : "/full0";
  out += " req:";
  AppendInt(out, this->fullInvalidateRequests);
  out += "/";
  AppendInt(out, this->rectInvalidateRequests);
  out += " draw:";
  AppendInt(out, this->drawCalls);
  out += "/";
  AppendInt(out, this->drawDirtyCalls);
  out += " render:";
  AppendInt(out, this->renderCalls);
  out += "/";
  AppendInt(out, this->renderDirtyCalls);
  out += " hits:";
  AppendInt(out, this->buttonHitCount);
  out += ",";
  AppendInt(out, this->textHitCount);
  out += ",";
  AppendInt(out, this->popupHitCount);
  out += ",";
  AppendInt(out, this->editHitCount);
  out += " ctl:";
  AppendInt(out, this->controlDrawCount);
  out += " collector.total:";
  AppendInt(out, this->totalCollectorVisitCount);
  out += " apply.total:";
  AppendInt(out, this->totalBoundaryApplyCount);
  out += " pool.btn:";
  AppendInt(out, static_cast<int>(this->buttonPoolHitCount));
  out += "/";
  AppendInt(out, static_cast<int>(this->buttonPoolMissCount));
  out += "/";
  AppendInt(out, static_cast<int>(this->buttonPoolEvictCount));
  out += "@";
  AppendInt(out, this->buttonPoolDepth);
  out += " pool.te:";
  AppendInt(out, static_cast<int>(this->editPoolHitCount));
  out += "/";
  AppendInt(out, static_cast<int>(this->editPoolMissCount));
  out += "/";
  AppendInt(out, static_cast<int>(this->editPoolEvictCount));
  out += "@";
  AppendInt(out, this->editPoolDepth);
  out += " pool.scroll.depth:";
  AppendInt(out, this->scrollBarPoolDepth);
  if (this->poolIntakeAuditFailCount > 0)
  {
    out += " pool.audit_fail:";
    AppendInt(out, this->poolIntakeAuditFailCount);
  }
  return out;
}

void ToolboxSceneDebugStats::reset()
{
  *this = ToolboxSceneDebugStats();
}

#if LOKA_RETRO68_DIAGNOSTICS
bool ToolboxSceneDebugStats::dumpToTimestampedFile(const char *overrideName) const
{
  std::time_t now = std::time(0);
  std::tm *local = std::localtime(&now);
  if (!local)
  {
    return false;
  }

  char path[13];
  if (overrideName)
  {
    std::strncpy(path, overrideName, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
  }
  else
  {
    BuildRedrawDumpFileName(path, *local);
  }

  FILE *fp = std::fopen(path, "wb");
  if (!fp)
  {
    return false;
  }

  std::fprintf(fp,
               "generated=%04d-%02d-%02d %02d:%02d:%02d\n",
               local->tm_year + 1900,
               local->tm_mon + 1,
               local->tm_mday,
               local->tm_hour,
               local->tm_min,
               local->tm_sec);
  std::fprintf(fp, "last.seq=%lu\n", this->changeSequence);
  std::fprintf(fp, "last.flags=%s\n", flagsToString(this->lastFlags).c_str());
  std::fprintf(fp, "last.full_rebuild=%d\n", this->lastFullRebuild ? 1 : 0);
  std::fprintf(fp, "last.root_present=%d\n", this->lastRootPresent ? 1 : 0);
  std::fprintf(fp, "last.invalidate.full=%d\n", this->fullInvalidateRequests);
  std::fprintf(fp, "last.invalidate.rect=%d\n", this->rectInvalidateRequests);
  std::fprintf(fp, "last.draw=%d\n", this->drawCalls);
  std::fprintf(fp, "last.draw_dirty=%d\n", this->drawDirtyCalls);
  std::fprintf(fp, "last.render=%d\n", this->renderCalls);
  std::fprintf(fp, "last.render_dirty=%d\n", this->renderDirtyCalls);
  std::fprintf(fp, "last.hits.button=%d\n", this->buttonHitCount);
  std::fprintf(fp, "last.hits.cell=%d\n", this->cellHitCount);
  std::fprintf(fp, "last.hits.edit=%d\n", this->editHitCount);
  std::fprintf(fp, "last.hits.text=%d\n", this->textHitCount);
  std::fprintf(fp, "last.hits.popup=%d\n", this->popupHitCount);
  std::fprintf(fp, "last.control_draws=%d\n", this->controlDrawCount);
  std::fprintf(fp, "last.relayout_texts=%d\n", this->relayoutTextCount);
  std::fprintf(fp, "last.relayout_preview=%s\n", this->relayoutTextPreview.c_str());
  std::fprintf(fp, "last.text_changed.cell=%d\n", this->textChangedCellCount);
  std::fprintf(fp, "last.text_changed.text=%d\n", this->textChangedTextCount);
  std::fprintf(fp, "last.text_changed.edit_hit=%d\n", this->textChangedEditHitCount);
  std::fprintf(fp, "last.text_changed.edit_control=%d\n", this->textChangedEditControlCount);
  std::fprintf(fp, "last.text_changed.pending=%d\n", this->textChangedPendingCount);
  std::fprintf(fp, "last.text_changed.immediate=%d\n", this->textChangedImmediateInvalidateCount);
  std::fprintf(fp, "last.batch.on_change=%d\n", this->batchOnChangeCount);
  std::fprintf(fp, "last.batch.null_root=%d\n", this->batchNullRootCount);
  std::fprintf(fp, "last.batch.full_rebuild=%d\n", this->batchFullRebuildCount);
  std::fprintf(fp, "last.batch.non_none_flags=%d\n", this->batchNonNoneFlagsCount);
  std::fprintf(fp, "last.batch.accum.on_change=%d\n", this->batchAccumOnChangeCount);
  std::fprintf(fp, "last.batch.accum.null_root=%d\n", this->batchAccumNullRootCount);
  std::fprintf(fp, "last.batch.accum.full_rebuild=%d\n", this->batchAccumFullRebuild ? 1 : 0);
  std::fprintf(fp, "last.batch.accum.flags=%s\n", flagsToString(this->batchAccumFlags).c_str());
  std::fprintf(fp, "last.batch.accum.trace=%s\n", this->batchAccumTrace.c_str());
  std::fprintf(fp, "last.fallback.root_is_boundary=%d\n", this->fallbackRootIsBoundary ? 1 : 0);
  std::fprintf(fp, "last.fallback.root_has_layout_bounds=%d\n", this->fallbackRootHasLayoutBounds ? 1 : 0);
  std::fprintf(fp, "last.fallback.queued_by_child=%d\n", this->fallbackQueuedByChild ? 1 : 0);
  std::fprintf(fp, "last.fallback.used_full_invalidate=%d\n", this->fallbackUsedFullInvalidate ? 1 : 0);
  std::fprintf(fp, "last.window.request_full=%d\n", this->windowFullRequestCount);
  std::fprintf(
      fp, "last.window.request_full_source=%s\n", this->windowFullRequestSource ? this->windowFullRequestSource : "");
  std::fprintf(fp, "last.request_invalidate.calls=%d\n", this->requestInvalidateCallCount);
  std::fprintf(fp, "last.request_invalidate.first.root_present=%d\n", this->requestInvalidateFirstRootPresent ? 1 : 0);
  std::fprintf(fp, "last.request_invalidate.first.full_rebuild=%d\n", this->requestInvalidateFirstFullRebuild ? 1 : 0);
  std::fprintf(
      fp, "last.request_invalidate.first.flags=%s\n", flagsToString(this->requestInvalidateFirstFlags).c_str());
  std::fprintf(fp, "last.request_invalidate.root_present=%d\n", this->requestInvalidateRootPresent ? 1 : 0);
  std::fprintf(fp, "last.request_invalidate.full_rebuild=%d\n", this->requestInvalidateFullRebuild ? 1 : 0);
  std::fprintf(fp, "last.request_invalidate.flags=%s\n", flagsToString(this->requestInvalidateFlags).c_str());
  std::fprintf(fp, "last.window.request_rect=%d\n", this->windowRectRequestCount);
  std::fprintf(fp, "last.window.flush_full=%d\n", this->windowFlushFullCount);
  std::fprintf(fp, "last.window.flush_dirty=%d\n", this->windowFlushDirtyCount);
  std::fprintf(fp, "last.window.updateevt_draw=%d\n", this->windowUpdateEvtDrawCount);
  std::fprintf(fp, "total.changes=%lu\n", this->totalChanges);
  std::fprintf(fp, "total.invalidate.full=%d\n", this->totalFullInvalidateRequests);
  std::fprintf(fp, "total.invalidate.rect=%d\n", this->totalRectInvalidateRequests);
  std::fprintf(fp, "total.draw=%d\n", this->totalDrawCalls);
  std::fprintf(fp, "total.draw_dirty=%d\n", this->totalDrawDirtyCalls);
  std::fprintf(fp, "total.render=%d\n", this->totalRenderCalls);
  std::fprintf(fp, "total.render_dirty=%d\n", this->totalRenderDirtyCalls);
  std::fprintf(fp, "total.control_draws=%d\n", this->totalControlDrawCount);
  std::fprintf(fp, "total.collector_visits=%d\n", this->totalCollectorVisitCount);
  std::fprintf(fp, "total.boundary_applies=%d\n", this->totalBoundaryApplyCount);
  std::fprintf(fp, "pool.button.hits=%lu\n", this->buttonPoolHitCount);
  std::fprintf(fp, "pool.button.misses=%lu\n", this->buttonPoolMissCount);
  std::fprintf(fp, "pool.button.evicts=%lu\n", this->buttonPoolEvictCount);
  std::fprintf(fp, "pool.button.depth=%d\n", this->buttonPoolDepth);
  std::fprintf(fp, "pool.edit.hits=%lu\n", this->editPoolHitCount);
  std::fprintf(fp, "pool.edit.misses=%lu\n", this->editPoolMissCount);
  std::fprintf(fp, "pool.edit.evicts=%lu\n", this->editPoolEvictCount);
  std::fprintf(fp, "pool.edit.depth=%d\n", this->editPoolDepth);
  std::fprintf(fp, "pool.scroll.depth=%d\n", this->scrollBarPoolDepth);
  std::fprintf(fp, "pool.intake_audit_fails=%d\n", this->poolIntakeAuditFailCount);
  loka::core::LokaAllocCensusDump(fp);
  // Process-cumulative pool facts, independent of resettable scene total.*.
  // Collection uses only this stack POD; the existing file writer owns I/O.
  loka::core::SmallObjectPoolReport pool;
  LokaClassicPoolReport(pool);
  std::fprintf(fp, "smallpool.rows=%lu\n", pool.rows);
  // Labels follow SmallObjectPool::classSize in core/SmallObjectPool.hpp.
  static const unsigned short sizes[10] = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256};
  for (unsigned int i = 0; i < 10; ++i)
  {
    std::fprintf(fp, "smallpool.chunks.%u=%lu\n", static_cast<unsigned int>(sizes[i]), pool.chunks[i]);
    std::fprintf(fp, "smallpool.free.%u=%lu\n", static_cast<unsigned int>(sizes[i]), pool.freeSlots[i]);
  }
  std::fprintf(fp, "smallpool.live=%lu\n", pool.live);
  std::fprintf(fp, "smallpool.unused_bytes=%lu\n", pool.unusedBytes);
  std::fprintf(fp, "smallpool.refills=%lu\n", pool.refills);
  std::fprintf(fp, "smallpool.larges=%lu\n", pool.larges);
  std::fprintf(fp, "smallpool.directs=%lu\n", pool.directs);
  std::fprintf(fp, "smallpool.fallbacks=%lu\n", pool.fallbacks);
  std::fprintf(fp, "smallpool.refused=%lu\n", pool.refused);
  std::fprintf(fp, "smallpool.poison_violations=%lu\n", pool.poisonViolations);
  std::fprintf(fp, "smallpool.valid=%d\n", pool.valid ? 1 : 0);
  std::fclose(fp);
  return true;
}
#endif // LOKA_RETRO68_DIAGNOSTICS
