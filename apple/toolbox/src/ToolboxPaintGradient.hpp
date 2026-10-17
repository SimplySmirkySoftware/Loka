#ifndef LOKA_TOOLBOX_PAINT_GRADIENT_HPP
#define LOKA_TOOLBOX_PAINT_GRADIENT_HPP

#include "ToolboxAlignedPattern.hpp"
#include <cassert>

namespace loka
{
  namespace toolbox
  {
    /** Equal row and column populations keep one-pixel strips phase independent. */
    static const ToolboxAlignedPattern kToolboxGradientPatterns[9] = {
        {{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
        {{{0x80, 0x10, 0x02, 0x40, 0x08, 0x01, 0x20, 0x04}}},
        {{{0x88, 0x22, 0x44, 0x11, 0x88, 0x22, 0x44, 0x11}}},
        {{{0xA4, 0x52, 0x29, 0x94, 0x4A, 0x25, 0x92, 0x49}}},
        {{{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}}},
        {{{0x5B, 0xAD, 0xD6, 0x6B, 0xB5, 0xDA, 0x6D, 0xB6}}},
        {{{0x77, 0xDD, 0xBB, 0xEE, 0x77, 0xDD, 0xBB, 0xEE}}},
        {{{0x7F, 0xEF, 0xFD, 0xBF, 0xF7, 0xFE, 0xDF, 0xFB}}},
        {{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}}};

    enum ToolboxGradientAxis
    {
      BOTTOM_TO_TOP
    };

    /** Fill equal bands using the current port's foreground and background.
        Each band owns [length*i/count, length*(i+1)/count) from the start edge. */
    inline void
    ToolboxPaintGradient(const Rect &rect, ToolboxGradientAxis axis, const unsigned char *levels, unsigned short count)
    {
      assert(levels && count);
      if (!levels || !count || EmptyRect(&rect))
        return;
      for (unsigned short i = 0; i < count; ++i)
      {
        assert(levels[i] <= 8);
        if (levels[i] > 8)
          return;
      }
      switch (axis)
      {
      case BOTTOM_TO_TOP:
        const unsigned long length = static_cast<long>(rect.bottom) - rect.top;
        for (unsigned short i = 0; i < count; ++i)
        {
          Rect band = rect;
          band.bottom = static_cast<short>(rect.bottom - static_cast<long>(length * i / count));
          band.top = static_cast<short>(rect.bottom - static_cast<long>(length * (i + 1UL) / count));
          FillRect(&band, &kToolboxGradientPatterns[levels[i]].pattern);
        }
        break;
      }
    }
  } // namespace toolbox
} // namespace loka
#endif
