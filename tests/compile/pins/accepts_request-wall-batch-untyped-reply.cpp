#include "app/nodes/controls/TextEditorRequests.hpp"
#include "request_command.hpp"
using namespace loka::app;
using namespace loka::app::scene;
class Probe : public ComposableNode
{
public:
  void check(IStateOwner *owner)
  {
    (void)owner;
    this->declareStates(2).state(request, UntypedCommand::None());
    this->declareStates(1).state(position, LineCursor::None());
  }
private:
  RequestQueue<UntypedCommand, 2> request;
  Request<LineCursor> position;
};
