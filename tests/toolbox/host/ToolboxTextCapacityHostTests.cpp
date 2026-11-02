#include "ToolboxTextCapacity.hpp"
#include "support/TestVerify.hpp"
int main()
{
  const loka::app::TextCapacity maximum = ToolboxTextCapacity();
  LOKA_VERIFY(maximum.lines == EXPECTED_LINES && maximum.bytes == EXPECTED_BYTES);
  const loka::app::TextCapacity profile = loka::app::TextCapacity::of(32768, 4 * 1024 * 1024).clampedTo(maximum);
  LOKA_VERIFY(profile.lines == EXPECTED_PROFILE_LINES && profile.bytes == EXPECTED_PROFILE_BYTES);
}
