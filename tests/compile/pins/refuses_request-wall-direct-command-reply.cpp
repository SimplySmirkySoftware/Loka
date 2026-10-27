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
    this->state(request, TestCommand::None());
    this->state(position, LineCursor::None());
  }
private:
  RequestWithReply<TestCommand> request;
  Request<LineCursor> position;
};
