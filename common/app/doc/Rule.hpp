#ifndef LOKA_APP_DOC_RULE_HPP
#define LOKA_APP_DOC_RULE_HPP

#include "app/doc/BlockReader.hpp"
#include "app/nodes/Text.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class RuleNode;

      /** A thematic break has no authored payload. */
      struct RuleProps : scene::NodePropsBase<RuleProps>
      {
        struct TypeTag
        {
        };
        typedef RuleNode NodeType;
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
      };

      /** Projects one centered text marker per mount. */
      class RuleNode : public BlockNode<RuleProps>
      {
      public:
        explicit RuleNode(const RuleProps &props)
            : BlockNode<RuleProps>(props)
        {
        }

      protected:
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          composition.declare(Text("* * *") + block::Centered);
        }
      };

      typedef scene::NodeDefinition<RuleProps, RuleNode> RuleDefinition;

      /** Authors one thematic break at the document root or in a quote. */
      inline RuleDefinition Rule()
      {
        return scene::Component(RuleProps());
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
