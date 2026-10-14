#include <cstdio>
#include <Processes.h>

#include "StandaloneFlowRunner.hpp"
#include "ToolboxProbeTiming.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "core/io/File.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka_toolbox_probe::Timer;
  const int kKeys = 32;

  class TypingNode;
  struct TypingProps : NodePropsBase<TypingProps>
  {
    typedef TypingNode NodeType;
    struct TypeTag
    {
    };
    bool grouped;
    explicit TypingProps(bool value = false)
        : grouped(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() && this->grouped < static_cast<const TypingProps &>(rhs).grouped;
    }
  };

  /** Owns the document and focus; the driver only borrows this mounted Boundary. */
  class TypingNode : public StdCompositionBoundaryNodeBase<TypingProps>
  {
  public:
    typedef TypingProps::TypeTag TypeTag;
    explicit TypingNode(const TypingProps &props)
        : StdCompositionBoundaryNodeBase<TypingProps>(props)
    {
      this->setTestId("Typing.Owner");
      this->state(this->cursor_, LineCursor::None());
      this->state(this->caret_, LineCursor::None());
      this->state(this->error_, String());
      this->state(this->focus_);
    }
    virtual void attachNode(NodeComposition &)
    {
      StateTracker *owner = 0;
      if (this->lines_.queryMutationTracker(owner) == EDIT_OK)
        return;
      StateTrackerGuard transaction(this->tracker());
      if (this->lines_.attach(this->tracker()->asPushTracker(), TextEditorProps::kMaxLines) != ATTACH_OK
          || this->lines_.insert(0, String()) != EDIT_OK)
        this->lines_.detach();
    }
    virtual void composeNode(NodeComposition &composition)
    {
      if (!this->ready())
        return;
      RibbonControl band;
      if (this->props.grouped)
        band << (RibbonGroup("File") << RibbonItem("New") << RibbonItem("Open...") << RibbonItem("Save")
                                     << RibbonItem("Save As..."));
      else
        band << RibbonItem("New") << RibbonItem("Open...") << RibbonItem("Save") << RibbonItem("Save As...");
      composition.declare(
          VStack() << band << Text(this->error_.state())
                   << TextEditor(this->lines_, this->cursor_).moveCaretTo(this->caret_).focusedAs(this->focus_, 1));
    }
    bool ready() const
    {
      return this->lines_.size() == 1 && this->cursor_.isValid() && this->caret_.isValid() && this->error_.isValid()
             && this->focus_.isValid();
    }
    bool reset()
    {
      if (!this->ready())
        return false;
      StateTrackerGuard transaction(this->tracker());
      if (this->lines_.update(this->lines_.at(0).id, String()) != EDIT_OK)
        return false;
      this->caret_.set(LineCursor(this->lines_.at(0).id, 0));
      this->focus_.post(1);
      return true;
    }
    bool focused() const
    {
      return this->focus_.isValid() && this->focus_.state()->get().is(1);
    }
    bool characterCount(std::size_t &out) const
    {
      // All injected characters are ASCII x, so UTF-8 units equal characters.
      return this->lines_.size() == 1 && this->lines_.at(0).value.requiredUnits(StringEncodingUtf8, out);
    }

  private:
    ObservableList<String> lines_;
    Reported<LineCursor> cursor_;
    Request<LineCursor> caret_;
    NodeState<String> error_;
    Focus<int> focus_;
  };

  /** Factory observation follows WindowClamp; the App retains ownership. */
  class ObservedWindowDefinition : public WindowDefinition<WindowProps>
  {
  public:
    ObservedWindowDefinition(const WindowProps &props, ToolboxWindow **out)
        : WindowDefinition<WindowProps>(props),
          out_(out)
    {
    }
    virtual WindowDefinitionBase *clone() const
    {
      return new (std::nothrow) ObservedWindowDefinition(*this);
    }
    virtual Window *create(PlatformContext *context) const
    {
      Window *window = WindowDefinition<WindowProps>::create(context);
      *this->out_ = window ? window->asToolboxWindow() : 0;
      return window;
    }

  private:
    ToolboxWindow **out_;
  };

  /** Finite event-loop driver. All borrows end before the App destroys its windows. */
  class ProbeConfig : public AppConfigurable
  {
  public:
    explicit ProbeConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          log_(0),
          result_(0),
          phase_(SETUP),
          arm_(0),
          keys_(0),
          totalUs_(0),
          totalTicks_(0),
          keyStartUs_(0),
          keyStartTicks_(0),
          runStart_(TickCount()),
          timer_(),
          before_()
    {
      this->windows_[0] = this->windows_[1] = 0;
      this->nodes_[0] = this->nodes_[1] = 0;
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (this->log_)
        std::setvbuf(this->log_, 0, _IONBF, 0);
      else
        this->result_ = 1;
    }
    virtual ~ProbeConfig()
    {
      if (this->log_)
        std::fclose(this->log_);
    }
    void setApp(App *app)
    {
      this->app_ = app;
    }
    int exitCode() const
    {
      return this->result_;
    }
    virtual void compose(AppComposition &composition)
    {
      for (int i = 0; i < 2; ++i)
        composition << ObservedWindowDefinition(
            WindowProps()
                .title(i ? "Typing group" : "Typing plain")
                .frame(20, 40, 500, 300)
                .visible(true)
                .scene(BoundaryDefinition<TypingProps, TypingNode>(TypingProps(i != 0)))
                .idlePolicy(IdlePolicy::everyTick())
                .onIdle(&ProbeConfig::OnIdle, this),
            &this->windows_[i]);
    }

  private:
    enum Phase
    {
      SETUP,
      SETTLING,
      KEY_DELIVERY,
      KEY_PRESENTATION,
      DONE
    };
    App *app_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    int result_;
    Phase phase_;
    int arm_, keys_;
    unsigned long totalUs_, totalTicks_, keyStartUs_, keyStartTicks_, runStart_;
    Timer timer_;
    ToolboxSceneDebugStats before_;
    ToolboxWindow *windows_[2];
    TypingNode *nodes_[2];

    void finish(const char *error)
    {
      this->phase_ = DONE;
      if (error)
      {
        this->result_ = 1;
        std::fprintf(this->log_, "ERROR arm=%d keys=%d %s\r", this->arm_, this->keys_, error);
      }
      std::fprintf(this->log_, "done result=%d\r", this->result_);
      if (std::ferror(this->log_) || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
      this->app_->quit();
    }
    bool quiet(ToolboxWindow *window) const
    {
      return !window->hasPendingScenePlatformSync() && !window->hasPendingInvalidate()
             && EmptyRgn(reinterpret_cast<WindowPeek>(window->window())->updateRgn);
    }
    void inject()
    {
      // PostEvent enters exactly the ordinary ToolboxApp keyDown dispatch.
      // The keyboard must have no held modifiers during the unattended run.
      this->keyStartTicks_ = TickCount();
      this->keyStartUs_ = this->timer_.elapsed();
      if (PostEvent(keyDown, (7L << 8) | 'x') != noErr)
      {
        this->finish("PostEvent refused");
        return;
      }
      this->phase_ = KEY_DELIVERY;
    }
    void step()
    {
      if (this->phase_ == DONE)
        return;
      if (TickCount() - this->runStart_ > 110UL * 60)
      {
        this->finish("110 second watchdog expired");
        return;
      }
      if (!this->windows_[0] || !this->windows_[1])
        return;
      const int index = this->arm_ % 2;
      ToolboxWindow *window = this->windows_[index];
      if (!this->nodes_[index])
      {
        Node *found = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Typing.Owner", found, error);
        // The unique ID belongs only to TypingNode in this self-contained fixture.
        this->nodes_[index] = found ? static_cast<TypingNode *>(found) : 0;
      }
      TypingNode *node = this->nodes_[index];
      if (!node || !window->scenePlatformController() || !window->window() || !node->ready())
      {
        this->finish("window/editor allocation refused");
        return;
      }
      std::size_t characters = 0;
      if (!node->characterCount(characters))
      {
        this->finish("character count unavailable");
        return;
      }
      switch (this->phase_)
      {
      case SETUP:
        SelectWindow(window->window());
        this->app_->setActiveWindow(window);
        if (!node->reset())
        {
          this->finish("document reset refused");
          return;
        }
        this->keys_ = 0;
        this->totalUs_ = this->totalTicks_ = 0;
        this->phase_ = SETTLING;
        return;
      case SETTLING:
        if (!this->quiet(window) || !node->focused())
          return;
        if (characters != 0)
        {
          this->finish("reset did not land");
          return;
        }
        this->before_ = window->scenePlatformController()->debugStatsForTesting();
        this->inject();
        return;
      case KEY_DELIVERY:
        if (characters == static_cast<std::size_t>(this->keys_))
          return; // OS update/activation events may precede the posted key.
        if (characters != static_cast<std::size_t>(this->keys_ + 1))
        {
          this->finish("unexpected text length");
          return;
        }
        // This callback precedes present() in the key's turn. Never sample here.
        this->phase_ = KEY_PRESENTATION;
        return;
      case KEY_PRESENTATION:
        if (!this->quiet(window))
          return;
        this->totalUs_ += this->timer_.elapsed() - this->keyStartUs_;
        this->totalTicks_ += TickCount() - this->keyStartTicks_;
        if (characters != static_cast<std::size_t>(++this->keys_) || !node->focused())
        {
          this->finish("text/focus sanity failed");
          return;
        }
        if (this->keys_ < kKeys)
        {
          this->inject();
          return;
        }
        this->report(characters);
        if (++this->arm_ == 4)
          this->finish(0);
        else
          this->phase_ = SETUP;
        return;
      case DONE:
        return;
      }
    }
    void report(std::size_t characters)
    {
      const ToolboxSceneDebugStats &after =
          this->windows_[this->arm_ % 2]->scenePlatformController()->debugStatsForTesting();
      const char *name = this->arm_ % 2 ? "group" : "plain";
      std::fprintf(this->log_,
                   "arm=%s round=%d keys=%d total_us=%lu us_per_key=%lu ticks=%lu timing=queue_to_present_observed\r",
                   name,
                   this->arm_ / 2 + 1,
                   this->keys_,
                   this->totalUs_,
                   this->totalUs_ / this->keys_,
                   this->totalTicks_);
      std::fprintf(this->log_,
                   "sanity arm=%s round=%d characters=%lu expected=%d\r",
                   name,
                   this->arm_ / 2 + 1,
                   static_cast<unsigned long>(characters),
                   kKeys);
      std::fprintf(this->log_,
                   "counts changes=%lu draw=%d draw_dirty=%d render=%d render_dirty=%d collector_visits=%d "
                   "boundary_apply=%d replay=unavailable fast_path=unavailable\r",
                   after.totalChanges - this->before_.totalChanges,
                   after.totalDrawCalls - this->before_.totalDrawCalls,
                   after.totalDrawDirtyCalls - this->before_.totalDrawDirtyCalls,
                   after.totalRenderCalls - this->before_.totalRenderCalls,
                   after.totalRenderDirtyCalls - this->before_.totalRenderDirtyCalls,
                   after.totalCollectorVisitCount - this->before_.totalCollectorVisitCount,
                   after.totalBoundaryApplyCount - this->before_.totalBoundaryApplyCount);
    }
    static void OnIdle(Window *, double, void *data)
    {
      static_cast<ProbeConfig *>(data)->step();
    }
  };
} // namespace

int main(int, char **)
{
  const int result = loka::standalone_tests::RunStandaloneFlowWithConfig<ProbeConfig>(0, 0);
  ExitToShell();
  return result;
}
