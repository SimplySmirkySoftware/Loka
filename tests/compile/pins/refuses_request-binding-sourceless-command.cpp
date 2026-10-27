#include "app/nodes/controls/TextEditorRequests.hpp"
#include "request_command.hpp"
#include "app/nodes/controls/TextEditor.hpp"

using namespace loka::app;
using namespace loka::app::scene;

void refusesSourcelessCommandBinding(const WriteSeat<TestCommand> &seat)
{
  RequestBinding<TestCommand> binding(seat);
  (void)binding;
}
