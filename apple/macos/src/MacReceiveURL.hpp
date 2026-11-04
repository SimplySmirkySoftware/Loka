#ifndef LOKA_MAC_RECEIVE_URL_HPP
#define LOKA_MAC_RECEIVE_URL_HPP

#include "core/String.hpp"

#ifdef __OBJC__
@class NSAppleEventDescriptor;
#else
class NSAppleEventDescriptor;
#endif

/** Main-thread only; borrows descriptor for this call. Coerces UTF-8 first,
    then typeChar, copying bytes with their explicit length into out before
    draining the local pool. No filtering or repair; both paths read UTF-8.
    Returns false without changing out if neither coercion succeeds. */
bool MacURLTextFromDescriptor(NSAppleEventDescriptor *descriptor, loka::core::String &out);

#endif
