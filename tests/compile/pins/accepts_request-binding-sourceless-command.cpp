#include "app/nodes/controls/TextEditorRequests.hpp"
#include "request_command.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/nodes/controls/TextEditor.hpp"

using namespace loka::app;
using namespace loka::app::scene;

struct BindingPropsAccess : TextEditorProps
{
  static RequestBinding<TestCommand> bind(RequestQueueBase<TestCommand> &queue)
  {
    return RequestBinding<TestCommand>(requestSeat(queue), replySeat(queue), queue);
  }
};

void acceptsSourcelessCaretAndQueuedCommandBinding(const WriteSeat<LineCursor> &seat)
{
  RequestBinding<LineCursor> caret(seat);
  RequestQueue<TestCommand, 2> queue;
  RequestBinding<TestCommand> binding;
  RequestBinding<TestCommand> queued(BindingPropsAccess::bind(queue));
  RequestBinding<TestCommand> copy(queued);
  binding = copy;
  (void)caret;
}

// p4: instantiate every runner door with an unrelated request and fact.
template class loka::app::scene::SeatRunner<TestCommand, LineCursor>;
