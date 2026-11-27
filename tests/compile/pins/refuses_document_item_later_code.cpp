#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  (doc::Item("x") << doc::CodeBlock("c")) << doc::Paragraph("y");
}
