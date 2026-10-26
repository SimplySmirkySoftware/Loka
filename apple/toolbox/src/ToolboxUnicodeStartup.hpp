#ifndef LOKA_TOOLBOX_UNICODE_STARTUP_HPP
#define LOKA_TOOLBOX_UNICODE_STARTUP_HPP
#include <Types.h>
namespace loka
{
  namespace toolbox
  {
    /** Admit only a successful system-version query meeting the OS 9 requirement. */
    bool UnicodeSystemAdmits(OSErr err, long response);
    /** Refuse startup with an emergency dialog and exit when the requirement fails. */
    void RequireUnicodeSystem();
  } // namespace toolbox
} // namespace loka
#endif
