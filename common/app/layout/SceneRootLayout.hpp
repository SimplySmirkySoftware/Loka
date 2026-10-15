#ifndef LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP
#define LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Where the macOS scene controller places a scene's root layout:
          kInset logical units in from each edge of the window's client area.
          Win32 uses win32/src/Win32SceneLayout.hpp; Toolbox places its root
          in ToolboxRender.cpp. */
      struct SceneRootLayout
      {
        enum
        {
          kInset = 20
        };
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP
