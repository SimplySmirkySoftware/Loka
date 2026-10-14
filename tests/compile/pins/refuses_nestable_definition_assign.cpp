#include "app/scene/Node.hpp"

// Base assignment would discard replaceChildrenFrom's refusal (#1313), so
// it is private; the probe forwards to it the way a derived operator= would.
struct NestableDefinitionProbe : public loka::app::scene::NestableDefinitionBase
{
  NestableDefinitionProbe() {}
  NestableDefinitionProbe &operator=(const NestableDefinitionProbe &other)
  {
    loka::app::scene::NestableDefinitionBase::operator=(other);
    return *this;
  }
  virtual const loka::app::scene::NodeDefinitionBase *asNodeDefinitionBase() const { return 0; }
};

void refusesNestableDefinitionAssignment(NestableDefinitionProbe &destination,
                                         const NestableDefinitionProbe &source)
{
  destination = source;
}
