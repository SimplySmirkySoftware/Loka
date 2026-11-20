#ifndef LOKA_APP_DOC_QUOTE_HPP
#define LOKA_APP_DOC_QUOTE_HPP
#include "app/doc/Container.hpp"
#include "app/nodes/nestable/Box.hpp"
namespace loka
{
  namespace app
  {
    namespace doc
    {
      /** Fixed Quote input. Cost: no child storage or traversal. */
      struct QuoteProps : scene::NodePropsBase<QuoteProps>
      {
        struct TypeTag
        {
        };
        typedef ContainerNode<QuoteProps> NodeType;
        /** Cost: materialization constructs a single stack value. */
        static StackProps layout()
        {
          return StackProps(STACK_AXIS_ROW).gap(0);
        }
        /** Cost: reconciliation compares two fixed inputs in constant time. */
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
      };
      /** Indented content column. Cost: captures one spacer and one column per declaration. */
      struct Quote : scene::NestableNodeDefinition<QuoteProps, QuoteProps::NodeType, Quote>
      {
        /** Cost: authoring captures one spacer and one column. */
        Quote()
        {
          Box spacer;
          spacer.width(QuoteIndent);
          Column content;
          scene::NestableDefinitionBase::addChild(&spacer);
          if (this->childrenCount() == 1)
            scene::NestableDefinitionBase::addChild(&content);
          if (this->childrenCount() != 2)
            this->clearChildren();
        }
        /** A refused shell replaces nothing, so the destination keeps its value.
            Cost: one shell check, then one definition assignment per author call. */
        Quote &operator=(const Quote &rhs)
        {
          if (rhs.childrenCount() == 2)
            scene::NestableNodeDefinition<QuoteProps, QuoteProps::NodeType, Quote>::operator=(rhs);
          return *this;
        }
        /** Cost: every borrowed insertion clones its authored subtree into this column once. */
        virtual void addChild(scene::NodeDefinitionBase *child)
        {
          if (scene::INestableDefinition *column = this->contentColumn())
            column->addChild(child);
        }
        /** Cost: owned insertions transfer one definition or release it on shell refusal. */
        virtual void addOwnedChild(scene::NodeDefinitionBase *child)
        {
          if (scene::INestableDefinition *column = this->contentColumn())
            column->addOwnedChild(child);
          else
            delete child;
        }
        /** Cost: each capture clones only this shell's completed definition tree. */
        virtual scene::NodeDefinitionBase *clone() const
        {
          if (this->childrenCount() != 2)
            return 0;
          Quote *copy = new Quote(EMPTY_SHELL);
          if (copy && !copy->assignFromDefinition(*this))
          {
            delete copy;
            return 0;
          }
          return copy;
        }

      private:
        enum EmptyShell
        {
          EMPTY_SHELL
        };
        /** Cost: each clone starts with one empty definition and captures no children here. */
        explicit Quote(EmptyShell) {}
        /** Cost: each insertion follows the same two fixed definition edges. */
        scene::INestableDefinition *contentColumn()
        {
          scene::NodeDefinitionBase *spacer = this->childrenHead();
          return spacer ? spacer->nextInComposition->asNestableDefinition() : 0;
        }
      };
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
