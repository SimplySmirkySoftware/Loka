#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"

void pin()
{
  using namespace loka::app;
  loka::app::Column() << (doc::Item("x").tag(1) << doc::List());
}
