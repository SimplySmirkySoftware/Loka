#include "ToolboxUnicodeStartup.hpp"
#include <Gestalt.h>
#include "support/TestVerify.hpp"
#include <cstddef>
#include <string>
static OSErr queryError;
static long queryVersion;
static std::string events;
OSErr Gestalt(OSType selector, long *response)
{
  LOKA_VERIFY(selector == gestaltSystemVersion && *response == 0);
  events += 'G';
  if (queryError == noErr)
    *response = queryVersion;
  return queryError;
}
namespace loka
{
  namespace toolbox
  {
    void RunEmergencyQuitDialog(const char *message, std::size_t length)
    {
      LOKA_VERIFY(std::string(message, length) == "This application needs Mac OS 9.0 or later.");
      events += 'D';
    }
  } // namespace toolbox
} // namespace loka
void ExitToShell()
{
  events += 'Q';
}
int main()
{
  using namespace loka::toolbox;
  LOKA_VERIFY(UnicodeSystemAdmits(noErr, 0x0900));
  LOKA_VERIFY(UnicodeSystemAdmits(noErr, 0x0922));
  LOKA_VERIFY(!UnicodeSystemAdmits(noErr, 0x08FF));
  LOKA_VERIFY(!UnicodeSystemAdmits(noErr, 0x0860));
  LOKA_VERIFY(!UnicodeSystemAdmits(paramErr, 0x0900));
  const long versions[] = {0x0900, 0x0922, 0x08FF, 0x0860};
  for (unsigned i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i)
  {
    queryVersion = versions[i];
    events.clear();
    RequireUnicodeSystem();
    LOKA_VERIFY(events == (i < 2 ? "G" : "GDQ"));
  }
  queryError = paramErr;
  events.clear();
  RequireUnicodeSystem();
  LOKA_VERIFY(events == "GDQ");
}
