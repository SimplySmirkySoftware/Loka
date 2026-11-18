#include "app/doc/Heading.hpp"
#include <climits>
int main()
{
  const int levels[] = {0, -1, 7, INT_MIN, INT_MAX, 1, 2, 3, 4, 5, 6};
  for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
  {
    const int expected = levels[i] < 1 ? 1 : (levels[i] > 6 ? 6 : levels[i]);
    if (loka::app::doc::NormalizeLevel(levels[i]) != expected)
      return 1;
  }
  return 0;
}
