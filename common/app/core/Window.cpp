#include "app/core/Window.hpp"
#include "app/FocusPublisher.hpp"
#include "app/text/TextDocumentPublisher.hpp"
#include "app/scene/Scene.hpp"

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

void Window::reconcileTextDocuments()
{
  loka::app::scene::Scene *current = this->scene();
  if (this->sceneManager_.applying_ || !current || current->isBorrowOpen() || current->isRunInProgress()
      || current->focus().isPublishing() || current->textDocuments().isPublishing() || !current->attached_.get()
      || !current->rootNode_ || !this->hasLiveScenePlatform() || !current->platformController_)
    return;
  loka::app::detail::TextDocumentPublisher::complete(current->textDocuments(), *current->platformController_);
}

loka::app::scene::NodeContext *Window::publishedFocusContext()
{
  loka::app::scene::Scene *current = this->scene();
  if (!current || !current->attached_.get() || !current->rootNode_)
    return 0;
  return loka::app::detail::FocusPublisher::publishedContext(current->focus());
}
