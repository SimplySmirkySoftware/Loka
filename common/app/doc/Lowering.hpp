#ifndef LOKA_APP_DOC_LOWERING_HPP
#define LOKA_APP_DOC_LOWERING_HPP
#include "app/nodes/AttributedText.hpp"
#include <functional>
namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Borrows the single projected text child, or zero if absent.
            Cost: at most one child and sibling check per retained block update. */
        inline AttributedTextNode *ProjectionLeaf(scene::Node *child)
        {
          if (!child)
            return 0;
          AttributedTextNode *leaf = child->asAttributedTextNode();
          const bool expected = leaf && !child->nextInComposition;
          assert(expected && "document leaf shape");
          return expected ? leaf : 0;
        }
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
