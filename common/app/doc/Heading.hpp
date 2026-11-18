#ifndef LOKA_APP_DOC_HEADING_HPP
#define LOKA_APP_DOC_HEADING_HPP
#include "app/doc/Lowering.hpp"
#include "app/scene/node/ComponentNode.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class HeadingNode;
      /** Normalizes the authored level once. Cost: O(1) per props construction. */
      inline int NormalizeLevel(int level)
      {
        assert(level >= 1 && level <= 6 && "heading level must be 1..6");
        return level < 1 ? 1 : (level > 6 ? 6 : level);
      }

      /** Complete semantic block input; no live content source.
          Cost: construction copies shared values; literal content allocates a segment. */
      struct HeadingProps : scene::NodePropsBase<HeadingProps>
      {
        typedef HeadingNode NodeType;
        struct TypeTag
        {
        };

      private:
        int level_;

      public:
        /** Author's normalized level. Cost: one field read per comparison or lowering. */
        int level() const
        {
          return this->level_;
        }
        AttributedString content;
        scene::NodeState<LinkTarget> activatedLink_;

        HeadingProps()
            : level_(1)
        {
        }
        HeadingProps(int level, const AttributedString &content)
            : level_(NormalizeLevel(level)),
              content(content)
        {
        }
        HeadingProps(int level, const char *content)
            : level_(NormalizeLevel(level)),
              content(Styled(content, TextStyle()))
        {
        }
        /** Value order for retained reconciliation. Cost: walks this pair's content only. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const HeadingProps &other = static_cast<const HeadingProps &>(rhs);
          if (this->level_ != other.level_)
            return this->level_ < other.level_;
          const int order = this->content.compareSegmentValues(other.content);
          if (order)
            return order < 0;
          return std::less<core::State<LinkTarget> *>()(this->activatedLink_.state(), other.activatedLink_.state());
        }
      };
      /** Default heading look for a normalized level: 24 / 18 / 14 / 12 / 12 / 12, bold.
          Built from explicit axes on each call, so it does not depend on the
          initialization order of the style constants. Cost: O(1), no allocation. */
      inline TextStyle DefaultStyle(int level)
      {
        const TextStyle bold = TextStyle().weight(TEXT_WEIGHT_BOLD);
        if (level == 1)
          return FontSize<24>() + bold;
        if (level == 2)
          return FontSize<18>() + bold;
        if (level == 3)
          return FontSize<14>() + bold;
        return FontSize<12>() + bold;
      }
      /** Shared leaf lowering for composition and retained application.
          Cost: once per materialization attempt or apply with a destination; folds this value's segments.
          Refusal remains an invalid child value; no eager body read or retry. */
      inline AttributedTextDefinitionWithAttr Lower(const HeadingProps &props)
      {
        return AttributedText(DefaultStyle(props.level()) + props.content).activatedLink(props.activatedLink_)
               + BlockStyle().wrap(TEXT_WRAP_WORD);
      }
      /** Semantic identity with a fixed projected subtree; props are its only state.
          Cost: constructor only copies props, including node-kind probes. */
      class HeadingNode : public scene::ComponentNodeWithProps<HeadingProps>
      {
      public:
        explicit HeadingNode(const HeadingProps &props)
            : scene::ComponentNodeWithProps<HeadingProps>(props)
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
      template <> struct NodePropsApplier<doc::HeadingNode, doc::HeadingProps>
      {
        static bool apply(doc::HeadingNode *node, const doc::HeadingProps &props)
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
      struct HeadingDefinition : scene::NodeDefinition<HeadingProps, HeadingNode>
      {
        explicit HeadingDefinition(const HeadingProps &props)
            : scene::NodeDefinition<HeadingProps, HeadingNode>(props)
        {
        }
        /** Borrows the owner's result box. Cost: O(1) per author call. */
        HeadingDefinition &activatedLink(const scene::NodeState<LinkTarget> &box)
        {
          this->props.activatedLink_ = box;
          return *this;
        }
      };
      /** Authors one semantic block. Cost: one props value per compose call;
          literals allocate one String; inline literals also allocate one segment. */
      inline HeadingDefinition Heading(int level, const AttributedString &content)
      {
        return HeadingDefinition(HeadingProps(level, content));
      }
      /** Literal block. Cost: one String and segment per author call. */
      inline HeadingDefinition Heading(int level, const char *content)
      {
        return HeadingDefinition(HeadingProps(level, content));
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
