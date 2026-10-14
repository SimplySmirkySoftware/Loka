#include "app/scene/Node.hpp"

// A by-value copy of the base cannot report replaceChildrenFrom's refusal
// (#1313), so the base copy constructor is private. The probe derives
// directly so the pin names that constructor and nothing else; its twin
// (accepts_nestable_definition_construction.cpp) shares the probe shape.
struct NestableDefinitionProbe : public loka::app::scene::NestableDefinitionBase
{
  NestableDefinitionProbe() {}
  NestableDefinitionProbe(const NestableDefinitionProbe &other)
      : loka::app::scene::NestableDefinitionBase(other)
  {
  }
  virtual const loka::app::scene::NodeDefinitionBase *asNodeDefinitionBase() const { return 0; }
};

void refusesNestableDefinitionCopy(const NestableDefinitionProbe &source)
{
  NestableDefinitionProbe copy(source);
  (void)copy;
}
