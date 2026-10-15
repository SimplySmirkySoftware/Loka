#ifndef LOKA_APP_SURFACE_GROUND_HPP
#define LOKA_APP_SURFACE_GROUND_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    /** Ground roles shared by the rails; native-owned and transparent kinds
        leave ground painting to the native control or the surface beneath. */
    enum SurfaceGround
    {
      SURFACE_GROUND_TRANSPARENT,
      SURFACE_GROUND_NATIVE,
      SURFACE_GROUND_WINDOW,
      SURFACE_GROUND_DOCUMENT,
      SURFACE_GROUND_CONTROL
    };

    /** The one function that decides a surface's ground from its node kind.
        There are no per-instance or per-rail overrides. Adding a NodeKind
        fails the Linux CI build until its ground is decided here.
        Window is not a NodeKind; its rail uses SURFACE_GROUND_WINDOW. */
    inline SurfaceGround GroundForKind(scene::NodeKind kind)
    {
      switch (kind)
      {
      case scene::NODE_KIND_RIBBON_GROUP_EDGE:
        return SURFACE_GROUND_WINDOW;
      case scene::NODE_KIND_CELL:
        return SURFACE_GROUND_CONTROL;
      case scene::NODE_KIND_RECT_SURFACE:
        return SURFACE_GROUND_DOCUMENT;
      case scene::NODE_KIND_BUTTON:
      case scene::NODE_KIND_EDIT_TEXT:
      case scene::NODE_KIND_POPUP_MENU:
      case scene::NODE_KIND_OPEN_FILE_DIALOG:
      case scene::NODE_KIND_CONFIRM_DIALOG:
      case scene::NODE_KIND_SCROLL_BAR:
      case scene::NODE_KIND_TEXT_EDITOR:
        return SURFACE_GROUND_NATIVE;
      case scene::NODE_KIND_UNKNOWN:
      case scene::NODE_KIND_BOX:
      case scene::NODE_KIND_CARD_BAND:
      case scene::NODE_KIND_CARD:
      case scene::NODE_KIND_RIBBON_GROUP:
      case scene::NODE_KIND_ZSTACK:
      case scene::NODE_KIND_GRID:
      case scene::NODE_KIND_STACK:
      case scene::NODE_KIND_TEXT:
      case scene::NODE_KIND_IMAGE_VIEW:
      case scene::NODE_KIND_SCROLL_VIEW:
      case scene::NODE_KIND_CANVAS:
      case scene::NODE_KIND_ATTRIBUTED_TEXT:
        return SURFACE_GROUND_TRANSPARENT;
      }
      return SURFACE_GROUND_TRANSPARENT;
    }
  } // namespace app
} // namespace loka

#endif
