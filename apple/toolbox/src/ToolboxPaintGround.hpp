#ifndef LOKA_TOOLBOX_PAINT_GROUND_HPP
#define LOKA_TOOLBOX_PAINT_GROUND_HPP

#include "ToolboxGround.hpp"
#include <Quickdraw.h>
#include <cassert>

namespace loka
{
  namespace toolbox
  {
    typedef char PlanarWhiteMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_WHITE) == whiteColor) ? 1 : -1];
    typedef char PlanarBlackMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_BLACK) == blackColor) ? 1 : -1];

    typedef char PlanarRedMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_RED) == redColor) ? 1 : -1];
    typedef char PlanarGreenMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_GREEN) == greenColor) ? 1 : -1];
    typedef char PlanarBlueMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_BLUE) == blueColor) ? 1 : -1];
    typedef char PlanarCyanMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_CYAN) == cyanColor) ? 1 : -1];
    typedef char PlanarMagentaMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_MAGENTA) == magentaColor) ? 1 : -1];
    typedef char PlanarYellowMustMatchQuickDraw[(static_cast<long>(TOOLBOX_PLANAR_YELLOW) == yellowColor) ? 1 : -1];

    /** One non-yielding glyph paint in the current basic port. The caller keeps
        that port current through destruction; no Color QuickDraw is needed. */
    class ToolboxTextInkScope
    {
    public:
      explicit ToolboxTextInkScope(long ink)
      {
        GrafPtr port;
        GetPort(&port);
        this->saved_ = port->fgColor;
        ForeColor(ink);
      }
      ~ToolboxTextInkScope()
      {
        ForeColor(this->saved_);
      }

    private:
      long saved_;
      ToolboxTextInkScope(const ToolboxTextInkScope &);
      ToolboxTextInkScope &operator=(const ToolboxTextInkScope &);
    };

    /** Sets the current port's background to the window ground. Every Loka
        window port keeps this background between paints, so a plain EraseRect
        restores the window ground beneath transparent kinds. */
    inline void ToolboxApplyWindowGround()
    {
      ToolboxPlanarColor windowColor;
      const bool answered = QueryToolboxGroundColor(app::SURFACE_GROUND_WINDOW, windowColor);
      assert(answered && "the window ground always answers");
      if (answered)
        BackColor(windowColor);
    }

    /** Paints a Loka-owned ground in the current port, then restores its window
        background. Declining roles violate the contract and paint nothing in
        release. System 7 / planar only; RGB needs #1196 page 4's color port. */
    inline void ToolboxPaintGround(app::SurfaceGround ground, const Rect &rect)
    {
      ToolboxPlanarColor color;
      const bool answered = QueryToolboxGroundColor(ground, color);
      assert(answered && "only Loka-owned grounds may be painted");
      if (!answered)
        return;
      BackColor(color);
      EraseRect(&rect);
      ToolboxApplyWindowGround();
    }
  }
}

#endif
