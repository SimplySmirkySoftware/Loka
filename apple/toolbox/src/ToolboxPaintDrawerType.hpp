#ifndef LOKA_TOOLBOX_PAINT_DRAWER_TYPE_HPP
#define LOKA_TOOLBOX_PAINT_DRAWER_TYPE_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    // Node.hpp declares only the nodes with an asXxx() method; this list also names TextEditorNode.
    class TextEditorNode;
  } // namespace app
} // namespace loka

/** Only these built-in drawers are cast. Registration protects the same
    type keys, including in release builds, as on the Win32 rail. */
inline bool IsToolboxPaintDrawerType(const void *key)
{
  return key == loka::app::scene::NodeTypeToken<loka::app::AttributedTextNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::RectSurfaceNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::ButtonNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::CellNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::RibbonGroupEdgeNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::CardBandNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::TextNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::EditTextNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::PopupMenuNode>();
}

#endif
