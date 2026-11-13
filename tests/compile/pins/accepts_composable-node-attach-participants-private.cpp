#include "app/scene/node/ComposableNode.hpp"
class AppNode : public loka::app::scene::ComposableNode
{
public:
  void declarationPin(loka::app::scene::NodeState<int> &value, loka::app::scene::Scene &scene)
  {
    (void)value;
    (void)scene;
    this->state(value, 0);
  }
};
