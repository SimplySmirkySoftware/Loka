#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  doc::NumberedList() << (doc::Item("x").tag(1) << doc::List());
}
