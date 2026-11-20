#ifndef LOKA_APP_DOC_CONTAINER_HPP
#define LOKA_APP_DOC_CONTAINER_HPP
#include "app/doc/BlockWalk.hpp"
#include "app/nodes/nestable/RowColumn.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      enum
      {
        QuoteIndent = 16,
        MarkerWidth = 12,
        ItemGap = 4,
        ListGap = 2
      };
      /** Cost: each item borrows one static UTF-8 literal per projection. */
      inline const char *ListBullet()
      {
        return "\xE2\x80\xA2"; // U+2022 BULLET, spelled in ASCII for every source charset
      }

      /** A semantic stack with a fixed native layout. Cost: one stack per authored container. */
      template <class Props> class ContainerNode : public StackNode
      {
      public:
        typedef typename Props::TypeTag TypeTag;
        /** Cost: materialization initializes one fixed stack. */
        explicit ContainerNode(const Props &p)
            : StackNode(p.layout())
        {
        }
        /** Cost: each exit visits this stack's children once. */
        virtual scene::DocumentBlockRead readDocumentBlock(BlockReader &reader) const
        {
          return detail::ReadContainer(reader, Props(), this->childrenHead());
        }
      };
    } // namespace doc
    namespace scene
    {
      /** Container inputs have no mutable layout values. Cost: O(1) per retained apply. */
      template <class Props> struct NodePropsApplier<doc::ContainerNode<Props>, Props>
      {
        /** Cost: retained reconciliation accepts one fixed input without walking children. */
        static bool apply(doc::ContainerNode<Props> *, const Props &)
        {
          return true;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
