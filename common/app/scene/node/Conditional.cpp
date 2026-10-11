#include "Conditional.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    namespace scene
    {
      ConditionalProps::ConditionalProps(loka::core::State<bool> *cond,
                                         NodeDefinitionBase *tDef,
                                         NodeDefinitionBase *fDef)
          : condition(cond),
            trueDef(tDef),
            falseDef(fDef)
      {
      }

      ConditionalProps::ConditionalProps(const loka::core::State<bool> *cond,
                                         NodeDefinitionBase *tDef,
                                         NodeDefinitionBase *fDef)
          : condition(const_cast<loka::core::State<bool> *>(cond)),
            trueDef(tDef),
            falseDef(fDef)
      {
      }

      ConditionalDefinition::ConditionalDefinition(const ConditionalProps &p)
          : props(p),
            ownedTrueDef(0),
            ownedFalseDef(0)
      {
        this->props.trueDef = 0;
        this->props.falseDef = 0;
        this->assignFrom(p, p.trueDef, p.falseDef, 0);
      }

      ConditionalDefinition::ConditionalDefinition(const ConditionalDefinition &other)
          : NodeDefinitionBase(),
            IBranchSeatDefinition(other),
            props(other.props),
            ownedTrueDef(0),
            ownedFalseDef(0)
      {
        this->props.trueDef = 0;
        this->props.falseDef = 0;
        this->assignFrom(other.props, other.ownedTrueDef, other.ownedFalseDef, &other);
      }

      ConditionalDefinition &ConditionalDefinition::operator=(const ConditionalDefinition &other)
      {
        if (this != &other)
        {
          this->assignFrom(other.props, other.ownedTrueDef, other.ownedFalseDef, &other);
        }
        return *this;
      }

      bool ConditionalDefinition::assignFrom(const ConditionalProps &nextProps,
                                             const NodeDefinitionBase *trueDef,
                                             const NodeDefinitionBase *falseDef,
                                             const NodeDefinitionBase *nextBase)
      {
        loka::core::OwnedDef<NodeDefinitionBase> nextTrueDef(trueDef ? trueDef->clone() : 0);
        if (trueDef && !nextTrueDef.isSet())
        {
          return false;
        }
        loka::core::OwnedDef<NodeDefinitionBase> nextFalseDef(falseDef ? falseDef->clone() : 0);
        if (falseDef && !nextFalseDef.isSet())
        {
          return false;
        }

        NodeDefinitionBase *oldTrueDef = this->ownedTrueDef;
        NodeDefinitionBase *oldFalseDef = this->ownedFalseDef;
        if (nextBase)
        {
          NodeDefinitionBase::operator=(*nextBase);
        }
        this->props = nextProps;
        this->ownedTrueDef = nextTrueDef.take();
        this->ownedFalseDef = nextFalseDef.take();
        this->props.trueDef = this->ownedTrueDef;
        this->props.falseDef = this->ownedFalseDef;
        delete oldTrueDef;
        delete oldFalseDef;
        return true;
      }

      NodeDefinitionBase *ConditionalDefinition::clone() const
      {
        ConditionalDefinition *copy =
            new ConditionalDefinition(ConditionalProps(this->props.condition, 0, 0));
        if (!copy)
        {
          return 0;
        }
        if (!copy->assignFrom(this->props,
                              this->ownedTrueDef,
                              this->ownedFalseDef,
                              this))
        {
          delete copy;
          return 0;
        }
        return copy;
      }

      ConditionalDefinition::~ConditionalDefinition()
      {
        delete this->ownedTrueDef;
        delete this->ownedFalseDef;
      }

      Node *ConditionalDefinition::create() const
      {
        assert(false && "conditional seats materialize only through Boundary plan application");
        return 0;
      }

      Node *ConditionalDefinition::createInPlace(void *) const
      {
        assert(false && "conditional seats have no runtime node");
        return 0;
      }

      size_t ConditionalDefinition::nodeSize() const
      {
        return 0;
      }

      size_t ConditionalDefinition::nodeAlign() const
      {
        return 1;
      }

    } // namespace scene
  } // namespace app
} // namespace loka
