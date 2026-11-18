#ifndef LOKA_APP_DOC_CODEBLOCK_HPP
#define LOKA_APP_DOC_CODEBLOCK_HPP
#include "app/doc/Lowering.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "app/nodes/controls/Card.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class CodeBlockNode;

      /** Complete semantic block input; no live content source.
          Cost: construction copies one String handle or constructs a literal String. */
      struct CodeBlockProps : scene::NodePropsBase<CodeBlockProps>
      {
        typedef CodeBlockNode NodeType;
        struct TypeTag
        {
        };
        core::String text;

        explicit CodeBlockProps(const core::String &text)
            : text(text)
        {
        }
        CodeBlockProps() {}
        explicit CodeBlockProps(const char *text)
            : text(core::String(text))
        {
        }
        /** Value order for retained reconciliation. Cost: walks this pair's content only. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const CodeBlockProps &other = static_cast<const CodeBlockProps &>(rhs);
          return style_internal::CompareLinkTargets(LinkTarget(this->text), LinkTarget(other.text)) < 0;
        }
      };
      /** Shared leaf lowering for composition and retained application.
          Cost: once per materialization attempt or props apply; allocates one segment.
          Refusal remains an invalid child value; no eager body read or retry. */
      inline AttributedTextDefinitionWithAttr Lower(const CodeBlockProps &props)
      {
        return AttributedText(Styled(props.text, Monospace))
               + BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_CLIP);
      }
      /** Semantic identity with a fixed projected subtree; props are its only state.
          Cost: constructor only copies props, including node-kind probes. */
      class CodeBlockNode : public scene::ComponentNodeWithProps<CodeBlockProps>
      {
      public:
        explicit CodeBlockNode(const CodeBlockProps &props)
            : scene::ComponentNodeWithProps<CodeBlockProps>(props)
        {
        }

      protected:
        /** Materializes existing projection nodes. Cost: one lowering per attach attempt. */
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          composition.declare(Card() << Lower(this->props));
        }
      };
    } // namespace doc
    namespace scene
    {
      /** Delivers content and result box together, then accepts semantic props.
          Cost: once per retained apply; one fixed owned child path, no collection walk. */
      template <> struct NodePropsApplier<doc::CodeBlockNode, doc::CodeBlockProps>
      {
        static bool apply(doc::CodeBlockNode *node, const doc::CodeBlockProps &props)
        {
          const AttributedTextDefinitionWithAttr lowered = doc::Lower(props);
          scene::Node *child = node->childrenHead();
          if (child)
          {
            const bool expected = child->kind() == scene::NODE_KIND_CARD && !child->nextInComposition;
            assert(expected && "document card shape");
            if (expected)
            {
              Node *content = static_cast<CardNode *>(child)->contentHead();
              assert(content && "CodeBlock requires Card content");
              doc::detail::ApplyLeaf(content, lowered);
            }
          }
          node->props = props;
          return true;
        }
      };
    } // namespace scene
    namespace doc
    {
      /** Fixed-subtree definition. Cost: copies one String handle per declaration. */
      typedef scene::NodeDefinition<CodeBlockProps, CodeBlockNode> CodeBlockDefinition;
      /** Authors one semantic block. Cost: one props value per compose call;
          a literal constructs one String. */
      inline CodeBlockDefinition CodeBlock(const core::String &text)
      {
        return scene::Component(CodeBlockProps(text));
      }
      /** Literal block. Cost: one String per author call. */
      inline CodeBlockDefinition CodeBlock(const char *text)
      {
        return scene::Component(CodeBlockProps(text));
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
