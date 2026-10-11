#include "app/core/App.hpp"
#include "app/core/Window.hpp"
#include "app/MenuComposition.hpp"
#include "app/PlatformContext.hpp"
#include "app/core/AppComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/scene/Scene.hpp"
#include <algorithm>

App::App(AppConfigurable *config)
    : group_(0),
      quitWhenLastWindowClosed_(true),
      config_(config),
      defaultMenuBar_(0),
      activeWindow_(0),
      idleAccumulatedSeconds_(0.0),
      windowSeat_(0),
      pendingWindowClosures_(),
      flushingWindowWork_(false)
{
}

App::~App()
{
  this->retireComponents();
}

void App::retireComponents()
{
  // Shutdown destroys every owner; discard borrowed tail snapshots first.
  this->pendingReclaim_.clear();
  for (size_t i = 0; i < this->pendingWindowClosures_.size(); ++i)
    this->pendingWindowClosures_[i]->closeDialogResults();
  if (this->group_)
  {
    const AppComponentGroup::Components components = this->group_->getComponents();
    for (size_t i = 0; i < components.size(); ++i)
    {
      Window *window = components[i] ? components[i]->asWindow() : 0;
      if (window)
        window->closeDialogResults();
    }
  }
  while (!pendingWindowClosures_.empty())
  {
    flushPendingWindowClosures();
  }
  delete group_;
  group_ = 0;
  delete this->windowSeat_;
  this->windowSeat_ = 0;
}

void App::run()
{
  if (!group_ && config_)
  {
    AppComposition composition(config_->getPlatformContext());
    config_->compose(composition);
    // Bootstrap owns the only composition; clone refusal leaves no default.
    loka::app::MenuBarDefinition bar;
    loka::app::MenuComposition menuComposition(&bar);
    config_->composeDefaultMenu(menuComposition);
    menuComposition.finish();
    this->setDefaultMenuBar(bar.empty() ? 0 : &bar);
    group_ = new AppComponentGroup(composition.build());
    this->windowSeat_ = composition.takeWindowSeat();
    const AppComponentGroup::Components components = this->group_->getComponents();
    for (size_t i = 0; i < components.size(); ++i)
    {
      Window *window = components[i] ? components[i]->asWindow() : 0;
      if (window && !this->windowAdopted(window))
        this->bootstrapWindowRefused(window);
    }
    // A launch factory may open another document; that entry waits for the
    // next walk. Walk until one attempts nothing before reading the group, so
    // a refused first document with a pending second cannot read as windowless.
    // Every attempted id is adopted or dropped (A′), so it is not attempted again.
    while (this->admitDocumentWindows(ADOPT_AT_LAUNCH) != 0)
    {
    }
    if (this->group_->getComponents().size() == 0)
      this->bootstrapWindowless();
  }
  projectInitialVisibilityChunks();
  this->projectMenuSources();
}

/** One ListRevision compare per admission (two per Win32 tail, every Toolbox
    pass, every macOS flush tick). When the revision moved, (a) walks desired x
    group rows and (b) walks group rows x desired once per stale row plus one
    final full scan; both over this App's group and its own seat's desired copy. */
unsigned short App::admitDocumentWindows(AdoptionPhase phase)
{
  if (!this->windowSeat_ || this->windowSeat_->key().matches())
    return 0;
  loka::app::WindowSeat &seat = *this->windowSeat_;
  loka::app::DocumentRosterBase &roster = seat.roster();
  const loka::core::ListRevision start = roster.revision().get();
  // Mount/factory callbacks can open documents. Keep this walk's identities
  // stable and commit only its starting revision, leaving later edits pending.
  const unsigned short n = seat.snapshotDesired();
  this->group_->reserve(n);
  unsigned short attempted = 0;
  // (a) increase before decrease, so replacing the last document cannot quit.
  for (unsigned short i = 0; i < n; ++i)
  {
    const loka::core::ItemId id = seat.desiredAt(i);
    if (this->group_->find(id))
      continue;
    ++attempted;
    Window *window = seat.createWindow(id, this->config_->getPlatformContext());
    if (!window)
    {
      roster.windowGone(id);
      continue;
    }
    this->group_->adopt(window, id);
    if (!this->windowAdopted(window))
    {
      if (phase == ADOPT_AT_LAUNCH)
        this->bootstrapWindowRefused(window);
      else
        this->requestWindowClose(window);
    }
  }
  // (b) decrease: a keyed row whose id is not in this walk's desired copy is
  // closed through the same door a native close takes. Restart the scan after
  // each close because requestWindowClose removes the row; it always removes a
  // row it finds, so the loop is bounded by the row count at entry.
  const size_t rowsAtEntry = this->group_->getComponents().size();
  for (size_t guard = rowsAtEntry; guard-- > 0;)
  {
    Window *stale = 0;
    const AppComponentGroup::Components rows = this->group_->getComponents();
    for (size_t i = 0; i < rows.size(); ++i)
    {
      Window *window = rows[i] ? rows[i]->asWindow() : 0;
      const loka::core::ItemId key = this->group_->keyOf(window);
      if (key.isNone())
        continue;
      bool desired = false;
      for (unsigned short j = 0; j < n; ++j)
      {
        if (seat.desiredAt(j) == key)
        {
          desired = true;
          break;
        }
      }
      if (!desired)
      {
        stale = window;
        break;
      }
    }
    if (!stale)
      break;
    const loka::core::ItemId key = this->group_->keyOf(stale);
    this->requestWindowClose(stale);
    assert(!this->group_->find(key));
    (void)key;
  }
  seat.key_ = loka::app::scene::KeySnapshot<loka::core::ListRevision>(&roster.revision(), start);
  return attempted;
}

bool App::windowAdopted(Window *)
{
  return true;
}

void App::bootstrapWindowRefused(Window *)
{
}

void App::bootstrapWindowless()
{
  this->quit();
}

loka::app::IdlePolicy App::idlePolicy() const
{
  if (activeWindow_)
  {
    return activeWindow_->idlePolicy();
  }
  return config_ ? config_->idlePolicy() : loka::app::IdlePolicy::none();
}

bool App::consumeIdle(double elapsedSeconds, double &dispatchElapsedSeconds)
{
  const loka::app::IdlePolicy policy = this->idlePolicy();
  if (policy.mode == loka::app::IDLE_MODE_NONE)
  {
    idleAccumulatedSeconds_ = 0.0;
    return false;
  }
  if (policy.mode == loka::app::IDLE_MODE_EVERY_TICK)
  {
    idleAccumulatedSeconds_ = 0.0;
    dispatchElapsedSeconds = elapsedSeconds;
    return true;
  }
  idleAccumulatedSeconds_ += elapsedSeconds;
  if (policy.intervalSeconds <= 0.0 || idleAccumulatedSeconds_ >= policy.intervalSeconds)
  {
    dispatchElapsedSeconds = idleAccumulatedSeconds_;
    idleAccumulatedSeconds_ = 0.0;
    return true;
  }
  return false;
}

void App::handleIdle(double elapsedSeconds)
{
  if (activeWindow_ && activeWindow_->handleIdle(elapsedSeconds))
  {
    return;
  }
  if (config_)
  {
    config_->onIdle(elapsedSeconds);
  }
}

bool App::handleKeyPress(char key)
{
  if (activeWindow_ && activeWindow_->handleKeyPress(key))
  {
    return true;
  }
  return config_ ? config_->handleKeyPress(key) : false;
}

void App::projectInitialVisibilityChunks()
{
  if (!group_)
    return;
  const AppComponentGroup::Components comps = group_->getComponents();
  for (size_t i = 0; i < comps.size(); ++i)
  {
    AppComponent *comp = comps[i];
    Window *win = comp->asWindow();
    if (win && win->visibilityState().get())
    {
      loka::core::StateTracker *tracker = win->getTracker();
      if (tracker)
      {
        loka::core::StandaloneTransactionGuard _(tracker);
        win->visibilityState().set(true, true);
      }
    }
  }
}

bool App::hasPendingWindowAdmission() const
{
  if (!this->pendingWindowClosures_.empty())
    return true;
  if (this->windowSeat_ && !this->windowSeat_->key().matches())
    return true;
  if (!this->group_)
    return false;
  const AppComponentGroup::Components components = this->group_->getComponents();
  for (size_t i = 0; i < components.size(); ++i)
  {
    Window *window = components[i] ? components[i]->asWindow() : 0;
    if (this->windowHasAdmissionWork(window))
      return true;
  }
  return false;
}

bool App::windowHasAdmissionWork(Window *window) const
{
  if (!window || this->isWindowClosePending(window)
      || (window->scene() && window->scene()->isBusy()))
    return false;
  loka::app::DialogResultDelivery *delivery = window->dialogResultDelivery();
  return window->hasPendingNativeVisibility() || window->hasPendingSceneInvalidation()
         || window->hasPendingScenePlatformSync() || (delivery && delivery->hasRunnableWork());
}

App::AdmittedWindow::AdmittedWindow(
    Window *value, loka::app::DialogResultDelivery *results,
    loka::app::DialogResultDelivery::Retirement *retirements)
    : window(value), scenes(0), delivery(results), dialogRetirements(retirements)
{
}

void App::AdmissionBatch::begin(const std::vector<Window *> &pending)
{
  if (this->isOpen())
    return;
  this->closes = pending;
  this->phase_ = OPEN;
}

void App::AdmissionBatch::remember(const AdmittedWindow &row)
{
  for (size_t i = 0; i < this->rows.size(); ++i)
  {
    if (this->rows[i].window == row.window)
    {
      this->rows[i] = row;
      return;
    }
  }
  this->rows.push_back(row);
}

void App::AdmissionBatch::clear()
{
  this->rows.clear();
  this->closes.clear();
  this->phase_ = BETWEEN_TAILS;
}

bool App::isWindowClosePending(Window *window) const
{
  return std::find(this->pendingWindowClosures_.begin(), this->pendingWindowClosures_.end(), window)
         != this->pendingWindowClosures_.end();
}

void App::flushWindowInvalidations()
{
  this->admitAndApplyWindows();
  this->reclaimWindows();
}

void App::admitAndApplyWindows()
{
  if (this->flushingWindowWork_)
    return;
  this->pendingReclaim_.begin(this->pendingWindowClosures_);
  this->flushingWindowWork_ = true;
  std::vector<AdmittedWindow> admitted;
  if (this->group_)
  {
    const AppComponentGroup::Components comps = this->group_->getComponents();
    for (size_t i = 0; i < comps.size(); ++i)
    {
      Window *win = comps[i] ? comps[i]->asWindow() : 0;
      if (this->windowHasAdmissionWork(win))
      {
        loka::app::DialogResultDelivery *delivery = win->dialogResultDelivery();
        if (admitted.empty())
          admitted.reserve(comps.size());
        admitted.push_back(AdmittedWindow(win, delivery, delivery ? delivery->retirementSnapshot() : 0));
      }
    }
  }
  // Snapshot our rows before callbacks can remove a Window from the group.
  // All seats apply before any Scene run: adoption from X's run waits even for Y.
  for (std::vector<AdmittedWindow>::iterator it = admitted.begin(); it != admitted.end(); ++it)
  {
    // An earlier row's callback may have delivered a terminal close for this
    // snapshot row. The App owns both lists; never recreate a close-queued rail.
    Window *window = it->window;
    if (this->isWindowClosePending(window))
      continue;
    window->applyNativeVisibility();
    if (this->isWindowClosePending(window))
      continue;
    if (it->delivery)
    {
      it->delivery->deliver();
      if (this->isWindowClosePending(window))
        continue;
    }
    it->scenes = window->applySceneWork();
  }
  for (std::vector<AdmittedWindow>::iterator it = admitted.begin(); it != admitted.end(); ++it)
  {
    Window *window = it->window;
    if (this->isWindowClosePending(window))
      continue;
    window->flushSceneInvalidation();
  }
  for (size_t i = 0; i < admitted.size(); ++i)
    this->pendingReclaim_.remember(admitted[i]);
  // A roster edit after the last admission (turn.close/reclaimWindows)
  // remains pending window admission work for the next tail.
  this->admitDocumentWindows(ADOPT_RUNNING);
  // Detach is observable work and belongs before the turn closes. Reclaim
  // keeps the Window alive until then. Mirror drainWindowClosures' busy skip.
  const size_t closeCount = this->pendingWindowClosures_.size();
  for (size_t i = 0; i < closeCount; ++i)
  {
    Window *window = this->pendingWindowClosures_[i];
    loka::app::scene::Scene *scene = window->scene();
    if (scene && !scene->isBusy())
      window->retireSceneForClose();
  }
  this->projectMenuSources();
  this->flushingWindowWork_ = false;
}

void App::reclaimWindows()
{
  if (this->flushingWindowWork_ || !this->pendingReclaim_.isOpen())
    return;
  this->flushingWindowWork_ = true;
  // Remove borrowed identities before the close drain can delete their owners.
  std::vector<AdmittedWindow> &rows = this->pendingReclaim_.rows;
  for (size_t i = 0; i < rows.size(); ++i)
    if (this->isWindowClosePending(rows[i].window))
      rows[i].window = 0;
  this->drainWindowClosures(this->pendingReclaim_.closes);
  for (size_t i = 0; i < rows.size(); ++i)
  {
    AdmittedWindow &row = rows[i];
    Window *window = row.window;
    if (!window || this->isWindowClosePending(window)
        || (window->scene() && window->scene()->isBusy()))
      continue;
    window->reclaimScenes(row.scenes);
    if (row.delivery)
      row.delivery->reclaim(row.dialogRetirements);
  }
  this->pendingReclaim_.clear();
  this->flushingWindowWork_ = false;
  PlatformContext *context = this->config_ ? this->config_->getPlatformContext() : 0;
  if (context)
    context->nativeResourceRetirement_.drain();
}

void App::windowClosed(Window *window)
{
  if (!window)
  {
    return;
  }
  if (group_)
  {
    const AppComponentGroup::Components comps = group_->getComponents();
    for (std::size_t i = 0; i < comps.size(); ++i)
    {
      assert((!comps[i] || comps[i]->asWindow() != window) && "App::windowClosed requires requestWindowClose detach first");
    }
  }
  assert(activeWindow_ != window && "A retired Window cannot remain active at reclaim");
  window->closeDialogResults();
  delete window;
}

void App::requestWindowClose(Window *window)
{
  if (!group_ || !window)
  {
    return;
  }
  for (size_t i = 0; i < pendingWindowClosures_.size(); ++i)
  {
    if (pendingWindowClosures_[i] == window)
    {
      return;
    }
  }

  const AppComponentGroup::Components comps = group_->getComponents();
  bool found = false;
  Window *nextActive = 0;
  for (std::size_t i = 0; i < comps.size(); ++i)
  {
    Window *candidate = comps[i] ? comps[i]->asWindow() : 0;
    if (candidate == window)
    {
      found = true;
      continue;
    }
    if (!nextActive && candidate)
    {
      nextActive = candidate;
    }
  }
  if (!found)
  {
    return;
  }

  // Reserve before changing ownership so an allocation failure cannot leave
  // a detached Window without a queue owner.
  pendingWindowClosures_.reserve(pendingWindowClosures_.size() + 1);
  window->closeDialogResults();
  const loka::core::ItemId key = this->group_->keyOf(window);
  if (!group_->remove(window))
  {
    return;
  }
  if (activeWindow_ == window)
  {
    this->setActiveWindow(nextActive);
  }
  pendingWindowClosures_.push_back(window);

  if (quitWhenLastWindowClosed_ && group_->getComponents().size() == 0)
  {
    this->quit();
  }
  // When the close came from the roster, windowGone returns EDIT_ID_NOT_FOUND
  // without advancing the revision, so an application close costs exactly one walk.
  if (!key.isNone() && this->windowSeat_)
    this->windowSeat_->roster().windowGone(key);
}

void App::flushPendingWindowClosures()
{
  if (this->flushingWindowWork_ || this->pendingReclaim_.isOpen()
      || this->pendingWindowClosures_.empty())
    return;
  const std::vector<Window *> pending(this->pendingWindowClosures_);
  this->flushingWindowWork_ = true;
  this->drainWindowClosures(pending);
  this->flushingWindowWork_ = false;
}

void App::drainWindowClosures(const std::vector<Window *> &pending)
{
  for (size_t i = 0; i < pending.size(); ++i)
  {
    Window *window = pending[i];
    if (window->scene() && window->scene()->isBusy())
      continue;
    this->pendingWindowClosures_.erase(
        std::find(this->pendingWindowClosures_.begin(), this->pendingWindowClosures_.end(), window));
    this->windowClosed(window);
  }
}

bool App::handleMenuCommand(int commandId, Window *window)
{
  (void)commandId;
  (void)window;
  return false;
}

void App::setDefaultMenuBar(const loka::app::MenuBarDefinition *menuBar)
{
  if (menuBar)
  {
    loka::core::OwnedDef<loka::app::MenuBarDefinition> next(menuBar->clone());
    if (!next.isSet())
      return;
    this->defaultMenuBar_.reset(next.take());
  }
  else
    this->defaultMenuBar_.reset();
}

void App::setActiveWindow(Window *window)
{
  if (activeWindow_ == window)
  {
    return;
  }
  activeWindow_ = window;
}

void App::projectMenu(Window *window, const loka::app::MenuBarDefinition *bar,
                      const loka::app::scene::Scene *source)
{
  (void)window;
  (void)bar;
  (void)source;
}

void App::projectMenuSources()
{
  // One pass per admission (twice in a Win32 tail), plus bootstrap. Walk only
  // App-owned rows; each offer owns one temporary merge, never a cached source.
  if (this->group_)
  {
    const AppComponentGroup::Components rows = this->group_->getComponents();
    for (size_t i = 0; i < rows.size(); ++i)
    {
      Window *window = rows[i] ? rows[i]->asWindow() : 0;
      if (!window || this->isWindowClosePending(window))
        continue;
      const loka::app::MenuBarDefinition *base =
          this->defaultMenuBar();
      const loka::app::scene::Scene *scene = window->scene();
      const loka::app::MenuBarDefinition *overlay = scene ? scene->menuBar() : 0;
      loka::core::OwnedDef<loka::app::MenuBarDefinition> merged(
          loka::app::MergeMenuBars(base, overlay));
      if ((base || overlay) && !merged.isSet())
        continue;
      this->projectMenu(window, merged.get(), overlay ? scene : 0);
    }
  }
  if (!this->activeWindow_)
    this->projectMenu(0, this->defaultMenuBar(), 0);
}

void App::reconcileFocus()
{
  if (this->flushingWindowWork_ || !this->group_)
    return;
  /** Each completion walks live App-owned windows (w), never foreign rows.
      Repeated scans visit O(w^2) group entries; linear visited lookups cost
      O(w^3) pointer comparisons in the worst case. Local storage is O(1)
      through eight windows and O(w) beyond that; only the overflow allocates.
      Visited pointers are identities only and are never dereferenced. If a
      new window reuses a visited address, the next completion picks it up. */
  class VisitedWindows
  {
  public:
    VisitedWindows() : inlineCount_(0) {}
    bool contains(Window *window) const
    {
      return std::find(this->inline_, this->inline_ + this->inlineCount_, window)
                 != this->inline_ + this->inlineCount_
             || std::find(this->spill_.begin(), this->spill_.end(), window) != this->spill_.end();
    }
    void add(Window *window)
    {
      if (this->inlineCount_ < INLINE_CAPACITY)
        this->inline_[this->inlineCount_++] = window;
      else
        this->spill_.push_back(window);
    }

  private:
    enum { INLINE_CAPACITY = 8 };
    Window *inline_[INLINE_CAPACITY];
    size_t inlineCount_;
    std::vector<Window *> spill_;
  };
  VisitedWindows visited;
  for (;;)
  {
    Window *next = 0;
    const AppComponentGroup::Components components = this->group_->getComponents();
    for (size_t i = 0; i < components.size(); ++i)
    {
      Window *window = components[i] ? components[i]->asWindow() : 0;
      if (window && !this->isWindowClosePending(window)
          && !visited.contains(window))
      {
        next = window;
        break;
      }
    }
    if (!next)
      return;
    visited.add(next);
    next->reconcileFocus();
  }
}
