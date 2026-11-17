#ifndef LOKA_TESTS_WIN32_TEXT_STORE_ACCESS_HPP
#define LOKA_TESTS_WIN32_TEXT_STORE_ACCESS_HPP
#include "text/Win32TextStore.hpp"
namespace loka
{
  namespace win32
  {
    namespace testing
    {
      struct TextStoreAccess
      {
        static HWND edit(loka::app::scene::TextStoreRow &row)
        {
          return static_cast<Win32TextStore &>(row).edit_;
        }
        static bool marked(loka::app::scene::TextStoreRow &row)
        {
          return static_cast<Win32TextStore &>(row).changes_.pending();
        }
      };
    } // namespace testing
  } // namespace win32
} // namespace loka
#endif
