#ifndef LOKA_APP_LAYOUT_NATURAL_WIDTH_SOURCE_HPP
#define LOKA_APP_LAYOUT_NATURAL_WIDTH_SOURCE_HPP

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
      /** Borrowed rail leaf measurement for natural Row allocation and offer-independent ScrollView
          content (including intrinsic images), answering a positive width or refusing without changing it.
          Answers and inputs must remain stable and outlive the synchronous pass, including repeated
          Row queries: no state writes, event pumping, or consuming measurement tokens. */
      class INaturalWidthSource
      {
      public:
        virtual ~INaturalWidthSource() {}
        virtual bool queryNaturalWidth(scene::Node *child, short &width) const = 0;
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif
