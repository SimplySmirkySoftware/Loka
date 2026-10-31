#include "app/nodes/controls/Toolbar.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/style/StyleVocab.hpp"

void toolbarDeclarationPin()
{
  using namespace loka::app;
  loka::core::MutableState<loka::core::String> title(loka::core::String::Literal("x"));
  ToolbarItem item(&title);
}
