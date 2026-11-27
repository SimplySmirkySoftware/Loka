#include "app/doc/List.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"

void pin()
{
  using namespace loka::app;
  const doc::ItemProps items[] = {doc::ItemProps("a")};
  loka::core::State<bool> visible(true);
  doc::List() << doc::Paragraph("p") << For(1, items, ComponentItemFactory<doc::ItemProps>())
              << (Show(visible) << doc::Item("a"));
  doc::List() << doc::Item("a");
}
