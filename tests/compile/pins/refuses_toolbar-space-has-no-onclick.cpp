#include "app/nodes/controls/Toolbar.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/style/StyleVocab.hpp"

void toolbarDeclarationPin()
{
  using namespace loka::app;
  loka::core::EmitterState emitter;
  ToolbarItem::flexibleSpace().onClick(&emitter);
}
