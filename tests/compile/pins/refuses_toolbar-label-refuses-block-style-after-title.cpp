#include "app/nodes/controls/Toolbar.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/style/StyleVocab.hpp"

void toolbarDeclarationPin()
{
  using namespace loka::app;

  ToolbarItem::label("x") + Title + BlockStyle();
}
