#include "ToolboxStaticBlur.hpp"
#include "ToolboxFrostCache.hpp"
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
  unsigned char Milk(unsigned value, unsigned resolution)
  {
    return static_cast<unsigned char>((value + (((255 - value) * 160) >> 8)) >> (8 - resolution));
  }
  /** In-place separable [1 2 1], retaining original neighbors, edge repeat. */
  void FilterLine(unsigned char *first, int count, long step)
  {
    unsigned previous = *first;
    for (int i = 0; i < count; ++i)
    {
      const unsigned current = *first;
      const unsigned next = i + 1 < count ? first[step] : current;
      *first = static_cast<unsigned char>((previous + 2 * current + next) / 4);
      previous = current;
      first += step;
    }
  }
} // namespace

/** Single candidate resource owner. Reduced storage is interleaved RGB bytes,
    exactly ceil(w/8)*ceil(h/8)*3, filtered in place. */
struct ToolboxStaticBlurPixels::Image
{
  GWorldPtr world;
  unsigned char *reduced;
  unsigned char *inverse;
  short resolution;
  Image()
      : world(0),
        reduced(0),
        inverse(0),
        resolution(0)
  {
  }
  ~Image()
  {
    delete[] this->reduced;
    delete[] this->inverse;
    if (this->world)
      DisposeGWorld(this->world);
  }
  bool build(GrafPtr window, const Rect &rect, unsigned long *timings)
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
    const int columns = (width + 7) / 8, rows = (height + 7) / 8;
    this->reduced = new (std::nothrow) unsigned char[static_cast<long>(columns) * rows * 3];
    if (!this->reduced)
      return false;
    CTabHandle table = (**locked.get()).pmTable;
    if (!table || (**table).ctSize != 255)
      return false;
    unsigned char *base = reinterpret_cast<unsigned char *>(GetPixBaseAddr(locked.get()));
    if (!base)
      return false;
    const long stride = (**locked.get()).rowBytes & 0x3FFF;
    for (int by = 0; by < rows; ++by)
      for (int bx = 0; bx < columns; ++bx)
      {
        unsigned long sum[3] = {0, 0, 0};
        const int bw = width - bx * 8 < 8 ? width - bx * 8 : 8;
        const int bh = height - by * 8 < 8 ? height - by * 8 : 8;
        for (int y = 0; y < bh; ++y)
          for (int x = 0; x < bw; ++x)
          {
            const RGBColor color = (**table).ctTable[base[(by * 8 + y) * stride + bx * 8 + x]].rgb;
            sum[0] += color.red >> 8;
            sum[1] += color.green >> 8;
            sum[2] += color.blue >> 8;
          }
        for (int c = 0; c < 3; ++c)
          this->reduced[(static_cast<long>(by) * columns + bx) * 3 + c] =
              static_cast<unsigned char>(sum[c] / (bw * bh));
      }
    timings[1] = Now() - start;
    start = Now();
    for (int c = 0; c < 3; ++c)
    {
      for (int y = 0; y < rows; ++y)
        FilterLine(this->reduced + static_cast<long>(y) * columns * 3 + c, columns, 3);
      for (int x = 0; x < columns; ++x)
        FilterLine(this->reduced + x * 3 + c, rows, static_cast<long>(columns) * 3);
    }
    timings[2] = Now() - start;
    start = Now();
    RGBColor probe = {65535, 0, 0};
    Color2Index(&probe); // Bring QuickDraw's movable inverse table up to date.
    GDHandle device = GetGWorldDevice(this->world);
    if (!device || !(**device).gdITable || QDError() != noErr)
      return false;
    ITabHandle inverseTable = (**device).gdITable;
    this->resolution = (**inverseTable).iTabRes;
    if (this->resolution < 3 || this->resolution > 5)
      return false;
    const long size = 1L << (3 * this->resolution);
    this->inverse = new (std::nothrow) unsigned char[size];
    if (!this->inverse)
      return false;
    // Allocation may move the handle; dereference it only after allocation.
    BlockMoveData((**inverseTable).iTTable, this->inverse, size);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
      {
        const unsigned char *rgb = this->reduced + (static_cast<long>(y / 8) * columns + x / 8) * 3;
        const unsigned index = (static_cast<unsigned>(Milk(rgb[0], this->resolution)) << (2 * this->resolution))
                               | (static_cast<unsigned>(Milk(rgb[1], this->resolution)) << this->resolution)
                               | Milk(rgb[2], this->resolution);
        base[y * stride + x] = this->inverse[index];
      }
    timings[3] = Now() - start;
    // Only snapshot pixels survive capture; workspace and the private table
    // have no future reader. The candidate remains the single cleanup owner.
    delete[] this->reduced;
    this->reduced = 0;
    delete[] this->inverse;
    this->inverse = 0;
    return true;
  }

private:
  Image(const Image &);
  Image &operator=(const Image &);
};

ToolboxStaticBlurPixels::ToolboxStaticBlurPixels()
    : image_(0)
{
  for (int i = 0; i < 5; ++i)
    this->timings_[i] = 0;
}
ToolboxStaticBlurPixels::~ToolboxStaticBlurPixels()
{
  this->clear();
}
void ToolboxStaticBlurPixels::clear()
{
  delete this->image_;
  this->image_ = 0;
}

bool ToolboxStaticBlurPixels::eligible(GrafPtr window, const Rect &rect)
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
bool ToolboxStaticBlurPixels::capture(GrafPtr window, const Rect &rect)
{
  this->clear();
  Image *candidate = new (std::nothrow) Image;
  if (!candidate)
    return false;
  if (!candidate->build(window, rect, this->timings_))
  {
    delete candidate;
    return false;
  }
  this->image_ = candidate;
  return true;
}
bool ToolboxStaticBlurPixels::paint(GrafPtr window, const Rect &rect)
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
  this->timings_[4] = Now() - start;
  return ok;
}
