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
    typedef void (*MacLinkPressHook)(MacLinkPressVisit, void *);
    /** A fixture borrows one observer for its lexical interval; no row walk. */
    void observeMacLinkPress(MacLinkPressHook hook, void *data);
    /** One callback per instrumented entry in a test build; no row walk. */
    void visitMacLinkPress(MacLinkPressVisit visit);
  } // namespace testing
} // namespace loka
#endif
#endif
