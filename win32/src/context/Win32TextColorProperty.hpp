#ifndef LOKA_WIN32_TEXT_COLOR_PROPERTY_HPP
#define LOKA_WIN32_TEXT_COLOR_PROPERTY_HPP
#include <windows.h>
#include "app/style/StyleVocab.hpp"

// An absent property reads as 0, which must be the generated UNSET.
typedef char TextColorUnsetMustReadAsAbsentProperty[(loka::app::TEXT_COLOR_UNSET == 0) ? 1 : -1];

/** Typed non-owning HWND property, parallel to Win32FocusParticipant. The
    property's value is the TextColor itself, carried in the handle, so it
    borrows nothing and every vocabulary member fits without a list. Unset
    removes the property. A refused SetPropW leaves the STATIC at its ground
    pair. Detach removes the property synchronously before retirement. */
class Win32TextColorProperty
{
public:
  static loka::app::TextColor read(HWND hwnd)
  {
    return static_cast<loka::app::TextColor>(reinterpret_cast<LONG_PTR>(GetPropW(hwnd, name())));
  }
  static void detach(HWND hwnd)
  {
    RemovePropW(hwnd, name());
  }
  static bool apply(HWND hwnd, loka::app::TextColor color)
  {
    const loka::app::TextColor previous = read(hwnd);
    if (previous == color)
      return false;
    detach(hwnd);
    if (color != loka::app::TEXT_COLOR_UNSET)
      SetPropW(hwnd, name(), reinterpret_cast<void *>(static_cast<LONG_PTR>(color)));
    return read(hwnd) != previous;
  }

private:
  static LPCWSTR name()
  {
    return L"Loka.Text.Color";
  }
};
#endif
