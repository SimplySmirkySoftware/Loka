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
    this->state(request, EditorCommand::None());
    this->state(position, LineCursor::None());
  }

private:
  Request<EditorCommand> request;
  Request<LineCursor> position;
};
