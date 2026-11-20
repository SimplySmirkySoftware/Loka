#include "RequiredPropsDefinition.hpp"

using namespace clone_shell_pin;

void pin()
{
  Definition source(RequiredProps(3));
  Definition copy(source);
  delete copy.clone();
}
