#ifndef LOKA_WIN32_TEXT_PARKING_HPP
#define LOKA_WIN32_TEXT_PARKING_HPP
#include <windows.h>
#include "app/text/TextStore.hpp"
/** Controller clock for parked text stores. Rows die before their native parent. */
class Win32TextParking
{
public:
  Win32TextParking()
      : window_(0)
  {
  }
  ~Win32TextParking()
  {
    this->destroy();
  }
  bool ensure();
  HWND window() const
  {
    return this->window_;
  }
  loka::app::scene::TextStoreLedger &ledger()
  {
    return this->ledger_;
  }
  /** Ordinary controller safe point; visits only this controller's retired rows. */
  void drain()
  {
    this->ledger_.drain();
  }
  /** Idempotent shutdown while the derived controller still answers retire doors. */
  void destroy();

private:
  Win32TextParking(const Win32TextParking &);
  Win32TextParking &operator=(const Win32TextParking &);
  static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
  // Parking twin of MacScenePlatformController::textStores_. EDIT requires a native parent.
  HWND window_;
  loka::app::scene::TextStoreLedger ledger_;
};
#endif
