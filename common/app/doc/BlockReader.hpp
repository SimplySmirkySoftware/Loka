#ifndef LOKA_APP_DOC_BLOCK_READER_HPP
#define LOKA_APP_DOC_BLOCK_READER_HPP

#include "app/scene/node/ComponentNode.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      struct NestedItemProps;
      struct ListProps;
      struct ItemProps;
      struct NumberedItemProps;
      struct NumberedListProps;
      struct QuoteProps;
      struct HeadingProps;
      struct ParagraphProps;
      struct ImageProps;
      struct CodeBlockProps;

      /** A synchronous exit borrows the authored props. Ordinary nodes allow descent.
          Leaves prune their projection. Containers visit their own children.
          Refusal is terminal. Discard the reader without calling leave on open containers.
          Cost: one read per leaf, one entry per container and one leave on successful traversal. */
      class BlockReader
      {
      public:
        /** Cost: one call per visited Heading block, borrowing its props only. */
        virtual bool read(const HeadingProps &) = 0;
        /** Cost: one call per visited Paragraph block, borrowing its props only. */
        virtual bool read(const ParagraphProps &) = 0;
        /** Cost: one call per visited CodeBlock block, borrowing its props only. */
        virtual bool read(const CodeBlockProps &) = 0;

        /** Cost: one call per visited image, borrowing its props only. */
        virtual bool read(const ImageProps &) = 0;

        /** Cost: one borrowed input per visited item. */
        virtual bool read(const ItemProps &) = 0;
        /** Each numbered container has one entry and one successful leave, with one read per item. */
        virtual bool enter(const NumberedListProps &) = 0;
        virtual bool leave(const NumberedListProps &) = 0;
        virtual bool read(const NumberedItemProps &) = 0;
        /** Visit the head and its children once per nested item. */
        virtual bool enter(const NestedItemProps &) = 0;
        virtual bool leave(const NestedItemProps &) = 0;
        /** Cost: the walk calls once per List enter, borrowing its props only. */
        virtual bool enter(const ListProps &) = 0;
        /** Cost: the walk calls once per List leave, borrowing its props only. */
        virtual bool leave(const ListProps &) = 0;
        /** Cost: the walk calls once per Quote enter, borrowing its props only. */
        virtual bool enter(const QuoteProps &) = 0;
        /** Cost: the walk calls once per Quote leave, borrowing its props only. */
        virtual bool leave(const QuoteProps &) = 0;

      protected:
        /** Cost: the stack owner destroys its reader once per export, with no rows to release. */
        ~BlockReader() {}
      };

      /** A semantic leaf exposes its authored props and prunes its native projection.
          Cost: forwards the existing props once per node construction. */
      template <class PropsT> class BlockNode : public scene::ComponentNodeWithProps<PropsT>
      {
      protected:
        /** Cost: each materialization forwards props to the existing component constructor once. */
        explicit BlockNode(const PropsT &props)
            : scene::ComponentNodeWithProps<PropsT>(props)
        {
        }

      private:
        /** Cost: one borrowed call per semantic visit, no projected child walk. */
        virtual scene::DocumentBlockRead readDocumentBlock(BlockReader &reader) const
        {
          return reader.read(this->props) ? scene::DOCUMENT_BLOCK_READ : scene::DOCUMENT_BLOCK_REFUSED;
        }
      };
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
