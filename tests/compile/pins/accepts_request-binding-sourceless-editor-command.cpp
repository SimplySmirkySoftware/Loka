#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/scene/node/ComposableNode.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/nodes/controls/TextEditor.hpp"

using namespace loka::app;
using namespace loka::app::scene;

struct BindingPropsAccess : TextEditorProps
{
  static RequestBinding<EditorCommand> bind(RequestQueueBase<EditorCommand> &queue)
  {
    return RequestBinding<EditorCommand>(requestSeat(queue), replySeat(queue), queue);
  }
};

void acceptsSourcelessCaretAndQueuedCommandBinding(const WriteSeat<LineCursor> &seat)
{
  RequestBinding<LineCursor> caret(seat);
  RequestQueue<EditorCommand, 2> queue;
  RequestBinding<EditorCommand> binding;
  RequestBinding<EditorCommand> queued(BindingPropsAccess::bind(queue));
  RequestBinding<EditorCommand> copy(queued);
  binding = copy;
  (void)caret;
}

// p4: instantiate every runner door with an unrelated request and fact.
template class loka::app::scene::SeatRunner<EditorCommand, LineCursor>;
