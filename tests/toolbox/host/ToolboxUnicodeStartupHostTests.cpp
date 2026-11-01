#include "ToolboxUnicodeStartup.hpp"
#include "ToolboxTextensionFake.hpp"
#include <Gestalt.h>
#include "support/TestVerify.hpp"
#include <cstddef>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
static bool initRefusal;
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
      LOKA_VERIFY(std::string(message, length)
                  == (initRefusal ? "The text engine could not start. Please quit."
                                  : "This application needs Mac OS 9.0 or later."));
      events += 'D';
    }
  } // namespace toolbox
} // namespace loka
void ExitToShell()
{
  events += 'Q';
  if (initRefusal)
  {
    LOKA_VERIFY(events == "DQ" && TextensionFake::inits() == 1 && TextensionFake::terminates() == 0);
    _exit(0);
  }
}
int main()
{
  using namespace loka::toolbox;
  TextensionFake::reset();
  {
    UnicodeText::Session session;
    LOKA_VERIFY(TextensionFake::inits() == 1 && TextensionFake::terminates() == 0);
  }
  LOKA_VERIFY(TextensionFake::terminates() == 1 && TextensionFake::live() == 0);
  LOKA_VERIFY(TextensionFake::calls()[0] == "maxapplzone" && TextensionFake::calls()[1] == "init"
              && TextensionFake::calls()[2] == "terminate");
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (!child)
  {
    TextensionFake::reset();
    events.clear();
    initRefusal = true;
    TextensionFake::failNext("init", memFullErr);
    UnicodeText::Session session;
    _exit(2);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFEXITED(status) && WEXITSTATUS(status) == 0);
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
