#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/scene/node/ComposableNode.hpp"
#include "app/nodes/controls/TextEditor.hpp"

using namespace loka::app;
using namespace loka::app::scene;

void refusesSourcelessCommandBinding(const WriteSeat<EditorCommand> &seat)
{
  RequestBinding<EditorCommand> binding(seat);
  (void)binding;
}
