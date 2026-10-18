#include "ToolboxTwoByteKey.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>

int main()
{
  ToolboxTwoByteKey key;
  LOKA_VERIFY(!key.holdsLead());
  key.release();
  LOKA_VERIFY(!key.holdsLead());
  for (unsigned int byte = 0x80; byte <= 0xff; ++byte)
  {
    key.hold(static_cast<unsigned char>(byte));
    LOKA_VERIFY(key.holdsLead());
    LOKA_VERIFY(key.lead() == byte);
    // Reads between event iterations preserve the held lead.
    LOKA_VERIFY(key.holdsLead());
    LOKA_VERIFY(key.lead() == byte);
    key.release();
    LOKA_VERIFY(!key.holdsLead());
  }
  std::puts("Two-byte key: empty/release and 128 hold/read/release cycles passed");
  return 0;
}
