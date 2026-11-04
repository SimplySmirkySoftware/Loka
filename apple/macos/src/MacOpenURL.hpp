#ifndef LOKA_MAC_OPEN_URL_HPP
#define LOKA_MAC_OPEN_URL_HPP

#include "app/url/WebURL.hpp"
#include "app/url/URLOpenKind.hpp"
#include <objc/objc.h>

#ifdef __OBJC__
@class NSURL;
#else
class NSURL;
#endif

/** Converts one WebURL and calls open synchronously, borrowing the thunk only
    for this call. The local pool keeps both native objects alive through open;
    conversion refusal or a native result other than YES means URL_NOT_OPENED. */
loka::app::URLOpenKind MacOpenWebURL(const loka::app::WebURL &url, BOOL (*open)(NSURL *url));

#endif
