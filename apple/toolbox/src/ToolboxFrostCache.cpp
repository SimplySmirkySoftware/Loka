#include "ToolboxFrostCache.hpp"
#include "ToolboxPaintGround.hpp"
#include <Gestalt.h>

bool queryToolboxFpuType(ToolboxFpuType &out)
{
#if defined(__m68k__)
  long answer;
  if (Gestalt(gestaltFPUType, &answer) != noErr)
    return false;
  switch (answer)
  {
  case gestaltNoFPU:
    out = TOOLBOX_FPU_NONE;
    return true;
  case gestalt68881:
    out = TOOLBOX_FPU_68881;
    return true;
  case gestalt68882:
    out = TOOLBOX_FPU_68882;
    return true;
  case gestalt68040FPU:
    out = TOOLBOX_FPU_68040;
    return true;
  }
  return false;
#elif defined(__ppc__) || defined(__POWERPC__)
  out = TOOLBOX_FPU_POWERPC;
  return true;
#else
#error Unsupported Toolbox architecture
#endif
}

namespace
{
  /** One paint restores both ports' environment, including the window ground
      on every refusal. Pixel locking has its own smaller lifetime below. */
  class FrostPaintScope
  {
  public:
    explicit FrostPaintScope(WindowPtr window)
        : window_(window)
    {
      GetGWorld(&this->port_, &this->device_);
      GetForeColor(&this->foreground_);
      GetBackColor(&this->background_);
    }
    ~FrostPaintScope()
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
    WindowPtr window_;
    CGrafPtr port_;
    GDHandle device_;
    RGBColor foreground_;
    RGBColor background_;
    FrostPaintScope(const FrostPaintScope &);
    FrostPaintScope &operator=(const FrostPaintScope &);
  };

  class FrostPixels
  {
  public:
    explicit FrostPixels(GWorldPtr world)
        : pixels_(GetGWorldPixMap(world))
    {
      if (!LockPixels(this->pixels_))
        this->pixels_ = 0;
    }
    ~FrostPixels()
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
    FrostPixels(const FrostPixels &);
    FrostPixels &operator=(const FrostPixels &);
  };

  unsigned short Milk(unsigned short channel)
  {
    const unsigned v = channel >> 8;
    return static_cast<unsigned short>((v + (((255 - v) * 160) >> 8)) * 257);
  }
  void CopyColors()
  {
    // Multiversal declares these colour traps with non-const RGBColor*.
    RGBColor black = {0, 0, 0};
    RGBColor white = {65535, 65535, 65535};
    RGBForeColor(&black);
    RGBBackColor(&white);
  }
} // namespace

ToolboxFrostCache::ToolboxFrostCache()
    : world_(0),
      hairline_(0),
      fpu_(TOOLBOX_FPU_NONE)
{
  // One query per controller. Both an absent and an unanswered FPU refuse.
  queryToolboxFpuType(this->fpu_);
}
ToolboxFrostCache::~ToolboxFrostCache()
{
  if (this->world_)
    DisposeGWorld(this->world_);
}

bool ToolboxFrostCache::prepare(WindowPtr window, short width, short height)
{
  if (width <= 0 || height <= 0)
    return false;
  if (this->world_)
  {
    const Rect capacity = (**GetGWorldPixMap(this->world_)).bounds;
    if (capacity.right >= width && capacity.bottom >= height)
      return true;
    if (capacity.right > width)
      width = capacity.right;
    if (capacity.bottom > height)
      height = capacity.bottom;
  }
  const Rect bounds = {0, 0, height, width};
  GWorldPtr replacement = 0;
  const QDErr error = NewGWorld(&replacement, 8, &bounds, 0, 0, 0);
  if (error != noErr || !replacement)
  {
    if (replacement)
      DisposeGWorld(replacement);
    return false;
  }
  bool ready = false;
  {
    FrostPixels pixels(replacement);
    ready = pixels.get() != 0;
    if (ready && !this->world_)
    {
      SetGWorld(replacement, 0);
      const CTabHandle table = (**pixels.get()).pmTable;
      ready = table && (**table).ctSize == 255;
      if (ready)
      {
        for (short i = 0; i < 256; ++i)
        {
          const RGBColor color = (**table).ctTable[i].rgb;
          RGBColor milk = {Milk(color.red), Milk(color.green), Milk(color.blue)};
          this->milkIndex_[i] = static_cast<unsigned char>(Color2Index(&milk));
        }
        RGBColor grey = {0xBBBB, 0xBBBB, 0xBBBB};
        this->hairline_ = static_cast<unsigned char>(Color2Index(&grey));
        ready = QDError() == noErr;
      }
    }
  }
  // Never dispose the current port. The caller's scope restores its saved
  // window/device after this operation; keep the replacement current on success.
  if (!ready)
  {
    SetPort(window);
    DisposeGWorld(replacement);
    return false;
  }
  SetGWorld(replacement, 0);
  GWorldPtr old = this->world_;
  this->world_ = replacement;
  if (old)
    DisposeGWorld(old);
  return true;
}

bool ToolboxFrostCache::paint(WindowPtr window, const Rect &rect, const Rect &unit)
{
  long version;
  if (this->fpu_ == TOOLBOX_FPU_NONE || Gestalt(gestaltQuickdrawVersion, &version) != noErr || version < gestalt32BitQD)
    return false;
  FrostPaintScope scope(window);
  scope.windowCurrent();
  Rect global = rect;
  LocalToGlobal(reinterpret_cast<Point *>(&global.top));
  LocalToGlobal(reinterpret_cast<Point *>(&global.bottom));
  GDHandle device = GetMaxDevice(&global);
  if (!device || !(**device).gdPMap)
    return false;
  const Rect screen = (**device).gdRect;
  if (global.left < screen.left || global.top < screen.top || global.right > screen.right
      || global.bottom > screen.bottom || (**(**device).gdPMap).pixelSize < 8)
    return false;
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0 || width > 32767 || height > 32767
      || !this->prepare(window, static_cast<short>(width), static_cast<short>(height)))
    return false;
  SetGWorld(this->world_, 0);
  FrostPixels pixels(this->world_);
  if (!pixels.get())
    return false;
  const Rect work = {0, 0, static_cast<short>(height), static_cast<short>(width)};
  CopyColors();
  CopyBits(&window->portBits, reinterpret_cast<BitMap *>(*pixels.get()), &rect, &work, srcCopy, 0);
  if (QDError() != noErr)
    return false;
  // A viewport may cut the unit: its last visible row is not its bottom.
  const int hairline = unit.bottom == rect.bottom ? height - 1 : -1;
  unsigned char *base = reinterpret_cast<unsigned char *>(GetPixBaseAddr(pixels.get()));
  const long stride = (**pixels.get()).rowBytes & 0x3FFF;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      base[y * stride + x] = y == hairline ? this->hairline_ : this->milkIndex_[base[y * stride + x]];
  scope.windowCurrent();
  CopyColors();
  // Do not widen the outer dirty clip. A changed screen CLUT leaves existing
  // on-screen indices unchanged until the next repaint of that unit.
  CopyBits(reinterpret_cast<BitMap *>(*pixels.get()), &window->portBits, &work, &rect, srcCopy, 0);
  return QDError() == noErr;
}
