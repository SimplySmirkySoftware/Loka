#include "app/nodes/controls/Card.hpp"

using namespace loka::app;

void pin(const Card &card)
{
  delete card.clone();
}
