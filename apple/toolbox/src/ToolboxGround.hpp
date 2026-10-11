#ifndef LOKA_TOOLBOX_GROUND_HPP
#define LOKA_TOOLBOX_GROUND_HPP

#include "app/style/SurfaceGround.hpp"
#include "app/style/StyleVocab.hpp"
#include <cassert>

namespace loka
{
  namespace toolbox
  {
    /** QuickDraw planar colors used by the System 7 ground column.
        Appearance/RGB grounds arrive with the color port (#1196 page 4). */
    enum ToolboxPlanarColor
    {
      TOOLBOX_PLANAR_WHITE = 30,
      TOOLBOX_PLANAR_BLACK = 33,
      TOOLBOX_PLANAR_RED = 205,
      TOOLBOX_PLANAR_GREEN = 341,
      TOOLBOX_PLANAR_BLUE = 409,
      TOOLBOX_PLANAR_CYAN = 273,
      TOOLBOX_PLANAR_MAGENTA = 137,
      TOOLBOX_PLANAR_YELLOW = 69
    };

    // Deliberate platform-seam twin of Win32Ground's text row.
    /** Text paired with the answering ground. Declining grounds assert in debug
        and return -1 in release; callers supply the ground beneath the text. */
    inline long ToolboxTextRoleColor(app::SurfaceGround ground)
    {
      switch (ground)
      {
      case app::SURFACE_GROUND_WINDOW:
      case app::SURFACE_GROUND_DOCUMENT:
      case app::SURFACE_GROUND_CONTROL:
        return TOOLBOX_PLANAR_BLACK;
      case app::SURFACE_GROUND_TRANSPARENT:
      case app::SURFACE_GROUND_NATIVE:
        break;
      }
      assert(false && "text requires an answering ground");
      return -1;
    }

    /** Basic GrafPort ink. Secondary provisionally uses the ground pair:
        gray requires the color port deferred to #1196 page 4. */
    inline long ToolboxTextInk(app::TextColor color, app::SurfaceGround ground)
    {
      switch (color)
      {
      case app::TEXT_COLOR_UNSET:
      case app::TEXT_COLOR_PRIMARY:
      case app::TEXT_COLOR_SECONDARY:
        return ToolboxTextRoleColor(ground);
      case app::TEXT_COLOR_BLACK:
        return TOOLBOX_PLANAR_BLACK;
      case app::TEXT_COLOR_WHITE:
        return TOOLBOX_PLANAR_WHITE;
      case app::TEXT_COLOR_RED:
        return TOOLBOX_PLANAR_RED;
      case app::TEXT_COLOR_GREEN:
        return TOOLBOX_PLANAR_GREEN;
      case app::TEXT_COLOR_BLUE:
        return TOOLBOX_PLANAR_BLUE;
      case app::TEXT_COLOR_CYAN:
        return TOOLBOX_PLANAR_CYAN;
      case app::TEXT_COLOR_MAGENTA:
        return TOOLBOX_PLANAR_MAGENTA;
      case app::TEXT_COLOR_YELLOW:
        return TOOLBOX_PLANAR_YELLOW;
      }
      assert(false && "unknown text color");
      return -1;
    }

    /** Answers only for Loka-owned grounds; transparent/native decline without
        changing out. Basic B&W ports use the System 7 / planar column only. */
    inline bool QueryToolboxGroundColor(app::SurfaceGround ground, ToolboxPlanarColor &out)
    {
      switch (ground)
      {
      case app::SURFACE_GROUND_TRANSPARENT:
      case app::SURFACE_GROUND_NATIVE:
        return false;
      case app::SURFACE_GROUND_WINDOW:
      case app::SURFACE_GROUND_DOCUMENT:
      case app::SURFACE_GROUND_CONTROL:
        out = TOOLBOX_PLANAR_WHITE;
        return true;
      }
      return false;
    }
  }
}

#endif
