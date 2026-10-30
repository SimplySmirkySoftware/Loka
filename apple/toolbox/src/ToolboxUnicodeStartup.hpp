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
    /** Closes nothing; leaves live TXNObjects and the TSM connection to process exit.
     * The interface header does not establish allocation-free termination,
     * re-entry, finite return, live-object or pre-init behavior, so contract
     * (b), (c), (d), (j) cannot be shown. A participant unable to show every item
     * is a no-op: this empty body satisfies all items without touching native
     * or Loka state, drawing, callbacks, waiting, or normal retirement.
     * Promotion requires observed harm without termination, evidence for every
     * contract item, and owner approval; see docs/SuddenTerminationDesign.md.
     */
    void UnicodeTextSuddenTerminate();
  } // namespace toolbox
} // namespace loka
#endif
