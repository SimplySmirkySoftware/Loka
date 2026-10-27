#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/scene/node/ComposableNode.hpp"
using namespace loka::app;
using namespace loka::app::scene;
class Probe : public ComposableNode
{
public:
  void check(IStateOwner *owner)
  {
    (void)owner;
    StateBatchBase::CreateImmediateState(owner, request, EditorCommand::None());
    StateBatchBase::CreateImmediateState(owner, position, LineCursor::None());
  }

private:
  RequestQueue<EditorCommand, 2> request;
  Request<LineCursor> position;
};
