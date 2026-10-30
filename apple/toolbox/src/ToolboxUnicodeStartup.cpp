#include "ToolboxUnicodeStartup.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "ToolboxDialogConstruction.hpp"
#include <Gestalt.h>
#include <Processes.h>
namespace loka
{
  namespace toolbox
  {
    bool UnicodeSystemAdmits(OSErr err, long response)
    {
      return err == noErr && response >= 0x0900;
    }
    void RequireUnicodeSystem()
    {
      long response = 0;
      const OSErr err = Gestalt(gestaltSystemVersion, &response);
      if (!UnicodeSystemAdmits(err, response))
      {
        const char message[] = "This application needs Mac OS 9.0 or later.";
        RunEmergencyQuitDialog(message, sizeof(message) - 1);
        ExitToShell();
      }
    }
  } // namespace toolbox
} // namespace loka
#endif

namespace loka
{
  namespace toolbox
  {
    void UnicodeTextSuddenTerminate() {}
  } // namespace toolbox
} // namespace loka
