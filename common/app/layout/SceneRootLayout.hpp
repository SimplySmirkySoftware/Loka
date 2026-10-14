#ifndef LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP
#define LOKA_APP_LAYOUT_SCENE_ROOT_LAYOUT_HPP

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Where the macOS and Win32 scene controllers place a scene's root layout:
          kInset logical units in from each edge of the window's client area.
          Each controller builds its own root LayoutState from it in
          performLayout. The Toolbox rail places its root differently
          (ToolboxRender.cpp). */
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
