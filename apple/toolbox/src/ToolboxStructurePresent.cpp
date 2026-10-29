// Included by the controller and the Toolbox host fixture.
void ToolboxScenePlatformController::requestStructurePresent()
{
  for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
    if (this->scrollBarLedger_.viewportScrollBars_[i].spans.isValid())
      this->scrollBarLedger_.viewportScrollBars_[i].spans->invalidate();
  if (window_)
  {
    window_->requestInvalidateWithReason(TOOLBOX_FULL_DRAW_STRUCTURE_SWAP);
  }
}

void ToolboxScenePlatformController::releaseNodeContexts(loka::app::scene::Node *node)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  if (!node)
  {
    return;
  }
  for (unsigned i = 0; loka::app::scene::Node *branch = node->retainedLifecycleBranch(i); ++i)
  {
    this->releaseNodeContexts(branch);
  }
  loka::app::scene::INestable *nestable = node->asNestable();
  if (nestable)
  {
    for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
    {
      this->releaseNodeContexts(child);
    }
  }

  if (loka::app::ScrollViewNode *scrollView = node->asScrollViewNode())
  {
    // The viewport ledger is not a NodeContext: remove its native event door
    // on the same synchronous detach path before the node can be reclaimed.
    this->destroyViewportScrollBarControl(
        scrollView, node->nativeLifetimeHint());
  }

  if (node->getContext())
  {
    this->requestStructurePresent();
  }
  node->setContext(0);
}

void ToolboxScenePlatformController::requestRelayout()
{
  this->refuseScrollViewShortRange();
  this->requestSceneRelayout(this->rootNode_);
}
