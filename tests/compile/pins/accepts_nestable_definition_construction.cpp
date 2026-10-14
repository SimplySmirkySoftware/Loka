#include "app/scene/Node.hpp"

// Twin of refuses_nestable_definition_{copy,assign}: the same include and
// probe shape construct and replace children through the refusal-capable
// door; only the two base copy operations refuse.
struct NestableDefinitionProbe : public loka::app::scene::NestableDefinitionBase
{
  NestableDefinitionProbe() {}
  bool takeChildrenFrom(const NestableDefinitionProbe &other)
  {
    return this->replaceChildrenFrom(other);
  }
  virtual const loka::app::scene::NodeDefinitionBase *asNodeDefinitionBase() const { return 0; }
};

bool acceptsNestableDefinitionConstruction(const NestableDefinitionProbe &source)
{
  NestableDefinitionProbe fresh;
  return fresh.takeChildrenFrom(source);
}
