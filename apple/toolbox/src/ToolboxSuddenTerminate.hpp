#ifndef LOKA_TOOLBOX_SUDDEN_TERMINATE_HPP
#define LOKA_TOOLBOX_SUDDEN_TERMINATE_HPP
namespace loka
{
  namespace toolbox
  {
    /** A call site immediately precedes each ExitToShell reached by QuitForOutOfMemory.
     * No invocation count is promised; participants handle re-entry under the
     * contract in docs/SuddenTerminationDesign.md. This door owns no resources.
     */
    void SuddenTerminate();
  } // namespace toolbox
} // namespace loka
#endif
