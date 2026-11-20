#include "RequiredPropsDefinition.hpp"

using namespace clone_shell_pin;

void pin()
{
  Definition source;
  Definition copy(source);
  delete copy.clone();
}
