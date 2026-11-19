#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"

using namespace loka::app;

void pin(const scene::NodeState<loka::core::Frame> &viewport)
{
  ScrollView definition = ScrollView(viewport) << Box();
  ScrollView copy(definition);
  delete copy.clone();
  ScrollView explicitProps((ScrollViewProps(viewport)));
  delete explicitProps.clone();
}
