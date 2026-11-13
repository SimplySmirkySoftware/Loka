#include "app/scene/node/ComposableNode.hpp"
class AppNode : public loka::app::scene::ComposableNode
{
public:
  void declarationPin(loka::app::scene::NodeState<int> &value)
  {
    (void)value;
    this->state(value, 0);
  }
};
