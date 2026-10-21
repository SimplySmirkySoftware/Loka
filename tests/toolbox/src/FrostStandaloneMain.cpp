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
#include "ToolboxAlignedPattern.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "app/RectSurface.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

#ifndef LOKA_FROST_STANDALONE_DEPTH
#define LOKA_FROST_STANDALONE_DEPTH 8
#endif

/** Read-only friend inspection adds no production API, storage or test gate. */
class ToolboxFrostCacheInspection
{
public:
  static GWorldPtr world(const ToolboxFrostCache &cache)
  {
    return cache.world_;
  }
};

/** Native inspection reports phase and last timings, never capture counts. */
class ToolboxBlurSnapshotInspection
{
public:
  static ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::Phase phase(const ToolboxScenePlatformController &c)
  {
    return c.bandBlur_.phase();
  }
  static Rect rect(const ToolboxScenePlatformController &c)
  {
    return c.bandBlur_.rect_;
  }
  static ToolboxSceneDebugStats draws(const ToolboxScenePlatformController &c)
  {
    return c.debugStats_;
  }
  static void timing(const ToolboxScenePlatformController &c, FILE *log)
  {
    const unsigned long *t = c.bandBlur_.pixels_.timings_;
    std::fprintf(log, "band-us capture=%lu prepare=%lu kernel=%lu copy-back=%lu\r", t[0], t[1], t[2], t[3]);
  }
};
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace frost_standalone;

  /** Test backdrop reads owner state without registering automatic damage:
      each freshness probe explicitly delivers exactly one dirty rectangle. */
  const Rect FirstGround = {0, 0, 60, Width};
  const Rect SecondGround = {60, 0, Height, Width};
  struct GroundProps : RectSurfaceProps
  {
    loka::core::State<int> *value;
    Rect area;
    GroundProps(loka::core::State<int> *input, const Rect &bounds)
        : value(input),
          area(bounds)
    {
    }
  };
  class GroundNode : public RectSurfaceNode, public IProjectedLayoutNode
  {
  public:
    explicit GroundNode(const GroundProps &props)
        : RectSurfaceNode(props),
          value_(props.value),
          area_(props.area)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      state.y += this->area_.bottom - this->area_.top;
      return Width;
    }
    virtual void render(IPlatformController *)
    {
      PenState pen;
      RGBColor foreground;
      GetForeColor(&foreground);
      GetPenState(&pen);
      PenNormal();
      for (short y = this->area_.top; y < this->area_.bottom; y += 10)
        for (short x = 0; x < Width; x += 10)
        {
          const int offset = y < UnitHeight ? this->value_->get() : 0;
          const Rect tile = {y, x, static_cast<short>(y + 10), static_cast<short>(x + 10)};
          RGBColor color = {static_cast<unsigned short>((x * 257 + offset * 19000) & 65535),
                            static_cast<unsigned short>((y * 577 + offset * 23000) & 65535),
                            static_cast<unsigned short>(((x + y) * 317 + offset * 13000) & 65535)};
          RGBForeColor(&color);
          PaintRect(&tile);
        }
      SetPenState(&pen);
      RGBForeColor(&foreground);
      loka::toolbox::ToolboxApplyWindowGround();
    }

  private:
    loka::core::State<int> *const value_;
    const Rect area_;
  };
  typedef NodeDefinition<GroundProps, GroundNode> Ground;
  class FrostNode;
  typedef BoundaryPropsFor<FrostNode> FrostProps;
  class FrostNode : public StdCompositionBoundaryNodeBase<FrostProps>
  {
  public:
    typedef FrostProps::TypeTag TypeTag;
    explicit FrostNode(const FrostProps &p)
        : StdCompositionBoundaryNodeBase<FrostProps>(p)
    {
      this->state(this->backdrop_, 0);
      this->state(this->visible_, false);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column().gap(0) << (ZStack().expBlurUnit()
                                    << Ground(GroundProps(this->backdrop_.state(), FirstGround))
                                    << (Show(*this->visible_.state()) << Box().size(Width, UnitHeight)))
                                << (ZStack().expBlurUnit()
                                    << Ground(GroundProps(this->backdrop_.state(), SecondGround))
                                    << (Show(*this->visible_.state()) << Box().size(Width, 20))));
    }
    ZStackNode *stack()
    {
      loka::dsl::CompositionCursor<Node> children(this->childrenHead(), this->childrenCount());
      loka::app::StackNode *column = static_cast<loka::app::StackNode *>(children.next());
      return column->childrenHead()->asZStackNode();
    }
    void show()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->visible_.set(true);
    }
    void changeGround()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->backdrop_.set(this->backdrop_.get() + 1);
    }

  private:
    NodeState<int> backdrop_;
    NodeState<bool> visible_;
  };

  /** Owns readbacks and the temporary covering window for the native sequence. */
  struct BandObservation
  {
    Capture *before;
    WindowPtr cover;
    unsigned long started;
    ToolboxSceneDebugStats draws;
    Rect key;
    unsigned waits;
    BandObservation()
        : before(0),
          cover(0),
          started(0),
          key(),
          waits(0)
    {
    }
    ~BandObservation()
    {
      delete this->before;
      if (this->cover)
        DisposeWindow(this->cover);
    }

  private:
    BandObservation(const BandObservation &);
    BandObservation &operator=(const BandObservation &);
  };

  class FrostConfig : public AppConfigurable
  {
  public:
    explicit FrostConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          node_(0),
          log_(0),
          phase_(0),
          pass_(0),
          fail_(0),
          originalDepth_(0),
          depthReady_(false),
          eligible_(false)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        ++this->fail_;
      // QuickDraw is initialized by App::run, after this constructor: every
      // device, depth and GWorld call waits for the first idle.
    }
    ~FrostConfig()
    {
      // finish() restores the depth and writes the verdict while the runner can
      // still read exitCode(); this only covers a run that never reached it.
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
              .title("Band blur and milk frost")
              .contentMargin(0)
              .scene(loka::scenario_tests::ObservedMainDefinition<FrostProps, FrostNode>(FrostProps(), &this->node_))
              .visible(true)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&FrostConfig::Idle, this));
    }

  private:
    BandObservation observation_;
    App *app_;
    FrostNode *node_;
    FILE *log_;
    int phase_, pass_, fail_;
    short originalDepth_;
    bool depthReady_, eligible_;
    loka::platform::file::FileHandle file_;

    void record(const char *name, bool ok)
    {
      if (ok)
        ++this->pass_;
      else
        ++this->fail_;
      std::fprintf(this->log_, "%s %s%s\r", ok ? "PASS" : "FAIL", name, ok ? "" : " pixel-or-gate-mismatch");
    }
    void finish()
    {
      if (this->depthReady_)
      {
        const OSErr result = SetDepth(GetMainDevice(), this->originalDepth_, 1, 1);
        this->depthReady_ = false;
        std::fprintf(this->log_, "restore-depth err=%d\r", result);
        if (result != noErr)
          ++this->fail_;
      }
      std::fprintf(this->log_, "RESULT pass=%d fail=%d\r", this->pass_, this->fail_);
      if (!loka::platform::file::FlushWrite(this->log_, this->file_))
        ++this->fail_;
      this->app_->quit();
    }
    static void Idle(Window *window, double, void *data)
    {
      FrostConfig *self = static_cast<FrostConfig *>(data);
      self->step(window->asToolboxWindow());
    }
    void step(ToolboxWindow *window)
    {
      if (this->phase_++ == 0)
      {
        this->originalDepth_ = (**(**GetMainDevice()).gdPMap).pixelSize;
        this->depthReady_ = HasDepth(GetMainDevice(), LOKA_FROST_STANDALONE_DEPTH, 1, 1)
                            && SetDepth(GetMainDevice(), LOKA_FROST_STANDALONE_DEPTH, 1, 1) == noErr;
        std::fprintf(this->log_,
                     "depth requested=%d original=%d accepted=%d\r",
                     LOKA_FROST_STANDALONE_DEPTH,
                     this->originalDepth_,
                     this->depthReady_);
        loka::platform::file::FlushWrite(this->log_, this->file_);
        this->node_->show();
        return;
      }
      SetPort(window->window());
      ClipRect(&Whole);
      ToolboxScenePlatformController *controller = window->scenePlatformController();
      if (this->phase_ == 2)
      {
        long qd = 0;
        const bool qdAnswered = Gestalt(gestaltQuickdrawVersion, &qd) == noErr;
        ToolboxFpuType fpu = TOOLBOX_FPU_NONE;
        const bool fpuAnswered = queryToolboxFpuType(fpu);
        Rect global = Unit;
        LocalToGlobal(reinterpret_cast<Point *>(&global.top));
        LocalToGlobal(reinterpret_cast<Point *>(&global.bottom));
        GDHandle device = GetMaxDevice(&global);
        const short depth = device ? (**(**device).gdPMap).pixelSize : 0;
        const bool contained = device && global.left >= (**device).gdRect.left && global.top >= (**device).gdRect.top
                               && global.right <= (**device).gdRect.right && global.bottom <= (**device).gdRect.bottom;
        std::fprintf(this->log_,
                     "gates qd_answer=%d qd=%ld fpu_answer=%d fpu=%d depth=%d containment=%d\r",
                     qdAnswered,
                     qd,
                     fpuAnswered,
                     static_cast<int>(fpu),
                     depth,
                     contained);
        this->record("gates", qdAnswered && contained);
        this->eligible_ = this->depthReady_ && qd >= gestalt32BitQD && fpuAnswered && fpu != TOOLBOX_FPU_NONE
                          && depth >= 8 && contained;
        if (!this->depthReady_)
        {
          std::fprintf(this->log_, "[skip] depth-dependent checks: HasDepth/SetDepth refused\r");
          this->finish();
          return;
        }
        Capture actual, reference;
        window->drawForFlush();
        const bool captured = actual.read(window->window());
        this->observation_.before = new Capture;
        const bool pictureRead = this->observation_.before->read(window->window());
        this->observation_.key = ToolboxBlurSnapshotInspection::rect(*controller);
        this->logPicture(window);
        this->record("band-phase",
                     ToolboxBlurSnapshotInspection::phase(*controller)
                         == (this->eligible_ ? ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::READY
                                             : ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::EMPTY));
        if (this->eligible_)
        {
          Capture rawPicture(LOKA_FROST_STANDALONE_DEPTH), rawMilk(LOKA_FROST_STANDALONE_DEPTH);
          const bool rawRead = rawPicture.read(window->window());
          this->backdrop(window);
          ToolboxFrostCache milk;
          const bool milkOk = milk.paint(window->window(), Unit, Unit) && rawMilk.read(window->window());
          const Rect line = {UnitHeight - 1, 0, UnitHeight, Width};
          this->record("band-hairline-native-pixel", rawRead && milkOk && rawPicture.equals(rawMilk, line));
          loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
          Capture ground;
          Rect interior = Unit;
          --interior.bottom;
          this->record("band-picture-not-ground",
                       pictureRead && ground.read(window->window())
                           && !this->observation_.before->equals(ground, interior));
        }
        if (LOKA_FROST_STANDALONE_DEPTH == 1)
        {
          loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
          this->record("white-at-1bit", captured && reference.read(window->window()) && actual.equals(reference, Unit));
        }
        else if (this->eligible_)
        {
          // L checks compare local milk only, never the selected blurred picture.
          this->backdrop(window);
          ToolboxFrostCache milk;
          const bool milkPainted = milk.paint(window->window(), Unit, Unit) && actual.read(window->window());
          EraseRect(&Whole);
          ZStackNode *stack = this->node_->stack();
          loka::dsl::CompositionCursor<Node> children(stack->childrenHead(), stack->childrenCount());
          RenderNode(children.next(), controller);
          bool ok = reference.read(window->window());
          if (ok)
            reference.referenceMilk();
          ok = ok && reference.write(window->window(), Unit) && reference.read(window->window());
          Rect interior = Unit;
          --interior.bottom;
          this->record("milk-sample", milkPainted && ok && actual.equals(reference, interior));
          const Rect line = {UnitHeight - 1, 0, UnitHeight, Width};
          this->record("hairline", milkPainted && ok && actual.equals(reference, line));
          std::fprintf(this->log_, "comparison=byte-exact 8-bit readback after reference screen round-trip\r");
          // A unit cut by a viewport: its last visible row is milk, as in the
          // full paint, never a hairline at the cut.
          EraseRect(&Whole);
          ZStackNode *cutStack = this->node_->stack();
          loka::dsl::CompositionCursor<Node> cutChildren(cutStack->childrenHead(), cutStack->childrenCount());
          RenderNode(cutChildren.next(), controller);
          ToolboxFrostCache cut;
          const Rect visible = {0, 0, UnitHeight / 2, Width};
          Capture cutCapture;
          const bool cutOk = cut.paint(window->window(), visible, Unit) && cutCapture.read(window->window());
          this->record("clipped-unit-hairline", milkPainted && cutOk && cutCapture.equals(actual, visible));
          this->tinyAndGrowth(window);
        }
        else
          std::fprintf(this->log_, "[skip] milk-sample hairline tiny-units growth-refusal: hardware gate\r");
        window->drawForFlush();
        const bool saved = actual.read(window->window());
        ToolboxInputDoor::render(*controller);
        this->record("band-non-erasing-copy",
                     saved && reference.read(window->window()) && actual.equals(reference, Unit));
        if (!this->eligible_)
        {
          this->finish();
          return;
        }
        this->pictureClip(window);
        this->secondMilk(window);
        window->drawForFlush();
        this->observation_.before->read(window->window());
        this->node_->changeGround();
        const Rect dirty = {20, 100, 30, 140};
        this->observation_.draws = ToolboxBlurSnapshotInspection::draws(*controller);
        this->observation_.started = Now();
        window->requestInvalidateRect(dirty);
        return;
      }
      if (this->phase_ == 3)
      {
        Capture partial, full;
        const bool ok = partial.read(window->window());
        const ToolboxSceneDebugStats now = ToolboxBlurSnapshotInspection::draws(*controller);
        this->record("band-one-dirty-delivery",
                     now.totalDrawDirtyCalls == this->observation_.draws.totalDrawDirtyCalls + 1
                         && now.windowFlushFullCount == this->observation_.draws.windowFlushFullCount);
        this->record("band-dirty-fresh", ok && !partial.equals(*this->observation_.before, Unit));
        std::fprintf(this->log_, "band-us dirty-under-band=%lu\r", Now() - this->observation_.started);
        this->logPicture(window);
        window->drawForFlush();
        this->record("band-dirty-equals-full", ok && full.read(window->window()) && partial.equals(full, Whole));
        this->observation_.before->read(window->window());
        this->node_->changeGround();
        this->observation_.draws = ToolboxBlurSnapshotInspection::draws(*controller);
        SizeWindow(window->window(), Width, Height + 20, true);
        InvalRect(&window->window()->portRect);
        return;
      }
      if (this->phase_ == 4)
      {
        const ToolboxSceneDebugStats now = ToolboxBlurSnapshotInspection::draws(*controller);
        if (now.windowUpdateEvtDrawCount == this->observation_.draws.windowUpdateEvtDrawCount
            && ++this->observation_.waits < 120)
        {
          --this->phase_;
          return;
        }
        Capture updated, full;
        const bool ok = updated.read(window->window());
        const Rect key = ToolboxBlurSnapshotInspection::rect(*controller);
        this->record("band-app-update-grow",
                     now.windowUpdateEvtDrawCount > this->observation_.draws.windowUpdateEvtDrawCount
                         && now.totalDrawDirtyCalls == this->observation_.draws.totalDrawDirtyCalls
                         && now.windowFlushFullCount == this->observation_.draws.windowFlushFullCount
                         && EqualRect(&key, &this->observation_.key) && ok
                         && !updated.equals(*this->observation_.before, Unit));
        this->logPicture(window);
        window->drawForFlush();
        this->record("band-update-equals-full", ok && full.read(window->window()) && updated.equals(full, Unit));
        Rect cover = {0, 0, UnitHeight, Width / 2};
        LocalToGlobal(reinterpret_cast<Point *>(&cover.top));
        LocalToGlobal(reinterpret_cast<Point *>(&cover.bottom));
        this->observation_.cover =
            NewWindow(0, &cover, "\pCover", true, plainDBox, reinterpret_cast<WindowPtr>(-1L), false, 0);
        this->record("band-cover-created", this->observation_.cover != 0);
        SetPort(window->window());
        window->drawForFlush();
        this->logPicture(window);
        return;
      }
      if (this->phase_ == 5)
      {
        // Consume the covered window's pending update before exposing it.
        window->drawForUpdate();
        if (this->observation_.cover)
        {
          DisposeWindow(this->observation_.cover);
          this->observation_.cover = 0;
        }
        this->observation_.draws = ToolboxBlurSnapshotInspection::draws(*controller);
        this->observation_.waits = 0;
        return;
      }
      if (this->phase_ == 6)
      {
        const ToolboxSceneDebugStats now = ToolboxBlurSnapshotInspection::draws(*controller);
        if (now.windowUpdateEvtDrawCount == this->observation_.draws.windowUpdateEvtDrawCount
            && ++this->observation_.waits < 120)
        {
          --this->phase_;
          return;
        }
        Capture exposed, white;
        const bool ok = exposed.read(window->window());
        this->logPicture(window);
        loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
        const Rect hidden = {8, 8, 24, Width / 2 - 16};
        this->record("band-exposed-white-stale",
                     now.windowUpdateEvtDrawCount > this->observation_.draws.windowUpdateEvtDrawCount && ok
                         && white.read(window->window()) && exposed.equals(white, hidden));
        this->finish();
      }
    }
    void backdrop(ToolboxWindow *window)
    {
      EraseRect(&Whole);
      ZStackNode *stack = this->node_->stack();
      RenderNode(stack->childrenHead(), window->scenePlatformController());
    }
    void logPicture(ToolboxWindow *window)
    {
      ToolboxBlurSnapshotInspection::timing(*window->scenePlatformController(), this->log_);
      Capture picture;
      std::fprintf(
          this->log_, "band-picture fnv=%08lx\r", picture.read(window->window()) ? picture.fingerprint(Unit) : 0UL);
    }
    void secondMilk(ToolboxWindow *window)
    {
      window->drawForFlush();
      Capture actual, expected;
      const Rect second = {60, 0, 80, Width};
      const bool ok = actual.read(window->window());
      loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, SecondGround);
      ZStackNode *secondStack = this->node_->stack()->nextInComposition->asZStackNode();
      RenderNode(secondStack->childrenHead(), window->scenePlatformController());
      ToolboxFrostCache milk;
      this->record("band-second-stack-milk",
                   ok && milk.paint(window->window(), second, second) && expected.read(window->window())
                       && actual.equals(expected, second));
    }
    void pictureClip(ToolboxWindow *window)
    {
      window->drawForFlush();
      Capture picture, clipped;
      const bool ok = picture.read(window->window());
      const Rect line = {UnitHeight - 1, 0, UnitHeight, Width};
      ForeColor(blackColor);
      PaintRect(&line);
      const Rect clip = {0, 0, UnitHeight, Width / 2};
      ClipRect(&clip);
      PenState oldPen, beforePen, afterPen;
      GetPenState(&oldPen);
      loka::toolbox::ToolboxAlignedPattern pattern;
      for (int i = 0; i < 8; ++i)
        pattern.pattern.pat[i] = static_cast<unsigned char>(i & 1 ? 0xAA : 0x55);
      PenPat(&pattern.pattern);
      PenMode(patOr);
      PenSize(3, 2);
      GetPenState(&beforePen);
      RGBColor beforeColor, afterColor;
      GetForeColor(&beforeColor);
      ToolboxInputDoor::render(*window->scenePlatformController());
      GetForeColor(&afterColor);
      GetPenState(&afterPen);
      this->record("band-pen-foreground-restored",
                   std::memcmp(&beforePen, &afterPen, sizeof(PenState)) == 0 && beforeColor.red == afterColor.red
                       && beforeColor.green == afterColor.green && beforeColor.blue == afterColor.blue);
      SetPenState(&oldPen);
      ClipRect(&Whole);
      const bool read = clipped.read(window->window());
      const Rect left = {UnitHeight - 1, 0, UnitHeight, Width / 2},
                 right = {UnitHeight - 1, Width / 2, UnitHeight, Width};
      this->record("band-hairline-caller-clip",
                   ok && read && clipped.equals(picture, left) && !clipped.equals(picture, right));
    }
    void tinyAndGrowth(ToolboxWindow *window)
    {
      ToolboxFrostCache cache;
      Capture before, after;
      if (!before.valid() || !after.valid())
      {
        this->record("tiny-units", false);
        this->record("growth-refusal", false);
        return;
      }
      bool tiny = true;
      for (short width = 1; width <= 3; ++width)
        for (short height = 1; height <= 3; ++height)
        {
          this->backdrop(window);
          const Rect rect = {60, 60, static_cast<short>(60 + height), static_cast<short>(60 + width)};
          bool ok = before.read(window->window());
          const unsigned long start = Now();
          ok = cache.paint(window->window(), rect, rect) && ok;
          std::fprintf(this->log_, "frost unit=%dx%d us=%lu\r", width, height, Now() - start);
          ok = after.read(window->window()) && ok;
          for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
              if ((x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom)
                  && before.at(x, y) != after.at(x, y))
                ok = false;
          tiny = tiny && ok;
        }
      this->record("tiny-units", tiny);
      GWorldPtr old = ToolboxFrostCacheInspection::world(cache);
      const unsigned long start = Now();
      this->backdrop(window);
      const bool grew = cache.paint(window->window(), Unit, Unit);
      const unsigned long elapsed = Now() - start;
      GWorldPtr replacement = ToolboxFrostCacheInspection::world(cache);
      const Rect bounds = replacement ? (**GetGWorldPixMap(replacement)).bounds : Rect();
      std::fprintf(this->log_,
                   "growth work=%dx%d replaced=%d us=%lu\r",
                   bounds.right,
                   bounds.bottom,
                   old != replacement,
                   elapsed);
      this->record("growth-refusal",
                   grew && old != replacement && bounds.right >= Width && bounds.bottom >= UnitHeight);
    }
  };
} // namespace
int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<FrostConfig>(0, 0);
}
