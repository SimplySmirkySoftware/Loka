#include "ToolboxUnicodeStartup.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "ToolboxDialogConstruction.hpp"
#include "unicode/MacTextEditorSubset.h"
#include <Gestalt.h>
#include <Memory.h>
#include <Processes.h>
namespace loka
{
  namespace toolbox
  {
    bool UnicodeText::serviceEvent(EventRecord &record)
    {
      return TXNTSMCheck(0, &record);
    }
    void UnicodeText::scrapToPublic()
    {
      if (TXNConvertToPublicScrap() != noErr)
      {
        // diagnostic: no Toolbox status-log seam; clipboard contents are unknown.
      }
    }
    void UnicodeText::scrapFromPublic()
    {
      if (TXNConvertFromPublicScrap() != noErr)
      {
        // diagnostic: no Toolbox status-log seam; clipboard contents are unknown.
      }
    }
    UnicodeText::Session::Session()
    {
      // Textension sizes its allocations against the zone as it stands, not
      // against the room the zone could still grow into: with the zone
      // unexpanded (FreeMem 1280 bytes on Mac OS 9.2.1) TXNNewObject answers
      // memFullErr. Loka's own accounting already covers both shapes
      // (queryLargestContiguousAllocation), so the TE rail stays as it is.
      MaxApplZone();
      if (TXNInitTextension(0, 0, 0) != noErr)
      {
        const char message[] = "The text engine could not start. Please quit.";
        RunEmergencyQuitDialog(message, sizeof(message) - 1);
        ExitToShell();
      }
    }
    UnicodeText::Session::~Session()
    {
      TXNTerminateTextension();
    }
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
#else
namespace loka
{
  namespace toolbox
  {
    bool UnicodeText::serviceEvent(EventRecord &)
    {
      return false;
    }
    void UnicodeText::scrapToPublic() {}
    void UnicodeText::scrapFromPublic() {}
    UnicodeText::Session::Session() {}
    UnicodeText::Session::~Session() {}
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
