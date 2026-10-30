#ifndef LOKA_APP_LAYOUT_NATURAL_SIZE_SOURCE_HPP
#define LOKA_APP_LAYOUT_NATURAL_SIZE_SOURCE_HPP

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class Node;
    }
    namespace layout
    {
      /** Borrowed rail leaf measurement: a leaf's natural size, one typed query per axis. Each query
          answers a positive value or refuses without changing its output. Answers and inputs must
          remain stable and outlive the synchronous pass, including repeated Row queries: no state
          writes, event pumping, or consuming measurement tokens. */
      class INaturalSizeSource
      {
      public:
        virtual ~INaturalSizeSource() {}
        /** Width for natural Row allocation and offer-independent ScrollView content, including
            intrinsic images. */
        virtual bool queryNaturalWidth(scene::Node *child, short &width) const = 0;
        /** One-line height used by the rail's Text leaf on successful layout, computed
            without layout state or offered width. Answers only when the shared height
            function succeeds with a result in 1..SHRT_MAX; otherwise leaves height
            unchanged. The class pass-stability contract applies. An answer does not
            promise that the leaf's later native measurement succeeds. */
        virtual bool queryNaturalHeight(scene::Node *, short &) const
        {
          return false;
        }
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif
