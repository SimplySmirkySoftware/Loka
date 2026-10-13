#ifndef LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP
#define LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Where the macOS and Win32 scene controllers place a scene's root layout:
          kInset logical units in from each edge of the window's client area.
          The Toolbox rail places its root differently (ToolboxRender.cpp). */
      struct SceneRootLayout
      {
        enum
        {
          kInset = 20
        };

        /** The root LayoutState for a client area. `clientWidth` is the width the
            controller measured; a negative remainder becomes 0. A `clientHeight`
            of 0 or less gives a height of 0. */
        static scene::LayoutState stateFor(int clientWidth, int clientHeight)
        {
          scene::LayoutState state;
          state.x = kInset;
          state.y = kInset;
          state.width = clientWidth - 2 * kInset;
          if (state.width < 0)
          {
            state.width = 0;
          }
          state.height = clientHeight > 0 ? clientHeight - 2 * kInset : 0;
          return state;
        }
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP
