#include "app/core/Window.hpp"
#include "app/FocusPublisher.hpp"
#include "app/text/TextDocumentPublisher.hpp"
#include "app/scene/Scene.hpp"

#include "core/LokaAlloc.hpp"

namespace
{
  loka::core::LokaAllocationSite handoffSite()
  {
    return loka::core::LokaAllocationSite("WindowProps", "InitialSceneHandoff");
  }
} // namespace

void WindowProps::releaseInitialSceneHandoff()
{
  if (!this->initialSceneHandoff_)
  {
    return;
  }
  --this->initialSceneHandoff_->references;
  if (this->initialSceneHandoff_->references == 0)
  {
    delete this->initialSceneHandoff_->scene;
    loka::core::LokaDelete(this->initialSceneHandoff_, handoffSite());
  }
  this->initialSceneHandoff_ = 0;
}

WindowProps::RootDefinition::RootDefinition(const RootDefinition &source)
    : definition_(source.owned() ? source.definition_->clone() : 0),
      refused_(source.refused() || (source.owned() && !this->owned()))
{
}

bool WindowProps::RootDefinition::tryAssign(const RootDefinition &source)
{
  RootDefinition candidate(source);
  if (source.owned() && candidate.refused())
    return false;
  this->definition_.reset(candidate.take());
  this->refused_ = candidate.refused();
  return true;
}

WindowProps &WindowProps::operator=(const WindowProps &rhs)
{
  if (this == &rhs)
  {
    return *this;
  }
  if (!this->root_.tryAssign(rhs.root_))
  {
    this->refuseContentWrite();
    return *this;
  }
  this->titleStatePtr = rhs.titleStatePtr;
  this->displayTitleStatePtr = rhs.displayTitleStatePtr;
  this->visibilityStatePtr = rhs.visibilityStatePtr;
  this->frameStatePtr = rhs.frameStatePtr;
  this->initialTitle = rhs.initialTitle;
  this->initialVisibility = rhs.initialVisibility;
  this->hasInitialTitle = rhs.hasInitialTitle;
  this->hasInitialVisibility = rhs.hasInitialVisibility;
  this->positionX = rhs.positionX;
  this->positionY = rhs.positionY;
  this->width = rhs.width;
  this->height = rhs.height;
  this->contentMarginValue = rhs.contentMarginValue;
  this->idlePolicyValue = rhs.idlePolicyValue;
  this->onIdleFn = rhs.onIdleFn;
  this->onIdleUserData = rhs.onIdleUserData;
  this->onKeyPressFn = rhs.onKeyPressFn;
  this->onKeyPressUserData = rhs.onKeyPressUserData;
  InitialSceneHandoff *nextInitialSceneHandoff = rhs.initialSceneHandoff_;
  if (nextInitialSceneHandoff)
  {
    ++nextInitialSceneHandoff->references;
  }
  this->releaseInitialSceneHandoff();
  this->initialSceneHandoff_ = nextInitialSceneHandoff;
  return *this;
}

WindowProps &WindowProps::scene(const loka::app::scene::NodeDefinitionBase &def)
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> candidate(def.clone());
  if (!candidate.isSet())
  {
    this->refuseContentWrite();
    return *this;
  }
  this->root_.adopt(candidate.take());
  this->releaseInitialSceneHandoff();
  return *this;
}

WindowProps &WindowProps::scene(loka::app::scene::Scene *scene)
{
  InitialSceneHandoff *candidate = 0;
  if (scene)
  {
    if (scene == this->peekInitialScene())
      return *this;
    if (scene->getRootDefinition())
      candidate = loka::core::LokaNew<InitialSceneHandoff>(handoffSite(), scene);
    if (!candidate)
    {
      delete scene;
      this->refuseContentWrite();
      return *this;
    }
  }
  this->releaseInitialSceneHandoff();
  this->initialSceneHandoff_ = candidate;
  this->root_.adopt(0);
  return *this;
}

void Window::retireSceneForClose()
{
  this->sceneManager_.retireCurrentScene();
}

void Window::unmountSceneForTeardown(loka::app::scene::Scene &scene)
{
  scene.unmount();
}

loka::app::scene::Scene *Window::applySceneWork()
{
  loka::app::scene::Scene *retired = this->sceneManager_.retiredScenes_.snapshot();
  if (this->sceneManager_.applyPendingWork())
    this->synchronizeScenePlatform();
  return retired;
}

void Window::reclaimScenes(loka::app::scene::Scene *retired)
{
  this->sceneManager_.retiredScenes_.drainSnapshot(retired);
}

bool Window::hasRetiredTextStores() const
{
  const loka::app::scene::Scene *current = this->scene();
  return current && current->platformController_ && current->platformController_->hasRetiredTextStores();
}

bool Window::flushSceneInvalidation()
{
  loka::app::scene::Scene *current = this->scene();
  if (current && current->isBorrowOpen())
    return false;
  const bool changed = current ? current->flushInvalidation() : false;
  if (changed || this->hasPendingScenePlatformSync())
    this->synchronizeScenePlatform();
  this->drainNativeRetirements();
  return changed;
}

void Window::reconcileFocus()
{
  loka::app::scene::Scene *current = this->scene();
  if (this->sceneManager_.applying_ || !current || current->isBorrowOpen() || current->isRunInProgress()
      || current->focus().isPublishing() || !current->attached_.get()
      || !current->rootNode_ || !this->hasLiveScenePlatform() || !current->platformController_)
    return;
  loka::app::detail::FocusPublisher::complete(current->focus(), *current->platformController_, *current->rootNode_);
}

loka::app::scene::Scene *Window::textDocumentScene()
{
  loka::app::scene::Scene *current = this->scene();
  if (this->sceneManager_.applying_ || !current || current->isBorrowOpen() || current->isRunInProgress()
      || current->focus().isPublishing() || current->textDocuments().isPublishing() || !current->attached_.get()
      || !current->rootNode_ || !this->hasLiveScenePlatform() || !current->platformController_)
    return 0;
  return current;
}

void Window::reconcileTextDocuments()
{
  loka::app::scene::Scene *current = this->textDocumentScene();
  if (current)
    loka::app::detail::TextDocumentPublisher::complete(current->textDocuments(), *current->platformController_);
}

bool Window::commitTextDocuments()
{
  loka::app::scene::Scene *current = this->textDocumentScene();
  if (!current)
    return false;
  loka::app::detail::TextDocumentPublisher::refresh(current->textDocuments(), *current->platformController_);
  return true;
}

loka::app::scene::NodeContext *Window::publishedFocusContext()
{
  loka::app::scene::Scene *current = this->scene();
  if (!current || !current->attached_.get() || !current->rootNode_)
    return 0;
  return loka::app::detail::FocusPublisher::publishedContext(current->focus());
}
