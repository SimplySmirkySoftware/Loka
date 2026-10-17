// Included by the controller and the Toolbox host fixture.
void ToolboxScenePlatformController::onBoundaryApply(loka::app::scene::Node *rootNode,
                                                     loka::app::scene::BoundaryNode *boundary,
                                                     const loka::app::scene::BoundaryLocalApplyInfo &info,
                                                     const loka::app::scene::PlatformApplyPlan &plan)
{
  ++debugStats_.totalBoundaryApplyCount;
  if (rootNode)
  {
    rootNode_ = rootNode;
  }
  if (!window_ || !window_->window() || !rootNode_ || !boundary || !plan.hasBoundaryApplyWork(boundary))
  {
    return;
  }
  if (!info.hasAnyWork())
  {
    return;
  }

  using namespace loka::app::scene;
  if (info.hasStructureWork || plan.hasStructureWork())
    this->requestStructurePresent();
  if (info.hasLayoutWork || plan.hasLayoutWork())
  {
    // Rectangle replay uses captured placement. Layout work must reach
    // render() before painting values that depend on the new geometry.
    this->window_->requestInvalidateWithReason("layout_dirty");
    return;
  }
  // Structure and composited work cannot use exact delivery. Keep their
  // existing fallback walk instead of collecting answers before it.
  if (info.hasPaintWork() && !info.hasStructureWork && !info.hasLayoutWork
      && !info.hasCompositedPaintWork() && !plan.hasStructureWork() && !plan.hasLayoutWork())
  {
    const PaintQuery query = {ToolboxPaintScope(),
                             PLACEMENT_ELIGIBLE};
    PaintAnswerBuffer<> answers;
    ToolboxPaintAnswerSource source(this->debugStats_);
    const PaintApplyVerdict verdict = CollectPaintAnswers(*boundary, query, answers, source);
    if (verdict.canSkipBroadPaint(info))
    {
      for (unsigned i = 0; i < answers.count(); ++i)
      {
        const PaintDamage &damage = answers.entry(i).damage;
        const Rect rect = {static_cast<short>(damage.y), static_cast<short>(damage.x),
                           static_cast<short>(damage.y + damage.height), static_cast<short>(damage.x + damage.width)};
        this->window_->requestInvalidateRect(rect);
      }
      return;
    }
    // A refusal already completed the one resident visit. Widen directly;
    // re-running the legacy surface collector would be a second traversal.
    this->window_->requestInvalidate();
    return;
  }

  if (!this->scrollBarLedger_.viewportScrollBars_.empty())
  {
    // Layout/composited viewport work needs the same broad presentation.
    window_->requestInvalidateWithReason(kViewportPaintWidenReason);
    return;
  }

  if (!info.hasBoundsHint())
  {
    Rect surfaceDirtyRect;
    if (info.hasPaintWork() && ContainsOnlyRectSurfacePainting(boundary, debugStats_)
        && CollectRectSurfaceDirtyRect(boundary, surfaceDirtyRect, debugStats_))
    {
      window_->requestInvalidateRect(surfaceDirtyRect);
      return;
    }
    loka::app::scene::Node *firstChild = 0;
    if (loka::app::scene::INestable *nestable = boundary->asNestable())
    {
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
      firstChild = it.next();
    }
    if (boundary->kind() == loka::app::scene::NODE_KIND_UNKNOWN && boundary->testId().empty() && firstChild
        && firstChild->kind() == loka::app::scene::NODE_KIND_ZSTACK)
    {
      return;
    }
    if (boundary->hasLayoutBounds())
    {
      window_->requestInvalidateRect(BoundaryToRect(boundary, window_->window()->portRect));
    }
    else
    {
      window_->requestInvalidate();
    }
    return;
  }

  Rect rect;
  rect.left = static_cast<short>(info.bounds->x);
  rect.top = static_cast<short>(info.bounds->y);
  rect.right = static_cast<short>(info.bounds->x + info.bounds->width);
  rect.bottom = static_cast<short>(info.bounds->y + info.bounds->height);
  Rect surfaceDirtyRect;
  if (CollectRectSurfaceDirtyRect(boundary, surfaceDirtyRect, debugStats_))
  {
    if (surfaceDirtyRect.left < rect.left)
    {
      rect.left = surfaceDirtyRect.left;
    }
    if (surfaceDirtyRect.top < rect.top)
    {
      rect.top = surfaceDirtyRect.top;
    }
    if (surfaceDirtyRect.right > rect.right)
    {
      rect.right = surfaceDirtyRect.right;
    }
    if (surfaceDirtyRect.bottom > rect.bottom)
    {
      rect.bottom = surfaceDirtyRect.bottom;
    }
  }
  window_->requestInvalidateRect(rect);
}
