#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

using namespace loka::app;

void pin(const scene::NodeState<loka::core::Frame> &viewport)
{
  ScrollViewProps(viewport).viewport(viewport);
}
