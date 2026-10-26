#include "support/TextBufferFixture.hpp"
#include "FocusPublisherTests.hpp"
#include "FocusPostTests.hpp"
#include "app/FocusParticipant.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullApp.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/app/WindowTestAccess.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/scene/SceneFocusTestAccess.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"
#include <vector>
#include <limits>
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

enum FocusTestField
{
  FOCUS_HEIGHT = 1,
  FOCUS_WEIGHT = 2,
  FOCUS_OTHER = 3
};
namespace loka
{
  namespace app
  {
    template <> struct FocusKeyTraits<FocusTestField> : UnsignedFocusKeyTraits<FocusTestField>
    {
    };
  } // namespace app
} // namespace loka
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef Focused<FocusTestField> Fact;
  struct Facts : HeadlessStateOwner
  {
    Focus<FocusTestField> first, second;
    Reported<LineCursor> cursor;
    unsigned invalidations;
    static void invalidated(void *data)
    {
      ++static_cast<Facts *>(data)->invalidations;
    }
    Facts()
        : invalidations(0)
    {
      this->setInvalidateCallback(&invalidated, this);
      StateBatchBase::CreateImmediateState(this, this->first);
      StateBatchBase::CreateImmediateState(this, this->second);
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
    }
    void declareIn(IStateOwner &owner)
    {
      StateBatchBase::CreateImmediateState(&owner, this->first);
      StateBatchBase::CreateImmediateState(&owner, this->second);
    }
  };
  struct Watch
  {
    Focus<FocusTestField> &fact;
    std::vector<int> &events;
    int prefix;
    Node *retire;
    Node *parkAndReturn;
    FocusRow *expectedPublished;
    FocusRow *expectedCut;
    NullScenePlatformController *nestedPlatform;
    Window *nestedWindow;
    NodeContext *nestedTarget;
    Watch(Focus<FocusTestField> &value, std::vector<int> &log, int id = 0)
        : fact(value),
          events(log),
          prefix(id),
          retire(0),
          parkAndReturn(0),
          expectedPublished(0),
          expectedCut(0),
          nestedPlatform(0),
          nestedWindow(0),
          nestedTarget(0)
    {
      this->fact.state()->bind(&changed, this, false);
    }
    ~Watch()
    {
      this->fact.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      Watch &self = *static_cast<Watch *>(data);
      const Fact value = self.fact.state()->get();
      self.events.push_back(self.prefix + (value != Fact::none() ? static_cast<int>(value.key()) : 0));
      if (!(value != Fact::none()))
      {
        if (self.expectedPublished)
          LOKA_VERIFY(SceneFocusTestAccess::published(*self.expectedPublished));
        if (self.expectedCut)
          LOKA_VERIFY(!SceneFocusTestAccess::published(*self.expectedCut));
        if (self.parkAndReturn)
        {
          Node *target = self.parkAndReturn;
          self.parkAndReturn = 0;
          NotifySubtreeNodeDetached(target);
          NotifySubtreeNodeAttached(target);
        }
      }
      if (self.nestedPlatform && !(value != Fact::none()))
      {
        self.nestedPlatform->simulateNativeFocus(self.nestedTarget);
        loka::app::testing::WindowTestAccess::reconcileFocus(*self.nestedWindow);
      }
      if (self.retire && !(value != Fact::none()))
      {
        Node *target = self.retire;
        self.retire = 0;
        LifecycleFactTestAccess::MarkSubtreeRetired(target);
        target->setContext(0);
      }
    }
  };
  void project(NullScenePlatformController &platform, Scene &scene, Node &node)
  {
    ComponentContext context;
    BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    context.setBoundary(root);
    context.setStateOwner(root);
    context.setScene(&scene);
    BoundaryNode::composeSubtree(&node, context, COMPOSE_EVENT_ATTACH, root);
    LayoutState bounds;
    bounds.width = 240;
    bounds.height = 320;
    platform.projectLayoutForTesting(&node, bounds);
    LOKA_VERIFY(node.getContext());
  }
  class EmptyFocusRoot : public BoundaryNodeFor<EmptyFocusRoot>
  {
  public:
    explicit EmptyFocusRoot(const BoundaryPropsFor<EmptyFocusRoot> &p)
        : BoundaryNodeFor<EmptyFocusRoot>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  class ReadController : public NullScenePlatformController
  {
  public:
    bool answered;
    unsigned reads;
    void (*duringApply)(void *);
    void *data;
    ReadController()
        : answered(true),
          reads(0),
          duringApply(0),
          data(0)
    {
    }
    virtual bool readNativeFocus(NodeContext *&out)
    {
      ++this->reads;
      if (!this->answered)
        return IPlatformController::readNativeFocus(out);
      return NullScenePlatformController::readNativeFocus(out);
    }
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
    {
      return false;
    }
    virtual void onChange(Node *root, NodeDirtyFlags flags, bool full)
    {
      NullScenePlatformController::onChange(root, flags, full);
      if (this->duringApply)
        this->duringApply(this->data);
    }
  };
  class FocusWindow : public NullWindow
  {
  public:
    bool live;
    FocusWindow(PlatformContext *context, const WindowProps &props, NullScenePlatformController *controller)
        : NullWindow(context, props, controller),
          live(true)
    {
    }

  protected:
    virtual bool hasLiveScenePlatform() const
    {
      return this->live && NullWindow::hasLiveScenePlatform();
    }
  };
  struct Fixture
  {
    Facts facts;
    ReadController platform;
    NullPlatformContext context;
    FocusWindow window;
    WindowAdmissionTestApp app;
    Scene &scene;
    EditTextNode height, weight;
    static WindowProps props()
    {
      WindowProps p;
      p.scene(new Scene(Boundary<EmptyFocusRoot>()));
      return p;
    }
    Fixture()
        : window(&this->context, props(), &this->platform),
          app(this->window),
          scene(*this->window.scene()),
          height(EditTextProps().focusedAs(this->facts.first, FOCUS_HEIGHT)),
          weight(EditTextProps().focusedAs(this->facts.first, FOCUS_WEIGHT))
    {
      this->app.flush();
      this->facts.declareIn(*loka::dsl::testing::SceneTestAccess::rootBoundary(this->scene));
      this->height.setPropsTypeId(EditTextProps::staticTypeId());
      this->weight.setPropsTypeId(EditTextProps::staticTypeId());
      LOKA_VERIFY(EditText(EditTextProps().focusedAs(this->facts.first, FOCUS_HEIGHT)).applyPropsToNode(&this->height));
      LOKA_VERIFY(EditText(EditTextProps().focusedAs(this->facts.first, FOCUS_WEIGHT)).applyPropsToNode(&this->weight));
      project(this->platform, this->scene, this->height);
      project(this->platform, this->scene, this->weight);
    }
    ~Fixture()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->height);
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->weight);
      this->height.setContext(0);
      this->weight.setContext(0);
    }
    void focus(Node &node)
    {
      this->platform.simulateNativeFocus(node.getContext());
      this->app.reconcileFocus();
    }
  };
  void replace(EditTextNode &node, Focus<FocusTestField> &fact, FocusTestField key)
  {
    EditText definition(EditTextProps().focusedAs(fact, key));
    LOKA_VERIFY(definition.applyPropsToNode(&node));
  }
} // namespace
void testFocusSameFact()
{
  Fixture f;
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  f.focus(f.height);
  events.clear();
  f.platform.simulateNativeFocus(f.weight.getContext());
  LOKA_VERIFY(events.empty() && f.facts.first.state()->get().is(FOCUS_HEIGHT));
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(events.size() == 1 && events[0] == FOCUS_WEIGHT);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
}
void testFocusAcrossFacts()
{
  Fixture f;
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  std::vector<int> events;
  Watch first(f.facts.first, events, 10), second(f.facts.second, events, 20);
  f.focus(f.height);
  events.clear();
  f.focus(f.weight);
  LOKA_VERIFY(events.size() == 2 && events[0] == 10 && events[1] == 22);
}
void testFocusUnpublished()
{
  Fixture f;
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  f.focus(f.height);
  events.clear();
  EditTextNode other(EditTextProps().focusedAs(f.facts.first, FOCUS_OTHER));
  project(f.platform, f.scene, other);
  LOKA_VERIFY(events.empty() && f.facts.first.state()->get().is(FOCUS_HEIGHT));
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  replace(f.weight, f.facts.first, FOCUS_WEIGHT);
  LOKA_VERIFY(events.empty() && f.facts.first.state()->get().is(FOCUS_HEIGHT));
  NotifySubtreeNodeDetached(&f.weight);
  LifecycleFactTestAccess::MarkSubtreeRetired(&other);
  other.setContext(0);
  LOKA_VERIFY(events.empty() && f.facts.first.state()->get().is(FOCUS_HEIGHT));
}
void testFocusReplacement()
{
  Fixture f;
  std::vector<int> events;
  Watch first(f.facts.first, events, 10), second(f.facts.second, events, 20);
  f.focus(f.height);
  events.clear();
  replace(f.height, f.facts.first, FOCUS_OTHER);
  LOKA_VERIFY(events.size() == 1 && events[0] == 13);
  events.clear();
  replace(f.height, f.facts.second, FOCUS_HEIGHT);
  LOKA_VERIFY(events.size() == 2 && events[0] == 10 && events[1] == 21);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  LOKA_VERIFY(f.facts.second.state()->get().is(FOCUS_HEIGHT));
}
void testFocusDetach()
{
  for (int terminal = 0; terminal != 2; ++terminal)
  {
    Fixture f;
    std::vector<int> events;
    Watch watch(f.facts.first, events);
    f.focus(f.height);
    events.clear();
    if (terminal)
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&f.height);
      f.height.setContext(0);
    }
    else
      NotifySubtreeNodeDetached(&f.height);
    LOKA_VERIFY(events.size() == 1 && events[0] == 0);
    NodeContext *source = f.weight.getContext();
    LOKA_VERIFY(f.platform.readNativeFocus(source) && !source);
    loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
    LOKA_VERIFY(events.size() == 1);
    if (!terminal)
    {
      LifecycleFactTestAccess::DeliverFacts(&f.height);
      NotifySubtreeNodeAttached(&f.height);
      LifecycleFactTestAccess::DeliverFacts(&f.height);
      loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
      LOKA_VERIFY(events.size() == 1);
      f.focus(f.height);
      LOKA_VERIFY(events.size() == 2 && events[1] == FOCUS_HEIGHT);
    }
  }
}
void testFocusObserverRetiresTarget()
{
  Fixture f;
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  std::vector<int> events;
  Watch first(f.facts.first, events, 10), second(f.facts.second, events, 20);
  f.focus(f.height);
  events.clear();
  first.retire = &f.weight;
  first.expectedPublished = f.weight.asFocusParticipant();
  first.expectedCut = f.height.asFocusParticipant();
  f.focus(f.weight);
  LOKA_VERIFY(events.size() == 1 && events[0] == 10);
  LOKA_VERIFY(!f.weight.getContext());
  LOKA_VERIFY(!(f.facts.second.state()->get() != Fact::none()));
}
void testFocusSameTarget()
{
  Fixture f;
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  f.focus(f.height);
  events.clear();
  f.focus(f.height);
  LOKA_VERIFY(events.empty());
}
void testFocusTwoWindows()
{
  Fixture first, second;
  first.focus(first.height);
  second.focus(second.weight);
  LOKA_VERIFY(first.facts.first.state()->get().is(FOCUS_HEIGHT));
  LOKA_VERIFY(second.facts.first.state()->get().is(FOCUS_WEIGHT));
  NotifySubtreeNodeDetached(&first.height);
  LOKA_VERIFY(second.facts.first.state()->get().is(FOCUS_WEIGHT));
}
void testFocusQuery()
{
  Fixture f;
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  f.focus(f.height);
  events.clear();
  f.platform.answered = false;
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  f.platform.answered = true;
  LOKA_VERIFY(events.empty() && f.facts.first.state()->get().is(FOCUS_HEIGHT));
  f.platform.simulateNativeFocus(0);
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(events.size() == 1 && events[0] == 0);
  f.platform.simulateNativeFocus(f.weight.getContext());
  NotifySubtreeNodeDetached(&f.weight);
  NodeContext *answer = f.height.getContext();
  LOKA_VERIFY(f.platform.readNativeFocus(answer) && !answer);
}
void testFocusTextEditor()
{
  Fixture f;
  loka::testing::TextBufferFixture lines;
  LOKA_VERIFY(lines.attach(f.facts.tracker()->asPushTracker(), 4) == ATTACH_OK);
  LOKA_VERIFY(lines.insert(0, String("focus")) == EDIT_OK);
  TextEditorNode editor(TextEditorProps(lines, f.facts.cursor).focusedAs(f.facts.second, FOCUS_HEIGHT));
  editor.setPropsTypeId(TextEditorProps::staticTypeId());
  project(f.platform, f.scene, editor);
  std::vector<int> events;
  Watch second(f.facts.second, events);
  f.focus(editor);
  events.clear();
  TextEditor next(TextEditorProps(lines, f.facts.cursor).focusedAs(f.facts.second, FOCUS_WEIGHT));
  LOKA_VERIFY(next.applyPropsToNode(&editor));
  LOKA_VERIFY(events.size() == 1 && events[0] == FOCUS_WEIGHT);
  NotifySubtreeNodeDetached(&editor);
  LOKA_VERIFY(events.size() == 2 && events[1] == 0);
  LifecycleFactTestAccess::MarkSubtreeRetired(&editor);
  editor.setContext(0);
}
void testFocusValues()
{
  LOKA_VERIFY(!(Fact() != Fact::none()));
  LOKA_VERIFY(Fact(FOCUS_HEIGHT) != Fact::none());
  LOKA_VERIFY(!Fact::none().is(FOCUS_HEIGHT));
  LOKA_VERIFY(Fact(FOCUS_HEIGHT).key() == FOCUS_HEIGHT);
  const long lowest = (std::numeric_limits<long>::min)();
  LOKA_VERIFY(FocusKeyTraits<long>::fromWord(FocusKeyTraits<long>::toWord(lowest)) == lowest);
  LOKA_VERIFY(FocusKeyTraits<int>::fromWord(FocusKeyTraits<int>::toWord(-1)) == -1);
  const ItemId id(12, 13);
  LOKA_VERIFY(FocusKeyTraits<ItemId>::fromWord(FocusKeyTraits<ItemId>::toWord(id)) == id);
  Facts facts;
  FocusBinding first = EditTextProps().focusedAs(facts.first, FOCUS_HEIGHT).focus_;
  FocusBinding copy(first), assigned;
  assigned = copy;
  LOKA_VERIFY(assigned.same(first));
  std::vector<int> events;
  Watch watch(facts.first, events);
  assigned.publish();
  const unsigned invalidations = facts.invalidations;
  assigned.publish();
  LOKA_VERIFY(facts.invalidations == invalidations && invalidations > 0);
  LOKA_VERIFY(events.size() == 1);
  LOKA_VERIFY(facts.first.state()->get().is(FOCUS_HEIGHT));
  LOKA_VERIFY(facts.tracker()->phase() == TRACKER_IDLE);
  assigned = FocusBinding();
  assigned.publish();
  LOKA_VERIFY(!assigned.state());
  LOKA_VERIFY(first.same(copy));
  Focus<FocusTestField> &alias = facts.first;
  LOKA_VERIFY(EditTextProps().focusedAs(alias, FOCUS_HEIGHT).focus_.same(first));
  const EditTextProps editHeight = EditTextProps().focusedAs(facts.first, FOCUS_HEIGHT);
  const EditTextProps editWeight = EditTextProps().focusedAs(facts.first, FOCUS_WEIGHT);
  LOKA_VERIFY(editHeight < editWeight);
  const TextEditorProps editorHeight = TextEditorProps().focusedAs(facts.first, FOCUS_HEIGHT);
  const TextEditorProps editorWeight = TextEditorProps().focusedAs(facts.first, FOCUS_WEIGHT);
  LOKA_VERIFY(editorHeight < editorWeight);
}

void testFocusCoalescedReattach()
{
  Fixture f;
  f.focus(f.height);
  NotifySubtreeNodeDetached(&f.height);
  NotifySubtreeNodeAttached(&f.height);
  LifecycleFactTestAccess::DeliverFacts(&f.height);
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  f.focus(f.height);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
}

void testFocusReentrantCompletion()
{
  Fixture f;
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  std::vector<int> events;
  Watch first(f.facts.first, events, 10), second(f.facts.second, events, 20);
  f.focus(f.height);
  events.clear();
  first.nestedPlatform = &f.platform;
  first.nestedWindow = &f.window;
  first.nestedTarget = f.height.getContext();
  f.focus(f.weight);
  LOKA_VERIFY(events.size() == 2 && events[0] == 10 && events[1] == 22);
  LOKA_VERIFY(f.facts.second.state()->get().is(FOCUS_WEIGHT));
  first.nestedPlatform = 0;
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(events.size() == 4 && events[2] == 20 && events[3] == 11);
}
void testFocusAdmission()
{
  Fixture f;
  EditTextNode unregistered(EditTextProps().focusedAs(f.facts.second, FOCUS_HEIGHT));
  unregistered.setContext(new NodeContext(&unregistered));
  f.platform.simulateNativeFocus(unregistered.getContext());
  NodeContext *answer = f.height.getContext();
  LOKA_VERIFY(f.platform.readNativeFocus(answer) && answer == unregistered.getContext());
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(!(f.facts.second.state()->get() != Fact::none()));
}

void testFocusInvalidBindingReplacement()
{
  Fixture f;
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  f.focus(f.height);
  events.clear();
  EditText empty;
  LOKA_VERIFY(empty.applyPropsToNode(&f.height));
  LOKA_VERIFY(events.size() == 1 && events[0] == 0);
  // Restore before another native completion: the published context is retained.
  replace(f.height, f.facts.first, FOCUS_HEIGHT);
  LOKA_VERIFY(events.size() == 2 && events[1] == FOCUS_HEIGHT);
  LOKA_VERIFY(empty.applyPropsToNode(&f.height));
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  events.clear();
  // After completion, this control is unpublished; replacement alone is silent.
  replace(f.height, f.facts.first, FOCUS_HEIGHT);
  LOKA_VERIFY(events.empty());
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(events.size() == 1 && events[0] == FOCUS_HEIGHT);
}
void testFocusNullRefusesSceneless()
{
  Facts facts;
  NullScenePlatformController platform;
  Scene unrelated((Boundary<EmptyFocusRoot>()));
  {
    EditTextNode bare(EditTextProps().focusedAs(facts.first, FOCUS_HEIGHT));
    LayoutState bounds;
    bounds.width = 240;
    bounds.height = 320;
    platform.projectLayoutForTesting(&bare, bounds);
    LOKA_VERIFY(bare.getContext());
    platform.simulateNativeFocus(bare.getContext());
    // No Window owns this unrelated Scene; no publication entry is available.
    LOKA_VERIFY(!(facts.first.state()->get() != Fact::none()));
  }
  // An unregistered native identity must not survive the bare control's lifetime.
  NodeContext *answer = 0;
  LOKA_VERIFY(platform.readNativeFocus(answer) && !answer);
  // No Window owns this unrelated Scene; no publication entry is available.
  LOKA_VERIFY(!(facts.first.state()->get() != Fact::none()));
}

void testFocusSourceLifetime()
{
  Facts facts;
  for (int order = 0; order != 2; ++order)
  {
    NullScenePlatformController *controller = new NullScenePlatformController();
    EditTextNode *node = new EditTextNode(EditTextProps());
    node->setContext(new NodeContext(node));
    controller->simulateNativeFocus(node->getContext());
    NodeContext *answer = 0;
    LOKA_VERIFY(controller->readNativeFocus(answer) && answer == node->getContext());
    if (order)
    {
      delete controller;
      delete node;
    }
    else
    {
      delete node;
      LOKA_VERIFY(controller->readNativeFocus(answer) && !answer);
      delete controller;
    }
  }
  Fixture f;
  f.platform.simulateNativeFocus(f.weight.getContext());
  LifecycleFactTestAccess::MarkSubtreeRetired(&f.weight);
  NodeContext *answer = f.height.getContext();
  LOKA_VERIFY(f.platform.readNativeFocus(answer) && !answer);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
}

void testFocusLogicalEligibility()
{
  Fixture f;
  // Retained members are in the Scene, but a native source cannot make them attached.
  NotifySubtreeNodeDetached(&f.weight);
  f.platform.simulateNativeFocus(f.weight.getContext());
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  NotifySubtreeNodeAttached(&f.weight);
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
  EditTextNode unbound((EditTextProps()));
  project(f.platform, f.scene, unbound);
  f.focus(unbound);
  LOKA_VERIFY(!SceneFocusTestAccess::published(*unbound.asFocusParticipant()));
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  NodeContext *answer = 0;
  LOKA_VERIFY(f.platform.readNativeFocus(answer) && answer == unbound.getContext());
  LifecycleFactTestAccess::MarkSubtreeRetired(&unbound);
  unbound.setContext(0);
}

namespace
{
  void publicationGate(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    const unsigned reads = f.platform.reads;
    loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
    LOKA_VERIFY(f.platform.reads == reads);
  }
  void runGate(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(f.scene.isRunInProgress());
    publicationGate(data);
  }
} // namespace
void testFocusSceneRun()
{
  Fixture f;
  f.platform.simulateNativeFocus(f.height.getContext());
  f.platform.duringApply = &runGate;
  f.platform.data = &f;
  f.scene.invalidate(NODE_DIRTY_MYSELF);
  f.platform.duringApply = 0;
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
}
void testFocusPublicationGate()
{
  Fixture f;
  f.platform.simulateNativeFocus(f.height.getContext());
  SceneFocusTestAccess::duringPublication(f.scene.focus(), &publicationGate, &f);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
}

namespace
{
  struct CardData
  {
    struct { Focus<FocusTestField> first; } facts;
  };
  CardData *cardData = 0;
  FocusTestField cardKey = FOCUS_HEIGHT;
  class Card : public BoundaryNodeFor<Card>
  {
  public:
    Focus<FocusTestField> &focus;
    explicit Card(const BoundaryPropsFor<Card> &p)
        : BoundaryNodeFor<Card>(p), focus(cardData->facts.first)
    {
      this->state(this->focus);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(EditText(EditTextProps().focusedAs(this->focus, cardKey)));
    }
  };
  EditTextNode *cardField(Scene &scene)
  {
    BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    return root->childrenHead()->asEditTextNode();
  }
  struct ApplyingProbe
  {
    Window *window;
    App *app;
    ReadController *controller;
    unsigned calls;
    static void run(void *data)
    {
      ApplyingProbe &p = *static_cast<ApplyingProbe *>(data);
      // Install projects outside a Scene run, inside manager apply and App flush.
      if (p.window->scene()->isRunInProgress())
        return;
      ++p.calls;
      const unsigned reads = p.controller->reads;
      loka::app::testing::WindowTestAccess::reconcileFocus(*p.window);
      LOKA_VERIFY(p.controller->reads == reads);
      p.app->reconcileFocus();
      LOKA_VERIFY(p.controller->reads == reads);
    }
  };
} // namespace
namespace {
  void cardCleared(void *data) { static_cast<std::vector<int> *>(data)->push_back(0); }
}
void testFocusSceneReplacement()
{
  CardData data, replacementData;
  cardData = &data;
  cardKey = FOCUS_HEIGHT;
  NullPlatformContext context;
  ReadController controller;
  WindowProps props;
  props.scene(new Scene(Boundary<Card>()));
  NullWindow window(&context, props, &controller);
  WindowAdmissionTestApp app(window);
  app.flush();
  controller.simulateNativeFocus(cardField(*window.scene())->getContext());
  app.reconcileFocus();
  std::vector<int> events;
  data.facts.first.state()->bind(&cardCleared, &events, false, true);
  cardKey = FOCUS_WEIGHT;
  {
    // An independent prepared projection exercises wrong-Scene membership.
    // Tear it down before preparing the real replacement: mount refuses a
    // second mount of the same Scene, and state handles follow their first root.
    CardData preparedData;
    cardData = &preparedData;
    ReadController preparationController;
    Scene prepared((Boundary<Card>()));
    LOKA_VERIFY(prepared.mount(&preparationController));
    loka::dsl::testing::SceneTestAccess::prepareComposition(prepared);
    EditTextNode *target = cardField(prepared);
    target->setContext(new NodeContext(target));
    controller.simulateNativeFocus(target->getContext());
    app.reconcileFocus();
    LOKA_VERIFY(events.size() == 1 && events[0] == 0);
    LOKA_VERIFY(!(data.facts.first.state()->get() != Fact::none()));
    controller.simulateNativeFocus(cardField(*window.scene())->getContext());
    app.reconcileFocus();
    loka::dsl::testing::SceneTestAccess::unmount(prepared);
  }
  cardData = &replacementData;
  Scene *next = new Scene(Boundary<Card>());
  LOKA_VERIFY(window.sceneManager()->commitTransaction(0, next));
  events.clear();
  data.facts.first.state()->bind(&cardCleared, &events, false, true);
  ApplyingProbe probe = {&window, &app, &controller, 0};
  controller.duringApply = &ApplyingProbe::run;
  controller.data = &probe;
  app.flush();
  controller.duringApply = 0;
  LOKA_VERIFY(probe.calls > 0 && window.scene() == next);
  EditTextNode *target = cardField(*next);
  LOKA_VERIFY(events.size() == 1 && events[0] == 0);
  controller.simulateNativeFocus(target->getContext());
  app.reconcileFocus();
  LOKA_VERIFY(events.size() == 1 && events[0] == 0);
  LOKA_VERIFY(replacementData.facts.first.state()->get().is(FOCUS_WEIGHT));
}

void testFocusReplaceSameKey()
{
  Fixture f;
  f.focus(f.height);
  std::vector<int> events;
  Watch watch(f.facts.first, events);
  EditTextNode replacement(EditTextProps().focusedAs(f.facts.first, FOCUS_HEIGHT));
  project(f.platform, f.scene, replacement);
  // Local REPLACE attaches the new branch before retiring the old branch.
  replace(f.height, f.facts.first, FOCUS_HEIGHT);
  LOKA_VERIFY(events.empty());
  LifecycleFactTestAccess::MarkSubtreeRetired(&f.height);
  f.focus(replacement);
  LOKA_VERIFY(events.size() == 2 && events[0] == 0 && events[1] == FOCUS_HEIGHT);
  LifecycleFactTestAccess::MarkSubtreeRetired(&replacement);
  replacement.setContext(0);
}

void testFocusAudit()
{
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  for (int scenario = 0; scenario != 5; ++scenario)
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      Fixture f, other;
      if (scenario == 0)
      {
        replace(f.weight, f.facts.first, FOCUS_HEIGHT);
        f.focus(f.height);
      }
      else if (scenario == 1)
      {
        f.focus(f.weight);
        replace(f.weight, f.facts.first, FOCUS_HEIGHT);
      }
      else if (scenario == 2)
      {
        replace(other.weight, f.facts.first, FOCUS_OTHER);
        f.focus(f.height);
        other.focus(other.weight);
      }
      else if (scenario == 3)
      {
        f.focus(f.height);
        other.focus(other.weight);
        replace(other.weight, f.facts.first, FOCUS_OTHER);
      }
      else
      {
        // Neither Scene has published: only placement can detect this borrow.
        replace(other.weight, f.facts.first, FOCUS_OTHER);
        other.app.reconcileFocus();
      }
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#else
  std::printf("[skip] focus audit death pins require Linux debug without ASan.\n");
#endif
}

void testFocusWindowGates()
{
  Fixture f;
  f.platform.simulateNativeFocus(f.height.getContext());
  loka::dsl::testing::SceneTestAccess::withoutRoot(f.scene, &publicationGate, &f);
  f.window.live = false;
  publicationGate(&f);
  f.window.live = true;
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
  // A rail may decline while retaining the last publication.
  f.platform.answered = false;
  f.platform.clearSimulatedFocus();
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
  f.platform.answered = true;
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  // attached_ publishes before teardown removes the root: isolate this gate.
  f.scene.getAttachedState()->bind(&publicationGate, &f, false);
  loka::dsl::testing::SceneTestAccess::updateAttached(f.scene, false);
  f.scene.getAttachedState()->unbind(&publicationGate, &f);
  const unsigned reads = f.platform.reads;
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(f.platform.reads == reads);
  // Bare Window/Scene and rail loss must not ask a dead controller.
  NullPlatformContext context;
  WindowProps emptyProps;
  NullWindow empty(&context, emptyProps, &f.platform);
  loka::app::testing::WindowTestAccess::reconcileFocus(empty);
  LOKA_VERIFY(f.platform.reads == reads);
  f.window.destroyScenePlatform();
  loka::app::testing::WindowTestAccess::reconcileFocus(f.window);
  LOKA_VERIFY(f.platform.reads == reads);
}

namespace
{
  struct FlushProbe
  {
    App *app;
    ReadController *other;
    unsigned calls;
    static void run(void *data)
    {
      FlushProbe &p = *static_cast<FlushProbe *>(data);
      ++p.calls;
      const unsigned reads = p.other->reads;
      p.app->reconcileFocus();
      LOKA_VERIFY(p.other->reads == reads);
    }
  };
} // namespace
void testFocusAppFlushGate()
{
  Fixture first, second;
  WindowAdmissionTestApp app(first.window, &second.window);
  second.platform.simulateNativeFocus(second.height.getContext());
  FlushProbe probe = {&app, &second.platform, 0};
  first.platform.duringApply = &FlushProbe::run;
  first.platform.data = &probe;
  first.scene.requestBoundaryUpdate(
      loka::dsl::testing::SceneTestAccess::rootBoundary(first.scene), NODE_DIRTY_MYSELF, false);
  app.flush();
  first.platform.duringApply = 0;
  LOKA_VERIFY(probe.calls > 0);
  LOKA_VERIFY(!(second.facts.first.state()->get() != Fact::none()));
  app.reconcileFocus();
  LOKA_VERIFY(second.facts.first.state()->get().is(FOCUS_HEIGHT));
}

namespace
{
  struct RebindObserver
  {
    EditTextNode *target;
    Focus<FocusTestField> *next;
    static void changed(void *data)
    {
      RebindObserver &p = *static_cast<RebindObserver *>(data);
      replace(*p.target, *p.next, FOCUS_OTHER);
    }
  };
} // namespace
void testFocusObserverRebindsTarget()
{
  Fixture f;
  Facts third;
  third.declareIn(*loka::dsl::testing::SceneTestAccess::rootBoundary(f.scene));
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  f.focus(f.height);
  RebindObserver observer = {&f.weight, &third.first};
  f.facts.first.state()->bind(&RebindObserver::changed, &observer, false);
  f.focus(f.weight);
  f.facts.first.state()->unbind(&RebindObserver::changed, &observer);
  LOKA_VERIFY(!(f.facts.second.state()->get() != Fact::none()));
  LOKA_VERIFY(third.first.state()->get().is(FOCUS_OTHER));
  // Release the borrowing row before this extra fact owner dies.
  NotifySubtreeNodeDetached(&f.weight);
}

void testFocusObserverCutsEdge()
{
  Fixture f;
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  std::vector<int> events;
  Watch first(f.facts.first, events, 10), second(f.facts.second, events, 20);
  f.focus(f.height);
  events.clear();
  first.parkAndReturn = &f.weight;
  f.focus(f.weight);
  LOKA_VERIFY(events.size() == 1 && events[0] == 10);
  LOKA_VERIFY(f.weight.lifecycleFact() == NODE_FACT_ATTACHED);
  LOKA_VERIFY(!SceneFocusTestAccess::published(*f.weight.asFocusParticipant()));
  f.focus(f.weight);
  LOKA_VERIFY(events.size() == 2 && events[1] == 22);
}

namespace
{
  class BorrowedCloseApp : public WindowAdmissionTestApp
  {
  public:
    BorrowedCloseApp(Window &first, Window &second, Window *third = 0)
        : WindowAdmissionTestApp(first, &second)
    {
      if (third)
        this->group_->adopt(third);
    }
    void add(Window &window) { this->group_->adopt(&window); }
    ~BorrowedCloseApp()
    {
      this->flush();
    }
    virtual void windowClosed(Window *) {}
  };
  struct ClosePairObserver
  {
    App *app;
    Window *first;
    Window *second;
    static void changed(void *data)
    {
      ClosePairObserver &p = *static_cast<ClosePairObserver *>(data);
      p.app->requestWindowClose(p.first);
      p.app->requestWindowClose(p.second);
    }
  };
  struct CloseObserver
  {
    App *app;
    Window *window;
    static void changed(void *data)
    {
      CloseObserver &p = *static_cast<CloseObserver *>(data);
      p.app->requestWindowClose(p.window);
    }
  };
} // namespace
void testFocusAppReenumerates()
{
  Fixture first, second;
  BorrowedCloseApp app(first.window, second.window);
  first.platform.simulateNativeFocus(first.height.getContext());
  second.platform.simulateNativeFocus(second.weight.getContext());
  CloseObserver observer = {&app, &first.window};
  first.facts.first.state()->bind(&CloseObserver::changed, &observer, false);
  app.reconcileFocus();
  first.facts.first.state()->unbind(&CloseObserver::changed, &observer);
  LOKA_VERIFY(second.facts.first.state()->get().is(FOCUS_WEIGHT));
  const unsigned reads = first.platform.reads;
  app.reconcileFocus();
  LOKA_VERIFY(first.platform.reads == reads);
}

void testFocusAppReenumeratesMultipleRemovals()
{
  Fixture first, second, third;
  BorrowedCloseApp app(first.window, second.window, &third.window);
  first.platform.simulateNativeFocus(first.height.getContext());
  second.platform.simulateNativeFocus(second.weight.getContext());
  third.platform.simulateNativeFocus(third.height.getContext());
  ClosePairObserver observer = {&app, &first.window, &second.window};
  second.facts.first.state()->bind(&ClosePairObserver::changed, &observer, false);
  const unsigned firstReads = first.platform.reads;
  const unsigned secondReads = second.platform.reads;
  const unsigned thirdReads = third.platform.reads;
  app.reconcileFocus();
  second.facts.first.state()->unbind(&ClosePairObserver::changed, &observer);
  LOKA_VERIFY(third.facts.first.state()->get().is(FOCUS_HEIGHT));
  LOKA_VERIFY(first.platform.reads == firstReads + 1);
  LOKA_VERIFY(second.platform.reads == secondReads + 1);
  LOKA_VERIFY(third.platform.reads == thirdReads + 1);
  app.reconcileFocus();
  LOKA_VERIFY(first.platform.reads == firstReads + 1);
  LOKA_VERIFY(second.platform.reads == secondReads + 1);
  LOKA_VERIFY(third.platform.reads == thirdReads + 2);
}

namespace
{
  class NullCompletionApp : public NullApp
  {
  public:
    explicit NullCompletionApp(Window &window)
        : NullApp(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &window));
    }
    ~NullCompletionApp()
    {
      this->group_->build();
    }
  };
} // namespace
void testFocusNullCompletion()
{
  Fixture f;
  NullCompletionApp app(f.window);
  f.platform.simulateNativeFocus(f.height.getContext());
  app.run();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
  f.platform.simulateNativeFocus(f.weight.getContext());
  loka::app::testing::AppTestAccess::flushWindowInvalidations(app);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
}

namespace
{
  class ForeignFocusNode : public Node
  {
  public:
    FocusRow row;
    virtual FocusRow *asFocusParticipant() { return &this->row; }
  };
  class ForeignFocusController : public NullScenePlatformController
  {
  public:
    NodeContext *foreign;
    ForeignFocusController() : foreign(0) {}
    virtual bool readNativeFocus(NodeContext *&out)
    {
      if (!this->foreign)
        return NullScenePlatformController::readNativeFocus(out);
      out = this->foreign;
      return true;
    }
  };
}

void testFocusForeignRow()
{
  Facts facts;
  NullPlatformContext context;
  ForeignFocusController controller;
  NullWindow window(&context, Fixture::props(), &controller);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene &scene = *window.scene();
  facts.declareIn(*loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  EditTextNode field(EditTextProps().focusedAs(facts.first, FOCUS_HEIGHT));
  project(controller, scene, field);
  ForeignFocusNode foreign;
  ComponentContext composition;
  BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
  composition.setBoundary(root);
  composition.setStateOwner(root);
  composition.setScene(&scene);
  BoundaryNode::composeSubtree(&foreign, composition, COMPOSE_EVENT_ATTACH, root);
  foreign.setContext(new NodeContext(&foreign));
  LOKA_VERIFY(SceneFocusTestAccess::owner(foreign.row) == &scene.focus());
  LOKA_VERIFY(SceneFocusTestAccess::owner(field.focusParticipant) == &scene.focus());
  LOKA_VERIFY(!FocusParticipant::from(&foreign.row));
  LOKA_VERIFY(!FocusParticipant::from(0));
  LOKA_VERIFY(FocusParticipant::from(&field.focusParticipant) == &field.focusParticipant);

  // Null's simulation door refuses foreign extensions too.
  controller.simulateNativeFocus(foreign.getContext());
  NodeContext *answer = foreign.getContext();
  LOKA_VERIFY(controller.readNativeFocus(answer) && !answer);
  // A rail can still answer with a foreign node: the publisher must check it.
  controller.foreign = foreign.getContext();
  app.reconcileFocus();
  LOKA_VERIFY(!(facts.first.state()->get() != Fact::none()));
  LOKA_VERIFY(!SceneFocusTestAccess::published(foreign.row));
  controller.foreign = 0;
  controller.simulateNativeFocus(field.getContext());
  app.reconcileFocus();
  // Publishing the real field audits both members, skipping the kernel row.
  LOKA_VERIFY(facts.first.state()->get().is(FOCUS_HEIGHT));
  LOKA_VERIFY(SceneFocusTestAccess::published(field.focusParticipant));
  controller.foreign = foreign.getContext();
  app.reconcileFocus();
  LOKA_VERIFY(!(facts.first.state()->get() != Fact::none()));
  LOKA_VERIFY(!SceneFocusTestAccess::published(foreign.row));
  controller.foreign = 0;
  LifecycleFactTestAccess::MarkSubtreeRetired(&foreign);
  LifecycleFactTestAccess::MarkSubtreeRetired(&field);
  field.setContext(0);
}

void testFocusAppVisitedSpill()
{
  Fixture *windows[10];
  unsigned before[10];
  for (unsigned i = 0; i != 10; ++i)
  {
    windows[i] = new Fixture();
    windows[i]->platform.simulateNativeFocus(windows[i]->height.getContext());
    before[i] = windows[i]->platform.reads;
  }
  {
    BorrowedCloseApp app(windows[0]->window, windows[1]->window);
    for (unsigned i = 2; i != 10; ++i)
      app.add(windows[i]->window);
    for (unsigned completion = 1; completion != 3; ++completion)
    {
      app.reconcileFocus();
      for (unsigned i = 0; i != 10; ++i)
      {
        LOKA_VERIFY(windows[i]->platform.reads == before[i] + completion);
        LOKA_VERIFY(windows[i]->facts.first.state()->get().is(FOCUS_HEIGHT));
      }
    }
  }
  for (unsigned i = 0; i != 10; ++i)
    delete windows[i];
}

namespace
{
  void noPublishedContext(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LOKA_VERIFY(!loka::app::testing::WindowTestAccess::publishedFocusContext(f.window));
  }
}

void testFocusPublishedContext()
{
  typedef loka::app::testing::WindowTestAccess Access;
  Fixture f;
  LOKA_VERIFY(!Access::publishedFocusContext(f.window));
  f.focus(f.height);
  LOKA_VERIFY(Access::publishedFocusContext(f.window) == f.height.getContext());
  // A native reactivation read does not ask the inactive rail or mutate the fact.
  f.platform.answered = false;
  const unsigned reads = f.platform.reads;
  LOKA_VERIFY(Access::publishedFocusContext(f.window) == f.height.getContext());
  LOKA_VERIFY(f.platform.reads == reads);
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
  loka::dsl::testing::SceneTestAccess::withoutRoot(f.scene, &noPublishedContext, &f);
  {
    Fixture gated;
    gated.focus(gated.height);
    // Observe the Scene's detached fact before teardown clears its root/rows.
    gated.scene.getAttachedState()->bind(&noPublishedContext, &gated, false);
    loka::dsl::testing::SceneTestAccess::updateAttached(gated.scene, false);
    gated.scene.getAttachedState()->unbind(&noPublishedContext, &gated);
    noPublishedContext(&gated);
  }
  NotifySubtreeNodeDetached(&f.height);
  LOKA_VERIFY(!Access::publishedFocusContext(f.window));
  // Force inconsistent endpoints only through kernel test access to pin each
  // defensive query wall independently of ordinary detach cutting publication.
  SceneFocusTestAccess::publish(f.scene.focus(), f.height.focusParticipant);
  LOKA_VERIFY(!Access::publishedFocusContext(f.window));
  NotifySubtreeNodeAttached(&f.height);
  LOKA_VERIFY(Access::publishedFocusContext(f.window) == f.height.getContext());
  {
    Fixture other;
    SceneFocusTestAccess::publish(f.scene.focus(), other.height.focusParticipant);
    LOKA_VERIFY(!Access::publishedFocusContext(f.window));
  }
  FocusRow foreign;
  SceneFocusTestAccess::join(f.scene.focus(), foreign);
  SceneFocusTestAccess::publish(f.scene.focus(), foreign);
  LOKA_VERIFY(!Access::publishedFocusContext(f.window));
  NullWindow empty(&f.context, WindowProps(), &f.platform);
  LOKA_VERIFY(!Access::publishedFocusContext(empty));
}

void testFocusPostCompletion()
{
  Fixture f;
  f.facts.first.post(FOCUS_HEIGHT);
  f.facts.first.post(FOCUS_WEIGHT);
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  f.app.reconcileFocus();
  NodeContext *native = 0;
  LOKA_VERIFY(f.platform.readNativeFocus(native) && native == f.weight.getContext());
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
  LOKA_VERIFY(!f.weight.props.focus_.requested());
  std::printf("[sizes] FocusBinding=%lu EditTextProps=%lu TextEditorProps=%lu\n",
      (unsigned long)sizeof(FocusBinding), (unsigned long)sizeof(EditTextProps), (unsigned long)sizeof(TextEditorProps));
}

void testFocusPostDeferred()
{
  Fixture f;
  f.facts.first.post(FOCUS_OTHER);
  f.app.reconcileFocus();
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  EditTextNode later(EditTextProps().focusedAs(f.facts.first, FOCUS_OTHER));
  project(f.platform, f.scene, later);
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_OTHER));
  NotifySubtreeNodeDetached(&f.height);
  f.facts.first.post(FOCUS_HEIGHT);
  f.app.reconcileFocus();
  LOKA_VERIFY(f.height.props.focus_.requested());
  NotifySubtreeNodeAttached(&f.height);
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
  LifecycleFactTestAccess::MarkSubtreeRetired(&later);
  later.setContext(0);
}

void testFocusPostInactive()
{
  Fixture f;
  f.platform.answered = false;
  f.facts.first.post(FOCUS_HEIGHT);
  f.app.reconcileFocus();
  LOKA_VERIFY(f.height.props.focus_.requested());
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
  f.platform.answered = true;
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
}

void testFocusPostUserMove()
{
  Fixture f;
  f.focus(f.height);
  f.facts.first.post(FOCUS_HEIGHT);
  f.platform.simulateNativeFocus(f.weight.getContext());
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
  LOKA_VERIFY(!f.height.props.focus_.requested());
  replace(f.weight, f.facts.second, FOCUS_WEIGHT);
  f.focus(f.height);
  f.facts.first.post(FOCUS_HEIGHT);
  f.platform.simulateNativeFocus(f.weight.getContext());
  f.app.reconcileFocus();
  LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_HEIGHT));
}

namespace
{
  struct ConsumeCallback
  {
    Fixture &fixture;
    unsigned mode;
    unsigned calls;
    static void run(void *data)
    {
      ConsumeCallback &p = *static_cast<ConsumeCallback *>(data);
      ++p.calls;
      if (p.calls != 1) return;
      if (p.mode == 0)
      {
        LifecycleFactTestAccess::MarkSubtreeRetired(&p.fixture.height);
        p.fixture.height.setContext(0);
      }
      if (p.mode == 1) replace(p.fixture.height, p.fixture.facts.second, FOCUS_HEIGHT);
      p.fixture.facts.first.post(FOCUS_WEIGHT);
      p.fixture.app.reconcileFocus();
    }
  };
}
void testFocusPostConsumeCallbacks()
{
  for (unsigned mode = 0; mode != 3; ++mode)
  {
    Fixture f;
    f.facts.first.post(FOCUS_HEIGHT);
    typedef loka::app::detail::FocusTarget<FocusTestField> Target;
    State<Target> *pending = const_cast<State<Target> *>(static_cast<const State<Target> *>(f.height.props.focus_.request()));
    ConsumeCallback callback = {f, mode, 0};
    pending->bind(&ConsumeCallback::run, &callback, false);
    f.app.reconcileFocus();
    pending->unbind(&ConsumeCallback::run, &callback);
    LOKA_VERIFY(callback.calls >= 1);
    LOKA_VERIFY(f.weight.props.focus_.requested());
    LOKA_VERIFY(mode == 2 ? f.facts.first.state()->get().is(FOCUS_HEIGHT)
                          : !(f.facts.first.state()->get() != Fact::none()));
    f.app.reconcileFocus();
    LOKA_VERIFY(f.facts.first.state()->get().is(FOCUS_WEIGHT));
  }
}

namespace
{
  class NativeCallbackController : public ReadController
  {
  public:
    Node *retire;
    bool inWrite;
    NativeCallbackController() : retire(0), inWrite(false) {}
    virtual bool applyNativeFocus(NodeContext &context)
    {
      this->inWrite = true;
      const bool result = NullScenePlatformController::applyNativeFocus(context);
      this->inWrite = false;
      return result;
    }
    virtual bool readNativeFocus(NodeContext *&out)
    {
      if (this->inWrite && this->retire)
      {
        Node *target = this->retire;
        this->retire = 0;
        LifecycleFactTestAccess::MarkSubtreeRetired(target);
        target->setContext(0);
      }
      return ReadController::readNativeFocus(out);
    }
  };
}
void testFocusPostNativeCallback()
{
  NullPlatformContext context;
  NativeCallbackController platform;
  FocusWindow window(&context, Fixture::props(), &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  Focus<FocusTestField> fact;
  StateBatchBase::CreateImmediateState(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()), fact);
  EditTextNode target(EditTextProps().focusedAs(fact, FOCUS_HEIGHT));
  project(platform, *window.scene(), target);
  platform.retire = &target;
  fact.post(FOCUS_HEIGHT);
  app.reconcileFocus();
  LOKA_VERIFY(!platform.retire && !target.getContext());
  LOKA_VERIFY(!(fact.state()->get() != Fact::none()));
  LOKA_VERIFY(!target.props.focus_.requested());
}

#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Show.hpp"
namespace
{
  class PostScreen : public BoundaryNodeFor<PostScreen>
  {
  public:
    Focus<FocusTestField> focus;
    NodeState<bool> shown, parked;
    explicit PostScreen(const BoundaryPropsFor<PostScreen> &p) : BoundaryNodeFor<PostScreen>(p)
    {
      this->declareStates(4).state(this->focus, FOCUS_HEIGHT).state(this->shown, true).state(this->parked, true);
      LOKA_VERIFY(!this->focus.isValid());
      this->focus.post(FOCUS_WEIGHT);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(F() << (Show(*this->shown.state()).destroyOnDetach()
                         << EditText(EditTextProps().focusedAs(this->focus, FOCUS_HEIGHT)))
                    << EditText(EditTextProps().focusedAs(this->focus, FOCUS_WEIGHT))
                    << (Show(*this->parked.state()) << EditText(EditTextProps().focusedAs(this->focus, FOCUS_OTHER))));
    }
    static void hide(void *data)
    {
      PostScreen &screen = *static_cast<PostScreen *>(data);
      if (screen.focus.state()->get().is(FOCUS_HEIGHT)) screen.shown.set(false);
    }
  };
}
void testFocusPostInitialAndShow()
{
  NullPlatformContext context;
  ReadController platform;
  WindowProps props;
  props.scene(new Scene(Boundary<PostScreen>()));
  FocusWindow window(&context, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  PostScreen &screen = *static_cast<PostScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_HEIGHT));
  screen.parked.set(false);
  screen.focus.post(FOCUS_OTHER);
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_HEIGHT));
  screen.parked.set(true);
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_OTHER));
  screen.focus.post(FOCUS_HEIGHT);
  app.reconcileFocus();
  screen.shown.set(false);
  screen.focus.post(FOCUS_HEIGHT);
  app.reconcileFocus();
  LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
  screen.shown.set(true);
  screen.focus.state()->bind(&PostScreen::hide, &screen, false);
  app.reconcileFocus();
  screen.focus.state()->unbind(&PostScreen::hide, &screen);
  LOKA_VERIFY(!screen.shown.get());
  LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
}
void testFocusPostInvalidBundle()
{
  Focus<FocusTestField> fact;
  fact.post(FOCUS_HEIGHT);
  LOKA_VERIFY(!fact.isValid());
  LOKA_VERIFY(!EditTextProps().focusedAs(fact, FOCUS_HEIGHT).focus_.state());
  LOKA_VERIFY(!TextEditorProps().focusedAs(fact, FOCUS_HEIGHT).focus_.state());
}

#include "support/LokaAllocFailure.hpp"
namespace
{
  class FocusFailureOwner : public BoundaryInnerStateOwner
  {
  public:
    unsigned failures;
    FocusFailureOwner() : failures(0) {}
    virtual void noteStateAllocationFailure() { ++this->failures; }
  };
}
void testFocusPostAllocationFailure()
{
  for (int box = 1; box != 3; ++box)
  {
    loka::core::testing::failLokaAllocRaw("StateOwner", "MutableState", box);
    {
      FocusFailureOwner owner;
      Focus<FocusTestField> focus;
      StateBatchBase::CreateImmediateState(&owner, focus);
      LOKA_VERIFY(owner.failures == 1 && !focus.isValid());
      focus.post(FOCUS_HEIGHT);
      LOKA_VERIFY(!EditTextProps().focusedAs(focus, FOCUS_HEIGHT).focus_.state());
      LOKA_VERIFY(!TextEditorProps().focusedAs(focus, FOCUS_HEIGHT).focus_.state());
      if (focus.state()) LOKA_VERIFY(!(focus.state()->get() != Fact::none()));
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
}

namespace
{
  class PostDeclarationScreen : public BoundaryNodeFor<PostDeclarationScreen>
  {
  public:
    Focus<FocusTestField> focus;
    explicit PostDeclarationScreen(const BoundaryPropsFor<PostDeclarationScreen> &p)
      : BoundaryNodeFor<PostDeclarationScreen>(p)
    {
      this->state(this->focus, FOCUS_WEIGHT);
      this->focus.post(FOCUS_HEIGHT); // Invalid until owner connection: must not overwrite initializer.
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(EditText(EditTextProps().focusedAs(this->focus, FOCUS_WEIGHT)));
    }
  };
}
void testFocusPostDeclaration()
{
  NullPlatformContext context;
  ReadController platform;
  WindowProps props;
  props.scene(new Scene(Boundary<PostDeclarationScreen>()));
  FocusWindow window(&context, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  PostDeclarationScreen &screen = *static_cast<PostDeclarationScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_WEIGHT));
}

void testFocusPostD1Continues()
{
  Fixture f;
  EditTextNode other(EditTextProps().focusedAs(f.facts.second, FOCUS_OTHER));
  project(f.platform, f.scene, other);
  f.focus(f.height);
  f.facts.first.post(FOCUS_HEIGHT);
  f.facts.second.post(FOCUS_OTHER);
  f.platform.simulateNativeFocus(f.weight.getContext());
  const unsigned reads = f.platform.reads;
  f.app.reconcileFocus();
  LOKA_VERIFY(!f.height.props.focus_.requested());
  LOKA_VERIFY(f.facts.second.state()->get().is(FOCUS_OTHER));
  // Admission, Null write confirmation, common readback; no D1-only extra read.
  LOKA_VERIFY(f.platform.reads == reads + 3);
  LifecycleFactTestAccess::MarkSubtreeRetired(&other);
  other.setContext(0);
}

#include "app/nodes/nestable/LazyView.hpp"
namespace
{
  class FocusItem;
  struct FocusItemProps : NodePropsBase<FocusItemProps>
  {
    typedef FocusItem NodeType;
    typedef FocusItemProps TypeTag;
    Focus<int> *focus;
    int key;
    FocusItemProps(Focus<int> *value = 0, int id = 0) : focus(value), key(id) {}
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId()) return false;
      const FocusItemProps &other = static_cast<const FocusItemProps &>(rhs);
      return this->focus != other.focus ? std::less<Focus<int> *>()(this->focus, other.focus) : this->key < other.key;
    }
    bool operator!=(const FocusItemProps &other) const { return this->focus != other.focus || this->key != other.key; }
  };
  class FocusItem : public ComponentNodeWithProps<FocusItemProps>
  {
  public:
    explicit FocusItem(const FocusItemProps &p) : ComponentNodeWithProps<FocusItemProps>(p) {}
    virtual void composeChildren(NodeComposition &c)
    {
      c.declare(EditText(EditTextProps().focusedAs(*this->props.focus, this->props.key)));
    }
  };
  class LazyPostScreen : public BoundaryNodeFor<LazyPostScreen>
  {
  public:
    Focus<int> focus;
    NodeState<Frame> viewport;
    ObservableList<FocusItemProps> list;
    explicit LazyPostScreen(const BoundaryPropsFor<LazyPostScreen> &p) : BoundaryNodeFor<LazyPostScreen>(p)
    {
      this->state(this->focus, 31);
      this->state(this->viewport, Frame(0, 0, 200, 40));
      LOKA_VERIFY(this->list.attach(this->tracker()->asPushTracker(), 32) == ATTACH_OK);
      for (int i = 0; i != 32; ++i)
        LOKA_VERIFY(this->list.insert(static_cast<unsigned short>(i), FocusItemProps(&this->focus, i)) == EDIT_OK);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(LazyColumn(this->list, reservation::SeatNodes<reservation::Nodes<FocusItem, 1,
          reservation::Nodes<EditTextNode, 1> > >(), 8).cells(200, 20).viewport(*this->viewport.state()));
    }
  };
  bool hasFocusKey(Node *node, Focus<int> &focus, int key)
  {
    EditTextNode *edit = node->asEditTextNode();
    if (edit && edit->props.focus_.same(EditTextProps().focusedAs(focus, key).focus_)) return true;
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
      if (hasFocusKey(child, focus, key)) return true;
    return false;
  }
}
void testFocusPostLazyRow()
{
  NullPlatformContext context;
  ReadController platform;
  WindowProps props;
  props.scene(new Scene(Boundary<LazyPostScreen>()));
  FocusWindow window(&context, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  LazyPostScreen &screen = *static_cast<LazyPostScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(!hasFocusKey(&screen, screen.focus, 31));
  app.reconcileFocus();
  LOKA_VERIFY(!screen.focus.state()->get().is(31));
  screen.viewport.set(Frame(0, 600, 200, 40));
  app.flush();
  LOKA_VERIFY(hasFocusKey(&screen, screen.focus, 31));
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(31));
}

namespace
{
  class CapacityPostScreen : public BoundaryNodeFor<CapacityPostScreen>
  {
  public:
    Focus<FocusTestField> focus;
    explicit CapacityPostScreen(const BoundaryPropsFor<CapacityPostScreen> &p) : BoundaryNodeFor<CapacityPostScreen>(p)
    {
      this->declareStates(1).state(this->focus, FOCUS_HEIGHT);
    }
    virtual void composeNode(NodeComposition &c)
    { c.declare(EditText(EditTextProps().focusedAs(this->focus, FOCUS_HEIGHT))); }
  };
}
void testFocusPostBatchCapacity()
{
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    CapacityPostScreen screen((BoundaryPropsFor<CapacityPostScreen>()));
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#elif defined(NDEBUG)
  // Existing batch overflow fallback registers the excess row individually.
  NullPlatformContext context;
  ReadController platform;
  WindowProps props;
  props.scene(new Scene(Boundary<CapacityPostScreen>()));
  FocusWindow window(&context, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  CapacityPostScreen &screen = *static_cast<CapacityPostScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(screen.focus.isValid());
  app.reconcileFocus();
  LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_HEIGHT));
#else
  std::printf("[skip] capacity death pin requires Linux debug without ASan; release tests overflow fallback.\n");
#endif
}

namespace
{
  void hideDuringConsume(void *data)
  {
    PostScreen &screen = *static_cast<PostScreen *>(data);
    screen.shown.set(false);
    screen.shown.set(true);
    screen.shown.set(false); // Reentrant runs can reclaim the original row's arena resident.
  }
}
void testFocusPostConsumeRunsScene()
{
  NullPlatformContext context;
  ReadController platform;
  WindowProps props;
  props.scene(new Scene(Boundary<PostScreen>()));
  FocusWindow window(&context, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  PostScreen &screen = *static_cast<PostScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  typedef loka::app::detail::FocusTarget<FocusTestField> Target;
  const FocusBinding binding = EditTextProps().focusedAs(screen.focus, FOCUS_HEIGHT).focus_;
  State<Target> *pending = const_cast<State<Target> *>(static_cast<const State<Target> *>(binding.request()));
  pending->bind(&hideDuringConsume, &screen, false, true);
  app.reconcileFocus();
  LOKA_VERIFY(!screen.shown.get());
  LOKA_VERIFY(!binding.requested());
  LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
}

#include <ctime>
void testFocusPostHostCost()
{
  const unsigned sizes[] = {0, 1, 32, 256};
  for (unsigned size = 0; size != 4; ++size)
  {
    NullPlatformContext context;
    ReadController platform;
    FocusWindow window(&context, Fixture::props(), &platform);
    WindowAdmissionTestApp app(window);
    app.flush();
    Focus<unsigned> focus;
    StateBatchBase::CreateImmediateState(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()), focus);
    std::vector<EditTextNode *> fields;
    for (unsigned i = 0; i != sizes[size]; ++i)
    {
      EditTextNode *field = new EditTextNode(EditTextProps().focusedAs(focus, i));
      fields.push_back(field);
      project(platform, *window.scene(), *field);
    }
    const unsigned iterations = 20000;
    const unsigned reads = platform.reads;
    const std::clock_t start = std::clock();
    for (unsigned i = 0; i != iterations; ++i)
      loka::app::testing::WindowTestAccess::reconcileFocus(window);
    const double elapsed = static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
    LOKA_VERIFY(platform.reads == reads + iterations);
    std::printf("[host focus completion] M=%u iterations=%u us/completion=%.3f\n", sizes[size], iterations, elapsed * 1e6 / iterations);
    for (unsigned i = 0; i != fields.size(); ++i)
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(fields[i]);
      fields[i]->setContext(0);
      delete fields[i];
    }
  }
}

namespace
{
  class RefusingFocusController : public ReadController
  {
  public:
    virtual bool applyNativeFocus(NodeContext &context)
    { return IPlatformController::applyNativeFocus(context); }
  };
  void retireD1Observation(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.weight);
    f.weight.setContext(0);
  }
}
void testFocusPostNativeRefusal()
{
  NullPlatformContext context;
  RefusingFocusController platform;
  FocusWindow window(&context, Fixture::props(), &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  Focus<FocusTestField> fact;
  StateBatchBase::CreateImmediateState(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()), fact);
  EditTextNode target(EditTextProps().focusedAs(fact, FOCUS_HEIGHT));
  project(platform, *window.scene(), target);
  fact.post(FOCUS_HEIGHT);
  app.reconcileFocus();
  LOKA_VERIFY(!target.props.focus_.requested());
  LOKA_VERIFY(!(fact.state()->get() != Fact::none()));
  LifecycleFactTestAccess::MarkSubtreeRetired(&target);
  target.setContext(0);
}
void testFocusPostD1RetiresObservation()
{
  Fixture f;
  f.focus(f.height);
  f.facts.first.post(FOCUS_HEIGHT);
  typedef loka::app::detail::FocusTarget<FocusTestField> Target;
  State<Target> *pending = const_cast<State<Target> *>(static_cast<const State<Target> *>(f.height.props.focus_.request()));
  pending->bind(&retireD1Observation, &f, false, true);
  f.platform.simulateNativeFocus(f.weight.getContext());
  const unsigned reads = f.platform.reads;
  f.app.reconcileFocus();
  LOKA_VERIFY(f.platform.reads == reads + 1);
  LOKA_VERIFY(!f.weight.getContext() && !f.height.props.focus_.requested());
  LOKA_VERIFY(!(f.facts.first.state()->get() != Fact::none()));
}

void testFocusSeatCompletionProjectsNextTurn()
{
  for (unsigned clock = 0; clock != 2; ++clock)
  {
    NullPlatformContext context;
    ReadController platform;
    WindowProps props;
    props.scene(new Scene(Boundary<PostScreen>()));
    FocusWindow window(&context, props, &platform);
    WindowAdmissionTestApp app(window);
    app.flush();
    Scene &scene = *window.scene();
    PostScreen &screen = *static_cast<PostScreen *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    screen.focus.state()->bind(&PostScreen::hide, &screen, false);
    if (clock)
    {
      Operation turn;
      turn.settle();
      app.admitAndApplyWindows();
      app.reconcileFocus();
      LOKA_VERIFY(!screen.shown.get() && screen.focus.state()->get().is(FOCUS_HEIGHT));
      // C1: second admission precedes close and cannot consume this commit.
      app.admitAndApplyWindows();
      LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_HEIGHT));
      turn.close();
      LOKA_VERIFY(scene.hasPendingInvalidation());
      LOKA_VERIFY(screen.focus.state()->get().is(FOCUS_HEIGHT));
      app.reclaimWindows();
      app.operationLoop();
    }
    else
    {
      app.admitAndApplyWindows();
      app.reconcileFocus();
      app.admitAndApplyWindows();
      app.reclaimWindows();
    }
    LOKA_VERIFY(!screen.shown.get());
    LOKA_VERIFY(!(screen.focus.state()->get() != Fact::none()));
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    screen.focus.state()->unbind(&PostScreen::hide, &screen);
  }
}
