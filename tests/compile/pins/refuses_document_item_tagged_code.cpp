#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  doc::Item("x").tag(1) << doc::Paragraph("y").tag(2);
}
