#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  doc::NumberedList() << doc::Item("x");
  doc::NumberedList() << doc::Item("y").tag(1);
}
