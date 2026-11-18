#ifndef LOKA_APP_DOC_BLOCK_READER_HPP
#define LOKA_APP_DOC_BLOCK_READER_HPP

#include "app/scene/node/ComponentNode.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      struct HeadingProps;
      struct ParagraphProps;
      struct CodeBlockProps;

      /** A synchronous exit borrows the authored props. False refuses the whole export.
          Ordinary nodes allow descent. A read block prunes its projection. A refused block stops the walk.
          Cost: one typed read per visited block, walking only that block's input. */
      class BlockReader
      {
      public:
        /** Cost: one call per visited Heading block, borrowing its props only. */
        virtual bool read(const HeadingProps &) = 0;
        /** Cost: one call per visited Paragraph block, borrowing its props only. */
        virtual bool read(const ParagraphProps &) = 0;
        /** Cost: one call per visited CodeBlock block, borrowing its props only. */
        virtual bool read(const CodeBlockProps &) = 0;

      protected:
        /** Cost: the stack owner destroys its reader once per export, with no rows to release. */
        ~BlockReader() {}
      };

      /** Every document block derives from this base to expose its authored meaning.
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
