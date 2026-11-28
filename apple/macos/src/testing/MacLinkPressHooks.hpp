#ifndef LOKA_MAC_LINK_PRESS_HOOKS_HPP
#define LOKA_MAC_LINK_PRESS_HOOKS_HPP
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    enum MacLinkPressVisit
    {
      MAC_LINK_DOWN_ENTRY,
      MAC_LINK_UP_ENTRY,
      MAC_LINK_PREPARE,
      MAC_LINK_CLEAR,
      MAC_LINK_INSTALL,
      MAC_LINK_DROP_WINDOW
    };
    /** Identity and multiplicity of the native event supplied to the leaf. */
    struct MacLinkPressEntry
    {
      long eventNumber;
      long clickCount;
    };
    /** Publish the DOWN or UP event for the duration of one observer call. */
    void visitMacLinkPressEntry(MacLinkPressVisit visit, long eventNumber, long clickCount);
    /** Copy the current entry. Outside its callback, return false and leave out untouched. */
    bool queryMacLinkPressEntry(MacLinkPressEntry &out);
    typedef void (*MacLinkPressHook)(MacLinkPressVisit, void *);
    /** A fixture borrows one observer for its lexical interval; no row walk. */
    void observeMacLinkPress(MacLinkPressHook hook, void *data);
    /** One callback per instrumented entry in a test build; no row walk. */
    void visitMacLinkPress(MacLinkPressVisit visit);
  } // namespace testing
} // namespace loka
#endif
#endif
