#include "app/text/TextSplice.hpp"
#include "app/scene/node/ComposableNode.hpp"
using namespace loka::app;
using namespace loka::app::scene;
class Probe : public ComposableNode
{
public:
  void check(IStateOwner *owner)
  {
    (void)owner;
    this->state(request, TextSplice::None());
  }

private:
  Request<TextSplice> request;
};
