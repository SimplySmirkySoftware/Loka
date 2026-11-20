#ifndef LOKA_TESTS_REQUIRED_PROPS_DEFINITION_HPP
#define LOKA_TESTS_REQUIRED_PROPS_DEFINITION_HPP

#include "app/nodes/nestable/Fragment.hpp"

namespace clone_shell_pin
{
  using namespace loka::app;
  struct RequiredProps : FragmentProps
  {
    explicit RequiredProps(int) {}
  };

  struct Definition : scene::NestableNodeDefinition<RequiredProps, FragmentNode, Definition>
  {
    typedef scene::NestableNodeDefinition<RequiredProps, FragmentNode, Definition> BaseType;
    explicit Definition(const RequiredProps &props)
        : BaseType(props)
    {
    }
#ifndef LOKA_PIN_OMIT_SHELL_FORWARDER
    explicit Definition(const scene::CloneShell<Definition> &shell)
        : BaseType(shell)
    {
    }
#endif
  };
} // namespace clone_shell_pin

#endif
