#include "Win32SceneLayout.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "Win32BuiltInSupport.hpp"
#include "app/nodes/AttributedText.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/Text.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "context/Win32ButtonContext.hpp"
#include "context/Win32CellContext.hpp"
#include "context/Win32RibbonGroupEdgeContext.hpp"
#include "context/Win32EditTextContext.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32ImageViewContext.hpp"
#include "context/Win32OpenFileDialogContext.hpp"
#include "context/Win32ConfirmDialogContext.hpp"
#include "context/Win32PopupMenuContext.hpp"
#include "context/Win32TextContext.hpp"
#include "context/Win32AttributedTextContext.hpp"

namespace
{
  loka::app::scene::RefusedNodeHandler
      gRefusedWin32CardBand(loka::app::scene::NodeTypeToken<loka::app::CardBandNode>());
  // Win32 has no ScrollBar context yet: a known unsupported kind must take
  // the typed-refusal path, not trip the accidental-miss assert. ScrollView
  // is a controller-owned projection-parent arm and does not use this leaf
  // ensure registry.
  loka::app::scene::RefusedNodeHandler gRefusedWin32ScrollBar(
      loka::app::scene::NodeTypeToken<loka::app::ScrollBarNode>());
} // namespace

void RegisterWin32BuiltInSupport(Win32ScenePlatformController &controller)
{
  RegisterWin32RibbonGroupEdgeNodeHandler(controller.nodeHandlerRegistry_);
  controller.nodeHandlerRegistry_.registerHandler(&gRefusedWin32CardBand);
  loka::app::layout::GridLayoutMetrics gridMetrics;
  gridMetrics.gapX = 0;
  gridMetrics.gapY = 0;
  loka::app::layout::RegisterBuiltinPlatformLayoutHandlers(
      controller.layoutHandlerRegistry_, &controller.rowLayoutMetrics(), &gridMetrics, Win32SceneLayout::kGap);
  RegisterWin32TextEditorNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32ButtonNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32TextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32AttributedTextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32ImageViewNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32EditTextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32PopupMenuNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32CellNodeHandler(controller.nodeHandlerRegistry_);
  RegisterWin32OpenFileDialogNodeHandler(controller.nodeHandlerRegistry_);
  controller.nodeHandlerRegistry_.registerHandler(&gRefusedWin32ScrollBar);
  RegisterWin32ConfirmDialogNodeHandler(controller.nodeHandlerRegistry_);
}
