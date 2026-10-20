#ifndef LOKA_FROST_STANDALONE_SUPPORT_HPP
#define LOKA_FROST_STANDALONE_SUPPORT_HPP
#include <QDOffscreen.h>
#include "ToolboxProbeTiming.hpp"
namespace frost_standalone
{
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
    using namespace loka_toolbox_probe;
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
    unsigned long fingerprint(const Rect &area) const
    {
      if (!this->valid() || area.left < 0 || area.top < 0 || area.right > Width || area.bottom > Height
          || area.left >= area.right || area.top >= area.bottom)
        return 0;
      unsigned long hash = 2166136261UL;
      for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x)
          hash = ((hash ^ this->at(x, y)) * 16777619UL) & 0xFFFFFFFFUL;
      return hash;
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
      RGBColor grey = {0xBBBB, 0xBBBB, 0xBBBB};
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

} // namespace frost_standalone
#endif
