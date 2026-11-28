#include "app/doc/List.hpp"
#include "app/doc/Rule.hpp"

void pin()
{
  using namespace loka::app;
  doc::Item("x") << doc::CodeBlock("c");
}
