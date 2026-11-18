#ifndef LOKA_APP_DOC_PARAGRAPH_HPP
#define LOKA_APP_DOC_PARAGRAPH_HPP
#include "app/doc/Lowering.hpp"
#include "app/scene/node/ComponentNode.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class ParagraphNode;

      /** Complete semantic block input; no live content source.
          Cost: construction copies shared values; literal content allocates a segment. */
      struct ParagraphProps : scene::NodePropsBase<ParagraphProps>
      {
        typedef ParagraphNode NodeType;
        struct TypeTag
        {
        };
        AttributedString content;
        scene::NodeState<LinkTarget> activatedLink_;

        ParagraphProps() {}
        ParagraphProps(const AttributedString &content)
            : content(content)
        {
        }
        ParagraphProps(const char *content)
            : content(Styled(content, TextStyle()))
        {
        }
        /** Value order for retained reconciliation. Cost: walks this pair's content only. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const ParagraphProps &other = static_cast<const ParagraphProps &>(rhs);
          const int order = detail::CompareContent(this->content, other.content);
          if (order)
            return order < 0;
          return std::less<core::State<LinkTarget> *>()(this->activatedLink_.state(), other.activatedLink_.state());
        }
      };
      /** Shared leaf lowering for composition and retained application.
          Cost: once per materialization attempt or props apply; copies shared content.
          Refusal remains an invalid child value; no eager body read or retry. */
      inline AttributedTextDefinitionWithAttr Lower(const ParagraphProps &props)
      {
        return AttributedText(props.content).activatedLink(props.activatedLink_) + BlockStyle().wrap(TEXT_WRAP_WORD);
      }
      /** Semantic identity with a fixed projected subtree; props are its only state.
          Cost: constructor only copies props, including node-kind probes. */
      class ParagraphNode : public scene::ComponentNodeWithProps<ParagraphProps>
      {
      public:
        explicit ParagraphNode(const ParagraphProps &props)
            : scene::ComponentNodeWithProps<ParagraphProps>(props)
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
          const AttributedTextDefinitionWithAttr lowered = doc::Lower(props);
          doc::detail::ApplyLeaf(node->childrenHead(), lowered);
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
