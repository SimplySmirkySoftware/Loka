#include <cstdio>
#include <Processes.h>
#include "ToolboxProbeTiming.hpp"
#include "ToolboxWindow.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "context/ToolboxAttributedTextContext.hpp"
#include "core/io/File.hpp"
#include "core/util/ScopedPtr.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#ifndef LOKA_PROBE_SERIES
#define LOKA_PROBE_SERIES 20, 50, -50, 100, -200, 200
#endif

namespace loka
{
  namespace testing
  {
    /** Probe-only use of the existing friend seam, outside the timed interval. */
    class ToolboxAttributedTextContextAccess
    {
    public:
      static bool completed(const ToolboxAttributedTextContext &context)
      {
        return context.table_.valid() && (EmptyRect(&context.paintRect_) || context.presented_.isKnown());
      }
    };
  } // namespace testing
} // namespace loka

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka_toolbox_probe;
  const unsigned long kBudget = 60000000UL;
  const int kSeries[] = {LOKA_PROBE_SERIES};

  class Paragraph;
  /** Immutable item value after model construction; same payload in both series. */
  struct ParagraphProps : NodePropsBase<ParagraphProps>
  {
    typedef ParagraphProps TypeTag;
    typedef Paragraph NodeType;
    AttributedString text;
    explicit ParagraphProps(const AttributedString &value = AttributedString())
        : text(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return false;
      return this->text.compare(static_cast<const ParagraphProps &>(rhs).text) < 0;
    }
  };
  class Paragraph : public ComponentNodeWithProps<ParagraphProps>
  {
  public:
    explicit Paragraph(const ParagraphProps &props)
        : ComponentNodeWithProps<ParagraphProps>(props)
    {
    }
    virtual void composeChildren(NodeComposition &composition)
    {
      composition.declare(AttributedText(this->props.text) + BlockStyle().wrap(TEXT_WRAP_WORD));
    }
  };

  class Document;
  struct DocumentProps : NodePropsBase<DocumentProps>
  {
    typedef DocumentProps TypeTag;
    typedef Document NodeType;
    ObservableList<ParagraphProps> *model;
    int series;
    DocumentProps(ObservableList<ParagraphProps> *m = 0, int s = 1)
        : model(m),
          series(s)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return false;
      const DocumentProps &other = static_cast<const DocumentProps &>(rhs);
      return this->model != other.model ? this->model < other.model : this->series < other.series;
    }
  };
  class Document : public StdCompositionBoundaryNodeBase<DocumentProps>
  {
  public:
    explicit Document(const DocumentProps &props)
        : StdCompositionBoundaryNodeBase<DocumentProps>(props)
    {
      this->state(this->offset_, loka::core::Frame(0, 0, 0, 0));
      this->state(this->viewport_, Frame(0, 0, 300, 192));
    }
    int offset() const
    {
      return this->offset_.get().y;
    }
    void scrollOneLine()
    {
      this->offset_.set(loka::core::Frame(0, this->offset_.get().y + 16, 0, 0));
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->offset_.state(), this, &Document::updateViewport, true);
    }
    void updateViewport()
    {
      this->viewport_.set(Frame(0, this->offset_.get().y, 300, 192));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      if (this->props.series == 1)
      {
        Column column;
        for (unsigned short i = 0; i < this->props.model->size(); ++i)
          column << (AttributedText(this->props.model->at(i).value.text) + BlockStyle().wrap(TEXT_WRAP_WORD));
        composition.declare(Box().size(316, 192) << (ScrollView(this->offset_) << column));
      }
      else
      {
        // LazyView selects in content coordinates; ScrollView translates once.
        composition.declare(Box().size(316, 192)
                            << (ScrollView(this->offset_)
                                << LazyColumn(*this->props.model, reservation::SeatNodes<reservation::Nodes<Paragraph, 1,
                                    reservation::Nodes<AttributedTextNode, 1> > >(), 6).cells(300, 64).viewport(*this->viewport_.state())));
      }
    }

  private:
    NodeState<loka::core::Frame> offset_;
    NodeState<Frame> viewport_;
  };
  typedef BoundaryDefinition<DocumentProps, Document> DocumentDefinition;
  class ProbeScene : public Scene
  {
  public:
    explicit ProbeScene(const DocumentDefinition &definition)
        : Scene(definition)
    {
    }
    Document *document() const
    {
      return static_cast<Document *>(this->rootNode_);
    }
  };

  /** One file for the launch; every record is made durable before continuing. */
  class Report
  {
  public:
    Report()
        : log_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (this->log_)
        std::setvbuf(this->log_, 0, _IONBF, 0);
    }
    ~Report()
    {
      if (this->log_)
        std::fclose(this->log_);
    }
    std::FILE *file() const
    {
      return this->log_;
    }
    bool flush()
    {
      return this->log_ && !std::ferror(this->log_) && loka::platform::file::FlushWrite(this->log_, this->file_);
    }
    void measurement(int selected, const char *phase, int step, unsigned long us, int offset, int texts)
    {
      std::fprintf(this->log_,
                   "series=S%d N=%d phase=%s step=%d us=%lu FreeMem=%ld offset=%d texts=%d\r",
                   selected > 0 ? 1 : 2,
                   selected > 0 ? selected : -selected,
                   phase,
                   step,
                   us,
                   FreeMem(),
                   offset,
                   texts);
      this->flush();
    }

  private:
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
  };

  int CountCompleted(Node *node)
  {
    if (!node)
      return 0;
    int count = 0;
    if (node->asAttributedTextNode())
    {
      ToolboxAttributedTextContext *context = static_cast<ToolboxAttributedTextContext *>(node->getContext());
      if (!context || !loka::testing::ToolboxAttributedTextContextAccess::completed(*context))
        return -1;
      ++count;
    }
    // Boundary nodes expose their own composition children here, including
    // LazyView -> LazyGeneration -> Fragment -> Canvas -> item components.
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
    {
      const int nested = CountCompleted(child);
      if (nested < 0)
        return -1;
      count += nested;
    }
    return count;
  }

  /** Config owns the model; each case destroys its App before this owner. */
  class ProbeConfig : public AppConfigurable
  {
  public:
    ProbeConfig(PlatformContext *platform, Report &report, int selected, const Timer &deadline)
        : AppConfigurable(platform),
          report_(report),
          selected_(selected),
          deadline_(deadline),
          app_(0),
          phase_(-1),
          interval_()
    {
    }
    bool buildModel()
    {
      const int count = this->selected_ > 0 ? this->selected_ : -this->selected_;
      if (!count || count > LOKA_LAZYFLEX_MAX_ITEMS || this->model_.attach(&this->tracker_, count) != ATTACH_OK)
        return false;
      for (int i = 0; i < count; ++i)
      {
        if (this->deadline_.elapsed() >= kBudget)
          return false;
        // Fresh allocated strings and attributed value per paragraph, 165 ASCII
        // bytes and three runs; model construction is outside mount timing.
        const AttributedString value =
            Styled(String(std::string("A quiet morning brings fresh light across the desk. This paragraph keeps one ")),
                   FontSize<12>())
            + Styled(String(std::string("word")), Bold)
            + Styled(String(std::string(
                         " in bold while the remaining words wrap naturally across the narrow document window.")),
                     FontSize<12>());
        if (!value.valid() || value.segmentCount() != 3)
          return false;
        StateTrackerGuard guard(&this->tracker_);
        if (this->model_.insert(this->model_.size(), ParagraphProps(value)) != EDIT_OK)
          return false;
      }
      return true;
    }
    void start(App *app)
    {
      this->app_ = app;
    }
    void startTiming()
    {
      Microseconds(&this->interval_);
    }
    virtual void compose(AppComposition &composition)
    {
      composition << WindowDefinition<WindowProps>(
          WindowProps()
              .title("Paragraph probe")
              .size(360, 260)
              .visible(true)
              .scene(new ProbeScene(DocumentDefinition(DocumentProps(&this->model_, this->selected_ > 0 ? 1 : 2))))
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&ProbeConfig::OnIdle, this));
    }

  private:
    Report &report_;
    const int selected_;
    const Timer &deadline_;
    PushStateTracker tracker_;
    ObservableList<ParagraphProps> model_;
    App *app_;
    int phase_;
    UnsignedWide interval_;

    void stop(const char *phase, unsigned long elapsed)
    {
      this->report_.measurement(this->selected_, phase, this->phase_ < 0 ? 0 : this->phase_, elapsed, 0, 0);
      this->app_->quit();
    }
    static void OnIdle(Window *window, double, void *data)
    {
      static_cast<ProbeConfig *>(data)->idle(window);
    }
    void idle(Window *window)
    {
      if (this->deadline_.elapsed() >= kBudget)
      {
        this->stop("budget-exceeded", this->deadline_.elapsed());
        return;
      }
      // Idle precedes presentation. Cross it once before observing settlement.
      if (this->phase_ < 0)
      {
        this->phase_ = 0;
        return;
      }
      ProbeScene *scene = static_cast<ProbeScene *>(window->scene());
      Document *document = scene ? scene->document() : 0;
      if (!document || document->composeResult().allocationFailed)
      {
        this->stop("mount-refused", this->deadline_.elapsed());
        return;
      }
      if (window->hasPendingSceneInvalidation() || window->hasPendingScenePlatformSync()
          || static_cast<ToolboxWindow *>(window)->hasPendingInvalidate())
        return;
      UnsignedWide now;
      Microseconds(&now);
      const unsigned long elapsed = now.lo - this->interval_.lo;
      const int count = CountCompleted(document);
      const int offset = document->offset();
      const layout::LazyLayout policy = layout::FixedGrid(300, 64, 1, this->model_.size());
      const int expected = this->selected_ > 0
                               ? this->selected_
                               : static_cast<int>(policy.indicesIn(Frame(0, offset, 300, 192)).count);
      if (count != expected || offset != this->phase_ * 16)
      {
        this->stop("projection-refused", elapsed);
        return;
      }
      this->report_.measurement(
          this->selected_, this->phase_ == 0 ? "first-paint" : "scroll", this->phase_, elapsed, offset, count);
      if (this->phase_ == 5)
      {
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
        LokaAllocCensusDump(this->report_.file());
#else
        std::fprintf(this->report_.file(), "census=unavailable\r");
#endif
        this->report_.flush();
        this->app_->quit();
        return;
      }
      ++this->phase_;
      this->startTiming();
      document->scrollOneLine();
    }
  };
} // namespace

int main(int, char **)
{
  loka::platform::InitPlatformRuntime();
  ScopedPtr<PlatformContext> platform(loka::platform::CreatePlatformContext());
  Report report;
  if (!platform.get() || !report.file())
    return 1;
  for (std::size_t i = 0; i < sizeof(kSeries) / sizeof(kSeries[0]); ++i)
  {
    const int selected = kSeries[i];
    const Timer deadline;
    std::fprintf(report.file(), "case=%d phase=begin budget_us=60000000\r", selected);
    report.flush();
    {
      ProbeConfig config(platform.get(), report, selected, deadline);
      if (!config.buildModel())
      {
        std::fprintf(report.file(), "case=%d phase=model-or-capacity-refused\r", selected);
        report.flush();
      }
      else
      {
        report.measurement(selected, "before-mount", 0, 0, 0, 0);
        config.startTiming();
        ScopedPtr<App> app(platform->createApp(&config, 0, 0));
        if (!app.get())
        {
          std::fprintf(report.file(), "case=%d phase=app-refused\r", selected);
          report.flush();
        }
        else
        {
          config.start(app.get());
          app->run();
        }
      }
    }
    const unsigned long elapsed = deadline.elapsed();
    std::fprintf(report.file(),
                 "case=%d phase=end FreeMem=%ld us=%lu budget=%s\r",
                 selected,
                 FreeMem(),
                 elapsed,
                 elapsed < kBudget ? "within" : "exceeded");
    report.flush();
  }
  std::fprintf(report.file(), "done\r");
  return report.flush() ? 0 : 1;
}
