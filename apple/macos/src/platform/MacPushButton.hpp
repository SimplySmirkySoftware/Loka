#ifndef LOKA_MAC_PUSH_BUTTON_HPP
#define LOKA_MAC_PUSH_BUTTON_HPP

#include "../MacObjCCompat.hpp"

namespace loka
{
  namespace macos
  {
    /** Shared push-button configuration for projection and native measurement. */
    inline void ConfigureMacPushButton(NSButton *button)
    {
      [button setBezelStyle:LOKA_MAC_BUTTON_BEZEL_STYLE];
      [button setButtonType:LOKA_MAC_BUTTON_TYPE_MOMENTARY_PUSH_IN];
    }
  } // namespace macos
} // namespace loka
#endif
