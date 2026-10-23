#include "support/TextBufferFixture.hpp"
#include "core/util/ScopedPtr.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullWindow.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/app/ComposableNodeTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "SceneOwnershipTests.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/DialogResultTestAccess.hpp"
#include <cassert>
#include <cstdio>
#include "app/PlatformContext.hpp"
#include "app/core/App.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"

// LeakSanitizer cannot complete its process scan after fork-based death checks.
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  static int g_sceneOwnershipDefinitionsAlive = 0;
  static int g_sceneOwnershipScenesAlive = 0;
  static int g_windowRetirementWindowsAlive = 0;

  struct SceneOwnershipDefinition : public loka::app::BoxDefinition
  {
    SceneOwnershipDefinition()
        : loka::app::BoxDefinition()
    {
      ++g_sceneOwnershipDefinitionsAlive;
    }

    SceneOwnershipDefinition(const SceneOwnershipDefinition &other)
        : loka::app::BoxDefinition(other)
    {
      ++g_sceneOwnershipDefinitionsAlive;
    }

    virtual ~SceneOwnershipDefinition()
    {
      --g_sceneOwnershipDefinitionsAlive;
    }

    typedef SceneOwnershipDefinition CloneType;
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    {
      return new SceneOwnershipDefinition(*this);
    }
  };

  class SceneOwnershipProbe : public loka::app::scene::Scene
  {
  public:
    SceneOwnershipProbe()
        : loka::app::scene::Scene(new SceneOwnershipDefinition())
    {
      ++g_sceneOwnershipScenesAlive;
    }

    virtual ~SceneOwnershipProbe()
    {
      --g_sceneOwnershipScenesAlive;
    }
  };

  class SceneRetirementCountProbe : public loka::app::scene::Scene
  {
  public:
    explicit SceneRetirementCountProbe(int *destructionCount)
        : loka::app::scene::Scene(new SceneOwnershipDefinition()),
          destructionCount_(destructionCount)
    {
      ++g_sceneOwnershipScenesAlive;
    }

    virtual ~SceneRetirementCountProbe()
    {
      ++*this->destructionCount_;
      --g_sceneOwnershipScenesAlive;
    }

  private:
    int *destructionCount_;
  };

  class ReentrantSceneRetirementProbe : public loka::app::scene::Scene
  {
  public:
    ReentrantSceneRetirementProbe(Window *window,
                                  loka::app::scene::Scene *retiree,
                                  loka::app::scene::Scene *replacement,
                                  int *scenesAliveAfterNestedFlush)
        : loka::app::scene::Scene(new SceneOwnershipDefinition()),
          window_(window),
          retiree_(retiree),
          replacement_(replacement),
          scenesAliveAfterNestedFlush_(scenesAliveAfterNestedFlush)
    {
      ++g_sceneOwnershipScenesAlive;
    }

    virtual ~ReentrantSceneRetirementProbe()
    {
      this->window_->sceneManager()->commitTransaction(this->retiree_, this->replacement_);
      this->window_->flushSceneInvalidation();
      *this->scenesAliveAfterNestedFlush_ = g_sceneOwnershipScenesAlive;
      --g_sceneOwnershipScenesAlive;
    }

  private:
    Window *window_;
    loka::app::scene::Scene *retiree_;
    loka::app::scene::Scene *replacement_;
    int *scenesAliveAfterNestedFlush_;
  };

  struct SceneLifecycleObservation
  {
    explicit SceneLifecycleObservation(loka::app::scene::Scene *observedScene)
        : scene(observedScene),
          managerWindow(0),
          notificationCount(0)
    {
      for (int i = 0; i < 2; ++i)
      {
        values[i] = ON_CREATE;
        attachedValues[i] = false;
        ownerWindows[i] = 0;
        managerScenes[i] = 0;
      }
    }

    loka::app::scene::Scene *scene;
    Window *managerWindow;
    int notificationCount;
    SceneLifecycle values[2];
    bool attachedValues[2];
    Window *ownerWindows[2];
    loka::app::scene::Scene *managerScenes[2];
  };

  struct SceneReclaimObservation
  {
    SceneReclaimObservation()
        : compositionAttachCalls(0),
          compositionDetachCalls(0),
          contextAttachCalls(0),
          contextDetachCalls(0),
          platformDestroyCalls(0)
    {
    }

    int compositionAttachCalls;
    int compositionDetachCalls;
    int contextAttachCalls;
    int contextDetachCalls;
    int platformDestroyCalls;
  };

  class SceneReclaimBoundaryNode;

  struct SceneReclaimBoundaryTypeTag
  {
  };

  struct SceneReclaimBoundaryProps : public loka::app::scene::NodePropsBase<SceneReclaimBoundaryProps>
  {
    typedef SceneReclaimBoundaryTypeTag TypeTag;
    typedef SceneReclaimBoundaryNode NodeType;

    explicit SceneReclaimBoundaryProps(SceneReclaimObservation *value = 0)
        : observation(value)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return this->propsTypeId() < rhs.propsTypeId();
      }
      const SceneReclaimBoundaryProps &other = static_cast<const SceneReclaimBoundaryProps &>(rhs);
      return this->observation < other.observation;
    }

    SceneReclaimObservation *observation;
  };

  class SceneReclaimBoundaryNode
      : public loka::app::scene::StdCompositionBoundaryNodeBase<SceneReclaimBoundaryProps>
  {
  public:
    explicit SceneReclaimBoundaryNode(const SceneReclaimBoundaryProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<SceneReclaimBoundaryProps>(props)
    {
    }

  protected:
    virtual void attachNode(loka::app::scene::NodeComposition &)
    {
      ++this->props.observation->compositionAttachCalls;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &)
    {
      ++this->props.observation->compositionDetachCalls;
    }
  };

  typedef loka::app::scene::BoundaryDefinition<SceneReclaimBoundaryProps, SceneReclaimBoundaryNode>
      SceneReclaimBoundaryDefinition;

  class SceneReclaimProbe : public loka::app::scene::Scene
  {
  public:
    explicit SceneReclaimProbe(SceneReclaimObservation *observation)
        : loka::app::scene::Scene(SceneReclaimBoundaryDefinition(SceneReclaimBoundaryProps(observation)))
    {
      ++g_sceneOwnershipScenesAlive;
    }

    virtual ~SceneReclaimProbe()
    {
      --g_sceneOwnershipScenesAlive;
    }
  };

  class SceneReclaimNodeContext : public loka::app::scene::NodeContext
  {
  public:
    explicit SceneReclaimNodeContext(SceneReclaimObservation *observation)
        : observation_(observation)
    {
    }

    /** Attach-time read: the announce successor. */
    void readLifecycleFactOnAttach()
    {
      if (this->owner() &&
          this->owner()->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
      {
        ++this->observation_->contextAttachCalls;
      }
    }

    // Living transitions (S2a) and the terminal (S2b) arrive through the
    // fact channel; the counters keep their original meaning.
    virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                               loka::app::scene::NodeLifecycleFact next)
    {
      (void)previous;
      if (next == loka::app::scene::NODE_FACT_ATTACHED)
      {
        ++this->observation_->contextAttachCalls;
      }
      else
      {
        ++this->observation_->contextDetachCalls;
      }
    }

  private:
    SceneReclaimObservation *observation_;
  };

  class SceneReclaimPlatformController : public loka::app::scene::IPlatformController
  {
  public:
    explicit SceneReclaimPlatformController(SceneReclaimObservation *observation)
        : observation_(observation)
    {
    }

    virtual void onChange(loka::app::scene::Node *rootNode,
                          loka::app::scene::NodeDirtyFlags,
                          bool)
    {
      SceneReclaimNodeContext *context = new SceneReclaimNodeContext(this->observation_);
      rootNode->setContext(context);
      context->readLifecycleFactOnAttach();
    }

    virtual void synchronize() {}

    virtual bool hasPendingSync() const
    {
      return false;
    }

    virtual void destroy()
    {
      ++this->observation_->platformDestroyCalls;
    }

  private:
    SceneReclaimObservation *observation_;
  };

  struct WindowCreatingPlatformContext : public PlatformContext
  {
    virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
    {
      return 0;
    }

    virtual Window *createWindow(const WindowProps &props)
    {
      return new Window(this, props);
    }

    virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *) const
    {
      return 0;
    }

    virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
    {
      return false;
    }

    virtual bool createImageFromBlob(const loka::core::resource::Blob &,
                                     std::size_t,
                                     std::size_t,
                                     loka::core::resource::Image &)
    {
      return false;
    }
  };

  class WindowRetirementProbe : public Window
  {
  public:
    WindowRetirementProbe(PlatformContext *context, const WindowProps &props)
        : Window(context, props)
    {
      ++g_windowRetirementWindowsAlive;
    }

    virtual ~WindowRetirementProbe()
    {
      --g_windowRetirementWindowsAlive;
    }
  };

  class WindowRetirementTestApp : public App
  {
  public:
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    using App::flushPendingWindowClosures;
    WindowRetirementTestApp()
        : App(0),
          quitCalls(0),
          appliedWindow(0),
          applyMenuCalls(0),
          closeDuringReclaim(0),
          reclaimCalls(0),
          windowsAliveDuringNestedFlush(0)
    {
      for (int i = 0; i < 4; ++i)
      {
        reclaimOrder[i] = 0;
      }
    }

    virtual void quit()
    {
      ++quitCalls;
    }

    void install(Window *window)
    {
      group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      this->setActiveWindow(window);
    }

    void install(Window *first, Window *second)
    {
      this->install(first);
      group_->adopt(second);
    }

    void install(Window *first, Window *second, Window *third)
    {
      this->install(first, second);
      group_->adopt(third);
    }

    void detachWithoutReselectForMisuseTest(Window *window)
    {
      group_->remove(window);
    }

    void flush()
    {
      this->flushWindowInvalidations();
    }

    int quitCalls;
    Window *appliedWindow;
    int applyMenuCalls;
    Window *closeDuringReclaim;
    int reclaimCalls;
    int windowsAliveDuringNestedFlush;
    Window *reclaimOrder[4];

    virtual void windowClosed(Window *window)
    {
      assert(reclaimCalls < 4);
      reclaimOrder[reclaimCalls] = window;
      ++reclaimCalls;
      if (closeDuringReclaim)
      {
        Window *next = closeDuringReclaim;
        closeDuringReclaim = 0;
        this->requestWindowClose(next);
        this->flushPendingWindowClosures();
        this->flushWindowInvalidations();
        windowsAliveDuringNestedFlush = g_windowRetirementWindowsAlive;
      }
      App::windowClosed(window);
    }

    // windowClosed is protected since the review fix; external misuse is now a
    // compile error. The death checks below exercise subclass-level misuse
    // through this bridge.
    void misuseWindowClosedForTest(Window *window)
    {
      this->windowClosed(window);
    }

  protected:
    virtual void projectMenu(Window *window, const loka::app::MenuSources &)
    {
      ++applyMenuCalls;
      appliedWindow = window;
    }
  };

  struct WindowRetirementNotification
  {
    WindowRetirementNotification()
        : app(0),
          window(0),
          scene(0),
          calls(0)
    {
    }

    WindowRetirementTestApp *app;
    WindowRetirementProbe *window;
    SceneOwnershipProbe *scene;
    int calls;
  };

  void RequestWindowCloseDuringNotification(void *userData)
  {
    WindowRetirementNotification *notification = static_cast<WindowRetirementNotification *>(userData);
    ++notification->calls;
    notification->app->requestWindowClose(notification->window);
    notification->app->requestWindowClose(notification->window);
    assert(g_windowRetirementWindowsAlive == 1);
    assert(g_sceneOwnershipScenesAlive == 1);
    LOKA_VERIFY(notification->window->scene() == notification->scene);
    assert(notification->scene->getWindow() == notification->window);
  }

#if defined(LOKA_LIFECYCLE_AUDIT) && defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  enum WindowClosedMisuse
  {
    WINDOW_CLOSED_WITHOUT_DETACH,
    WINDOW_CLOSED_WHILE_ACTIVE
  };

  void AssertWindowClosedMisuseAborts(WindowClosedMisuse misuse)
  {
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
      WindowCreatingPlatformContext context;
      WindowRetirementTestApp app;
      WindowRetirementProbe *first = new WindowRetirementProbe(&context, WindowProps());
      if (misuse == WINDOW_CLOSED_WITHOUT_DETACH)
      {
        WindowRetirementProbe *second = new WindowRetirementProbe(&context, WindowProps());
        app.install(first, second);
        app.misuseWindowClosedForTest(second);
      }
      else
      {
        app.install(first);
        app.detachWithoutReselectForMisuseTest(first);
        app.misuseWindowClosedForTest(first);
      }
      _exit(0);
    }

    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status));
  LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
  }
#endif

  struct ReentrantSceneFlushContext
  {
    ReentrantSceneFlushContext()
        : window(0),
          callbackCalls(0),
          scenesAliveAfterFlush(0)
    {
    }

    Window *window;
    int callbackCalls;
    int scenesAliveAfterFlush;
  };

  void FlushWindowDuringDetach(void *userData)
  {
    ReentrantSceneFlushContext *context = static_cast<ReentrantSceneFlushContext *>(userData);
    ++context->callbackCalls;
    context->window->flushSceneInvalidation();
    context->scenesAliveAfterFlush = g_sceneOwnershipScenesAlive;
  }

  void RecordSceneLifecycleNotification(void *userData)
  {
    SceneLifecycleObservation *observation = static_cast<SceneLifecycleObservation *>(userData);
    const int index = observation->notificationCount;
    assert(index < 2);
    observation->values[index] = observation->scene->getLifecycleState()->get();
    observation->attachedValues[index] = observation->scene->getAttachedState()->get();
    observation->ownerWindows[index] = observation->scene->getWindow();
    Window *managerWindow = observation->managerWindow ? observation->managerWindow : observation->ownerWindows[index];
    observation->managerScenes[index] = managerWindow ? managerWindow->scene() : 0;
    ++observation->notificationCount;
  }
} // namespace

void testWindowDefaultFrameContract()
{
  const loka::core::Frame frame = Window::defaultFrame();
  LOKA_VERIFY(frame == loka::core::Frame(50, 50, 300, 300));
}

void testWindowDefinitionTransfersSceneOwnershipToWindow()
{
  printf("\n==== [testWindowDefinitionTransfersSceneOwnershipToWindow] start ====\n");

  assert(g_sceneOwnershipDefinitionsAlive == 0);
  {
    WindowDefinition<WindowProps> definition;
    definition.props.scene(SceneOwnershipDefinition());
    assert(g_sceneOwnershipDefinitionsAlive == 1);

    WindowCreatingPlatformContext context;
    Window *window = definition.create(&context);
    assert(window != 0);
    assert(g_sceneOwnershipDefinitionsAlive == 2);

    delete window;
    assert(g_sceneOwnershipDefinitionsAlive == 1);
  }
  assert(g_sceneOwnershipDefinitionsAlive == 0);

  printf("==== [testWindowDefinitionTransfersSceneOwnershipToWindow] end ====\n");
}

void testWindowPropsSceneHandoffIsOneShotAcrossCopies()
{
  printf("\n==== [testWindowPropsSceneHandoffIsOneShotAcrossCopies] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps first;
  SceneOwnershipProbe *scene = new SceneOwnershipProbe();
  first.scene(scene);
  WindowProps second(first);

  Window *firstWindow = new Window(&context, first);
  Window *secondWindow = new Window(&context, second);
  LOKA_VERIFY(firstWindow->scene() == scene);
  LOKA_VERIFY(secondWindow->scene() == 0);

  delete firstWindow;
  delete secondWindow;
  assert(g_sceneOwnershipScenesAlive == 0);

  printf("==== [testWindowPropsSceneHandoffIsOneShotAcrossCopies] end ====\n");
}

void testWindowRetiresDetachedSceneAtFlushBoundary()
{
  printf("\n==== [testWindowRetiresDetachedSceneAtFlushBoundary] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneOwnershipProbe *first = new SceneOwnershipProbe();
  SceneOwnershipProbe *second = new SceneOwnershipProbe();
  props.scene(first);
  Window *window = new Window(&context, props);
  WindowAdmissionTestApp admission(*window);

  window->sceneManager()->commitTransaction(first, second);
  LOKA_VERIFY(window->scene() == first);
  assert(g_sceneOwnershipScenesAlive == 2);

  admission.flush();
  LOKA_VERIFY(window->scene() == second);
  assert(g_sceneOwnershipScenesAlive == 2);
  admission.flush();
  assert(g_sceneOwnershipScenesAlive == 1);

  SceneOwnershipProbe *third = new SceneOwnershipProbe();
  window->sceneManager()->commitTransaction(second, third);
  LOKA_VERIFY(window->scene() == second);
  assert(g_sceneOwnershipScenesAlive == 2);

  admission.flush();
  LOKA_VERIFY(window->scene() == third);
  SceneOwnershipProbe *fourth = new SceneOwnershipProbe();
  window->sceneManager()->commitTransaction(third, fourth);
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 3);
  // Teardown owns the retired second, applied third, and unapplied fourth.
  delete window;
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);

  printf("==== [testWindowRetiresDetachedSceneAtFlushBoundary] end ====\n");
}

void testWindowRecommittingCurrentSceneIsNoOp()
{
  printf("\n==== [testWindowRecommittingCurrentSceneIsNoOp] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneOwnershipProbe *scene = new SceneOwnershipProbe();
  props.scene(scene);
  Window *window = new Window(&context, props);
  WindowAdmissionTestApp admission(*window);

  loka::app::scene::Scene *installed = window->scene();
  LOKA_VERIFY(installed == scene);

  window->sceneManager()->commitTransaction(scene, scene);

  installed = window->scene();
  LOKA_VERIFY(installed == scene);
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 1);

  admission.flush();

  installed = window->scene();
  const bool attached = installed->getAttachedState()->get();
  Window *owner = installed->getWindow();
  LOKA_VERIFY(installed == scene);
  LOKA_VERIFY(attached);
  LOKA_VERIFY(owner == window);
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 1);

  delete window;
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);

  printf("==== [testWindowRecommittingCurrentSceneIsNoOp] end ====\n");
}

void testWindowDefersSceneRetiredDuringDrainUntilNextFlush()
{
  printf("\n==== [testWindowDefersSceneRetiredDuringDrainUntilNextFlush] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  int scenesAliveAfterNestedFlush = 0;
  WindowCreatingPlatformContext context;
  Window *window = new Window(&context, WindowProps());
  WindowAdmissionTestApp admission(*window);
  SceneOwnershipProbe *second = new SceneOwnershipProbe();
  SceneOwnershipProbe *third = new SceneOwnershipProbe();
  ReentrantSceneRetirementProbe *first =
      new ReentrantSceneRetirementProbe(window, second, third, &scenesAliveAfterNestedFlush);

  window->sceneManager()->commitTransaction(0, first);
  admission.flush();
  window->sceneManager()->commitTransaction(first, second);
  assert(g_sceneOwnershipScenesAlive == 3);

  admission.flush();
  LOKA_VERIFY(window->scene() == second);
  LOKA_VERIFY(scenesAliveAfterNestedFlush == 0);
  admission.flush();
  assert(scenesAliveAfterNestedFlush == 3);
  assert(g_sceneOwnershipScenesAlive == 2);
  LOKA_VERIFY(window->scene() == second);
  LOKA_VERIFY(window->sceneManager()->hasPendingReplacement());
  assert(window->hasPendingSceneInvalidation());

  admission.flush();
  LOKA_VERIFY(window->scene() == third);
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 2);
  admission.flush();
  assert(g_sceneOwnershipScenesAlive == 1);
  assert(!window->hasPendingSceneInvalidation());

  delete window;
  assert(g_sceneOwnershipScenesAlive == 0);

  printf("==== [testWindowDefersSceneRetiredDuringDrainUntilNextFlush] end ====\n");
}

void testWindowDoesNotReclaimSceneDuringDetachNotification()
{
  printf("\n==== [testWindowDoesNotReclaimSceneDuringDetachNotification] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneOwnershipProbe *first = new SceneOwnershipProbe();
  SceneOwnershipProbe *second = new SceneOwnershipProbe();
  props.scene(first);
  Window *window = new Window(&context, props);
  WindowAdmissionTestApp admission(*window);

  ReentrantSceneFlushContext flushContext;
  flushContext.window = window;
  first->getAttachedState()->bind(&FlushWindowDuringDetach, &flushContext, false);

  window->sceneManager()->commitTransaction(first, second);
  LOKA_VERIFY(flushContext.callbackCalls == 0);
  admission.flush();
  assert(flushContext.callbackCalls == 1);
  assert(flushContext.scenesAliveAfterFlush == 2);
  assert(g_sceneOwnershipScenesAlive == 2);

  admission.flush();
  assert(g_sceneOwnershipScenesAlive == 1);
  delete window;
  assert(g_sceneOwnershipScenesAlive == 0);

  printf("==== [testWindowDoesNotReclaimSceneDuringDetachNotification] end ====\n");
}

void testWindowReclaimDoesNotNotifySceneLifecycleObservers()
{
  printf("\n==== [testWindowReclaimDoesNotNotifySceneLifecycleObservers] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneOwnershipProbe *first = new SceneOwnershipProbe();
  SceneOwnershipProbe *second = new SceneOwnershipProbe();
  SceneLifecycleObservation firstObservation(first);
  SceneLifecycleObservation secondObservation(second);
  props.scene(first);

  assert(first->getLifecycleState()->get() == ON_CREATE);
  LOKA_VERIFY(!first->getAttachedState()->get());
  assert(first->getWindow() == 0);
  assert(second->getLifecycleState()->get() == ON_CREATE);
  LOKA_VERIFY(!second->getAttachedState()->get());
  assert(second->getWindow() == 0);
  first->getLifecycleState()->bind(&RecordSceneLifecycleNotification, &firstObservation, false);
  assert(firstObservation.notificationCount == 0);
  Window *window = new Window(&context, props);
  WindowAdmissionTestApp admission(*window);
  firstObservation.managerWindow = window;
  secondObservation.managerWindow = window;
  assert(firstObservation.notificationCount == 1);
  assert(firstObservation.values[0] == ON_ATTACH);
  assert(firstObservation.attachedValues[0]);
  assert(firstObservation.ownerWindows[0] == window);
  assert(firstObservation.managerScenes[0] == first);

  second->getLifecycleState()->bind(&RecordSceneLifecycleNotification, &secondObservation, false);
  assert(secondObservation.notificationCount == 0);
  window->sceneManager()->commitTransaction(first, second);
  admission.flush();
  assert(first->getLifecycleState()->get() == ON_DETACH);
  assert(firstObservation.notificationCount == 2);
  assert(firstObservation.values[1] == ON_DETACH);
  assert(!firstObservation.attachedValues[1]);
  assert(firstObservation.ownerWindows[1] == 0);
  assert(firstObservation.managerScenes[1] == first);
  assert(secondObservation.notificationCount == 1);
  assert(secondObservation.values[0] == ON_ATTACH);
  assert(secondObservation.attachedValues[0]);
  assert(secondObservation.ownerWindows[0] == window);
  assert(secondObservation.managerScenes[0] == second);

  admission.flush();
  assert(g_sceneOwnershipScenesAlive == 1);
  assert(firstObservation.notificationCount == 2);

  delete window;
  assert(g_sceneOwnershipScenesAlive == 0);
  assert(firstObservation.notificationCount == 2);
  assert(secondObservation.notificationCount == 2);
  assert(secondObservation.values[1] == ON_DETACH);
  assert(!secondObservation.attachedValues[1]);
  assert(secondObservation.ownerWindows[1] == 0);
  assert(secondObservation.managerScenes[1] == second);

  printf("==== [testWindowReclaimDoesNotNotifySceneLifecycleObservers] end ====\n");
}

void testWindowReclaimFiresNoSceneCompositionCallbacks()
{
  printf("\n==== [testWindowReclaimFiresNoSceneCompositionCallbacks] start ====\n");

  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneReclaimObservation firstObservation;
  SceneReclaimObservation secondObservation;
  SceneReclaimProbe *first = new SceneReclaimProbe(&firstObservation);
  SceneReclaimProbe *second = new SceneReclaimProbe(&secondObservation);
  SceneReclaimPlatformController firstPlatform(&firstObservation);
  SceneReclaimPlatformController secondPlatform(&secondObservation);
  props.scene(first);
  Window *window = new Window(&context, props);
  WindowAdmissionTestApp admission(*window);

  first->mount(&firstPlatform);
  assert(firstObservation.compositionAttachCalls == 1);
  assert(firstObservation.contextAttachCalls == 1);
  window->sceneManager()->commitTransaction(first, second);
  admission.flush();
  assert(firstObservation.compositionDetachCalls == 1);
  assert(firstObservation.contextDetachCalls == 1);
  assert(firstObservation.platformDestroyCalls == 1);

  admission.flush();
  assert(g_sceneOwnershipScenesAlive == 1);
  assert(firstObservation.compositionDetachCalls == 1);
  assert(firstObservation.contextDetachCalls == 1);
  assert(firstObservation.platformDestroyCalls == 1);

  second->mount(&secondPlatform);
  assert(secondObservation.compositionAttachCalls == 1);
  assert(secondObservation.contextAttachCalls == 1);
  delete window;
  assert(g_sceneOwnershipScenesAlive == 0);
  assert(secondObservation.compositionDetachCalls == 1);
  assert(secondObservation.contextDetachCalls == 1);
  assert(secondObservation.platformDestroyCalls == 1);

  printf("==== [testWindowReclaimFiresNoSceneCompositionCallbacks] end ====\n");
}

void testAppDefersWindowReclaimUntilInvalidationFlush()
{
  printf("\n==== [testAppDefersWindowReclaimUntilInvalidationFlush] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  assert(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneOwnershipProbe *scene = new SceneOwnershipProbe();
  props.scene(scene);
  WindowRetirementProbe *window = new WindowRetirementProbe(&context, props);
  WindowRetirementTestApp app;
  app.install(window);
  assert(app.applyMenuCalls == 0);
  assert(app.appliedWindow == 0);

  WindowRetirementNotification notification;
  notification.app = &app;
  notification.window = window;
  notification.scene = scene;
  loka::core::EmitterState close;
  close.bind(&RequestWindowCloseDuringNotification, &notification, false);
  close.emit();

  assert(notification.calls == 1);
  assert(g_windowRetirementWindowsAlive == 1);
  assert(g_sceneOwnershipScenesAlive == 1);
  assert(app.activeWindow() == 0); // loka-assert-ok: activeWindow is a pure reader
  assert(app.appliedWindow == 0);
  assert(app.applyMenuCalls == 0);
  assert(app.quitCalls == 1);

  app.flush();
  assert(app.applyMenuCalls == 1);
  assert(app.reclaimCalls == 1);
  assert(g_windowRetirementWindowsAlive == 0);
  assert(g_sceneOwnershipScenesAlive == 0);

  app.flush();
  assert(app.reclaimCalls == 1);

  printf("==== [testAppDefersWindowReclaimUntilInvalidationFlush] end ====\n");
}

void testAppDefersReentrantWindowCloseRequestUntilNextFlush()
{
  printf("\n==== [testAppDefersReentrantWindowCloseRequestUntilNextFlush] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  WindowCreatingPlatformContext context;
  WindowRetirementProbe *first = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementProbe *second = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementTestApp app;
  app.install(first, second);
  assert(app.applyMenuCalls == 0);
  app.closeDuringReclaim = second;

  app.requestWindowClose(first);
  assert(app.activeWindow() == second); // loka-assert-ok: activeWindow is a pure reader
  assert(app.appliedWindow == 0);
  assert(app.applyMenuCalls == 0);
  assert(g_windowRetirementWindowsAlive == 2);

  app.flush();
  assert(app.reclaimCalls == 1);
  assert(app.windowsAliveDuringNestedFlush == 2);
  assert(g_windowRetirementWindowsAlive == 1);
  assert(app.activeWindow() == 0); // loka-assert-ok: activeWindow is a pure reader
  assert(app.appliedWindow == second);
  assert(app.applyMenuCalls == 1);
  assert(app.quitCalls == 1);

  app.flush();
  assert(app.reclaimCalls == 2);
  assert(g_windowRetirementWindowsAlive == 0);

  printf("==== [testAppDefersReentrantWindowCloseRequestUntilNextFlush] end ====\n");
}

void testAppReclaimsWindowCloseBatchInRequestOrder()
{
  printf("\n==== [testAppReclaimsWindowCloseBatchInRequestOrder] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  WindowCreatingPlatformContext context;
  WindowRetirementProbe *first = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementProbe *second = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementProbe *third = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementTestApp app;
  app.install(first, second, third);

  app.requestWindowClose(second);
  app.requestWindowClose(first);
  app.requestWindowClose(third);
  assert(g_windowRetirementWindowsAlive == 3);
  assert(app.quitCalls == 1);

  app.flush();
  assert(app.reclaimCalls == 3);
  assert(app.reclaimOrder[0] == second);
  assert(app.reclaimOrder[1] == first);
  assert(app.reclaimOrder[2] == third);
  assert(g_windowRetirementWindowsAlive == 0);

  printf("==== [testAppReclaimsWindowCloseBatchInRequestOrder] end ====\n");
}

void testAppWindowReclaimDrainsRetiredScenesExactlyOnce()
{
  printf("\n==== [testAppWindowReclaimDrainsRetiredScenesExactlyOnce] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  assert(g_sceneOwnershipScenesAlive == 0);
  int retiredSceneDestructions = 0;
  int currentSceneDestructions = 0;
  WindowCreatingPlatformContext context;
  WindowProps props;
  SceneRetirementCountProbe *retired = new SceneRetirementCountProbe(&retiredSceneDestructions);
  SceneRetirementCountProbe *current = new SceneRetirementCountProbe(&currentSceneDestructions);
  props.scene(retired);
  WindowRetirementProbe *window = new WindowRetirementProbe(&context, props);
  window->sceneManager()->commitTransaction(retired, current);
  WindowRetirementTestApp app;
  app.install(window);
  app.flush();

  LOKA_VERIFY(window->sceneManager()->hasRetiredScenes());
  app.requestWindowClose(window);
  assert(retiredSceneDestructions == 0);
  assert(currentSceneDestructions == 0);
  assert(g_sceneOwnershipScenesAlive == 2);

  app.flush();
  assert(retiredSceneDestructions == 1);
  assert(currentSceneDestructions == 1);
  assert(g_sceneOwnershipScenesAlive == 0);
  assert(app.reclaimCalls == 1);

  app.flush();
  assert(retiredSceneDestructions == 1);
  assert(currentSceneDestructions == 1);
  assert(app.reclaimCalls == 1);

  printf("==== [testAppWindowReclaimDrainsRetiredScenesExactlyOnce] end ====\n");
}

void testAppWindowCloseRequestsAreIdempotent()
{
  printf("\n==== [testAppWindowCloseRequestsAreIdempotent] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  WindowCreatingPlatformContext context;
  WindowRetirementProbe *owned = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementProbe *foreign = new WindowRetirementProbe(&context, WindowProps());
  WindowRetirementTestApp app;
  app.install(owned);

  app.requestWindowClose(foreign);
  app.flush();
  assert(app.activeWindow() == owned); // loka-assert-ok: activeWindow is a pure reader
  assert(app.appliedWindow == owned);
  assert(app.applyMenuCalls == 1);
  assert(app.quitCalls == 0);
  assert(app.reclaimCalls == 0);
  assert(g_windowRetirementWindowsAlive == 2);

  app.requestWindowClose(owned);
  app.requestWindowClose(owned);
  app.flush();
  assert(app.reclaimCalls == 1);
  assert(app.reclaimOrder[0] == owned);
  assert(app.quitCalls == 1);
  assert(g_windowRetirementWindowsAlive == 1);

  app.flush();
  assert(app.reclaimCalls == 1);
  delete foreign;
  assert(g_windowRetirementWindowsAlive == 0);

  printf("==== [testAppWindowCloseRequestsAreIdempotent] end ====\n");
}

void testAppWindowClosedRejectsUndetachedOrActiveWindow()
{
  printf("\n==== [testAppWindowClosedRejectsUndetachedOrActiveWindow] start ====\n");

#if defined(LOKA_LIFECYCLE_AUDIT) && defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  AssertWindowClosedMisuseAborts(WINDOW_CLOSED_WITHOUT_DETACH);
  AssertWindowClosedMisuseAborts(WINDOW_CLOSED_WHILE_ACTIVE);
#endif

  printf("==== [testAppWindowClosedRejectsUndetachedOrActiveWindow] end ====\n");
}

void testAppDrainsPendingWindowClosuresAtDestruction()
{
  printf("\n==== [testAppDrainsPendingWindowClosuresAtDestruction] start ====\n");

  assert(g_windowRetirementWindowsAlive == 0);
  WindowCreatingPlatformContext context;
  {
    WindowRetirementProbe *window = new WindowRetirementProbe(&context, WindowProps());
    WindowRetirementTestApp app;
    app.install(window);
    app.requestWindowClose(window);
    assert(g_windowRetirementWindowsAlive == 1);
  }
  assert(g_windowRetirementWindowsAlive == 0);

  printf("==== [testAppDrainsPendingWindowClosuresAtDestruction] end ====\n");
}

namespace
{
  class SceneCensusRoot;
  struct SceneCensusTypeTag {};
  struct SceneCensusProps : public loka::app::scene::NodePropsBase<SceneCensusProps>
  {
    typedef SceneCensusTypeTag TypeTag;
    typedef SceneCensusRoot NodeType;
    explicit SceneCensusProps(const char *value = "Census") : label(value) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->label < static_cast<const SceneCensusProps &>(rhs).label;
    }
    const char *label;
  };

  /** A mounted root with a real owner-scoped UI callback to census. */
  class SceneCensusRoot : public loka::app::scene::StdCompositionBoundaryNodeBase<SceneCensusProps>
  {
  public:
    typedef SceneCensusTypeTag TypeTag;
    explicit SceneCensusRoot(const SceneCensusProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<SceneCensusProps>(props) {}
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->scene()->getAttachedState(), this, &SceneCensusRoot::observeAttachment);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(static_cast<const loka::app::scene::NodeDefinitionBase &>(
          loka::app::Button(this->props.label)));
    }
    void observeAttachment() {}
  };
  typedef loka::app::scene::BoundaryDefinition<SceneCensusProps, SceneCensusRoot> SceneCensusDefinition;

  class SceneCensusProbe : public loka::app::scene::Scene
  {
  public:
    explicit SceneCensusProbe(const char *label)
        : loka::app::scene::Scene(SceneCensusDefinition(SceneCensusProps(label)))
    {
      ++g_sceneOwnershipScenesAlive;
    }
    virtual ~SceneCensusProbe() { --g_sceneOwnershipScenesAlive; }
  };

  size_t SceneRootUiCallbackCount(const loka::app::scene::Scene &scene)
  {
    loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(scene);
    // Mounted replacements retain a live native projection and root callbacks.
    return root ? loka::app::scene::ComposableNodeTestAccess::uiCallbackCount(
                      *root->asComposable()) : 0;
  }

  struct SceneRoundTripCensus
  {
    explicit SceneRoundTripCensus(NullWindow &window)
        : scenes(g_sceneOwnershipScenesAlive),
          handles(window.scenePlatformController()->createdCount() -
                  window.scenePlatformController()->disposedCount()),
          callbacks(SceneRootUiCallbackCount(*window.scene()))
    {
    }
    const int scenes;
    const unsigned long handles;
    const size_t callbacks;
  };

  void RunSceneCensusRoundTrip(NullWindow *window, WindowAdmissionTestApp &admission)
  {
    window->sceneManager()->commitTransaction(window->scene(), new SceneCensusProbe("Two"));
    admission.flush();
    admission.flush();
    window->sceneManager()->commitTransaction(window->scene(), new SceneCensusProbe("One"));
    admission.flush();
    admission.flush();
  }

  void VerifySceneCensusIntake(const loka::core::PushStateTracker &tracker)
  {
    typedef loka::core::testing::PushStateTrackerTestAccess Access;
    LOKA_VERIFY(Access::nextDirtyCount(tracker) == 0);
    LOKA_VERIFY(Access::nextDeferredCount(tracker) == 0);
  }
}

void testSceneReplacementRoundTripReturnsCensusToBaseline()
{
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new SceneCensusProbe("One"));
  NullWindow *window = new NullWindow(&context, props);
  WindowAdmissionTestApp admission(*window);
  admission.flush();
  LOKA_VERIFY(window->scenePlatformController()->createdCount() > 0);
  RunSceneCensusRoundTrip(window, admission);
  const SceneRoundTripCensus baseline(*window);
  printf("P1 baseline: scenes=%d handles=%lu callbacks=%lu retired=%lu window current=%lu next=%lu deferred=%lu\n",
         baseline.scenes, baseline.handles, static_cast<unsigned long>(baseline.callbacks),
         static_cast<unsigned long>(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager())),
         static_cast<unsigned long>(loka::core::testing::PushStateTrackerTestAccess::currentDirtyCount(*window->getTracker()->asPushTracker())),
         static_cast<unsigned long>(loka::core::testing::PushStateTrackerTestAccess::nextDirtyCount(*window->getTracker()->asPushTracker())),
         static_cast<unsigned long>(loka::core::testing::PushStateTrackerTestAccess::nextDeferredCount(*window->getTracker()->asPushTracker())));
  for (int round = 1; round < 8; ++round)
  {
    RunSceneCensusRoundTrip(window, admission);
    const SceneRoundTripCensus actual(*window);
    LOKA_VERIFY(actual.scenes == baseline.scenes);
    LOKA_VERIFY(actual.handles == baseline.handles);
    LOKA_VERIFY(actual.callbacks == baseline.callbacks);
    LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
    VerifySceneCensusIntake(*window->getTracker()->asPushTracker());
  }
  delete window;
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
}

namespace
{
  struct ReplacementLifecycleCount
  {
    explicit ReplacementLifecycleCount(loka::app::scene::Scene *value)
        : scene(value), attaches(0), detaches(0) {}
    static void record(void *data)
    {
      ReplacementLifecycleCount *count = static_cast<ReplacementLifecycleCount *>(data);
      if (count->scene->getLifecycleState()->get() == ON_ATTACH)
        ++count->attaches;
      if (count->scene->getLifecycleState()->get() == ON_DETACH)
      {
        ++count->detaches;
        count->detachClocks.push_back(loka::core::Operation::hasActive());
      }
    }
    loka::app::scene::Scene *scene;
    int attaches;
    int detaches;
    std::vector<bool> detachClocks;
  };

  /** Stack-owned callback inputs; all referenced scenes belong to the Window. */
  struct ReplacementAdoptions
  {
    ReplacementAdoptions(Window &owner, loka::app::scene::Scene *first,
                         loka::app::scene::Scene *last)
        : window(owner), intermediate(first), finalScene(last) {}
    static void adopt(void *data)
    {
      ReplacementAdoptions *request = static_cast<ReplacementAdoptions *>(data);
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->intermediate));
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->finalScene));
    }
    Window &window;
    loka::app::scene::Scene *intermediate;
    loka::app::scene::Scene *finalScene;
  };

  void VerifySupersededReplacement(bool returnToApplied)
  {
    LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
    WindowCreatingPlatformContext context;
    SceneCensusProbe *first = new SceneCensusProbe("A");
    ReplacementLifecycleCount firstCount(first);
    ReplacementLifecycleCount middleCount(0);
    first->getLifecycleState()->bind(&ReplacementLifecycleCount::record, &firstCount, false);
    WindowProps props;
    props.scene(first);
    NullWindow *window = new NullWindow(&context, props);
    WindowAdmissionTestApp admission(*window);
    admission.flush();
    const SceneRoundTripCensus baseline(*window);
    SceneCensusProbe *middle = new SceneCensusProbe("B");
    middleCount.scene = middle;
    middle->getLifecycleState()->bind(&ReplacementLifecycleCount::record, &middleCount, false);
    loka::app::scene::Scene *last = returnToApplied ? first : new SceneCensusProbe("C");
    ReplacementAdoptions request(*window, middle, last);
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<int> pulse(0);
    tracker.addState(&pulse);
    pulse.bind(&ReplacementAdoptions::adopt, &request, false);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      pulse.set(1);
    }
    LOKA_VERIFY(window->scene() == first);
    LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::desiredScene(*window->sceneManager()) == last);
    LOKA_VERIFY(first->getAttachedState()->get());
    LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 1);
    LOKA_VERIFY(!window->sceneManager()->commitTransaction(0, middle));
    LOKA_VERIFY(middleCount.attaches == 0);
    LOKA_VERIFY(window->sceneManager()->hasPendingReplacement() == !returnToApplied);
    admission.flush();
    LOKA_VERIFY(window->scene() == last);
    LOKA_VERIFY(middleCount.attaches == 0);
    LOKA_VERIFY(middleCount.detaches == 0);
    LOKA_VERIFY(firstCount.attaches == 1);
    LOKA_VERIFY(firstCount.detaches == (returnToApplied ? 0 : 1));
    admission.flush();
    const SceneRoundTripCensus actual(*window);
    LOKA_VERIFY(actual.scenes == baseline.scenes);
    LOKA_VERIFY(actual.handles == baseline.handles);
    LOKA_VERIFY(actual.callbacks == baseline.callbacks);
    LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
    delete window;
    LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
  }

  class ReplacementRefusableRoot;
  struct ReplacementRefusableTypeTag {};
  struct ReplacementRefusableProps : public loka::app::scene::NodePropsBase<ReplacementRefusableProps>
  {
    typedef ReplacementRefusableTypeTag TypeTag;
    typedef ReplacementRefusableRoot NodeType;
    explicit ReplacementRefusableProps(const bool *value = 0) : refusal(value) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->refusal < static_cast<const ReplacementRefusableProps &>(rhs).refusal;
    }
    const bool *refusal;
  };
  class ReplacementRefusableRoot
      : public loka::app::scene::StdCompositionBoundaryNodeBase<ReplacementRefusableProps>
  {
  public:
    typedef ReplacementRefusableTypeTag TypeTag;
    explicit ReplacementRefusableRoot(const ReplacementRefusableProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<ReplacementRefusableProps>(props) {}
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->scene()->getAttachedState(), this, &ReplacementRefusableRoot::observeAttachment);
    }
    void observeAttachment() {}
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->props.refusal && *this->props.refusal)
      {
        this->noteComposeAllocationFailure();
        return;
      }
      composition.declare(static_cast<const loka::app::scene::NodeDefinitionBase &>(loka::app::Button("Prepared")));
    }
  };
  typedef loka::app::scene::BoundaryDefinition<ReplacementRefusableProps, ReplacementRefusableRoot>
      ReplacementRefusableDefinition;

  /** Records newer seat intent during the candidate's refused preparation. */
  class ReplacementRearmingRefusableRoot : public ReplacementRefusableRoot
  {
  public:
    explicit ReplacementRearmingRefusableRoot(const ReplacementRefusableProps &props)
        : ReplacementRefusableRoot(props) {}
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->props.refusal && *this->props.refusal)
        this->scene()->getWindow()->sceneManager()->requestRearm();
      ReplacementRefusableRoot::composeNode(composition);
    }
  };

  void VerifyInitialPrepareRefusalPreservesRequest(bool newerRearm)
  {
    typedef loka::app::testing::SceneManagerTestAccess SeatAccess;
    typedef loka::dsl::testing::SceneTestAccess SceneAccess;
    WindowCreatingPlatformContext context;
    bool refusal = true;
    NullWindow window(&context, WindowProps());
    WindowAdmissionTestApp admission(window);
    LOKA_VERIFY(window.scene() == 0);
    loka::app::scene::Scene *candidate = newerRearm
        ? new loka::app::scene::Scene(new loka::app::scene::BoundaryDefinition<
              ReplacementRefusableProps, ReplacementRearmingRefusableRoot>(ReplacementRefusableProps(&refusal)))
        : new loka::app::scene::Scene(new ReplacementRefusableDefinition(ReplacementRefusableProps(&refusal)));
    LOKA_VERIFY(window.sceneManager()->commitTransaction(0, candidate));
    window.sceneManager()->requestDetach();
    admission.flush();
    const bool refused = SeatAccess::lastPrepareRefusal(*window.sceneManager()) == candidate;
    const bool pending = SeatAccess::hasPendingRequest(*window.sceneManager());
    printf("Initial prepare refusal: newerRearm=%d refused=%d installed=%d requestPending=%d\n",
           newerRearm, refused, window.scene() != 0, pending);
    LOKA_VERIFY(refused);
    LOKA_VERIFY(window.scene() == 0);
    // Before the fix, instrumentation reported a refused candidate and an
    // empty seat, but no pending detach: clearing the snapshot lost the intent.
    LOKA_VERIFY(pending);
    LOKA_VERIFY(SeatAccess::desiredScene(*window.sceneManager()) == candidate);

    refusal = false;
    admission.flush();
    printf("Initial prepare retry: newerRearm=%d installed=%d attached=%d requestPending=%d\n",
           newerRearm, window.scene() == candidate, candidate->getAttachedState()->get(),
           SeatAccess::hasPendingRequest(*window.sceneManager()));
    LOKA_VERIFY(window.scene() == candidate);
    LOKA_VERIFY(candidate->getAttachedState()->get() == newerRearm);
    LOKA_VERIFY((SceneAccess::rootNode(*candidate) != 0) == newerRearm);
    LOKA_VERIFY(!window.sceneManager()->hasPendingWork());
  }

  struct AttachReplacementRequest
  {
    AttachReplacementRequest(Window &owner, loka::app::scene::Scene *candidate,
                             loka::app::scene::Scene *following)
        : window(owner), applying(candidate), next(following), calls(0) {}
    static void adopt(void *data)
    {
      AttachReplacementRequest *request = static_cast<AttachReplacementRequest *>(data);
      if (request->applying->getLifecycleState()->get() != ON_ATTACH)
        return;
      ++request->calls;
      LOKA_VERIFY(request->window.scene() == request->applying);
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->next));
      // An applying identity can be adopted again without retiring it.
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->applying));
      LOKA_VERIFY(!request->window.sceneManager()->commitTransaction(0, request->next));
      request->next = new SceneCensusProbe("C");
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->next));
      LOKA_VERIFY(!request->window.flushSceneInvalidation());
      LOKA_VERIFY(request->window.scene() == request->applying);
    }
    Window &window;
    loka::app::scene::Scene *applying;
    loka::app::scene::Scene *next;
    int calls;
  };
}

void testSceneReplacementSupersedesUnattachedDesiredScene()
{
  VerifySupersededReplacement(false);
}

void testSceneReplacementReturnsToAppliedWithoutDetach()
{
  VerifySupersededReplacement(true);
}

void testEmptyWindowConsumesSceneRequestsWithoutReadmission()
{
  class AdmissionObservedWindow : public NullWindow
  {
  public:
    explicit AdmissionObservedWindow(PlatformContext *context)
        : NullWindow(context, WindowProps()), flushCalls_(0) {}
    virtual void drainNativeRetirements()
    {
      // Window::flushSceneInvalidation calls this exactly once, even without a Scene.
      ++this->flushCalls_;
      NullWindow::drainNativeRetirements();
    }
    int flushCalls() const { return this->flushCalls_; }

  private:
    int flushCalls_;
  };

  for (int requests = 1; requests <= 3; ++requests)
  {
    WindowCreatingPlatformContext context;
    AdmissionObservedWindow window(&context);
    WindowAdmissionTestApp admission(window);
    LOKA_VERIFY(window.scene() == 0);
    LOKA_VERIFY(!window.sceneManager()->hasPendingWork());
    admission.flush();
    LOKA_VERIFY(window.flushCalls() == 0);

    if (requests & 1)
      window.sceneManager()->requestDetach();
    if (requests & 2)
      window.sceneManager()->requestRearm();
    LOKA_VERIFY(window.sceneManager()->hasPendingWork());
    admission.flush();
    const bool pending = window.sceneManager()->hasPendingWork();
    const int firstFlushCalls = window.flushCalls();
    admission.flushWindowInvalidations();
    printf("Empty seat requests=%d: pending=%d firstFlushCalls=%d totalFlushCalls=%d\n",
           requests, pending, firstFlushCalls, window.flushCalls());
    LOKA_VERIFY(!pending);
    LOKA_VERIFY(firstFlushCalls == 1);
    LOKA_VERIFY(window.flushCalls() == 1);
    LOKA_VERIFY(!window.sceneManager()->hasPendingWork());
    LOKA_VERIFY(window.scene() == 0);
  }
}

void testInitialScenePrepareRefusalPreservesDetachRequest()
{
  VerifyInitialPrepareRefusalPreservesRequest(false);
}

void testInitialScenePrepareRefusalPreservesNewerRearmRequest()
{
  VerifyInitialPrepareRefusalPreservesRequest(true);
}

void testSceneReplacementPreservesAppliedOnPrepareRefusal()
{
  WindowCreatingPlatformContext context;
  bool refusal = true;
  ReplacementLifecycleCount oldCount(0);
  WindowProps props;
  props.scene(new SceneCensusProbe("A"));
  NullWindow *window = new NullWindow(&context, props);
  WindowAdmissionTestApp admission(*window);
  oldCount.scene = window->scene();
  oldCount.scene->getLifecycleState()->bind(&ReplacementLifecycleCount::record, &oldCount, false);
  admission.flush();
  const SceneRoundTripCensus baseline(*window);
  loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*window->scene());
  loka::app::scene::Scene *next = new loka::app::scene::Scene(new ReplacementRefusableDefinition(ReplacementRefusableProps(&refusal)));
  LOKA_VERIFY(window->sceneManager()->commitTransaction(0, next));
  const unsigned long projections = window->scenePlatformController()->onChangeCallCount();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == 0);
  admission.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == next);
  LOKA_VERIFY(window->scene() == oldCount.scene);
  LOKA_VERIFY(window->scene()->getAttachedState()->get());
  LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*window->scene()) == root);
  LOKA_VERIFY(oldCount.detaches == 0);
  LOKA_VERIFY(window->sceneManager()->hasPendingReplacement());
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
  LOKA_VERIFY(window->scenePlatformController()->onChangeCallCount() == projections);
  const SceneRoundTripCensus afterRefusal(*window);
  LOKA_VERIFY(afterRefusal.handles == baseline.handles);
  // A repeated admission records the same still-owned refusal and preserves A.
  admission.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == next);
  LOKA_VERIFY(window->scene() == oldCount.scene);
  LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*window->scene()) == root);
  refusal = false;
  admission.operationLoop();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == 0);
  LOKA_VERIFY(window->scene() == next);
  LOKA_VERIFY(next->getAttachedState()->get());
  LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*next) != 0);
  LOKA_VERIFY(!window->sceneManager()->hasPendingReplacement());
  LOKA_VERIFY(oldCount.detaches == 1);
  LOKA_VERIFY(oldCount.detachClocks.size() == 1 && oldCount.detachClocks[0]);
  admission.flush();
  LOKA_VERIFY(oldCount.detaches == 1);
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);

  // Supersession moves the refused identity into the pool; the observation
  // remains valid there and clears at admission before that identity is reclaimed.
  refusal = true;
  loka::app::scene::Scene *superseded = new loka::app::scene::Scene(
      new ReplacementRefusableDefinition(ReplacementRefusableProps(&refusal)));
  LOKA_VERIFY(window->sceneManager()->commitTransaction(0, superseded));
  admission.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == superseded);
  LOKA_VERIFY(window->sceneManager()->commitTransaction(0, next));
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == superseded);
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 1);
  admission.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::lastPrepareRefusal(*window->sceneManager()) == 0);
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
  LOKA_VERIFY(window->scene() == next);
  delete window;
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
}

namespace
{
  /** Reuses the census root's props and projection; its immediate ATTACH
      binding writes visibility while replacement preparation borrows the rail. */
  class HideWindowOnAttachRoot : public SceneCensusRoot
  {
  public:
    explicit HideWindowOnAttachRoot(const SceneCensusProps &props) : SceneCensusRoot(props) {}
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->scene()->getAttachedState(), this,
                  &HideWindowOnAttachRoot::hideWindow, true);
    }
    void hideWindow()
    {
      if (this->scene()->getAttachedState()->get())
        return;
      Window *window = this->scene()->getWindow();
      LOKA_VERIFY(window != 0);
      loka::core::StateTrackerGuard guard(window->getTracker());
      window->visibilityState().set(false);
    }
  };
  typedef loka::app::scene::BoundaryDefinition<SceneCensusProps, HideWindowOnAttachRoot>
      HideWindowOnAttachDefinition;

}

namespace
{
  class LoseRailDuringPrepareRoot;
  struct LoseRailDuringPrepareTypeTag {};
  struct LoseRailDuringPrepareProps : public loka::app::scene::NodePropsBase<LoseRailDuringPrepareProps>
  {
    typedef LoseRailDuringPrepareTypeTag TypeTag;
    typedef LoseRailDuringPrepareRoot NodeType;
    explicit LoseRailDuringPrepareProps(const bool *enabled = 0) : lossEnabled(enabled) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->lossEnabled < static_cast<const LoseRailDuringPrepareProps &>(rhs).lossEnabled;
    }
    const bool *lossEnabled;
  };

  /** Deliberate rail loss during preparation, independently of visibility. */
  class LoseRailDuringPrepareRoot
      : public loka::app::scene::StdCompositionBoundaryNodeBase<LoseRailDuringPrepareProps>
  {
  public:
    typedef LoseRailDuringPrepareTypeTag TypeTag;
    explicit LoseRailDuringPrepareRoot(const LoseRailDuringPrepareProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<LoseRailDuringPrepareProps>(props) {}
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->scene()->getAttachedState(), this,
                  &LoseRailDuringPrepareRoot::loseRail, true);
    }
    void loseRail()
    {
      if (!this->props.lossEnabled || !*this->props.lossEnabled ||
          this->scene()->getAttachedState()->get())
        return;
      NullWindow *window = static_cast<NullWindow *>(this->scene()->getWindow());
      LOKA_VERIFY(window != 0);
      LOKA_VERIFY(window->scene() != this->scene());
      LOKA_VERIFY(window->scenePlatformController() != 0);
      window->destroyScenePlatform();
      LOKA_VERIFY(window->scenePlatformController() == 0);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(static_cast<const loka::app::scene::NodeDefinitionBase &>(
          loka::app::Button("Prepared after rail loss")));
    }
  };
  typedef loka::app::scene::BoundaryDefinition<LoseRailDuringPrepareProps, LoseRailDuringPrepareRoot>
      LoseRailDuringPrepareDefinition;
}

void testSceneReplacementRefusesControllerLostDuringPrepare()
{
  typedef loka::app::testing::SceneManagerTestAccess SeatAccess;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  WindowCreatingPlatformContext context;
  bool lossEnabled = true;
  WindowProps props;
  props.scene(new SceneCensusProbe("Applied"));
  NullWindow window(&context, props);
  WindowAdmissionTestApp admission(window);
  admission.flush();
  loka::app::scene::Scene *applied = window.scene();
  loka::app::scene::Scene *candidate = new loka::app::scene::Scene(
      LoseRailDuringPrepareDefinition(LoseRailDuringPrepareProps(&lossEnabled)));
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, candidate));
  admission.flush();
  LOKA_VERIFY(window.visibilityState().get());
  LOKA_VERIFY(window.scenePlatformController() == 0);
  LOKA_VERIFY(SeatAccess::lastPrepareRefusal(*window.sceneManager()) == candidate);
  LOKA_VERIFY(window.scene() == applied);
  LOKA_VERIFY(applied->getAttachedState()->get());
  LOKA_VERIFY(SceneAccess::rootNode(*applied) == 0);
  LOKA_VERIFY(SceneAccess::platformController(*applied) == 0);
  LOKA_VERIFY(SeatAccess::desiredScene(*window.sceneManager()) == candidate);
  LOKA_VERIFY(window.sceneManager()->hasPendingReplacement());
  LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
  const bool candidateClean = candidate->getWindow() == 0 &&
      !candidate->getAttachedState()->get() && !SceneAccess::composed(*candidate) &&
      SceneAccess::rootNode(*candidate) == 0 && SceneAccess::platformController(*candidate) == 0;
  LOKA_VERIFY(candidateClean);
  printf("Rail loss refusal: candidate refused, applied scene preserved, candidate borrow cleared\n");

  lossEnabled = false;
  admission.flush();
  LOKA_VERIFY(window.scenePlatformController() != 0);
  LOKA_VERIFY(SeatAccess::lastPrepareRefusal(*window.sceneManager()) == 0);
  LOKA_VERIFY(window.scene() == candidate);
  LOKA_VERIFY(candidate->getAttachedState()->get());
  LOKA_VERIFY(SceneAccess::rootNode(*candidate) != 0);
  LOKA_VERIFY(SceneAccess::platformController(*candidate) == window.scenePlatformController());
  LOKA_VERIFY(!window.sceneManager()->hasPendingReplacement());
  admission.flush();
  LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
  printf("Rail loss retry: recreated rail, candidate installed, retirement settled\n");
}

void testSceneReplacementHideDuringAttachWaitsForNextAdmission()
{
  // Replaces testSceneReplacementRefusesControllerLostDuringAttach (#668):
  // synchronous visibility teardown lost the candidate's borrowed rail during
  // prepare. Close intent now preserves that rail until the next App admission.
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new SceneCensusProbe("Applied"));
  NullWindow window(&context, props);
  WindowAdmissionTestApp admission(window);
  admission.flush();
  NullScenePlatformController *controller = window.scenePlatformController();
  loka::app::scene::Scene *candidate = new loka::app::scene::Scene(
      HideWindowOnAttachDefinition(SceneCensusProps("Candidate")));
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, candidate));
  admission.flush();
  typedef loka::app::testing::SceneManagerTestAccess SeatAccess;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  LOKA_VERIFY(!window.visibilityState().get());
  LOKA_VERIFY(window.scenePlatformController() == controller);
  LOKA_VERIFY(SeatAccess::lastPrepareRefusal(*window.sceneManager()) == 0);
  LOKA_VERIFY(window.scene() == candidate);
  LOKA_VERIFY(SceneAccess::rootNode(*candidate) != 0);
  LOKA_VERIFY(!window.sceneManager()->hasPendingReplacement());
  admission.flush();
  LOKA_VERIFY(window.scenePlatformController() == 0);
  LOKA_VERIFY(SceneAccess::rootNode(*candidate) == 0);
  LOKA_VERIFY(SceneAccess::platformController(*candidate) == 0);
  LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(true);
  }
  LOKA_VERIFY(window.scenePlatformController() == 0);
  admission.flush();
  LOKA_VERIFY(window.scenePlatformController() != 0);
  LOKA_VERIFY(window.scene() == candidate);
  LOKA_VERIFY(SceneAccess::rootNode(*candidate) != 0);
  LOKA_VERIFY(SceneAccess::platformController(*candidate) == window.scenePlatformController());
  admission.flush();
  LOKA_VERIFY(window.visibilityState().get());
}

void testSceneReplacementAdoptedDuringAttachWaitsForNextAdmission()
{
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new SceneCensusProbe("A"));
  NullWindow *window = new NullWindow(&context, props);
  WindowRetirementTestApp app;
  app.install(window);
  app.flush();
  const SceneRoundTripCensus baseline(*window);
  SceneCensusProbe *second = new SceneCensusProbe("B");
  AttachReplacementRequest request(*window, second, new SceneCensusProbe("superseded"));
  second->getLifecycleState()->bind(&AttachReplacementRequest::adopt, &request, false);
  LOKA_VERIFY(window->sceneManager()->commitTransaction(0, second));
  app.flush();
  LOKA_VERIFY(request.calls == 1);
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 2);
  LOKA_VERIFY(window->scene() == second);
  LOKA_VERIFY(window->sceneManager()->hasPendingReplacement());
  app.flush();
  LOKA_VERIFY(window->scene() == request.next);
  LOKA_VERIFY(!window->sceneManager()->hasPendingReplacement());
  app.flush();
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
  const SceneRoundTripCensus actual(*window);
  LOKA_VERIFY(actual.handles == baseline.handles);
  LOKA_VERIFY(actual.callbacks == baseline.callbacks);
  app.requestWindowClose(window);
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
}

namespace
{
  struct ReadoptOutgoingRequest
  {
    ReadoptOutgoingRequest(Window &owner, loka::app::scene::Scene *scene)
        : window(owner), outgoing(scene), calls(0) {}
    static void adopt(void *data)
    {
      ReadoptOutgoingRequest *request = static_cast<ReadoptOutgoingRequest *>(data);
      if (request->outgoing->getAttachedState()->get())
        return;
      ++request->calls;
      LOKA_VERIFY(request->window.sceneManager()->commitTransaction(0, request->outgoing));
      LOKA_VERIFY(!request->window.flushSceneInvalidation());
    }
    Window &window;
    loka::app::scene::Scene *outgoing;
    int calls;
  };
}

// Outgoing-readoption contract: adoption during its own detach keeps A as the
// detached desired scene, outside retirement. B finishes installing; the next
// App admission remounts A with a fresh root generation, then retires B.
void testSceneReplacementReadoptsOutgoingDuringDetach()
{
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new SceneCensusProbe("A"));
  NullWindow *window = new NullWindow(&context, props);
  WindowAdmissionTestApp admission(*window);
  admission.flush();
  loka::app::scene::Scene *first = window->scene();
  ReadoptOutgoingRequest request(*window, first);
  first->getAttachedState()->bind(&ReadoptOutgoingRequest::adopt, &request, false);
  SceneCensusProbe *second = new SceneCensusProbe("B");
  LOKA_VERIFY(window->sceneManager()->commitTransaction(0, second));
  admission.flush();
  LOKA_VERIFY(request.calls == 1);
  LOKA_VERIFY(window->scene() == second);
  LOKA_VERIFY(window->sceneManager()->hasPendingReplacement());
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 0);
  LOKA_VERIFY(!first->getAttachedState()->get());
  admission.flush();
  LOKA_VERIFY(window->scene() == first);
  LOKA_VERIFY(first->getAttachedState()->get());
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window->sceneManager()) == 1);
  admission.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 1);
  first->getAttachedState()->unbind(&ReadoptOutgoingRequest::adopt, &request);
  delete window;
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
}

namespace
{
  /** The App close queue owns a removed Window until the following admission. */
  class CloseWindowDuringApplyController : public NullScenePlatformController
  {
  public:
    explicit CloseWindowDuringApplyController(WindowRetirementTestApp &app) : app_(app) {}
    virtual void beginApplyCycle()
    {
      Window *closing = this->app_.activeWindow();
      loka::app::scene::Scene *scene = closing->scene();
      loka::app::scene::Node *root = loka::dsl::testing::SceneTestAccess::rootNode(*scene);
      this->app_.requestWindowClose(closing);
      this->app_.flush();
      LOKA_VERIFY(this->app_.reclaimCalls == 0);
      LOKA_VERIFY(g_sceneOwnershipScenesAlive == 2);
      LOKA_VERIFY(root && root == loka::dsl::testing::SceneTestAccess::rootNode(*scene));
    }
  private:
    WindowRetirementTestApp &app_;
  };
}

void testAppKeepsAdmissionSnapshotAliveDuringWindowClose()
{
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
  WindowCreatingPlatformContext context;
  WindowRetirementTestApp app;
  CloseWindowDuringApplyController controller(app);
  WindowProps firstProps;
  firstProps.scene(new SceneCensusProbe("closing"));
  NullWindow *first = new NullWindow(&context, firstProps, &controller);
  WindowProps secondProps;
  secondProps.scene(new SceneCensusProbe("remaining"));
  NullWindow *second = new NullWindow(&context, secondProps);
  app.install(first, second);
  const unsigned long projections = second->scenePlatformController()->onChangeCallCount();
  first->scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
  second->scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 2);
  LOKA_VERIFY(app.reclaimCalls == 0);
  // Removing the first group row must neither skip nor invalidate the second snapshot row.
  LOKA_VERIFY(second->scenePlatformController()->onChangeCallCount() > projections);
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 1);
  LOKA_VERIFY(app.reclaimCalls == 1);
  app.requestWindowClose(second);
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
  LOKA_VERIFY(app.reclaimCalls == 2);
}

namespace
{
  /** Fixture-owned callback borrows the App and the still-owned later row. */
  struct CloseLaterAdmittedWindow
  {
    WindowRetirementTestApp &app;
    NullWindow &closing;
    static void onDetach(void *data)
    {
      CloseLaterAdmittedWindow *request = static_cast<CloseLaterAdmittedWindow *>(data);
      // Simulate loss of native presence before terminal close intake. Null's
      // teardown body supplies that loss; the target has no Scene run in flight.
      request->closing.destroyScenePlatform();
      request->app.requestWindowClose(&request->closing);
      request->app.flush();
      LOKA_VERIFY(request->app.reclaimCalls == 0);
    }
  };
}

void testClosedAdmittedWindowIsNotRecreatedByEarlierSeatCallback()
{
  WindowCreatingPlatformContext context;
  WindowRetirementTestApp app;
  WindowProps firstProps;
  firstProps.scene(new SceneCensusProbe("first"));
  NullWindow *first = new NullWindow(&context, firstProps);
  WindowProps secondProps;
  secondProps.scene(new SceneCensusProbe("closing"));
  NullWindow *second = new NullWindow(&context, secondProps);
  app.install(first, second);
  CloseLaterAdmittedWindow request = {app, *second};
  first->scene()->getAttachedState()->bind(&CloseLaterAdmittedWindow::onDetach, &request, false);
  LOKA_VERIFY(first->sceneManager()->commitTransaction(0, new SceneCensusProbe("replacement")));
  second->scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
  app.flush();
  // The App still owns the close-queued identity, but visibility=true must not
  // resurrect its native rail from the already captured admission row.
  LOKA_VERIFY(second->visibilityState().get());
  LOKA_VERIFY(second->scenePlatformController() == 0);
  LOKA_VERIFY(app.reclaimCalls == 0);
  app.flush();
  LOKA_VERIFY(app.reclaimCalls == 1);
  app.requestWindowClose(first);
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == 0);
}

namespace
{
  using namespace loka::app::scene;
  using loka::dsl::testing::SceneTestAccess;

  class PreparedReveal : public BoundaryNodeFor<PreparedReveal>
  {
  public:
    NodeState<bool> visible;
    explicit PreparedReveal(const BoundaryPropsFor<PreparedReveal> &props)
        : BoundaryNodeFor<PreparedReveal>(props) { this->state(this->visible, false); }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(loka::app::Show(*this->visible.state()) << loka::app::EditText());
    }
  };

  class PreparedParent : public BoundaryNodeFor<PreparedParent>
  {
  public:
    explicit PreparedParent(const BoundaryPropsFor<PreparedParent> &props)
        : BoundaryNodeFor<PreparedParent>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Boundary<PreparedReveal>());
    }
  };

  Node *FindPreparedEdit(Node *node)
  {
    if (!node || node->asEditTextNode()) return node;
    INestable *nest = node->asNestable();
    if (!nest) return 0;
    loka::dsl::CompositionCursor<Node> it(nest->childrenHead(), nest->childrenCount());
    for (Node *child = it.next(); child; child = it.next())
    {
      Node *found = FindPreparedEdit(child);
      if (found) return found;
    }
    return 0;
  }

  /** Window owns both scenes throughout the synchronous notification. */
  struct PreparedNotification
  {
    NullWindow &window;
    Scene &observed;
    Scene &target;
    bool onAttach;
    bool reveal;
    int calls;
    PreparedNotification(NullWindow &w, Scene &o, Scene &t, bool attach, bool show)
        : window(w), observed(o), target(t), onAttach(attach), reveal(show), calls(0) {}
    static void run(void *data)
    {
      PreparedNotification &probe = *static_cast<PreparedNotification *>(data);
      if (probe.observed.getAttachedState()->get() != probe.onAttach) return;
      ++probe.calls;
      NullScenePlatformController *rail = probe.window.scenePlatformController();
      const unsigned long changes = rail->onChangeCallCount();
      const unsigned long created = rail->createdCount();
      LOKA_VERIFY(probe.window.scene() == &probe.observed);
      if (probe.reveal)
        static_cast<PreparedReveal *>(SceneTestAccess::rootBoundary(probe.target))->visible.set(true);
      probe.target.invalidate(NODE_DIRTY_LAYOUT);
      if (!probe.onAttach)
      {
        LOKA_VERIFY(rail->onChangeCallCount() == changes);
        LOKA_VERIFY(rail->createdCount() == created);
      }
      else
      {
        LOKA_VERIFY(probe.window.scene() == &probe.target);
        LOKA_VERIFY(rail->onChangeCallCount() > changes);
      }
    }
  };

  void VerifyPreparedReplacement(bool reveal, bool observeAttach, bool ordinary, bool clocked = false)
  {
    WindowCreatingPlatformContext context;
    WindowProps props;
    props.scene(new Scene(new loka::app::EditTextDefinition()));
    NullWindow window(&context, props);
    WindowAdmissionTestApp app(window);
    Scene *a = window.scene();
    Scene *b = reveal ? new Scene(Boundary<PreparedReveal>())
                      : new Scene(new loka::app::EditTextDefinition());
    PreparedNotification probe(window, observeAttach ? *b : *a, *b, observeAttach, reveal);
    if (!ordinary) probe.observed.getAttachedState()->bind(&PreparedNotification::run, &probe, false);
    const unsigned long before = window.scenePlatformController()->onChangeCallCount();
    LOKA_VERIFY(window.sceneManager()->commitTransaction(0, b));
    loka::core::ScopedPtr<loka::core::Operation> turn(clocked ? new loka::core::Operation : 0);
    app.flush();
    LOKA_VERIFY(window.scene() == b);
    Node *edit = FindPreparedEdit(SceneTestAccess::rootNode(*b));
    LOKA_VERIFY(edit && edit->getContext());
    if (ordinary) LOKA_VERIFY(window.scenePlatformController()->onChangeCallCount() == before + 1);
    else
    {
      LOKA_VERIFY(probe.calls == 1);
      probe.observed.getAttachedState()->unbind(&PreparedNotification::run, &probe);
    }
  }
}

void testPreparedSceneDefersProjection() { VerifyPreparedReplacement(false, false, false); }
void testPreparedScenePreservesStructuralWork() { VerifyPreparedReplacement(true, false, false); }
void testPreparedSceneOrdinaryInstallProjectsOnce() { VerifyPreparedReplacement(false, false, true); }
void testPreparedSceneAttachedObserverProjectsWhileCurrent() { VerifyPreparedReplacement(true, true, false); }

void testSceneDetachObserverDropsWork()
{
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new Scene(new loka::app::EditTextDefinition()));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  Scene &scene = *window.scene();
  PreparedNotification probe(window, scene, scene, false, false);
  scene.getAttachedState()->bind(&PreparedNotification::run, &probe, false);
  window.sceneManager()->requestDetach();
  app.flush();
  LOKA_VERIFY(probe.calls == 1);
  LOKA_VERIFY(!SceneTestAccess::director(scene).firstPendingBoundary());
  LOKA_VERIFY(!SceneTestAccess::rootNode(scene));
  scene.getAttachedState()->unbind(&PreparedNotification::run, &probe);
}

void testPreparedSceneSchedulerDoesNotPoll()
{
  NullScenePlatformController rail;
  Scene scene(new loka::app::EditTextDefinition());
  scene.mount(&rail);
  SceneTestAccess::prepareComposition(scene);
  scene.requestInvalidate(NODE_DIRTY_LAYOUT);
  int attempts = 0;
  LOKA_VERIFY(!SceneTestAccess::runCountingRefreshes(scene, attempts));
  LOKA_VERIFY(attempts == 1);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  for (int i = 0; i < 3; ++i)
    LOKA_VERIFY(!SceneTestAccess::runCountingRefreshes(scene, attempts));
  LOKA_VERIFY(attempts == 1);
  LOKA_VERIFY(SceneTestAccess::director(scene).firstPendingBoundary());
}

void testPreparedSceneCoalescesDescendantWork()
{
  for (int descendantOnly = 0; descendantOnly < 2; ++descendantOnly)
  {
    NullScenePlatformController rail;
    Scene scene((Boundary<PreparedParent>()));
    scene.mount(&rail);
    SceneTestAccess::prepareComposition(scene);
    PreparedReveal *child = static_cast<PreparedReveal *>(SceneTestAccess::rootBoundary(scene)->childrenHead());
    child->visible.set(true);
    for (int i = 0; i < 3; ++i)
    {
      if (!descendantOnly) scene.invalidate(NODE_DIRTY_LAYOUT);
      SceneTestAccess::director(scene).requestBoundaryUpdate(child, NODE_DIRTY_LAYOUT, true);
    }
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    LOKA_VERIFY(rail.onChangeCallCount() == 0);
    SceneTestAccess::updateAttached(scene, true);
    LOKA_VERIFY(scene.hasPendingInvalidation());
    int attempts = 0;
    LOKA_VERIFY(SceneTestAccess::runCountingRefreshes(scene, attempts));
    LOKA_VERIFY(attempts == 1);
    Node *edit = FindPreparedEdit(SceneTestAccess::rootNode(scene));
    LOKA_VERIFY(edit && edit->getContext());
    LOKA_VERIFY(!SceneTestAccess::director(scene).firstPendingBoundary());
    LOKA_VERIFY(!SceneTestAccess::runCountingRefreshes(scene, attempts));
    LOKA_VERIFY(attempts == 1);
  }
}

namespace
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  struct LoseRailDuringInstall
  {
    NullWindow &window;
    Scene &outgoing;
    Scene &incoming;
    static void run(void *data)
    {
      LoseRailDuringInstall &probe = *static_cast<LoseRailDuringInstall *>(data);
      if (probe.outgoing.getAttachedState()->get()) return;
      LOKA_VERIFY(probe.window.scene() == &probe.outgoing);
      LOKA_VERIFY(SceneTestAccess::platformController(probe.incoming) != 0);
      probe.window.destroyScenePlatform();
      LOKA_VERIFY(probe.window.scenePlatformController() == 0);
      LOKA_VERIFY(SceneTestAccess::platformController(probe.incoming) != 0);
      std::fprintf(stderr, "#920: detach observer destroyed rail; incoming borrow remains\n");
    }
  };
#endif
}

void testSceneReplacementDetectsControllerLostDuringInstall()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    WindowCreatingPlatformContext context;
    WindowProps props;
    props.scene(new Scene(new loka::app::EditTextDefinition()));
    NullWindow window(&context, props);
    WindowAdmissionTestApp app(window);
    Scene *outgoing = window.scene();
    Scene *incoming = new Scene(Boundary<PreparedReveal>());
    LoseRailDuringInstall probe = {window, *outgoing, *incoming};
    outgoing->getAttachedState()->bind(&LoseRailDuringInstall::run, &probe, false);
    LOKA_VERIFY(window.sceneManager()->commitTransaction(0, incoming));
    app.flush();
#ifdef NDEBUG
    LOKA_VERIFY(window.scene() == incoming);
    LOKA_VERIFY(window.visibilityState().get());
    LOKA_VERIFY(window.scenePlatformController() == 0);
    LOKA_VERIFY(incoming->getAttachedState()->get());
    Window *installedWindow = incoming->getWindow();
    LOKA_VERIFY(installedWindow == &window);
    LOKA_VERIFY(SceneTestAccess::platformController(*incoming) == 0);
    LOKA_VERIFY(SceneTestAccess::composed(*incoming));
    LOKA_VERIFY(!window.sceneManager()->hasPendingReplacement());
    outgoing->getAttachedState()->unbind(&LoseRailDuringInstall::run, &probe);
    app.flush();
    LOKA_VERIFY(!window.sceneManager()->hasRetiredScenes());
#endif
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  std::fprintf(stderr, "#920: child status=%d signal=%d\n", status,
               WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#ifdef NDEBUG
  LOKA_VERIFY(WIFEXITED(status));
  LOKA_VERIFY(WEXITSTATUS(status) == 0);
#else
  LOKA_VERIFY(WIFSIGNALED(status));
  LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
#endif
#else
  std::printf("[skip] Installation rail-loss pin requires Linux without ASan (fork death check).\n");
#endif
}

void testPendingSceneInvalidationWakesWindowAdmission()
{
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new SceneCensusProbe("pending"));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  app.flush();
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
  const unsigned long projections = window.scenePlatformController()->onChangeCallCount();
  window.scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(window.scene()->hasPendingInvalidation());
  const bool pending = app.hasPendingWindowAdmission();
  printf("B11: Scene pending=1 App admission pending=%d\n", pending);
  fflush(stdout);
  LOKA_VERIFY(pending);
  app.flush();
  LOKA_VERIFY(window.scenePlatformController()->onChangeCallCount() > projections);
  LOKA_VERIFY(!app.hasPendingWindowAdmission());
}

namespace
{
  /** Stack-owned observation; the App owns the probed Windows. */
  class TailTraceWindow : public NullWindow
  {
  public:
    TailTraceWindow(PlatformContext *context, std::vector<char> &trace, char death)
        : NullWindow(context, props()), trace_(trace), death_(death), applies(0) {}
    virtual ~TailTraceWindow() { this->trace_.push_back(this->death_); }
    virtual void applyNativeVisibility()
    {
      ++this->applies;
      this->trace_.push_back('A');
    }
    virtual void drainNativeRetirements()
    {
      this->trace_.push_back('P');
      NullWindow::drainNativeRetirements();
    }
    static WindowProps props()
    {
      WindowProps value;
      value.scene(new SceneCensusProbe("tail"));
      return value;
    }
    std::vector<char> &trace_;
    char death_;
    int applies;
  };

  struct CollectCloseHandler
  {
    WindowRetirementTestApp &app;
    Window *window;
    static void handle(void *data)
    {
      CollectCloseHandler *handler = static_cast<CollectCloseHandler *>(data);
      handler->app.requestWindowClose(handler->window);
    }
  };

  std::vector<char> CollectCloseTrace(bool split, bool oldOrder)
  {
    WindowCreatingPlatformContext context;
    std::vector<char> trace;
    WindowRetirementTestApp app;
    TailTraceWindow *closing = new TailTraceWindow(&context, trace, 'C');
    TailTraceWindow *remaining = new TailTraceWindow(&context, trace, 'D');
    app.install(closing, remaining);
    app.flush();
    trace.clear();
    closing->applies = 0;
    remaining->applies = 0;
    CollectCloseHandler handler = {app, closing};
    CollectCloseHandler::handle(&handler);
    remaining->scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
    if (oldOrder)
      app.flushPendingWindowClosures();
    if (split)
    {
      app.admitAndApplyWindows();
      LOKA_VERIFY(app.reclaimCalls == 0);
      LOKA_VERIFY(closing->applies == 0);
      LOKA_VERIFY(remaining->applies == 1);
      LOKA_VERIFY(trace.size() == 2 && trace[0] == 'A' && trace[1] == 'P');
      app.reclaimWindows();
    }
    else
      app.flush();
    LOKA_VERIFY(app.reclaimCalls == 1);
    LOKA_VERIFY(remaining->applies == 1);
    // Copy before App teardown appends the remaining Window's death.
    return std::vector<char>(trace);
  }

  /** Delivery seam observation also counts reclaim calls with empty snapshots. */
  class TailCountingDelivery : public loka::app::DialogResultDelivery
  {
  public:
    TailCountingDelivery() : deliveries(0), reclaims(0) {}
    virtual bool hasRunnableWork() const { return true; }
    virtual void deliver() { ++this->deliveries; }
    virtual Retirement *retirementSnapshot() const { return 0; }
    virtual void reclaim(Retirement *)
    {
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      ++this->reclaims;
    }
    int deliveries;
    int reclaims;
  };

  class TailDeliveryWindow : public NullWindow
  {
  public:
    TailDeliveryWindow(PlatformContext *context, TailCountingDelivery &delivery)
        : NullWindow(context, TailTraceWindow::props()), delivery_(delivery) {}
    virtual loka::app::DialogResultDelivery *dialogResultDelivery() { return &this->delivery_; }
  private:
    TailCountingDelivery &delivery_;
  };
}

void testWindowCloseRequestedDuringCollectIsReclaimedAfterApply()
{
  const std::vector<char> after = CollectCloseTrace(true, false);
  const std::vector<char> before = CollectCloseTrace(false, true);
  LOKA_VERIFY(after.size() == 3 && before.size() == 3);
  LOKA_VERIFY(after[2] == 'C' && before[0] == 'C');
  // Explicit old-order control: the two surviving-window projection calls match.
  LOKA_VERIFY(after[0] == before[1] && after[1] == before[2]);
  printf("B9: old=CAP split=APC; surviving projection=AP in both\n");
}

void testFlushWindowInvalidationsIsAdmitApplyThenReclaim()
{
  const std::vector<char> split = CollectCloseTrace(true, false);
  const std::vector<char> convenience = CollectCloseTrace(false, false);
  LOKA_VERIFY(split == convenience);
  printf("B12: split=APC convenience=APC\n");
}

void testWindowAdmissionTestAppOperationLoopReclaimsOncePerTail()
{
  WindowCreatingPlatformContext context;
  TailCountingDelivery delivery;
  TailDeliveryWindow window(&context, delivery);
  WindowAdmissionTestApp app(window);
  app.flush();
  const int reclaims = delivery.reclaims;
  const int deliveries = delivery.deliveries;
  const int alive = g_sceneOwnershipScenesAlive;
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, new SceneCensusProbe("second")));
  app.operationLoop();
  // Second admission captures the Scene retired by the first; one suffix drain.
  LOKA_VERIFY(delivery.deliveries == deliveries + 2);
  LOKA_VERIFY(delivery.reclaims == reclaims + 1);
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == alive);
  LOKA_VERIFY(loka::app::testing::SceneManagerTestAccess::retiredSceneCount(*window.sceneManager()) == 0);
  app.reclaimWindows();
  LOKA_VERIFY(delivery.reclaims == reclaims + 1);
  app.operationLoop();
  LOKA_VERIFY(delivery.reclaims == reclaims + 2);
  printf("B13: admissions=2 reclaim=1 retiredScenes=0 repeatedReclaim=no-op\n");
}

void testEmptyFirstAdmissionKeepsLaterCloseForNextTail()
{
  WindowCreatingPlatformContext context;
  std::vector<char> trace;
  WindowRetirementTestApp app;
  TailTraceWindow *window = new TailTraceWindow(&context, trace, 'C');
  app.install(window);
  app.flush();
  trace.clear();
  app.admitAndApplyWindows();
  LOKA_VERIFY(trace.empty());
  // Models focus completion after an empty first admission.
  app.requestWindowClose(window);
  app.admitAndApplyWindows();
  app.flushPendingWindowClosures();
  app.reclaimWindows();
  LOKA_VERIFY(app.reclaimCalls == 0 && trace.empty());
  app.admitAndApplyWindows();
  LOKA_VERIFY(app.reclaimCalls == 0);
  app.reclaimWindows();
  LOKA_VERIFY(app.reclaimCalls == 1 && trace.size() == 1 && trace[0] == 'C');
}

void testWindowReclaimDefersBusyCapturedScene()
{
  WindowCreatingPlatformContext context;
  NullWindow window(&context, TailTraceWindow::props());
  WindowAdmissionTestApp app(window);
  app.flush();
  const int alive = g_sceneOwnershipScenesAlive;
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, new SceneCensusProbe("busy")));
  app.admitAndApplyWindows();
  app.admitAndApplyWindows();
  {
    loka::app::scene::BorrowScope borrow(*window.scenePlatformController());
    app.reclaimWindows();
    LOKA_VERIFY(g_sceneOwnershipScenesAlive == alive + 1);
  }
  app.flush();
  LOKA_VERIFY(g_sceneOwnershipScenesAlive == alive);
}

void testTwoAdmissionsReclaimLatestDialogSnapshot()
{
  typedef loka::app::DialogResultTransport Transport;
  WindowCreatingPlatformContext context;
  NullWindow window(&context, WindowProps());
  WindowAdmissionTestApp app(window);
  Transport::Registration *registration = window.dialogResults().reserve(loka::app::OpenFileDialogProps());
  LOKA_VERIFY(registration);
  {
    Transport::ReturnPort port(registration);
    LOKA_VERIFY(port.seal(loka::app::FileChooserResult::Canceled()) == &window);
  }
  LOKA_VERIFY(loka::app::testing::DialogResultTestAccess::census(window.dialogResults()) == 1);
  app.admitAndApplyWindows();
  LOKA_VERIFY(loka::app::testing::DialogResultTestAccess::census(window.dialogResults()) == 1);
  app.admitAndApplyWindows();
  app.reclaimWindows();
  LOKA_VERIFY(loka::app::testing::DialogResultTestAccess::census(window.dialogResults()) == 0);
  delete registration;
}

namespace
{
  class EnrolledRoot : public loka::app::scene::BoundaryNodeFor<EnrolledRoot>
  {
  public:
    explicit EnrolledRoot(const loka::app::scene::BoundaryPropsFor<EnrolledRoot> &props)
        : loka::app::scene::BoundaryNodeFor<EnrolledRoot>(props)
    {
      this->state(this->value_, 7);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::Box());
    }
  private:
    loka::app::scene::NodeState<int> value_;
  };
}

void testSceneReplacementTearsDownEnrolledRootWithoutAssert()
{
  using namespace loka::core;
  using namespace loka::app::scene;
  WindowCreatingPlatformContext context;
  WindowProps props;
  props.scene(new Scene(loka::app::scene::Boundary<EnrolledRoot>()));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene *outgoing = window.scene();
  StateTracker *tracker = loka::dsl::testing::SceneTestAccess::rootBoundary(*outgoing)->tracker();
  LOKA_VERIFY(loka::core::testing::PushStateTrackerTestAccess::hasRegisteredStates(*tracker->asPushTracker()));
  Operation turn;
  LOKA_VERIFY(Operation::openActive(tracker) == OPEN_OK);
  Scene *incoming = new Scene(new loka::app::Box());
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, incoming));
  app.admitAndApplyWindows();
  LOKA_VERIFY(window.scene() == incoming);
  LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*outgoing) == 0);
  LOKA_VERIFY(loka::core::testing::OperationTestAccess::empty(turn));
  const OperationOutcome outcome = turn.close();
  LOKA_VERIFY(outcome.status == OPERATION_SETTLED);
  app.reclaimWindows();
}

void testSceneInstallProjectsAttachedObserverInsideTurn() { VerifyPreparedReplacement(true, true, false, true); }

void testSceneRearmCommitsBeforeReadInsideTurn()
{
  for (unsigned clocked = 0; clocked != 2; ++clocked)
  {
    WindowCreatingPlatformContext context;
    NullWindow window(&context, WindowProps().scene(new Scene(new loka::app::EditTextDefinition())));
    WindowAdmissionTestApp app(window);
    app.flush();
    loka::core::ScopedPtr<loka::core::Operation> turn(clocked ? new loka::core::Operation : 0);
    window.sceneManager()->requestRearm();
    app.flush();
    if (clocked)
      LOKA_VERIFY(loka::core::testing::OperationTestAccess::empty(*turn));
    Node *edit = FindPreparedEdit(SceneTestAccess::rootNode(*window.scene()));
    LOKA_VERIFY(edit && edit->getContext());
  }
}

#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "support/Headless.hpp"
#include "testing/app/WindowTestAccess.hpp"

namespace
{
  /** Surviving owner makes detach writes and their tail notification distinct. */
  struct CloseTurnOwner : loka::app::scene::HeadlessStateOwner
  {
    loka::app::scene::Request<loka::app::LineCursor> request;
    loka::app::Focus<unsigned> focus;
    loka::testing::TextBufferFixture lines;
    loka::app::scene::Reported<loka::app::LineCursor> cursor;
    std::vector<bool> detaches;
    std::vector<bool> requestCommits;
    std::vector<bool> focusCommits;
    std::vector<bool> destructions;

    CloseTurnOwner()
    {
      loka::app::scene::StateBatchBase::CreateImmediateState(this, this->request, loka::app::LineCursor::None());
      loka::app::scene::StateBatchBase::CreateImmediateState(this, this->cursor, loka::app::LineCursor::None());
      LOKA_VERIFY(this->lines.attach(this->tracker()->asPushTracker(), 1) == loka::core::ATTACH_OK);
      LOKA_VERIFY(this->lines.insert(0, loka::core::String("abc")) == loka::core::EDIT_OK);
    }
    static void requestChanged(void *data)
    {
      CloseTurnOwner &owner = *static_cast<CloseTurnOwner *>(data);
      owner.requestCommits.push_back(loka::core::Operation::hasActive());
    }
    static void focusChanged(void *data)
    {
      CloseTurnOwner &owner = *static_cast<CloseTurnOwner *>(data);
      LOKA_VERIFY(!(owner.focus.state()->get() != loka::app::Focused<unsigned>::none()));
      owner.focusCommits.push_back(loka::core::Operation::hasActive());
    }
  };

  class CloseTurnRoot;
  struct CloseTurnProps : loka::app::scene::NodePropsBase<CloseTurnProps>
  {
    typedef CloseTurnRoot NodeType;
    struct TypeTag {};
    CloseTurnOwner *owner;
    explicit CloseTurnProps(CloseTurnOwner &value) : owner(&value) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return std::less<CloseTurnOwner *>()(this->owner, static_cast<const CloseTurnProps &>(rhs).owner);
    }
  };
  class CloseTurnRoot : public loka::app::scene::StdCompositionBoundaryNodeBase<CloseTurnProps>
  {
  public:
    explicit CloseTurnRoot(const CloseTurnProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<CloseTurnProps>(props)
    {
      this->state(this->props.owner->focus);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::F()
          << loka::app::TextEditor(loka::app::TextEditorProps(this->props.owner->lines, this->props.owner->cursor).moveCaretTo(this->props.owner->request))
          << loka::app::EditText(loka::app::EditTextProps().focusedAs(this->props.owner->focus, 1u)));
    }
    virtual void detachNode(loka::app::scene::NodeComposition &)
    {
      this->props.owner->detaches.push_back(loka::core::Operation::hasActive());
    }
  };
  class CloseTurnWindow : public NullWindow
  {
  public:
    CloseTurnWindow(PlatformContext &context, CloseTurnOwner &owner)
        : NullWindow(&context, makeProps(owner)), owner_(owner) {}
    virtual ~CloseTurnWindow()
    {
      this->owner_.destructions.push_back(loka::core::Operation::hasActive());
    }
  private:
    CloseTurnOwner &owner_;
    static WindowProps makeProps(CloseTurnOwner &owner)
    {
      typedef loka::app::scene::BoundaryDefinition<CloseTurnProps, CloseTurnRoot> Root;
      return WindowProps().scene(new loka::app::scene::Scene(Root(CloseTurnProps(owner))));
    }
  };
  /** Observations outlive the Window; the Scene borrow is used only by callbacks. */
  struct CloseTurnFacts
  {
    loka::app::scene::Scene &scene;
    std::vector<bool> lifecycle;
    std::vector<bool> attached;
    explicit CloseTurnFacts(loka::app::scene::Scene &value) : scene(value) {}
    static void lifecycleChanged(void *data)
    {
      CloseTurnFacts &facts = *static_cast<CloseTurnFacts *>(data);
      if (facts.scene.getLifecycleState()->get() == ON_DETACH)
      {
        facts.lifecycle.push_back(loka::core::Operation::hasActive());
        std::fprintf(stderr, "C5 P7: lifecycle count=%lu active=%d\n",
                     static_cast<unsigned long>(facts.lifecycle.size()), facts.lifecycle.back() ? 1 : 0);
      }
    }
    static void attachedChanged(void *data)
    {
      CloseTurnFacts &facts = *static_cast<CloseTurnFacts *>(data);
      if (!facts.scene.getAttachedState()->get())
        facts.attached.push_back(loka::core::Operation::hasActive());
    }
  };

  struct CloseTurnAction
  {
    WindowAdmissionTestApp &app;
    Window &window;
    static void close(void *data)
    {
      CloseTurnAction &action = *static_cast<CloseTurnAction *>(data);
      action.app.requestWindowClose(&action.window);
    }
  };
}

void testWindowCloseDetachHookRunsInsideTurn()
{
  WindowCreatingPlatformContext context;
  CloseTurnOwner owner;
  CloseTurnWindow *window = new CloseTurnWindow(context, owner);
  WindowAdmissionTestApp app(*window);
  app.operationLoop();
  CloseTurnAction action = {app, *window};
  app.operationLoop(&CloseTurnAction::close, &action);
  LOKA_VERIFY(owner.detaches.size() == 1);
  std::fprintf(stderr, "C5 P1: DETACH active=%d\n", owner.detaches[0] ? 1 : 0);
  LOKA_VERIFY(owner.detaches[0]);
  LOKA_VERIFY(owner.destructions.size() == 1 && !owner.destructions[0]);
}

void testWindowCloseRequestCommitsAtTurnTail()
{
  WindowCreatingPlatformContext context;
  CloseTurnOwner owner;
  CloseTurnWindow *window = new CloseTurnWindow(context, owner);
  WindowAdmissionTestApp app(*window);
  app.operationLoop();
  loka::core::Operation turn;
  owner.request.set(loka::app::LineCursor(1, 2));
  turn.settle();
  LOKA_VERIFY(owner.request.get() != loka::app::LineCursor::None());
  owner.setInvalidateCallback(&CloseTurnOwner::requestChanged, &owner);
  app.requestWindowClose(window);
  app.admitAndApplyWindows();
  LOKA_VERIFY(owner.request.get() == loka::app::LineCursor::None());
  LOKA_VERIFY(owner.requestCommits.empty());
  turn.close();
  LOKA_VERIFY(owner.requestCommits.size() == 1 && owner.requestCommits[0]);
  app.reclaimWindows();
  LOKA_VERIFY(owner.requestCommits.size() == 1);
  owner.setInvalidateCallback(0, 0);
}

void testWindowCloseFocusClearsInsideTurn()
{
  WindowCreatingPlatformContext context;
  CloseTurnOwner owner;
  CloseTurnWindow *window = new CloseTurnWindow(context, owner);
  WindowAdmissionTestApp app(*window);
  app.operationLoop();
  owner.focus.post(1u);
  app.operationLoop();
  LOKA_VERIFY(owner.focus.state()->get().is(1u));
  owner.focus.state()->bind(&CloseTurnOwner::focusChanged, &owner, false);
  loka::core::Operation turn;
  app.requestWindowClose(window);
  app.admitAndApplyWindows();
  LOKA_VERIFY(owner.focusCommits.size() == 1 && owner.focusCommits[0]);
  LOKA_VERIFY(loka::app::testing::WindowTestAccess::publishedFocusContext(*window) == 0);
  loka::app::testing::WindowTestAccess::reconcileFocus(*window);
  turn.close();
  LOKA_VERIFY(owner.focusCommits.size() == 1 && owner.focusCommits[0]);
  app.reclaimWindows();
  app.reconcileFocus();
  LOKA_VERIFY(owner.focusCommits.size() == 1);
}

void testWindowCloseTwoAdmissionsDetachOnceReclaimLater()
{
  WindowCreatingPlatformContext context;
  CloseTurnOwner owner;
  CloseTurnWindow *window = new CloseTurnWindow(context, owner);
  WindowAdmissionTestApp app(*window);
  app.operationLoop();
  loka::core::Operation turn;
  app.requestWindowClose(window);
  app.admitAndApplyWindows();
  LOKA_VERIFY(owner.detaches.size() == 1 && owner.detaches[0]);
  LOKA_VERIFY(owner.destructions.empty());
  app.admitAndApplyWindows();
  LOKA_VERIFY(owner.detaches.size() == 1 && owner.destructions.empty());
  turn.close();
  LOKA_VERIFY(owner.destructions.empty());
  app.reclaimWindows();
  LOKA_VERIFY(owner.detaches.size() == 1);
  LOKA_VERIFY(owner.destructions.size() == 1 && !owner.destructions[0]);
}

void testWindowCloseRetirementFactsRunInsideTurn()
{
  WindowCreatingPlatformContext context;
  CloseTurnOwner owner;
  CloseTurnWindow *window = new CloseTurnWindow(context, owner);
  WindowAdmissionTestApp app(*window);
  app.operationLoop();
  CloseTurnFacts facts(*window->scene());
  facts.scene.getLifecycleState()->bind(&CloseTurnFacts::lifecycleChanged, &facts, false);
  facts.scene.getAttachedState()->bind(&CloseTurnFacts::attachedChanged, &facts, false);
  loka::core::Operation turn;
  app.requestWindowClose(window);
  app.admitAndApplyWindows();
  const bool retiredAtAdmission = facts.lifecycle.size() == 1 && facts.attached.size() == 1
      && facts.scene.getWindow() == 0 && !facts.scene.getAttachedState()->get()
      && facts.scene.getLifecycleState()->get() == ON_DETACH;
  app.admitAndApplyWindows();
  turn.close();
  app.reclaimWindows();
  LOKA_VERIFY(facts.lifecycle.size() == 1 && facts.lifecycle[0]);
  LOKA_VERIFY(facts.attached.size() == 1 && facts.attached[0]);
  LOKA_VERIFY(retiredAtAdmission);
  LOKA_VERIFY(owner.destructions.size() == 1 && !owner.destructions[0]);
}
