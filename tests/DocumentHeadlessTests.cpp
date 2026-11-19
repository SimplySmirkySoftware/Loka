#include "DocumentHeadlessTests.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/doc/Render.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/internal/NativeResourceReservation.hpp"
#include "app/scene/state/FlowSlot.hpp"
#include "core/diag/LifecycleAudit.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/app/NativeResourceRetirementTestAccess.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/PropsReconciliation.hpp"
#include "support/TextBufferFixture.hpp"
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  template <class Page, class Data> struct Input : NodePropsBase<Input<Page, Data> >
  {
    typedef Page NodeType;
    struct TypeTag
    {
    };
    Data *data;
    explicit Input(Data &value)
        : data(&value)
    {
    }
    bool operator<(const PropsBase &other) const
    {
      return this->data < static_cast<const Input &>(other).data;
    }
  };

  template <class D> void expect(const D &page, const char *expected)
  {
    const std::string prefix("before\0page", 11);
    std::string out(prefix);
    const bool accepted = doc::RenderMarkdown(page, out);
    LOKA_VERIFY(accepted == (expected != 0));
    LOKA_VERIFY(out == prefix + (expected ? expected : ""));
  }

  typedef reservation::Nodes<doc::ParagraphNode, 1, reservation::Nodes<AttributedTextNode, 1> > ParagraphNodes;
  class LateWrite;
  typedef Input<LateWrite, NodeState<int> > WriteProps;
  class LateWrite : public ComponentNodeWithProps<WriteProps>
  {
  public:
    explicit LateWrite(const WriteProps &p)
        : ComponentNodeWithProps<WriteProps>(p)
    {
    }
    virtual void attachNode(NodeComposition &)
    {
      this->props.data->set(1);
    }
    virtual void composeChildren(NodeComposition &) {}
  };

  class BranchPage;
  typedef Input<BranchPage, int> BranchProps;
  class BranchPage : public StdCompositionBoundaryNodeBase<BranchProps>
  {
    NodeState<int> key_;

  public:
    explicit BranchPage(const BranchProps &p)
        : StdCompositionBoundaryNodeBase<BranchProps>(p)
    {
      this->state(this->key_, 0);
    }
    void arm(NodeComposition &c)
    {
      ++*this->props.data;
      NodeState<int> local;
      c.declareStates().state(local, this->key_.get());
      if (local.isValid())
        c.declare(doc::Paragraph(local.get() ? "key1" : "key0"));
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(
          Column() << Match(*this->key_.state()).arm(0, doc::Paragraph("match0")).otherwise(doc::Paragraph("match1"))
                   << Keyed(*this->key_.state(), this, &BranchPage::arm, reservation::SeatNodes<ParagraphNodes>())
                   << Component(WriteProps(this->key_)));
    }
  };

  struct MissingClone : StdCompositionBoundaryDefinition
  {
    virtual NodeDefinitionBase *clone() const
    {
      return 0;
    }
  };

  struct Clock
  {
    unsigned requested, updates;
    explicit Clock(unsigned n)
        : requested(n),
          updates(0)
    {
    }
  };
  class ClockPage;
  typedef Input<ClockPage, Clock> ClockProps;
  class ClockPage : public StdCompositionBoundaryNodeBase<ClockProps>
  {
    typedef StdCompositionBoundaryNodeBase<ClockProps> Base;

  public:
    explicit ClockPage(const ClockProps &p)
        : Base(p)
    {
    }
    virtual void composeWithContext(ComponentContext &c, ComposeEvent event)
    {
      this->Base::composeWithContext(c, event);
      Clock &clock = *this->props.data;
      if (event == COMPOSE_EVENT_UPDATE)
        ++clock.updates;
      if (event != COMPOSE_EVENT_DETACH && clock.updates < clock.requested)
        c.scene()->requestLayoutAfterRun();
      if (event == COMPOSE_EVENT_ATTACH && !clock.requested)
        c.scene()->requestRetiredSubtreeDrainAfterRun();
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(doc::Paragraph("settled"));
    }
  };

  struct CountStep
  {
    typedef int In;
    typedef int Out;
    int *calls;
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      ++*this->calls;
      out = in;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };
  typedef loka::dsl::FlowChain<int, int> Chain;
  struct Lifetime
  {
    int attaches, detaches, publications, releases, calls;
    bool refuse;
    MutableState<int> trigger;
    Chain chain;
    State<SceneLifecycle> *lifecycle;
    explicit Lifetime(bool failure)
        : attaches(0),
          detaches(0),
          publications(0),
          releases(0),
          calls(0),
          refuse(failure),
          trigger(0),
          chain(loka::dsl::Flow() | loka::dsl::Step(1, step())),
          lifecycle(0)
    {
    }
    CountStep step()
    {
      CountStep count = {&this->calls};
      return count;
    }
    static void published(void *p)
    {
      Lifetime &life = *static_cast<Lifetime *>(p);
      LOKA_VERIFY(life.lifecycle->get() == ON_DETACH);
      ++life.publications;
      life.lifecycle = 0;
    }
    static void release(Lifetime *p)
    {
      ++p->releases;
      p->trigger.set(2);
    }
  };
  class LifecyclePage;
  typedef Input<LifecyclePage, Lifetime> LifeProps;
  class LifecyclePage : public StdCompositionBoundaryNodeBase<LifeProps>
  {
    FlowSlot<Chain> flow_;

  public:
    explicit LifecyclePage(const LifeProps &p)
        : StdCompositionBoundaryNodeBase<LifeProps>(p),
          flow_(*this)
    {
    }
    virtual void declareBindings(BindingToken &)
    {
      Lifetime &life = *this->props.data;
      this->flow_.set(life.chain).bindTrigger(&life.trigger);
    }
    virtual void composeNode(NodeComposition &c)
    {
      Lifetime &life = *this->props.data;
      ++life.attaches;
      LOKA_VERIFY(c.scene()->getAttachedState()->get());
      life.lifecycle = c.scene()->getLifecycleState();
      LOKA_VERIFY(life.lifecycle->get() == ON_ATTACH);
      life.lifecycle->bind(&Lifetime::published, &life, false);
      LOKA_VERIFY(c.hold(&life, &Lifetime::release).isValid());
      life.trigger.set(1);
      LOKA_VERIFY(life.calls == 1);
      c.declare(Column() << doc::Paragraph("first") << doc::Paragraph(life.refuse ? "\vrefused" : "second"));
    }
    virtual void detachNode(NodeComposition &c)
    {
      ++this->props.data->detaches;
      LOKA_VERIFY(!c.scene()->getAttachedState()->get());
      LOKA_VERIFY(c.scene()->getLifecycleState()->get() == ON_ATTACH);
      this->StdCompositionBoundaryNodeBase<LifeProps>::detachNode(c);
    }
  };

  void lifetime(bool failure)
  {
    {
      Lifetime life(failure);
      expect(Boundary<LifecyclePage>(LifeProps(life)), failure ? 0 : "first\n\nsecond\n");
      life.trigger.set(3);
      LOKA_VERIFY(life.calls == 1);
      LOKA_VERIFY(life.attaches == 1 && life.detaches == 1 && life.publications == 1 && life.releases == 1);
    }
    LOKA_AUDIT_CHECKPOINT("standalone document destroyed");
  }
} // namespace

void testDocumentHeadlessBranches()
{
  int arms = 0;
  {
    const BoundaryDefinition<BranchProps, BranchPage> page = Boundary<BranchPage>(BranchProps(arms));
    expect(page, "match1\n\nkey1\n");
    expect(page, "match1\n\nkey1\n");
    LOKA_VERIFY(arms == 4);
  }
  LOKA_AUDIT_CHECKPOINT("replaced and parked document arms destroyed");
}

void testDocumentHeadlessClone()
{
  expect(MissingClone(), 0);
  expect(StdCompositionBoundary(StdCompositionProps()), "");
}

void testDocumentHeadlessMountRefusal()
{
  int arms = 0;
  loka::core::testing::failLokaAllocRaw("NodeDefinition", "Node", 1);
  expect(Boundary<BranchPage>(BranchProps(arms)), 0);
  loka::core::testing::allowLokaAllocRaw();
  arms = 0;
  loka::core::testing::failLokaAllocRaw("StateOwner", "MutableState", 1);
  expect(Boundary<BranchPage>(BranchProps(arms)), 0);
  LOKA_VERIFY(arms == 1);
  loka::core::testing::allowLokaAllocRaw();
  arms = 0;
  loka::core::testing::failLokaAllocRaw("StateOwner", "MutableState", 1);
  {
    ColumnDefinition column;
    column << doc::Paragraph("survivor") << Boundary<BranchPage>(BranchProps(arms));
    typedef PropsReconciliationSupport::Props<ColumnDefinition> P;
    expect(Boundary<P::NodeType>(P(&column)), 0);
    LOKA_VERIFY(arms == 1);
  }
  loka::core::testing::allowLokaAllocRaw();
}

void testDocumentHeadlessUpdateRefusal()
{
  int arms = 0;
  loka::core::testing::failLokaAllocRaw("StateOwner", "MutableState", 2);
  expect(Boundary<BranchPage>(BranchProps(arms)), 0);
  LOKA_VERIFY(arms == 2);
  loka::core::testing::allowLokaAllocRaw();
  LOKA_AUDIT_CHECKPOINT("refused document update destroyed");
}

void testDocumentHeadlessLifecycle()
{
  lifetime(false);
}
void testDocumentHeadlessWriterRefusal()
{
  lifetime(true);
}

void testDocumentHeadlessAfterRun()
{
  for (unsigned n = 0; n <= 1; ++n)
  {
    Clock clock(n);
    expect(Boundary<ClockPage>(ClockProps(clock)), "settled\n");
    LOKA_VERIFY(clock.updates == n);
  }
}
void testDocumentHeadlessLastFlush()
{
  Clock clock(100);
  expect(Boundary<ClockPage>(ClockProps(clock)), "settled\n");
  LOKA_VERIFY(clock.updates == 100);
}
void testDocumentHeadlessExhaustion()
{
  Clock clock(101);
  expect(Boundary<ClockPage>(ClockProps(clock)), 0);
  LOKA_VERIFY(clock.updates == 100);
}

namespace
{
  class IdleTitle : public AppConfigurable
  {
  public:
    MutableState<bool> ready;
    IdleTitle()
        : AppConfigurable(0),
          ready(false)
    {
    }
    virtual void compose(AppComposition &) {}
    virtual IdlePolicy idlePolicy() const
    {
      return IdlePolicy::everyTick();
    }
    virtual void onIdle(double)
    {
      this->ready.set(true);
    }
  };

  class WindowlessPage;
  typedef Input<WindowlessPage, IdleTitle> WindowlessProps;
  class WindowlessPage : public StdCompositionBoundaryNodeBase<WindowlessProps>
  {
    typedef StdCompositionBoundaryNodeBase<WindowlessProps> Base;
    PushStateTracker listTracker_;
    ObservableList<doc::ParagraphProps> list_;
    NodeState<Frame> viewport_;
    TextDocument document_;
    Focus<int> focus_;
    loka::testing::TextBufferFixture buffer_;
    Reported<LineCursor> cursor_;
    EmitterState command_;
    int clicks_;

    static void clicked(void *p)
    {
      ++*static_cast<int *>(p);
    }

  public:
    explicit WindowlessPage(const WindowlessProps &p)
        : Base(p),
          clicks_(0)
    {
      this->state(this->viewport_, Frame(0, 0, 100, 20));
      this->document(this->document_);
      this->state(this->focus_, 1);
      this->state(this->cursor_, LineCursor());
    }
    virtual void composeNode(NodeComposition &c)
    {
      LOKA_VERIFY(!this->attachedContext() && !c.componentContext()->window());
      LOKA_VERIFY(this->list_.attach(&this->listTracker_, 3) == ATTACH_OK);
      const char *rows[] = {"window", "margin", "outside"};
      for (unsigned short i = 0; i < 3; ++i)
        LOKA_VERIFY(this->list_.insert(i, doc::ParagraphProps(rows[i])) == EDIT_OK);
      LOKA_VERIFY(this->buffer_.attach(&this->listTracker_, 1) == ATTACH_OK);
      LOKA_VERIFY(this->buffer_.insert(0, String("editor body")) == EDIT_OK);
      this->command_.bind(&clicked, &this->clicks_, false);
      LOKA_VERIFY(c.menuBar(MenuBarDefinition() << (Menu("File") << MenuItem("Open").onClick(&this->command_))));
      c.declare(Column() << (ScrollView(this->viewport_) << doc::Paragraph("scroll child"))
                         << LazyColumn(this->list_, reservation::SeatNodes<ParagraphNodes>(), 3)
                                .cells(100, 20)
                                .viewport(*this->viewport_.state())
                         << LazyColumn(this->list_, reservation::SeatNodes<ParagraphNodes>(), 3).cells(100, 20)
                         << TextDocumentEditor(this->document_).focusedAs(this->focus_, 1)
                         << TextEditor(this->buffer_, this->cursor_)
                         << Match(this->props.data->ready)
                                .arm(false, doc::Paragraph("before idle"))
                                .otherwise(doc::Paragraph("after idle")));
      c.scene()->requestLayoutAfterRun();
    }
    virtual void composeWithContext(ComponentContext &c, ComposeEvent event)
    {
      this->Base::composeWithContext(c, event);
      if (event == COMPOSE_EVENT_UPDATE)
        LOKA_VERIFY(c.scene()->menuBar() && c.scene()->menuBar()->menuAt(0));
    }
    virtual void detachNode(NodeComposition &c)
    {
      LOKA_VERIFY(!(this->focus_.state()->get() != Focused<int>::none()));
      LOKA_VERIFY(this->document_.fact()->get().state() == TEXT_DOCUMENT_AWAITING_STORE);
      LOKA_VERIFY(this->clicks_ == 0);
      this->Base::detachNode(c);
    }
  };

#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  class StrictWindow : public BoundaryNodeFor<StrictWindow>
  {
  public:
    explicit StrictWindow(const BoundaryPropsFor<StrictWindow> &p)
        : BoundaryNodeFor<StrictWindow>(p)
    {
    }
    virtual void composeNode(NodeComposition &c)
    {
      (void)c.window();
    }
  };
#endif
  void disposeImage(void *count)
  {
    ++*static_cast<int *>(count);
  }
} // namespace

void testDocumentHeadlessOmissions()
{
  IdleTitle idle;
  expect(Boundary<WindowlessPage>(WindowlessProps(idle)), "scroll child\n\nwindow\n\nmargin\n\nbefore idle\n");
  LOKA_VERIFY(!idle.ready.get());
  idle.onIdle(1);
  expect(Boundary<WindowlessPage>(WindowlessProps(idle)), "scroll child\n\nwindow\n\nmargin\n\nafter idle\n");
}

void testDocumentHeadlessPreconditions()
{
#if defined(__linux__) && !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  for (int i = 0; i < 2; ++i)
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      std::string out;
      if (i == 0)
      {
        Operation turn;
        doc::RenderMarkdown(StdCompositionBoundary(StdCompositionProps()), out);
      }
      else
        doc::RenderMarkdown(Boundary<StrictWindow>(), out);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#else
  std::puts("[skip] Standalone document assertion pins require Linux Debug without ASan.");
#endif
}

void testDocumentHeadlessImage()
{
  typedef loka::app::testing::NativeResourceRetirementTestAccess Access;
  NullPlatformContext producer;
  int disposals = 0;
  {
    resource::Image image;
    internal::Reservation reservation(producer, &disposeImage);
    LOKA_VERIFY(reservation.publishImage(&disposals, 1, 1, image));
    MutableState<resource::Image> state(image);
    ImageViewDefinition view = ImageView().image(&state);
    typedef PropsReconciliationSupport::Props<ImageViewDefinition> P;
    expect(Boundary<P::NodeType>(P(&view)), "");
    LOKA_VERIFY(Access::held(producer) == 1 && !Access::queued(producer));
  }
  LOKA_VERIFY(disposals == 0 && Access::queued(producer) == 1);
  Access::drain(producer);
  LOKA_VERIFY(disposals == 1 && !Access::queued(producer));
}
