#include "ToolboxBlurSnapshot.hpp"
#include "ToolboxBlurKernel.hpp"
#include "ToolboxFrostCache.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxPaintGround.hpp"
#include <QDOffscreen.h>
#include <Gestalt.h>
#include <Memory.h>
#include <new>
#include <Timer.h>

namespace
{
#if defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  /** Multiversal omits Timer.h's Microseconds trap and two-word result. */
  struct UnsignedWide
  {
    unsigned long hi, lo;
  };
  extern "C"
  {
    pascal void Microseconds(UnsignedWide *value) M68K_INLINE(0xA193, 0x225F, 0x22C8, 0x2280);
  }
#endif
  unsigned long Now()
  {
    UnsignedWide value;
    Microseconds(&value);
    return value.lo;
  }

  /** Restores the port before candidate destruction, including every refusal. */
  class StaticPaintScope
  {
  public:
    explicit StaticPaintScope(GrafPtr window)
        : window_(window)
    {
      GetGWorld(&this->port_, &this->device_);
      GetForeColor(&this->foreground_);
      GetBackColor(&this->background_);
    }
    ~StaticPaintScope()
    {
      SetPort(this->window_);
      loka::toolbox::ToolboxApplyWindowGround();
      SetGWorld(this->port_, this->device_);
      RGBForeColor(&this->foreground_);
      RGBBackColor(&this->background_);
      if (reinterpret_cast<GrafPtr>(this->port_) == this->window_)
        loka::toolbox::ToolboxApplyWindowGround();
    }
    void windowCurrent()
    {
      SetGWorld(reinterpret_cast<CGrafPtr>(this->window_), this->device_);
    }

  private:
    GrafPtr window_;
    CGrafPtr port_;
    GDHandle device_;
    RGBColor foreground_, background_;
    StaticPaintScope(const StaticPaintScope &);
    StaticPaintScope &operator=(const StaticPaintScope &);
  };
  class LockedPixels
  {
  public:
    explicit LockedPixels(GWorldPtr world)
        : pixels_(GetGWorldPixMap(world))
    {
      if (!this->pixels_ || !LockPixels(this->pixels_))
        this->pixels_ = 0;
    }
    ~LockedPixels()
    {
      if (this->pixels_)
        UnlockPixels(this->pixels_);
    }
    PixMapHandle get() const
    {
      return this->pixels_;
    }

  private:
    PixMapHandle pixels_;
    LockedPixels(const LockedPixels &);
    LockedPixels &operator=(const LockedPixels &);
  };
  class CaptureMask
  {
  public:
    CaptureMask(GrafPtr window, const Rect &rect)
        : region_(NewRgn())
    {
      if (this->region_)
      {
        CopyRgn(window->visRgn, this->region_);
        OffsetRgn(this->region_, -rect.left, -rect.top);
        if (QDError() != noErr)
        {
          DisposeRgn(this->region_);
          this->region_ = 0;
        }
      }
    }
    ~CaptureMask()
    {
      if (this->region_)
        DisposeRgn(this->region_);
    }
    RgnHandle get() const
    {
      return this->region_;
    }

  private:
    RgnHandle region_;
    CaptureMask(const CaptureMask &);
    CaptureMask &operator=(const CaptureMask &);
  };
  void CopyColors()
  {
    RGBColor black = {0, 0, 0}, white = {65535, 65535, 65535};
    RGBForeColor(&black);
    RGBBackColor(&white);
  }
  /** Queries the current capture GWorld; retains no movable QuickDraw pointers. */
  class CaptureColourQuery : public loka::toolbox::ToolboxBlurColourQuery
  {
  public:
    virtual bool indexFor(unsigned short red, unsigned short green, unsigned short blue, unsigned char &index)
    {
      RGBColor color = {red, green, blue};
      return loka::toolbox::ToolboxBlurAcceptColorIndex(Color2Index(&color), index);
    }
  };
  /** Owns centre answers and the CLUT copied before the kernel allocates. */
  class CaptureTables
  {
  public:
    CaptureTables()
        : palette(new(std::nothrow) unsigned char[768]),
          inverse(0)
    {
    }
    ~CaptureTables()
    {
      delete[] this->palette;
      delete[] this->inverse;
    }
    unsigned char *const palette;
    unsigned char *inverse;

  private:
    CaptureTables(const CaptureTables &);
    CaptureTables &operator=(const CaptureTables &);
  };
} // namespace

/** Single candidate resource owner; only the completed GWorld survives. */
struct ToolboxBlurPixels::Image
{
  GWorldPtr world;
  Image()
      : world(0)
  {
  }
  ~Image()
  {
    if (this->world)
      DisposeGWorld(this->world);
  }
  bool build(GrafPtr window, const Rect &rect, int cell, unsigned long *timings)
  {
    // This scope ends before Image is disposed, on success and on all failures.
    unsigned long start = Now();
    StaticPaintScope scope(window);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const Rect bounds = {0, 0, static_cast<short>(height), static_cast<short>(width)};
    QDErr error = NewGWorld(&this->world, 8, &bounds, 0, 0, useTempMem);
    if (error != noErr || !this->world)
    {
      scope.windowCurrent();
      if (this->world)
        DisposeGWorld(this->world);
      this->world = 0;
      error = NewGWorld(&this->world, 8, &bounds, 0, 0, 0);
    }
    if (error != noErr || !this->world)
      return false;
    LockedPixels locked(this->world);
    if (!locked.get())
      return false;
    SetGWorld(this->world, 0);
    CopyColors();
    EraseRect(&bounds);
    if (QDError() != noErr)
      return false;
    CaptureMask mask(window, rect);
    if (!mask.get())
      return false;
    CopyBits(&window->portBits, reinterpret_cast<BitMap *>(*locked.get()), &rect, &bounds, srcCopy, mask.get());
    if (QDError() != noErr)
      return false;

    timings[0] = Now() - start;
    start = Now();
    CaptureTables tables;
    if (!tables.palette)
      return false;
    CTabHandle table = (**locked.get()).pmTable;
    if (!table || (**table).ctSize != 255)
      return false;
    for (int i = 0; i < 256; ++i)
    {
      const RGBColor color = (**table).ctTable[i].rgb;
      tables.palette[3 * i] = static_cast<unsigned char>(color.red >> 8);
      tables.palette[3 * i + 1] = static_cast<unsigned char>(color.green >> 8);
      tables.palette[3 * i + 2] = static_cast<unsigned char>(color.blue >> 8);
    }
    RGBColor probe = {65535, 0, 0};
    Color2Index(&probe); // Bring QuickDraw's movable inverse table up to date.
    if (QDError() != noErr)
      return false;
    GDHandle device = GetGWorldDevice(this->world);
    if (!device || !*device || !(**device).gdITable || !*(**device).gdITable)
      return false;
    ITabHandle inverseTable = (**device).gdITable;
    const short resolution = (**inverseTable).iTabRes;
    if (resolution < 3 || resolution > 5)
      return false;
    const long size = 1L << (3 * resolution);
    tables.inverse = new (std::nothrow) unsigned char[size];
    if (!tables.inverse)
      return false;
    // Each reachable slot is QuickDraw's answer for that centre colour during
    // this capture's preparation. It does not change while the kernel runs.
    CaptureColourQuery query;
    const bool filled = loka::toolbox::ToolboxBlurFillInverse(tables.inverse, size, resolution, query);
    const QDErr fillError = QDError();
    if (!filled || fillError != noErr)
      return false;
    timings[1] = Now() - start;
    start = Now();
    loka::platform::CpuClass cpu;
    if (!loka::platform::queryCpuClass(cpu))
      cpu = loka::platform::CPU_CLASS_68K;
    unsigned char *base = reinterpret_cast<unsigned char *>(GetPixBaseAddr(locked.get()));
    const long stride = (**locked.get()).rowBytes & 0x3FFF;
    if (!loka::toolbox::ToolboxBlurKernel(base,
                                          width,
                                          height,
                                          stride,
                                          tables.palette,
                                          cell,
                                          loka::toolbox::ToolboxBlurEnlargementFor(cpu),
                                          tables.inverse,
                                          resolution))
      return false;
    timings[2] = Now() - start;
    return true;
  }

private:
  Image(const Image &);
  Image &operator=(const Image &);
};

ToolboxBlurPixels::ToolboxBlurPixels()
    : image_(0)
{
  for (int i = 0; i < 4; ++i)
    this->timings_[i] = 0;
}
ToolboxBlurPixels::~ToolboxBlurPixels()
{
  this->clear();
}
void ToolboxBlurPixels::clear()
{
  delete this->image_;
  this->image_ = 0;
}

bool ToolboxBlurPixels::eligible(GrafPtr window, const Rect &rect)
{
  long version;
  ToolboxFpuType fpu = TOOLBOX_FPU_NONE;
  if (!queryToolboxFpuType(fpu) || fpu == TOOLBOX_FPU_NONE || Gestalt(gestaltQuickdrawVersion, &version) != noErr
      || version < gestalt32BitQD)
    return false;
  StaticPaintScope scope(window);
  scope.windowCurrent();
  Rect global = rect;
  LocalToGlobal(reinterpret_cast<Point *>(&global.top));
  LocalToGlobal(reinterpret_cast<Point *>(&global.bottom));
  GDHandle device = GetMaxDevice(&global);
  if (!device || !(**device).gdPMap || (**(**device).gdPMap).pixelSize < 8)
    return false;
  const Rect screen = (**device).gdRect;
  return rect.left < rect.right && rect.top < rect.bottom && global.left >= screen.left && global.top >= screen.top
         && global.right <= screen.right && global.bottom <= screen.bottom;
}
bool ToolboxBlurPixels::capture(GrafPtr window, const Rect &rect, int cell)
{
  this->clear();
  Image *candidate = new (std::nothrow) Image;
  if (!candidate)
    return false;
  if (!candidate->build(window, rect, cell, this->timings_))
  {
    delete candidate;
    return false;
  }
  this->image_ = candidate;
  return true;
}
bool ToolboxBlurPixels::paint(GrafPtr window, const Rect &rect)
{
  if (!this->image_)
    return false;
  const unsigned long start = Now();
  StaticPaintScope scope(window);
  LockedPixels locked(this->image_->world);
  if (!locked.get())
    return false;
  scope.windowCurrent();
  CopyColors();
  const Rect bounds = (**locked.get()).bounds;
  // Equal-size per-pixel copy under the caller's current clip. No resampling
  // decision depends on the dirty rectangle, so partial and full paints agree.
  CopyBits(reinterpret_cast<BitMap *>(*locked.get()), &window->portBits, &bounds, &rect, srcCopy, 0);
  const bool ok = QDError() == noErr;
  this->timings_[3] = Now() - start;
  return ok;
}
