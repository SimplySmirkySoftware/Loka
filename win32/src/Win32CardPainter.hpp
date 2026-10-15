#ifndef LOKA_WIN32_CARD_PAINTER_HPP
#define LOKA_WIN32_CARD_PAINTER_HPP
#include "platform/Win32DisplayScale.hpp"
#include "app/nodes/controls/Card.hpp"

namespace loka
{
  namespace win32
  {
    /** Reconstruct a band sample, projecting shared real edges before making
        the virtual edge remote from the sample. Only corners on known edges
        are constructed; the body-side thickness must be below D/2. */
    void PaintCardBandInk(HDC dc,
                          const core::Frame &band,
                          const app::CardBandProps &props,
                          const Win32DisplayScale &scale,
                          int phaseX,
                          int phaseY);
    /** Paint only ink into the caller's clip. Body edges are absolute layout
        coordinates; deviceOrigin is the projected drawing surface origin.
        Phase is that surface's offset from the top-level client origin.
        Used by band HWNDs and whole-frame offscreen rendering alike. */
    void PaintCardInk(HDC dc,
                      const core::Frame &body,
                      const app::CardStyle &style,
                      const Win32DisplayScale &scale,
                      int deviceOriginX,
                      int deviceOriginY,
                      int phaseX,
                      int phaseY);
  } // namespace win32
} // namespace loka
#endif
