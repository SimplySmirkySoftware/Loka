// The MLTE routine comes from the repository's own declaration subset; the
// converter routines are PPC CFM ABI declarations written here, since no SDK
// header is needed. This link-only probe is never a runtime acceptance
// application.
#include "unicode/MacTextEditorSubset.h"
struct OpaqueTextToUnicodeInfo;
typedef OpaqueTextToUnicodeInfo *TextToUnicodeInfo;
extern "C" long TECCountAvailableTextEncodings(ItemCount *count);
extern "C" long CreateTextToUnicodeInfoByEncoding(TextEncoding encoding, TextToUnicodeInfo *info);
int main()
{
  TXNFeatureBits flags = 0;
  ItemCount count = 0;
  TextToUnicodeInfo info = 0;
  const TXNVersionValue version = TXNVersionInformation(&flags);
  const long counted = TECCountAvailableTextEncodings(&count);
  const long created = CreateTextToUnicodeInfoByEncoding(0, &info);
  return static_cast<int>(version + counted + created);
}
