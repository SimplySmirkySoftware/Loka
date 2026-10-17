#include "app/core/SceneManager.hpp"
#include "app/core/Window.hpp"
#include <cassert>

SceneManager::SceneManager()
    : request_(REQUEST_NONE), currentScene_(0), desired_(0), applying_(0), retiredScenes_(), window_(0)
{
#ifdef TEST_BUILD
  this->lastPrepareRefusal_ = 0;
#endif
}

SceneManager::~SceneManager()
{
  assert(!this->applying_ && "SceneManager cannot die during seat apply");
  this->window_ = 0;
  loka::app::scene::Scene *current = this->currentScene_;
  if (current)
  {
    if (current->getLifecycleState()->get() != ON_DETACH)
      this->retireScene(current);
    this->retiredScenes_.retire(current);
  }
  this->retiredScenes_.retire(this->desired_);
  while (!this->retiredScenes_.empty())
    this->retiredScenes_.drain();
}

bool SceneManager::commitTransaction(loka::app::scene::Scene *, loka::app::scene::Scene *to)
{
  if (!this->window_ || !to || this->retiredScenes_.contains(to))
    return false;
  loka::app::scene::Scene *superseded = this->desired_;
  this->desired_ = to;
  if (superseded != to && superseded != this->currentScene_ && superseded != this->applying_)
    this->retiredScenes_.retire(superseded);
  return true;
}

void SceneManager::requestDetach()
{
  this->request(REQUEST_DETACH);
}

void SceneManager::requestRearm()
{
  this->request(REQUEST_REARM);
}

bool SceneManager::applyPendingWork()
{
  if (this->applying_)
    return false;
  const SceneRequest request = this->request_;
  this->request_ = REQUEST_NONE;
  const bool replaced = this->applyReplacement();
  loka::app::scene::Scene *current = this->currentScene_;
  if (!current)
  {
    // Retry refused preparation unless it recorded newer intent.
    // An empty seat with no pending replacement consumes the request as a no-op.
    if (this->hasPendingReplacement() && this->request_ == REQUEST_NONE)
      this->request(request);
    return replaced;
  }
  if (request == REQUEST_NONE)
    return replaced;

  // Protect the installed identity against adoption by synchronous observers.
  // Only this Scene's owned composition is walked, once per admitted request.
  this->applying_ = current;
  current->updateAttached(false);
  current->updateLifecycle(ON_DETACH);
  switch (request)
  {
  case REQUEST_NONE:
  case REQUEST_DETACH:
    break;
  case REQUEST_REARM:
    current->updateAttached(true);
    current->updateLifecycle(ON_ATTACH);
    break;
  }
  // A refused fresh composition still owes this seat a renewal. Preserve any
  // newer observer intent; retry only at the following App admission.
  if (request == REQUEST_REARM && current->mounted_ && !current->composed_
      && this->request_ == REQUEST_NONE)
    this->request(request);
  this->applying_ = 0;
  return true;
}

void SceneManager::seedScene(loka::app::scene::Scene *scene)
{
  assert(!this->currentScene_ && !this->desired_);
  this->commitTransaction(0, scene);
  this->applying_ = scene;
  this->installScene(scene);
  this->applying_ = 0;
}

bool SceneManager::applyReplacement()
{
#ifdef TEST_BUILD
  this->lastPrepareRefusal_ = 0;
#endif
  if (this->applying_ || !this->hasPendingReplacement())
    return false;
  loka::app::scene::Scene *next = this->desired_;
  this->applying_ = next;
  next->setWindow(this->window_);
  const bool mounted = this->window_->mountReplacementScene(next);
  if (mounted && next->mounted_)
    next->composeIfNeeded(loka::app::scene::COMPOSE_EVENT_ATTACH, false);
  // Validate the owner after ATTACH callbacks before installing through the
  // candidate's borrow; rails not yet using deferred visibility can lose it.
  if (!mounted || (next->mounted_ &&
                   (!this->window_->hasLiveScenePlatform() || !next->composed_)))
  {
    // Preparation has not projected native contexts. Drop the borrowed rail
    // before this candidate can outlive the concrete Window's controller.
    next->platformController_ = 0;
    next->unmount();
    next->setWindow(0);
#ifdef TEST_BUILD
    // The desired role or retirement pool owns this identity until the next
    // admission clears the observation before any captured retirees are drained.
    this->lastPrepareRefusal_ = next;
#endif
    this->applying_ = 0;
    if (next != this->desired_)
      this->retiredScenes_.retire(next);
    return false;
  }
  this->installScene(next);
  this->applying_ = 0;
  return true;
}

void SceneManager::retireScene(loka::app::scene::Scene *scene)
{
  scene->setWindow(0);
  scene->updateAttached(false);
  scene->updateLifecycle(ON_DETACH);
  scene->unmount();
}

void SceneManager::retireCurrentScene()
{
  loka::app::scene::Scene *current = this->currentScene_;
  if (current)
    this->retireScene(current);
}

void SceneManager::installScene(loka::app::scene::Scene *next)
{
  loka::app::scene::Scene *old = this->currentScene_;
  if (old)
  {
    this->retireScene(old);
    // #920: outgoing detach observers can destroy the prepared Scene's rail.
    if (next->platformController_ && !this->window_->hasLiveScenePlatform())
    {
      assert(false && "window rail lost while installing a replacement Scene (#920)");
      next->platformController_ = 0;
    }
  }
  this->currentScene_ = next;
  next->setWindow(this->window_);
  // Outgoing detach resets the shared controller. Attach the prepared scene,
  // project its root, then publish ON_ATTACH; applying protects reentrant adoption.
  next->updateAttached(true);
  if (next->composed_ && next->platformController_)
    next->platformController_->onChange(next->rootNode_, loka::app::scene::NODE_DIRTY_INITIAL, true);
  next->updateLifecycle(ON_ATTACH);
  if (old != this->desired_)
    this->retiredScenes_.retire(old);
}
