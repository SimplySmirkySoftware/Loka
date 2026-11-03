#ifndef LOKA_APP_HPP
#define LOKA_APP_HPP

#include "app/core/AppComponentGroup.hpp"
#include "app/core/DocumentRoster.hpp"
#include "app/core/AppComponent.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/Menu.hpp"
#include "app/MenuSources.hpp"
#include "core/util/OwnedDef.hpp"
#include "app/core/DialogResultDelivery.hpp"
#include <cassert>
#include "app/url/URLOpenKind.hpp"
#include "app/url/ReceiveResult.hpp"

class Window;
class AppComposition;

namespace loka
{
  namespace app
  {
    class WindowSeat;
    class URLReceiveSeatBase;
    class WebURL;
    class URLOpenerBase;
    namespace scene { class Scene; }
    namespace testing
    {
      class AppTestAccess;
    }
  }
  namespace dsl
  {
    namespace testing
    {
      class OwnershipDump;
    }
  } // namespace dsl
} // namespace loka

// App is owned by the platform/application layer. Code that needs an App
// instance should reach it through an owner-side path such as Window, not
// through a global current-App accessor.
class App : public AppComponent
{
public:
  explicit App(AppConfigurable *config);
  virtual ~App();

  virtual void run();
  /** Main-thread rail native entry; arms must not call this. Borrows text only
      for this call. Offers to the config-owned inbox without State writes or
      Flow execution, so no active clock is required. Once per native URL event:
      O(input) conversion and bounded parsing, at most one inbox slot written. */
  loka::app::ReceiveResult receiveURL(const loka::core::String &text);
  /** Rail completion entry; enumerates current live windows after App work. */
  void reconcileAtCompletion();
  enum CompletionEntry
  {
    COMPLETION_OUTERMOST,
    COMPLETION_INHERITED_CLOCK
  };
  /** Caller captures inherited clock status before constructing its turn.
      Refuses inherited clocks and busy Scenes. Receives at most one queued
      URL from a registered seat when no quit attempt is active, then opens
      at most one queued URL from a registered idle opener; re-reads open
      eligibility after the arm. True requires another
      admission before closing the turn. Once per outer completion: O(1) when
      neither direction is due, otherwise one walk of this App's group rows,
      one fresh receive chain and at most one native open. */
  bool serveURLsAtCompletion(CompletionEntry entry);
  virtual void quit() = 0;
  /** Ask each document in roster order; any decline published after the ask ends this attempt. */
  void requestQuit();
  /** Reads roster document facts only; static window contents are not covered. */
  bool hasEditedDocuments() const;
  /** Reserve Quit for the next eligible App admission tail: completion is not
      the native query's return, Operation::close, or process termination.
      Coalesces with REQUESTED/ASKING and covers roster documents only.
      A reservation still pending when the loop exits is abandoned. */
  void requestQuitAtCompletion();
  /** True transfers the close decision to the keyed document's node. */
  bool closeAsked(Window *window);
  /** Detaches a Window immediately and queues its silent reclaim for the App clock boundary. */
  void requestWindowClose(Window *window);
  virtual bool handleMenuCommand(int commandId, Window *window);
  loka::app::IdlePolicy idlePolicy() const;
  bool consumeIdle(double elapsedSeconds, double &dispatchElapsedSeconds);
  void handleIdle(double elapsedSeconds);
  bool handleKeyPress(char key);
  /** Bootstrap-only default from composeDefaultMenu; absent without a config
      or when its clone is refused. Its identity stays fixed for the App lifetime. */
  const loka::app::MenuBarDefinition *defaultMenuBar() const
  {
    return this->defaultMenuBar_.get();
  }
  void setActiveWindow(Window *window);
  Window *activeWindow() const
  {
    return activeWindow_;
  }

protected:
  /** Once per composition bootstrap after registration; default O(1), empty. */
  virtual void collectLaunchRequests() {}

  /** Rails that own projection state call this first in their destructor;
      the base destructor's own call is the fallback for rails that own none.
      Repeated calls derive their work from the remaining group and close queue. */
  void retireComponents();

  /** Drain-internal reclaim step: deletes an already-detached Window. Only
      drainWindowClosures() and subclass observation hooks may call
      this; everything else must go through requestWindowClose(). */
  virtual void windowClosed(Window *window);
  AppComponentGroup *group_;
  bool quitWhenLastWindowClosed_;
  AppConfigurable *config_;
private:
  loka::core::OwnedDef<loka::app::MenuBarDefinition> defaultMenuBar_;
protected:
  Window *activeWindow_;
  double idleAccumulatedSeconds_;

  /** Synchronous borrowed offer from completion or bootstrap. A null window
      means no active window: project the default. Global rails accept only
      window == activeWindow() (including both null); per-window rails accept
      every non-null row and ignore null. Attachments revoke source on detach. */
  virtual void projectMenu(Window *window, const loka::app::MenuSources &sources);

  /** Rail setup for a Window just placed in the App's group. Bootstrap and
      runtime adoption (from w1) call this inline, one window at a time.
      Returns false when the window cannot be used (currently Toolbox scene
      composition refused for memory). The default accepts without work. */
  virtual bool windowAdopted(Window *window);
  /** Bootstrap-only consequence of refused adoption. The default does nothing;
      Toolbox reports out of memory and quits without returning. */
  virtual void bootstrapWindowRefused(Window *window);
  /** The bootstrap opened no window at all: every static WindowDef was refused,
      the roster seeded nothing, or every launch create was refused. Called once,
      after the single launch walk, when windowlessAfterLaunch() holds. The default
      quits (quit()); Toolbox reports out of memory and quits without returning;
      a rail that cannot post a quit before its loop exists answers in its own run. */
  virtual void bootstrapWindowless();

  void projectInitialVisibilityChunks();
  /** Admits seats before Scene runs. The first admission of a tail captures
      collect-time closes; closes requested during apply, delivery or focus wait
      for the next tail, even with multiple admissions. Retirement rows accumulate
      until reclaim, with the latest snapshot per Window. Nested work is refused. */
  void admitAndApplyWindows();
  /** Deletes only the tail's captured closes, then drains captured retirements.
      Completes the batch; rails call this once after their completion work. */
  void reclaimWindows();
  /** Convenience tail with no intervening work: admit/apply, then reclaim.
      Rails call the two doors separately around their completion work. */
  void flushWindowInvalidations();
  /** Pre-wait progress: close rows, a moved roster revision, or serviceable
      Window completion work. */
  bool hasPendingWindowAdmission() const;
  /** The launch walk left no window and no document that could become one.
      Read by run() to report through bootstrapWindowless and by a rail that
      must not enter its loop for nothing (macOS); one home so the two cannot
      disagree. A document a launch factory opened is pending, not windowless:
      the runtime admission clock admits it (#1233). The roster count is the
      fact, not the seat key: a refused seed drops itself through windowGone,
      which moves the revision past the walk's snapshot, so the key reports a
      re-walk that would attempt nothing. */
  bool windowlessAfterLaunch() const;
  /** Completion rails skip windows whose close has already been requested. */
  bool isWindowClosePending(Window *window) const;
  /** Drains one queue snapshot outside an open split tail; requests made during
      the drain wait for the next flush. */
  void flushPendingWindowClosures();

  /** Read-only attempt outcome door for rails holding a native reply.
      The App keeps no origin for the attempt. */
  bool hasQuitAttempt() const
  {
    return this->quitAttempt_.phase == QuitAttempt::REQUESTED || this->quitAttempt_.phase == QuitAttempt::ASKING;
  }

private:
  enum AdoptionPhase { ADOPT_AT_LAUNCH, ADOPT_RUNNING };
  /** Returns how many window creates this walk attempted; 0 when it did not
      walk. A walk that attempted nothing ran no factory or mount, so no roster
      edit could have left its snapshot mismatched. */
  unsigned short admitDocumentWindows(AdoptionPhase phase);
  loka::app::WindowSeat *windowSeat_;
  loka::app::URLReceiveSeatBase *urlReceiveSeat_;
  /** Config-owned borrow; remains valid through App retirement and destruction. */
  loka::app::URLOpenerBase *urlOpener_;
  /** One shared walk over this App's rows per completion with work due. */
  bool anySceneBusy() const;
  /** One receive from completion, or each entry in the launch batch. */
  bool serveReceive();
  static loka::app::URLOpenKind openURLThunk(const loka::app::WebURL &url, void *context);
  /** One outstanding quit request, with explicit absence and no parallel flags. */
  struct QuitAttempt
  {
    enum Phase
    {
      NONE,
      REQUESTED,
      ASKING
    };
    Phase phase;
    loka::core::ItemId id;
    unsigned long seq;
    explicit QuitAttempt(Phase value = NONE)
        : phase(value),
          id(loka::core::ItemId::none()),
          seq(0)
    {
    }
    explicit QuitAttempt(const loka::app::CloseAsk &ask)
        : phase(ASKING),
          id(ask.id),
          seq(ask.seq)
    {
    }
    static QuitAttempt none() { return QuitAttempt(); }
  };
  QuitAttempt quitAttempt_;
  void advanceQuitAttempt();
  bool alreadyQuitAfterLastWindow() const;

  /** Borrowed rows remain owned by the App group or close queue. */
  struct AdmittedWindow
  {
    AdmittedWindow(Window *value, loka::app::DialogResultDelivery *results,
                   loka::app::DialogResultDelivery::Retirement *retirements);
    Window *window;
    loka::app::scene::Scene *scenes;
    loka::app::DialogResultDelivery *delivery;
    loka::app::DialogResultDelivery::Retirement *dialogRetirements;
  };

  /** One tail's borrowed work. An open batch may have no rows or closes. */
  class AdmissionBatch
  {
  public:
    AdmissionBatch() : phase_(BETWEEN_TAILS) {}
    bool isOpen() const { return this->phase_ == OPEN; }
    void begin(const std::vector<Window *> &pending);
    void remember(const AdmittedWindow &row);
    void clear();
    std::vector<AdmittedWindow> rows;
    std::vector<Window *> closes;
  private:
    enum Phase { BETWEEN_TAILS, OPEN };
    Phase phase_;
  };
  void drainWindowClosures(const std::vector<Window *> &pending);
  bool windowHasAdmissionWork(Window *window) const;
  std::vector<Window *> pendingWindowClosures_;
  bool flushingWindowWork_;
  AdmissionBatch pendingReclaim_;

  void projectMenuSources();

  friend class loka::dsl::testing::OwnershipDump;
  friend class loka::app::testing::AppTestAccess;
};

#endif // LOKA_APP_HPP
