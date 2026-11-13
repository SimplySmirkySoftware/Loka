#include "Win32LinkPress.hpp"

Win32LinkPressTable::Win32LinkPressTable()
    : active_(0)
{
}
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
  this->candidate_.drop();
  if (this->active_)
    this->active_->invalidated_ = true;
}
void Win32LinkPressTable::losePress()
{
  this->candidate_.drop();
}
bool Win32LinkPressTable::enter(Win32LinkPressProcedure &procedure)
{
  if (this->active_)
    return false;
  this->active_ = &procedure;
  return true;
}
void Win32LinkPressTable::leave(Win32LinkPressProcedure &procedure)
{
  assert(this->active_ == &procedure);
  (void)procedure;
  this->active_ = 0;
}
Win32LinkPressProcedure::Win32LinkPressProcedure(Win32LinkPressTable &links)
    : links_(links.enter(*this) ? &links : 0),
      invalidated_(false)
{
}
Win32LinkPressProcedure::~Win32LinkPressProcedure()
{
  this->close();
}
void Win32LinkPressProcedure::close()
{
  if (this->links_)
  {
    this->links_->leave(*this);
    this->links_ = 0;
  }
}
bool Win32LinkPressProcedure::takeCandidate(Win32PressedRun &out)
{
  assert(this->links_);
  return this->links_->candidate_.take(out);
}
void Win32LinkPressProcedure::placeCandidate(const Win32PressedRun &run)
{
  assert(this->links_);
  this->links_->candidate_.place(run);
}
bool Win32LinkPressProcedure::candidateHolds(const Win32PressedRun &run) const
{
  assert(this->links_);
  return this->links_->candidate_.holds(run);
}
