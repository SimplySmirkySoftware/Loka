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
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif
