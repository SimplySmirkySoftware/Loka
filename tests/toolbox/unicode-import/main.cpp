// PPC CFM ABI declarations only; no MLTE/TEC SDK headers are needed.
// This link-only probe is never a runtime acceptance application.
typedef unsigned long ItemCount;
typedef unsigned long TextEncoding;
struct OpaqueTextToUnicodeInfo;
typedef OpaqueTextToUnicodeInfo *TextToUnicodeInfo;
extern "C" unsigned long TXNVersionInformation(unsigned long *featureFlags);
extern "C" long TECCountAvailableTextEncodings(ItemCount *count);
extern "C" long CreateTextToUnicodeInfoByEncoding(TextEncoding encoding, TextToUnicodeInfo *info);
int main()
{
  unsigned long flags = 0;
  ItemCount count = 0;
  TextToUnicodeInfo info = 0;
  const unsigned long version = TXNVersionInformation(&flags);
  const long counted = TECCountAvailableTextEncodings(&count);
  const long created = CreateTextToUnicodeInfoByEncoding(0, &info);
  return static_cast<int>(version + counted + created);
}
