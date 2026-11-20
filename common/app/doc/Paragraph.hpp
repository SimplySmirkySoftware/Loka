#ifndef LOKA_APP_DOC_PARAGRAPH_HPP
#define LOKA_APP_DOC_PARAGRAPH_HPP
#include "app/doc/InlineBlock.hpp"
#include "app/doc/BlockReader.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class ParagraphNode;

      /** Authored Paragraph content and borrowed link destination. Cost: one value per declaration. */
      struct ParagraphProps : InlineBlockProps<ParagraphProps>
      {
        typedef ParagraphNode NodeType;
        /** Cost: default construction initializes one empty input. */
        ParagraphProps() {}
        /** Cost: authoring copies one shared content value. */
        ParagraphProps(const AttributedString &value)
            : doc::InlineBlockProps<ParagraphProps>(value)
        {
        }
        /** Cost: authoring constructs one string and one segment. */
        ParagraphProps(const char *value)
            : doc::InlineBlockProps<ParagraphProps>(value)
        {
        }
      };
      /** Semantic identity with a fixed projected subtree; props are its only state.
          Cost: constructor only copies props, including node-kind probes. */
      class ParagraphNode : public BlockNode<ParagraphProps>
      {
      public:
        explicit ParagraphNode(const ParagraphProps &props)
            : doc::BlockNode<ParagraphProps>(props)
        {
        }

      protected:
        /** Materializes existing projection nodes. Cost: one lowering per attach attempt. */
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          composition.declare(Lower(this->props));
        }
      };
    } // namespace doc
    namespace scene
    {
      /** Delivers content and result box together, then accepts semantic props.
          Cost: once per retained apply; one fixed owned child path, no collection walk. */
      template <> struct NodePropsApplier<doc::ParagraphNode, doc::ParagraphProps>
      {
        static bool apply(doc::ParagraphNode *node, const doc::ParagraphProps &props)
        {
          if (AttributedTextNode *leaf = doc::detail::ProjectionLeaf(node->childrenHead()))
            doc::Lower(props).applyPropsToNode(leaf);
          node->props = props;
          return true;
        }
      };
    } // namespace scene
    namespace doc
    {
      /** Named authoring definition using the canonical props node and applier.
          Cost: copies one props value per construction. */
      struct ParagraphDefinition : scene::NodeDefinition<ParagraphProps, ParagraphNode>
      {
        explicit ParagraphDefinition(const ParagraphProps &props)
            : scene::NodeDefinition<ParagraphProps, ParagraphNode>(props)
        {
        }
        /** Borrows the owner's result box. Cost: O(1) per author call. */
        ParagraphDefinition &activatedLink(const scene::NodeState<LinkTarget> &box)
        {
          this->props.activatedLink_ = box;
          return *this;
        }
      };
      /** Authors one semantic block. Cost: one props value per compose call;
          literals allocate one String; inline literals also allocate one segment. */
      inline ParagraphDefinition Paragraph(const AttributedString &content)
      {
        return ParagraphDefinition(ParagraphProps(content));
      }
      /** Literal block. Cost: one String and segment per author call. */
      inline ParagraphDefinition Paragraph(const char *content)
      {
        return ParagraphDefinition(ParagraphProps(content));
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
