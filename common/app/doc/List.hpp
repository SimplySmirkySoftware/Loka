#ifndef LOKA_APP_DOC_LIST_HPP
#define LOKA_APP_DOC_LIST_HPP
#include "app/doc/InlineBlock.hpp"
#include "app/doc/Container.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/Text.hpp"
#include <cstdio>
#include <new>

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
      class NestedItem;
      struct NumberedList;

      /** Unordered list. Cost: the nestable definition owns one capture per authored child. */
      struct List : scene::NestableNodeDefinition<ListProps, ListProps::NodeType, List>
      {
        using scene::NestableNodeDefinition<ListProps, ListProps::NodeType, List>::operator<<;
        List &operator<<(const NestedItem &item);

        /** Cost: authoring starts with one childless definition. */
        List() {}
        /** Cost: each clone starts with one childless definition. */
        explicit List(const scene::CloneShell<List> &shell)
            : scene::NestableNodeDefinition<ListProps, ListProps::NodeType, List>(shell)
        {
        }

      private:
        List &operator<<(const List &);
        List &operator<<(const NumberedList &);
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
      namespace detail
      {
        /** Format an admitted ordinal for its screen or Markdown marker. */
        inline std::string OrdinalMarker(unsigned ordinal, char delimiter)
        {
          char buffer[4];
          ::snprintf(buffer, sizeof(buffer), "%u%c", ordinal, delimiter);
          return buffer;
        }
      } // namespace detail

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
          return detail::OrdinalMarker(this->ordinal_, delimiter);
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

      /** The item and its indented lists share one vertical stack. */
      struct NestedItemProps : scene::NodePropsBase<NestedItemProps>
      {
        struct TypeTag
        {
        };
        typedef ContainerNode<NestedItemProps> NodeType;
        static StackProps layout()
        {
          return ListProps::layout();
        }
        bool operator<(const scene::PropsBase &) const
        {
          return false;
        }
      };

      namespace detail
      {
        struct NestedItemDefinition
            : scene::NestableNodeDefinition<NestedItemProps, NestedItemProps::NodeType, NestedItemDefinition>
        {
          NestedItemDefinition() {}
          explicit NestedItemDefinition(const scene::CloneShell<NestedItemDefinition> &shell)
              : scene::NestableNodeDefinition<NestedItemProps, NestedItemProps::NodeType, NestedItemDefinition>(shell)
          {
          }
        };
      } // namespace detail

      /** An item and its captured child lists. Copying clones the captured subtrees. */
      class NestedItem
      {
        typedef scene::NodeDefinition<ItemProps, ItemNode> Head;
        Head head_;
        Column lists_;

        friend struct List;
        friend struct NumberedList;
        friend NestedItem operator<<(const Head &, const List &);
        friend NestedItem operator<<(const Head &, const NumberedList &);
        NestedItem &operator=(const NestedItem &);

        NestedItem(const Head &head, const scene::NodeDefinitionBase &first)
            : head_(head)
        {
          this->lists_.gap(ListGap);
          this->capture(first);
        }

        void capture(const scene::NodeDefinitionBase &list)
        {
          const size_t before = this->lists_.childrenCount();
          this->lists_ << list;
          if (this->lists_.childrenCount() != before + 1)
            this->lists_ = Column().gap(ListGap);
        }

        NestedItem &appendList(const scene::NodeDefinitionBase &list)
        {
          if (this->lists_.childrenCount())
            this->capture(list);
          return *this;
        }

        bool captured() const
        {
          if (this->lists_.childrenCount())
            return true;
          scene::NoteDefinitionCaptureRefusal();
          return false;
        }

        /** Complete both owning definitions before transferring the shell to its parent. */
        void appendTo(scene::NestableDefinitionBase &parent, const scene::NodeDefinitionBase &head, int indent) const
        {
          core::OwnedDef<detail::NestedItemDefinition> shell(new (std::nothrow) detail::NestedItemDefinition());
          core::OwnedDef<Row> row(new (std::nothrow) Row());
          if (shell.isSet() && row.isSet())
          {
            *shell << head;
            if (shell->childrenCount() == 1)
            {
              row->gap(0) << Box().width(indent) << this->lists_;
              if (row->childrenCount() == 2)
              {
                shell->addOwnedChild(row.take());
                parent.addOwnedChild(shell.take());
                return;
              }
            }
          }
          scene::NoteDefinitionCaptureRefusal();
        }

      public:
        NestedItem(const NestedItem &other)
            : head_(other.head_),
              lists_(other.lists_)
        {
        }
        NestedItem &operator<<(const List &list)
        {
          return this->appendList(list);
        }
        NestedItem &operator<<(const NumberedList &list);
      };

      /** Capture the first child list and preserve the authored head's metadata. */
      inline NestedItem operator<<(const scene::NodeDefinition<ItemProps, ItemNode> &head, const List &list)
      {
        return NestedItem(head, list);
      }

      inline List &List::operator<<(const NestedItem &item)
      {
        if (item.captured())
          item.appendTo(*this, item.head_, MarkerWidth + ItemGap);
        return *this;
      }

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
          if (this->full())
            return *this;
          Base::operator<<(this->numberedHead(item));
          return *this;
        }

        NumberedList &operator<<(const NestedItem &item)
        {
          if (!this->full() && item.captured())
            item.appendTo(*this, this->numberedHead(item.head_), NumberedMarkerWidth + ItemGap);
          return *this;
        }

      private:
        bool full() const
        {
          return this->childrenCount() >= NumberedItemProps::MaximumOrdinal;
        }

        scene::NodeDefinition<NumberedItemProps, NumberedItemNode>
        numberedHead(const scene::NodeDefinition<ItemProps, ItemNode> &item) const
        {
          scene::NodeDefinition<NumberedItemProps, NumberedItemNode> numbered(
              NumberedItemProps(item.props, static_cast<unsigned>(this->childrenCount()) + 1));
          numbered.copyTestIdPolicyFrom(item);
          return numbered;
        }
      };
      inline NestedItem operator<<(const scene::NodeDefinition<ItemProps, ItemNode> &head, const NumberedList &list)
      {
        return NestedItem(head, list);
      }

      inline NestedItem &NestedItem::operator<<(const NumberedList &list)
      {
        return this->appendList(list);
      }

      typedef NumberedList OrderedList;
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
