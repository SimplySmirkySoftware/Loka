#include "app/doc/List.hpp"
#include "app/doc/Rule.hpp"

void pin()
{
  using namespace loka::app;
  doc::NumberedList() << doc::Item("x");
}
