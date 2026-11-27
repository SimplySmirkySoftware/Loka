#include "app/doc/List.hpp"

void pin()
{
  using namespace loka::app;
  doc::List() << doc::Item("a").tag(17);

}
