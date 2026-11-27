#ifndef LOKA_WIN32_LINK_PRESS_HPP
#define LOKA_WIN32_LINK_PRESS_HPP
#include "Win32AttributedTextTable.hpp"
#include "app/internal/LinkPressSession.hpp"

/** Owns the table and couples every mutation to press invalidation. */
class Win32LinkPressTable
{
public:
  Win32LinkPressTable() {}
  loka::app::internal::LinkPressSession<loka::app::internal::PressedRun> &session()
  {
    return this->session_;
  }
  const Win32AttributedTextTable &table() const
  {
    return this->table_;
  }
  bool rebuild(const loka::app::AttributedString &,
               const loka::app::BlockStyle &,
               int,
               HDC,
               const Win32ScenePlatformController &);
  void clear();
  void invalidatePress();
  void losePress();

private:
  friend class loka::testing::Win32AttributedTextAccess;
  Win32AttributedTextTable table_;
  loka::app::internal::LinkPressSession<loka::app::internal::PressedRun> session_;
  Win32LinkPressTable(const Win32LinkPressTable &);
  Win32LinkPressTable &operator=(const Win32LinkPressTable &);
};

#endif
