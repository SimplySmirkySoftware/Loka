// Included by the production controller and the Toolbox host fixture.

namespace
{
  bool UseBoundaryDirty(const loka::app::scene::BoundaryNode *boundary)
  {
    return boundary && boundary->parentBoundary() && boundary->hasLayoutBounds();
  }

  Rect BoundaryToRect(const loka::app::scene::BoundaryNode *boundary, const Rect &fallback)
  {
    if (!UseBoundaryDirty(boundary))
    {
      return fallback;
    }
    const loka::app::scene::BoundaryNode::LayoutBounds &bounds = boundary->layoutBounds();
    Rect rect;
    rect.left = static_cast<short>(bounds.x);
    rect.top = static_cast<short>(bounds.y);
    rect.right = static_cast<short>(bounds.x + bounds.width);
    rect.bottom = static_cast<short>(bounds.y + bounds.height);
    return rect;
  }
} // namespace

void ToolboxScenePlatformController::beginBatchUpdate()
{
  inBatchUpdate_ = true;
  pendingDirtyRects_.clear();
  pendingTextStates_.clear();
  pendingFullInvalidate_ = false;
  pendingInvalidateFlags_ = loka::app::scene::NODE_DIRTY_NONE;
  pendingRootNode_ = 0;
  debugStats_.batchAccumOnChangeCount = 0;
  debugStats_.batchAccumNullRootCount = 0;
  debugStats_.batchAccumFullRebuild = false;
  debugStats_.batchAccumFlags = loka::app::scene::NODE_DIRTY_NONE;
}

void ToolboxScenePlatformController::endBatchUpdate()
{
  inBatchUpdate_ = false;
  if (window_)
  {
    if (pendingRootNode_)
    {
      rootNode_ = pendingRootNode_;
    }
    const bool handledLocalDirty = !pendingDirtyRects_.empty();
    const bool handledLocalText = !pendingTextStates_.empty();
    const bool hasChildDirty = (pendingInvalidateFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0;
    const bool skipFollowupInvalidate =
        !pendingFullInvalidate_ && !hasChildDirty && (handledLocalDirty || handledLocalText);
    // Record pending dirty rects; the app presents them after dispatch.
    for (size_t i = 0; i < pendingDirtyRects_.size(); ++i)
    {
      window_->requestInvalidateRect(pendingDirtyRects_[i]);
    }
    for (size_t i = 0; i < pendingTextStates_.size(); ++i)
    {
      requestInvalidateForText(pendingTextStates_[i]);
    }
    if (!skipFollowupInvalidate)
    {
      requestInvalidateForChange(
          pendingRootNode_ ? pendingRootNode_ : rootNode_, pendingInvalidateFlags_, pendingFullInvalidate_);
    }
  }
  pendingDirtyRects_.clear();
  pendingTextStates_.clear();
  pendingFullInvalidate_ = false;
  pendingInvalidateFlags_ = loka::app::scene::NODE_DIRTY_NONE;
  pendingRootNode_ = 0;
}

void ToolboxScenePlatformController::requestInvalidateForChange(loka::app::scene::Node *rootNodeForChange,
                                                                loka::app::scene::NodeDirtyFlags flags,
                                                                bool fullRebuild)
{
  if (debugStats_.requestInvalidateCallCount == 0)
  {
    debugStats_.requestInvalidateFirstRootPresent = (rootNodeForChange != 0);
    debugStats_.requestInvalidateFirstFullRebuild = fullRebuild;
    debugStats_.requestInvalidateFirstFlags = flags;
  }
  ++debugStats_.requestInvalidateCallCount;
  debugStats_.requestInvalidateRootPresent = (rootNodeForChange != 0);
  debugStats_.requestInvalidateFullRebuild = fullRebuild;
  debugStats_.requestInvalidateFlags = flags;
  if (!window_ || !window_->window())
  {
    return;
  }
  if ((flags == loka::app::scene::NODE_DIRTY_NONE) && !fullRebuild)
  {
    return;
  }
  if (flags & loka::app::scene::NODE_DIRTY_CHILD)
  {
    // Classic redraw currently prefers broad invalidation for child changes.
    ++debugStats_.fullInvalidateRequests;
    ++debugStats_.totalFullInvalidateRequests;
    window_->requestInvalidateWithReason(TOOLBOX_FULL_DRAW_CHILD_DIRTY);
    return;
  }
  if (fullRebuild || !rootNodeForChange)
  {
    ++debugStats_.fullInvalidateRequests;
    ++debugStats_.totalFullInvalidateRequests;
    window_->requestInvalidateWithReason(TOOLBOX_FULL_DRAW_FULL_REBUILD_OR_NO_ROOT);
    return;
  }

  Rect fallback = window_->window()->portRect;
  debugStats_.fallbackQueuedByChild = false;
  loka::app::scene::BoundaryNode *boundary = rootNodeForChange->asBoundary();
  debugStats_.fallbackRootIsBoundary = (boundary != 0);
  debugStats_.fallbackRootHasLayoutBounds = boundary && boundary->hasLayoutBounds();
  if (boundary && boundary->hasLayoutBounds())
  {
    ++debugStats_.rectInvalidateRequests;
    ++debugStats_.totalRectInvalidateRequests;
    window_->requestInvalidateRect(BoundaryToRect(boundary, fallback));
  }
  else
  {
    debugStats_.fallbackUsedFullInvalidate = true;
    ++debugStats_.rectInvalidateRequests;
    ++debugStats_.totalRectInvalidateRequests;
    window_->requestInvalidateRect(fallback);
  }
}

void ToolboxScenePlatformController::requestInvalidateForText(loka::core::State<loka::core::String> *text)
{
  if (!window_ || !text)
  {
    return;
  }
  for (size_t i = 0; i < hitLedger_.textHits_.size(); ++i)
  {
    if (hitLedger_.textHits_[i].text == text)
    {
      window_->requestInvalidateRect(hitLedger_.textHits_[i].rect);
      return;
    }
  }
}

void ToolboxScenePlatformController::onChange(loka::app::scene::Node *rootNode,
                                              loka::app::scene::NodeDirtyFlags flags,
                                              bool fullRebuild)
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->borrowPhase().open());
#endif
  rootNode_ = rootNode;
  debugStats_.begin(flags, fullRebuild);
  debugStats_.lastRootPresent = (rootNode != 0);
  if (!window_ || !window_->window())
  {
    return;
  }
  if (inBatchUpdate_)
  {
    ++debugStats_.batchOnChangeCount;
    ++debugStats_.batchAccumOnChangeCount;
    if (!debugStats_.batchAccumTrace.empty())
    {
      debugStats_.batchAccumTrace += " ";
    }
    debugStats_.batchAccumTrace += ToolboxSceneDebugStats::flagsToString(flags);
    debugStats_.batchAccumTrace += "/";
    debugStats_.batchAccumTrace += fullRebuild ? "1" : "0";
    debugStats_.batchAccumTrace += "/";
    debugStats_.batchAccumTrace += rootNode ? "1" : "0";
    if (!rootNode)
    {
      ++debugStats_.batchNullRootCount;
      ++debugStats_.batchAccumNullRootCount;
    }
    if (fullRebuild)
    {
      ++debugStats_.batchFullRebuildCount;
      debugStats_.batchAccumFullRebuild = true;
    }
    if (flags != loka::app::scene::NODE_DIRTY_NONE)
    {
      ++debugStats_.batchNonNoneFlagsCount;
    }
    debugStats_.batchAccumFlags = static_cast<loka::app::scene::NodeDirtyFlags>(debugStats_.batchAccumFlags | flags);
    pendingInvalidateFlags_ = static_cast<loka::app::scene::NodeDirtyFlags>(pendingInvalidateFlags_ | flags);
    if (rootNode)
    {
      pendingRootNode_ = rootNode;
    }
    if (fullRebuild)
    {
      pendingFullInvalidate_ = true;
    }
    return;
  }
  requestInvalidateForChange(rootNode, flags, fullRebuild);
}
