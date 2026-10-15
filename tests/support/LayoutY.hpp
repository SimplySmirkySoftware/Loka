#ifndef LOKA_TESTS_SUPPORT_LAYOUT_Y_HPP
#define LOKA_TESTS_SUPPORT_LAYOUT_Y_HPP
#include <climits>
namespace loka
{
  namespace testing
  {
    inline bool acceptShortLayoutY(void *, int y)
    {
      return y >= SHRT_MIN && y <= SHRT_MAX;
    }
  } // namespace testing
} // namespace loka
#endif
