#include "Win32LinkPress.hpp"

bool Win32LinkPressTable::rebuild(const loka::app::AttributedString &value,
                                  const loka::app::BlockStyle &style,
                                  int width,
                                  HDC dc,
                                  const Win32ScenePlatformController &controller)
{
  this->invalidatePress();
  return this->table_.build(value, style, width, dc, controller);
}
void Win32LinkPressTable::clear()
{
  this->invalidatePress();
  this->table_.clear();
}
void Win32LinkPressTable::invalidatePress()
{
  this->session_.invalidate();
}
void Win32LinkPressTable::losePress()
{
  this->session_.lose();
}
