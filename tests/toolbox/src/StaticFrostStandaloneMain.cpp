#include <cstdio>
#include <cstring>
#include <Gestalt.h>
#include <Palettes.h>
#include "ToolboxProbeTiming.hpp"
#include "FrostStandaloneSupport.hpp"
#include "StandaloneFlowRunner.hpp"
#include "ObservedMainDefinition.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxPaintGround.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#ifndef LOKA_FROST_STANDALONE_DEPTH
#define LOKA_FROST_STANDALONE_DEPTH 8
#endif

#include "app/RectSurface.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"

/** Read-only inspection: capture counts belong to the runner, not production. */
class ToolboxStaticBlurInspection
{
public:
  static ToolboxStaticBlur::Phase phase(const ToolboxScenePlatformController &controller)
  {
    return controller.staticBlur_.phase();
  }
  static bool present(const ToolboxScenePlatformController &controller)
  {
    return controller.staticBlur_.stack_ != 0;
  }
  static void timing(const ToolboxScenePlatformController &controller, FILE *log)
  {
    const unsigned long *t = controller.staticBlur_.pixels_.timings_;
    std::fprintf(log, "static-us capture=%lu prepare=%lu kernel=%lu copy-back=%lu\r", t[0], t[1], t[2], t[3]);
  }
};
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace frost_standalone;

  /** A test-only colorful ground; model state writes dirty the actual node. */
  class GroundNode : public RectSurfaceNode, public IProjectedLayoutNode
  {
  public:
    explicit GroundNode(const RectSurfaceProps &props)
        : RectSurfaceNode(props)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      state.y += Height;
      return Width;
    }
    virtual void render(IPlatformController *)
    {
      const short offset = this->props.model_->get().rectCount;
      for (short y = 0; y < Height; y += 10)
        for (short x = 0; x < Width - 20; x += 10)
        {
          const Rect tile = {y, x, static_cast<short>(y + 10), static_cast<short>(x + 10)};
          RGBColor color = {static_cast<unsigned short>((x * 257 + offset * 19000) & 65535),
                            static_cast<unsigned short>((y * 577 + offset * 23000) & 65535),
                            static_cast<unsigned short>(((x + y) * 317 + offset * 13000) & 65535)};
          RGBForeColor(&color);
          PaintRect(&tile);
        }
      loka::toolbox::ToolboxApplyWindowGround();
    }
  };
  typedef NodeDefinition<RectSurfaceProps, GroundNode> Ground;
  class StaticNode;
  typedef BoundaryPropsFor<StaticNode> StaticProps;
  class StaticNode : public StdCompositionBoundaryNodeBase<StaticProps>
  {
  public:
    typedef StaticProps::TypeTag TypeTag;
    explicit StaticNode(const StaticProps &props)
        : StdCompositionBoundaryNodeBase<StaticProps>(props)
    {
      this->state(this->visible_, false);
      this->state(this->ground_, RectSurfaceModel());
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(ZStack().expStaticBlurUnit()
                << Ground(RectSurfaceProps().model(this->ground_.state()).size(Width, Height))
                << (Show(*this->visible_.state()) << Box().size(Width, UnitHeight)));
    }
    void show(bool value)
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->visible_.set(value);
    }
    void changeGround()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      RectSurfaceModel value = this->ground_.get();
      ++value.rectCount;
      this->ground_.set(value);
    }

  private:
    NodeState<bool> visible_;
    NodeState<RectSurfaceModel> ground_;
  };
  class StaticConfig : public AppConfigurable
  {
  public:
    explicit StaticConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          node_(0),
          log_(0),
          phase_(0),
          waits_(0),
          pass_(0),
          fail_(0),
          originalDepth_(0),
          depthReady_(false),
          before_(0),
          ground_(0),
          captures_(0),
          opened_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        ++this->fail_;
    }
    ~StaticConfig()
    {
      delete this->before_;
      delete this->ground_;
      if (this->depthReady_)
        SetDepth(GetMainDevice(), this->originalDepth_, 1, 1);
      if (this->log_)
        std::fclose(this->log_);
    }
    int exitCode() const
    {
      return this->fail_ ? 1 : 0;
    }
    void setApp(App *app)
    {
      this->app_ = app;
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDefinition<WindowProps>(
          WindowProps()
              .frame(40, 60, Width, Height)
              .title("Static frost")
              .contentMargin(0)
              .scene(loka::scenario_tests::ObservedMainDefinition<StaticProps, StaticNode>(StaticProps(), &this->node_))
              .visible(true)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&StaticConfig::Idle, this));
    }

  private:
    App *app_;
    StaticNode *node_;
    FILE *log_;
    int phase_, waits_, pass_, fail_;
    short originalDepth_;
    bool depthReady_;
    Capture *before_;
    Capture *ground_; // The ground before the unit opens, for the change check.
    unsigned captures_;
    unsigned long opened_;
    loka::platform::file::FileHandle file_;
    void record(const char *name, bool ok)
    {
      if (ok)
        ++this->pass_;
      else
        ++this->fail_;
      std::fprintf(this->log_, "%s %s\r", ok ? "PASS" : "FAIL", name);
    }
    void finish()
    {
      delete this->before_;
      this->before_ = 0;
      delete this->ground_;
      this->ground_ = 0;
      if (this->depthReady_)
      {
        if (SetDepth(GetMainDevice(), this->originalDepth_, 1, 1) != noErr)
          ++this->fail_;
        this->depthReady_ = false;
      }
      std::fprintf(this->log_, "RESULT pass=%d fail=%d captures=%u\r", this->pass_, this->fail_, this->captures_);
      if (!loka::platform::file::FlushWrite(this->log_, this->file_))
        ++this->fail_;
      this->app_->quit();
    }
    static void Idle(Window *window, double, void *data)
    {
      static_cast<StaticConfig *>(data)->step(window->asToolboxWindow());
    }
    /** Geometry changes start real snapshot lifetimes without inspection writes.
        The unpainted right strip of GroundNode also discriminates missing erase. */
    void checkDrawCoverage(ToolboxWindow *window, ToolboxScenePlatformController &controller)
    {
      const Rect narrow = {5, 5, 15, 25};
      const Rect comparison = {0, 0, UnitHeight, Width - 1};
      const Rect picture = {0, 0, Height, Width - 1};
      SizeWindow(window->window(), Width - 1, Height, false);
      // Reconcile the new key without erase evidence; the entire unit is ground.
      ToolboxInputDoor::render(controller);
      this->record("static-partial-update-starts-empty",
                   ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::EMPTY);
      ValidRect(&Whole);
      InvalRect(&narrow);
      BeginUpdate(window->window());
      window->draw(ToolboxWindow::FULL_DRAW_FOR_UPDATE);
      EndUpdate(window->window());
      this->record("static-partial-update-empty",
                   ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::EMPTY
                       && !window->hasPendingInvalidate());
      Capture partial, plain;
      const bool partialRead = partial.read(window->window());
      loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, picture);
      this->record("static-partial-update-ground",
                   partialRead && plain.read(window->window()) && partial.equals(plain, picture));
      window->requestInvalidate();
      window->flushInvalidate();
      this->record("static-external-flush-ready",
                   ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::READY);
      Capture clean, dirty;
      const bool captured = clean.read(window->window());
      // Change the key twice: the full OS update must capture its new lifetime.
      SizeWindow(window->window(), Width, Height, false);
      window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
      SizeWindow(window->window(), Width - 1, Height, false);
      ToolboxInputDoor::render(controller);
      this->record("static-full-update-starts-empty",
                   ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::EMPTY);
      InvalRect(&window->window()->portRect);
      BeginUpdate(window->window());
      window->draw(ToolboxWindow::FULL_DRAW_FOR_UPDATE);
      const bool readyInUpdate = ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::READY;
      EndUpdate(window->window());
      this->record("static-full-update-ready", readyInUpdate && !window->hasPendingInvalidate());
      this->record("static-full-update-equals-flush",
                   captured && dirty.read(window->window()) && dirty.equals(clean, picture));
      // Visit the original geometry so returning to the smaller one is fresh.
      SizeWindow(window->window(), Width, Height, false);
      window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
      SizeWindow(window->window(), Width - 1, Height, false);
      ClipRect(&Whole);
      ForeColor(blackColor);
      PaintRect(&Whole);
      ClipRect(&narrow);
      window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
      RgnHandle restored = NewRgn();
      if (restored)
        GetClip(restored);
      this->record("static-flush-restores-clip",
                   restored && (**restored).rgnSize == sizeof(Region) && EqualRect(&(**restored).rgnBBox, &narrow));
      if (restored)
        DisposeRgn(restored);
      this->record("static-flush-owns-clip",
                   captured && dirty.read(window->window()) && dirty.equals(clean, comparison)
                       && ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::READY);
      ClipRect(&Whole);
      SizeWindow(window->window(), Width, Height, false);
    }
    void step(ToolboxWindow *window)
    {
      if (!this->log_)
      {
        this->app_->quit();
        return;
      }
      if (this->phase_ == 0)
      {
        this->originalDepth_ = (**(**GetMainDevice()).gdPMap).pixelSize;
        this->depthReady_ = HasDepth(GetMainDevice(), LOKA_FROST_STANDALONE_DEPTH, 1, 1)
                            && SetDepth(GetMainDevice(), LOKA_FROST_STANDALONE_DEPTH, 1, 1) == noErr;
        std::fprintf(this->log_, "depth=%d accepted=%d\r", LOKA_FROST_STANDALONE_DEPTH, this->depthReady_);
        if (!this->depthReady_)
        {
          this->record("depth", false);
          this->finish();
          return;
        }
        ++this->phase_;
        return;
      }
      SetPort(window->window());
      ClipRect(&Whole);
      ToolboxScenePlatformController &controller = *window->scenePlatformController();
      if (this->phase_ == 1)
      {
        window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
        this->ground_ = new Capture;
        this->record("static-ground-readback", this->ground_->read(window->window()));
        this->opened_ = Now();
        this->node_->show(true);
        if (loka::core::Operation *turn = loka::core::testing::OperationTestAccess::active())
          turn->settle();
        // Apply the revealed branch before measuring its first native paint.
        window->flushSceneInvalidation();
        const unsigned long drawStart = Now();
        window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
        std::fprintf(this->log_, "static-us first-full-open=%lu\r", Now() - drawStart);
        this->record("static-flush-first-capture",
                     LOKA_FROST_STANDALONE_DEPTH == 1
                         ? ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::EMPTY
                         : ToolboxStaticBlurInspection::phase(controller) == ToolboxStaticBlur::READY);
        ++this->phase_;
        return;
      }
      if (this->phase_ == 2 || this->phase_ == 7)
      {
        if (LOKA_FROST_STANDALONE_DEPTH == 1)
        {
          Capture actual, white;
          window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
          const bool ok = actual.read(window->window());
          loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
          this->record("static-white-at-1bit", ok && white.read(window->window()) && actual.equals(white, Unit));
          this->finish();
          return;
        }
        if (ToolboxStaticBlurInspection::phase(controller) != ToolboxStaticBlur::READY)
        {
          if (++this->waits_ < 12)
            return;
          this->record(this->phase_ == 2 ? "static-capture-ready" : "static-recaptured-after-reopen", false);
          this->finish();
          return;
        }
        ++this->captures_; // Count observed READY lifetimes through read-only inspection.
        this->waits_ = 0;
        ToolboxStaticBlurInspection::timing(controller, this->log_);
        Capture picture;
        std::fprintf(this->log_,
                     "static-picture fnv=%08lx\r",
                     picture.read(window->window()) ? picture.fingerprint(Whole) : 0UL);
        if (this->phase_ == 7)
        {
          this->record("static-reconcile", this->captures_ == 2);
          this->record("static-recaptured-after-reopen", true);
          this->checkDrawCoverage(window, controller);
          this->finish();
          return;
        }
        this->record("static-capture-ready", true);
        window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
        std::fprintf(this->log_, "static-us open-to-ready=%lu\r", Now() - this->opened_);
        this->before_ = new Capture;
        this->record("static-readback", this->before_->read(window->window()));
        this->node_->changeGround();
        ++this->phase_;
        return;
      }
      if (this->phase_ == 3)
      {
        Capture after;
        const bool ok = after.read(window->window());
        // The band fills the ground (#1508), so the changed ground is hidden
        // everywhere while the unit is shown; phase 5 proves the change.
        const bool unitEqual = ok && after.equals(*this->before_, Unit);
        std::fprintf(this->log_,
                     "static-frozen unit-equal=%d whole-equal=%d\r",
                     unitEqual,
                     ok && after.equals(*this->before_, Whole));
        this->record("static-frozen", unitEqual);
        const Rect dirty = {20, 100, 55, Width};
        window->requestInvalidateRect(dirty);
        ++this->phase_;
        return;
      }
      if (this->phase_ == 4)
      {
        Capture partial, full;
        const bool ok = partial.read(window->window());
        window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
        this->record("static-partial-equals-full", ok && full.read(window->window()) && partial.equals(full, Whole));
        this->node_->show(false);
        ++this->phase_;
        return;
      }
      if (this->phase_ == 5)
      {
        this->record("static-released", !ToolboxStaticBlurInspection::present(controller));
        Capture beneath;
        window->draw(ToolboxWindow::FULL_DRAW_FOR_FLUSH);
        this->record("static-ground-changed-beneath",
                     beneath.read(window->window()) && !beneath.equals(*this->ground_, Unit));
        this->node_->show(true);
        if (loka::core::Operation *turn = loka::core::testing::OperationTestAccess::active())
          turn->settle();
        // Reopening must reach READY again at a subsequent full redraw.
        this->phase_ = 7;
      }
    }
  };
} // namespace
int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<StaticConfig>(0, 0);
}
