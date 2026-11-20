#ifndef LOKA_APP_DOC_LIST_HPP
#define LOKA_APP_DOC_LIST_HPP
#include "app/doc/InlineBlock.hpp"
#include "app/doc/Container.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/Text.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      /** Fixed List input. Cost: no child storage or traversal. */
      struct ListProps : scene::NodePropsBase<ListProps>
      {
        struct TypeTag
        {
        };
        typedef ContainerNode<ListProps> NodeType;
        /** Cost: materialization constructs a single stack value. */
        static StackProps layout()
        {
          return StackProps(STACK_AXIS_COLUMN).gap(ListGap);
        }
        /** Cost: reconciliation compares two fixed inputs in constant time. */
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
      };
      /** Unordered list. Cost: the nestable definition owns one capture per authored child. */
      struct List : scene::NestableNodeDefinition<ListProps, ListProps::NodeType, List>
      {
      };
      class ItemNode;

      /** Authored Item content and borrowed link destination. Cost: one value per declaration. */
      struct ItemProps : InlineBlockProps<ItemProps>
      {
        typedef ItemNode NodeType;
        /** Cost: default construction initializes one empty input. */
        ItemProps() {}
        /** Cost: authoring copies one shared content value. */
        ItemProps(const AttributedString &value)
            : doc::InlineBlockProps<ItemProps>(value)
        {
        }
        /** Cost: authoring constructs one string and one segment. */
        ItemProps(const char *value)
            : doc::InlineBlockProps<ItemProps>(value)
        {
        }
      };
      /** Semantic identity with a fixed projected subtree; props are its only state.
          Cost: constructor only copies props, including node-kind probes. */
      class ItemNode : public BlockNode<ItemProps>
      {
      public:
        /** Cost: materialization copies one semantic input. */
        explicit ItemNode(const ItemProps &props)
            : doc::BlockNode<ItemProps>(props)
        {
        }

      protected:
        /** Materializes existing projection nodes. Cost: one lowering per attach attempt. */
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          Box marker;
          marker.width(MarkerWidth) << Text(ListBullet());
          if (marker.childrenCount() != 1)
            return;
          Row row;
          row.gap(ItemGap) << marker << Lower(this->props);
          if (row.childrenCount() == 2)
            composition.declare(row);
        }
      };
    } // namespace doc
    namespace scene
    {
      /** Delivers content and result box together, then accepts semantic props.
          Cost: once per retained apply; one fixed owned child path, no collection walk. */
      template <> struct NodePropsApplier<doc::ItemNode, doc::ItemProps>
      {
        /** Cost: the retained definition follows the item’s single row and applies one body. */
        static bool apply(doc::ItemNode *node, const doc::ItemProps &props)
        {
          if (Node *root = node->childrenHead())
          {
            StackNode *row = root->asStackNode();
            Node *marker = row ? row->childrenHead() : 0;
            const bool expected = !root->nextInComposition && row && row->props.axis_ == STACK_AXIS_ROW && marker
                                  && marker->asBoxNode() && marker->nextInComposition;
            assert(expected && "document item row");
            if (expected)
              if (AttributedTextNode *leaf = doc::detail::ProjectionLeaf(marker->nextInComposition))
                doc::Lower(props).applyPropsToNode(leaf);
          }
          node->props = props;
          return true;
        }
      };
    } // namespace scene
    namespace doc
    {
      /** Named authoring definition using the canonical props node and applier.
          Cost: copies one props value per construction. */
      struct ItemDefinition : scene::NodeDefinition<ItemProps, ItemNode>
      {
        /** Cost: each declaration copies one props value. */
        explicit ItemDefinition(const ItemProps &props)
            : scene::NodeDefinition<ItemProps, ItemNode>(props)
        {
        }
        /** Borrows the owner's result box. Cost: O(1) per author call. */
        ItemDefinition &activatedLink(const scene::NodeState<LinkTarget> &box)
        {
          this->props.activatedLink_ = box;
          return *this;
        }
      };
      /** Authors one semantic block. Cost: one props value per compose call;
          literals allocate one String; inline literals also allocate one segment. */
      inline ItemDefinition Item(const AttributedString &content)
      {
        return ItemDefinition(ItemProps(content));
      }
      /** Literal block. Cost: one String and segment per author call. */
      inline ItemDefinition Item(const char *content)
      {
        return ItemDefinition(ItemProps(content));
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
