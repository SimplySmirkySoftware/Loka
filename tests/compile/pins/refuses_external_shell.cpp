#include "app/nodes/controls/Card.hpp"

using namespace loka::app;

void pin(const Card &card)
{
  scene::CloneShell<Card> shell(card);
  (void)shell;
}
