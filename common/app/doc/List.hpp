#ifndef LOKA_APP_DOC_LIST_HPP
#define LOKA_APP_DOC_LIST_HPP
#include "app/doc/InlineBlock.hpp"
#include "app/doc/Container.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/Text.hpp"
#include <cstdio>

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
        /** Cost: authoring starts with one childless definition. */
        List() {}
        /** Cost: each clone starts with one childless definition. */
        explicit List(const scene::CloneShell<List> &shell)
            : scene::NestableNodeDefinition<ListProps, ListProps::NodeType, List>(shell)
        {
        }
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
      class NumberedItemNode;
      /** A captured numbered item owns its declaration ordinal and borrows its link destination. */
      struct NumberedItemProps : InlineBlockProps<NumberedItemProps>
      {
        typedef NumberedItemNode NodeType;
        enum
        {
          MaximumOrdinal = 99
        };

        unsigned ordinal() const
        {
          return this->ordinal_;
        }

        /** Format the committed ordinal for a screen or Markdown marker in bounded space. */
        std::string marker(char delimiter) const
        {
          char buffer[4];
          ::snprintf(buffer, sizeof(buffer), "%u%c", this->ordinal_, delimiter);
          return buffer;
        }

        /** Cost: reconciliation calls this once per offered item. Different ordinals compare in O(1);
            equal ordinals, the retained case, fall through to comparing this item's own inline segments. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const NumberedItemProps &other = static_cast<const NumberedItemProps &>(rhs);
          if (this->ordinal_ != other.ordinal_)
            return this->ordinal_ < other.ordinal_;
          return InlineBlockProps<NumberedItemProps>::operator<(rhs);
        }

      private:
        friend struct NumberedList;
        const unsigned ordinal_;
        NumberedItemProps(const ItemProps &item, unsigned ordinal)
            : InlineBlockProps<NumberedItemProps>(item.content),
              ordinal_(ordinal)
        {
          this->activatedLink_ = item.activatedLink_;
        }
      };

      namespace detail
      {
        /** Both kinds of item materialize the same fixed marker and body row once per attach. */
        template <class Props>
        void DeclareItemRow(scene::NodeComposition &composition, const Box &marker, const Props &props)
        {
          if (marker.childrenCount() != 1)
            return;
          Row row;
          row.gap(ItemGap) << marker << Lower(props);
          if (row.childrenCount() == 2)
            composition.declare(row);
        }

        /** Retained items update only their one projected body, following a fixed child path. */
        template <class Props> bool ApplyItemBody(BlockNode<Props> *node, const Props &props)
        {
          using namespace scene;
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
          InlineBlockProps<Props> &body = node->props;
          body = props;
          return true;
        }
      } // namespace detail

      class ItemNode : public BlockNode<ItemProps>
      {
      public:
        explicit ItemNode(const ItemProps &p)
            : BlockNode<ItemProps>(p)
        {
        }

      protected:
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          detail::DeclareItemRow(composition, Box().width(MarkerWidth) << Text(ListBullet()), this->props);
        }
      };

      /** Projects the declaration ordinal into a fixed-width, right-aligned marker. */
      class NumberedItemNode : public BlockNode<NumberedItemProps>
      {
      public:
        explicit NumberedItemNode(const NumberedItemProps &p)
            : BlockNode<NumberedItemProps>(p)
        {
        }

      protected:
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          detail::DeclareItemRow(composition,
                                 Box().width(NumberedMarkerWidth)
                                     << (Text(this->props.marker('.').c_str()) + block::RightAligned),
                                 this->props);
        }
      };
    } // namespace doc
    namespace scene
    {
      template <> struct NodePropsApplier<doc::ItemNode, doc::ItemProps>
      {
        static bool apply(doc::ItemNode *node, const doc::ItemProps &props)
        {
          return doc::detail::ApplyItemBody(node, props);
        }
      };
      template <> struct NodePropsCompatibility<doc::NumberedItemNode, doc::NumberedItemProps>
      {
        static bool accepts(const doc::NumberedItemNode *node, const doc::NumberedItemProps &props)
        {
          return node->props.ordinal() == props.ordinal();
        }
      };
      template <> struct NodePropsApplier<doc::NumberedItemNode, doc::NumberedItemProps>
      {
        static bool apply(doc::NumberedItemNode *node, const doc::NumberedItemProps &props)
        {
          return doc::detail::ApplyItemBody(node, props);
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

      /** Ordered list layout matches an unordered list. Each exit receives its distinct semantic type. */
      struct NumberedListProps : scene::NodePropsBase<NumberedListProps>
      {
        struct TypeTag
        {
        };
        typedef ContainerNode<NumberedListProps> NodeType;
        static StackProps layout()
        {
          return ListProps::layout();
        }
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
      };

      /** A list of up to 99 inline items, numbered in declaration order. */
      struct NumberedList : scene::NestableNodeDefinition<NumberedListProps, NumberedListProps::NodeType, NumberedList>
      {
        typedef scene::NestableNodeDefinition<NumberedListProps, NumberedListProps::NodeType, NumberedList> Base;
        NumberedList() {}
        explicit NumberedList(const scene::CloneShell<NumberedList> &shell)
            : Base(shell)
        {
        }

        /** Capture one item using the next committed ordinal. No sibling traversal.
            Takes the item's definition base so a chained `doc::Item("x").tag(1)` is accepted too. */
        NumberedList &operator<<(const scene::NodeDefinition<ItemProps, ItemNode> &item)
        {
          // The limit is an input rule, not an allocation failure: the list keeps its 99 items and mounts.
          if (this->childrenCount() >= NumberedItemProps::MaximumOrdinal)
            return *this;
          scene::NodeDefinition<NumberedItemProps, NumberedItemNode> numbered(
              NumberedItemProps(item.props, static_cast<unsigned>(this->childrenCount()) + 1));
          numbered.copyTestIdPolicyFrom(item);
          Base::operator<<(numbered);
          return *this;
        }
      };
      typedef NumberedList OrderedList;
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
