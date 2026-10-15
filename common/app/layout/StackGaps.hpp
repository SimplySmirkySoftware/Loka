#ifndef LOKA_APP_LAYOUT_STACK_GAPS_HPP
#define LOKA_APP_LAYOUT_STACK_GAPS_HPP

#include "app/layout/LayoutHeuristics.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** One traversal's gap progression. A band may begin after an earlier seat. */
      class StackGaps
      {
      public:
        explicit StackGaps(int gap, bool precedingSeat = false)
            : gap_(gap),
              seenSeat_(precedingSeat)
        {
          assert(gap >= 0);
        }
        int before(scene::Node *child)
        {
          if (!childTakesLayoutSeat(child))
            return 0;
          const int result = this->seenSeat_ ? this->gap_ : 0;
          this->seenSeat_ = true;
          return result;
        }

      private:
        const int gap_;
        bool seenSeat_;
      };
    } // namespace layout
  } // namespace app
} // namespace loka
#endif
