#ifndef LOKA_WIN32_OPEN_URL_HPP
#define LOKA_WIN32_OPEN_URL_HPP

#include "app/url/WebURL.hpp"
#include "app/url/URLOpenKind.hpp"
#include <windows.h>
#include <shellapi.h>

/** Widens an ASCII WebURL and borrows execute synchronously for one call.
    Owns a call-local STA COM reference when initialization succeeds. An MTA
    host gets best-effort execution, without an STA guarantee for extensions.
    URL_OPENED means only that the system accepted the launch. */
loka::app::URLOpenKind Win32OpenWebURL(const loka::app::WebURL &url, BOOL(WINAPI *execute)(SHELLEXECUTEINFOW *info));

#endif
