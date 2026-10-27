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
    StateBatchBase::CreateImmediateState(owner, request, UntypedCommand::None());
    StateBatchBase::CreateImmediateState(owner, position, LineCursor::None());
  }
private:
  Request<UntypedCommand> request;
  Request<LineCursor> position;
};
