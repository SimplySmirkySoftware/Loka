#include "MacSceneLayout.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "MacBuiltInSupport.hpp"
#include "app/nodes/AttributedText.hpp"
#include "MacScenePlatformController.hpp"
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
#include "context/MacButtonContext.hpp"
#include "context/MacCellContext.hpp"
#include "context/MacRibbonGroupEdgeContext.hpp"
#include "context/MacCardBandContext.hpp"
#include "context/MacEditTextContext.hpp"
#include "context/MacTextEditorContext.hpp"
#include "context/MacImageViewContext.hpp"
#include "context/MacOpenFileDialogContext.hpp"
#include "context/MacConfirmDialogContext.hpp"
#include "context/MacPopupMenuContext.hpp"
#include "context/MacTextContext.hpp"
#include "context/MacAttributedTextContext.hpp"

namespace
{
  // Mac has no ScrollBar context yet: a known unsupported kind must take the
  // typed-refusal path, not trip the accidental-miss assert. ScrollView is a
  // controller-owned projection-parent arm and does not use this leaf ensure
  // registry.
  loka::app::scene::RefusedNodeHandler gRefusedMacScrollBar(
      loka::app::scene::NodeTypeToken<loka::app::ScrollBarNode>());
} // namespace

void RegisterMacBuiltInSupport(MacScenePlatformController &controller)
{
  RegisterMacRibbonGroupEdgeNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacCardBandNodeHandler(controller.nodeHandlerRegistry_);
  loka::app::layout::GridLayoutMetrics gridMetrics;
  gridMetrics.gapX = 0;
  gridMetrics.gapY = 0;
  loka::app::layout::RegisterBuiltinNonZStackLayoutHandlers(
      controller.layoutHandlerRegistry_, &controller.rowLayoutMetrics(), &gridMetrics, MacSceneLayout::spacing());
  RegisterMacTextEditorNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacButtonNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacTextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacAttributedTextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacImageViewNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacEditTextNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacPopupMenuNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacCellNodeHandler(controller.nodeHandlerRegistry_);
  RegisterMacOpenFileDialogNodeHandler(controller.nodeHandlerRegistry_);
  controller.nodeHandlerRegistry_.registerHandler(&gRefusedMacScrollBar);
  RegisterMacConfirmDialogNodeHandler(controller.nodeHandlerRegistry_);
}
