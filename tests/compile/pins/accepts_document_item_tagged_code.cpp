#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  doc::Item("x").tag(1) << doc::CodeBlock("c").tag(2);
}
