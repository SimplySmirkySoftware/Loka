#include <cstdio>
#include <cstring>
#include <Gestalt.h>
#include <Palettes.h>
#include <Timer.h>
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

/** Read-only friend inspection adds no production API, storage or test gate. */
class ToolboxFrostCacheInspection
{
public:
  static GWorldPtr world(const ToolboxFrostCache &cache)
  {
    return cache.world_;
  }
};

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  enum
  {
    Width = 220,
    Height = 100,
    UnitHeight = 40
  };
  const Rect Whole = {0, 0, Height, Width};
  const Rect Unit = {0, 0, UnitHeight, Width};

  unsigned long Now()
  {
    UnsignedWide now;
    Microseconds(&now);
    return now.lo;
  }

  /** A pinned 8-bit readback. Comparisons exclude row padding. */
  class Capture
  {
  public:
    Capture()
        : world_(0),
          pixels_(0)
    {
      if (NewGWorld(&this->world_, 8, &Whole, 0, 0, 0) == noErr && this->world_)
      {
        this->pixels_ = GetGWorldPixMap(this->world_);
        if (!LockPixels(this->pixels_))
          this->pixels_ = 0;
      }
    }
    ~Capture()
    {
      if (this->pixels_)
        UnlockPixels(this->pixels_);
      if (this->world_)
        DisposeGWorld(this->world_);
    }
    bool valid() const
    {
      return this->pixels_ != 0;
    }
    bool read(WindowPtr window)
    {
      if (!this->valid())
        return false;
      CGrafPtr port;
      GDHandle device;
      GetGWorld(&port, &device);
      SetGWorld(this->world_, 0);
      ForeColor(blackColor);
      BackColor(whiteColor);
      CopyBits(&window->portBits, reinterpret_cast<BitMap *>(*this->pixels_), &Whole, &Whole, srcCopy, 0);
      const bool ok = QDError() == noErr;
      SetGWorld(port, device);
      return ok;
    }
    unsigned char &at(int x, int y) const
    {
      unsigned char *base = reinterpret_cast<unsigned char *>(GetPixBaseAddr(this->pixels_));
      return base[y * ((**this->pixels_).rowBytes & 0x3FFF) + x];
    }
    bool equals(const Capture &other, const Rect &area) const
    {
      if (!this->valid() || !other.valid())
        return false;
      for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x)
          if (this->at(x, y) != other.at(x, y))
            return false;
      return true;
    }
    void referenceMilk()
    {
      CGrafPtr port;
      GDHandle device;
      GetGWorld(&port, &device);
      SetGWorld(this->world_, 0);
      unsigned char table[256];
      CTabHandle clut = (**this->pixels_).pmTable;
      for (int i = 0; i < 256; ++i)
      {
        RGBColor c = (**clut).ctTable[i].rgb;
        c.red = milk(c.red);
        c.green = milk(c.green);
        c.blue = milk(c.blue);
        table[i] = static_cast<unsigned char>(Color2Index(&c));
      }
      const RGBColor grey = {0xBBBB, 0xBBBB, 0xBBBB};
      const unsigned char line = static_cast<unsigned char>(Color2Index(&grey));
      for (int y = 0; y < UnitHeight; ++y)
        for (int x = 0; x < Width; ++x)
          this->at(x, y) = y == UnitHeight - 1 ? line : table[this->at(x, y)];
      SetGWorld(port, device);
    }
    bool write(WindowPtr window, const Rect &area)
    {
      CGrafPtr port;
      GDHandle device;
      GetGWorld(&port, &device);
      SetGWorld(reinterpret_cast<CGrafPtr>(window), device);
      ForeColor(blackColor);
      BackColor(whiteColor);
      CopyBits(reinterpret_cast<BitMap *>(*this->pixels_), &window->portBits, &area, &area, srcCopy, 0);
      const bool ok = QDError() == noErr;
      SetGWorld(port, device);
      return ok;
    }

  private:
    static unsigned short milk(unsigned short v)
    {
      const unsigned value = v >> 8;
      return static_cast<unsigned short>((value + (255 - value) * 160 / 256) * 257);
    }
    GWorldPtr world_;
    PixMapHandle pixels_;
    Capture(const Capture &);
    Capture &operator=(const Capture &);
  };

  class FrostNode;
  typedef BoundaryPropsFor<FrostNode> FrostProps;
  class FrostNode : public StdCompositionBoundaryNodeBase<FrostProps>
  {
  public:
    typedef FrostProps::TypeTag TypeTag;
    explicit FrostNode(const FrostProps &p)
        : StdCompositionBoundaryNodeBase<FrostProps>(p)
    {
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(
          ZStack().expBlurUnit() << (Column()
                                     << (Text("MMMMMMMMMMMMMMMMMMMM") + loka::app::TextStyle().color(TEXT_COLOR_RED))
                                     << (Text("MMMMMMMMMMMMMMMMMMMM") + loka::app::TextStyle().color(TEXT_COLOR_BLUE))
                                     << (Text("MMMMMMMMMMMMMMMMMMMM") + loka::app::TextStyle().color(TEXT_COLOR_GREEN)))
                                 << Box().size(Width, UnitHeight));
    }
    ZStackNode *stack()
    {
      loka::dsl::CompositionCursor<Node> children(this->childrenHead(), this->childrenCount());
      return children.next()->asZStackNode();
    }
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
      if (this->depthReady_)
      {
        const OSErr result = SetDepth(GetMainDevice(), this->originalDepth_, 1, 1);
        if (this->log_)
          std::fprintf(this->log_, "restore-depth err=%d\r", result);
        if (result != noErr)
          ++this->fail_;
      }
      if (this->log_)
      {
        std::fprintf(this->log_, "RESULT pass=%d fail=%d\r", this->pass_, this->fail_);
        loka::platform::file::FlushWrite(this->log_, this->file_);
        std::fclose(this->log_);
      }
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
              .title("Milk frost")
              .contentMargin(0)
              .scene(loka::scenario_tests::ObservedMainDefinition<FrostProps, FrostNode>(FrostProps(), &this->node_))
              .visible(true)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&FrostConfig::Idle, this));
    }

  private:
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
      loka::platform::file::FlushWrite(this->log_, this->file_);
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
        window->draw();
        const bool captured = actual.read(window->window());
        if (LOKA_FROST_STANDALONE_DEPTH == 1)
        {
          loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
          this->record("white-at-1bit", captured && reference.read(window->window()) && actual.equals(reference, Unit));
        }
        else if (this->eligible_)
        {
          // Reference uses the same work palette, independently transforms the
          // freshly rendered backdrop, then round-trips it through the screen.
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
          this->record("milk-sample", captured && ok && actual.equals(reference, interior));
          const Rect line = {UnitHeight - 1, 0, UnitHeight, Width};
          this->record("hairline", captured && ok && actual.equals(reference, line));
          std::fprintf(this->log_, "comparison=byte-exact 8-bit readback after reference screen round-trip\r");
          this->tinyAndGrowth(window);
        }
        else
          std::fprintf(this->log_, "[skip] milk-sample hairline tiny-units growth-refusal: hardware gate\r");
        window->draw();
        // Public non-erasing render door: no test hook or fact guard.
        ToolboxInputDoor::render(*controller);
        bool ok = actual.read(window->window());
        loka::toolbox::ToolboxPaintGround(SURFACE_GROUND_WINDOW, Unit);
        this->record("non-erased-pass-white", ok && reference.read(window->window()) && actual.equals(reference, Unit));
        window->draw();
        const Rect dirty = {20, 100, 55, Width};
        window->requestInvalidateRect(dirty);
        return;
      }
      // A separate idle lets normal pending damage present before capture.
      if (this->phase_ == 3)
      {
        Capture partial;
        const bool ok = partial.read(window->window());
        window->draw();
        Capture full;
        this->record("partial-equals-full", ok && full.read(window->window()) && partial.equals(full, Whole));
        this->finish();
      }
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
          window->draw();
          const Rect rect = {60, 60, static_cast<short>(60 + height), static_cast<short>(60 + width)};
          bool ok = before.read(window->window());
          const unsigned long start = Now();
          ok = cache.paint(window->window(), rect) && ok;
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
      const bool grew = cache.paint(window->window(), Unit);
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
