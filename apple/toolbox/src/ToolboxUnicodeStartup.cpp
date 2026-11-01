#include "ToolboxUnicodeStartup.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "ToolboxDialogConstruction.hpp"
#include "unicode/MacTextEditorSubset.h"
#include <Gestalt.h>
#include <Memory.h>
#include <Processes.h>
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
#include <cstdio>
#endif
namespace
{
  enum ScrapSync
  {
    UNKNOWN,
    IMPORTED,
    PRIVATE
  };
  ScrapSync scrapSync = UNKNOWN;
  void noteScrapFailure(const char *operation, OSStatus status)
  {
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    if (status != noErr)
      std::fprintf(stderr, "[unicode-scrap] %s failed: %ld\n", operation, static_cast<long>(status));
#else
    (void)operation;
    (void)status;
#endif
  }
} // namespace
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
      noteScrapFailure("to-public", TXNConvertToPublicScrap());
    }
    void UnicodeText::scrapFromPublic()
    {
      const OSStatus status = TXNConvertFromPublicScrap();
      scrapSync = status == noErr ? IMPORTED : UNKNOWN;
      noteScrapFailure("from-public", status);
    }
    void UnicodeText::noteNativeScrap(long status)
    {
      scrapSync = status == noErr ? PRIVATE : UNKNOWN;
      noteScrapFailure("native-copy/cut", status);
    }
    bool UnicodeText::scrapReady()
    {
      if (scrapSync != UNKNOWN)
        return true;
      UnicodeText::scrapFromPublic();
      return scrapSync == IMPORTED;
    }
    UnicodeText::Session::Session()
    {
      // Textension sizes its allocations against the zone as it stands, not
      // against the room the zone could still grow into: with the zone
      // unexpanded (FreeMem 1280 bytes on Mac OS 9.2.1) TXNNewObject answers
      // memFullErr. Loka's own accounting already covers both shapes
      // (queryLargestContiguousAllocation), so the TE rail stays as it is.
      scrapSync = UNKNOWN;
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
    void UnicodeText::noteNativeScrap(long) {}
    bool UnicodeText::scrapReady()
    {
      return true;
    }
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
