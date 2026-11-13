#ifndef LOKA_WIN32_LINK_PRESS_HPP
#define LOKA_WIN32_LINK_PRESS_HPP
#include "Win32AttributedTextTable.hpp"

/** Completed maximal run identity within one unchanged table value. */
class Win32PressedRun
{
public:
  Win32PressedRun()
      : first_(static_cast<std::size_t>(-1)),
        last_(static_cast<std::size_t>(-1))
  {
  }
  explicit Win32PressedRun(const loka::app::AttributedString::LinkRun &run)
      : first_(run.first),
        last_(run.last)
  {
  }
  std::size_t first() const
  {
    return this->first_;
  }
  std::size_t last() const
  {
    return this->last_;
  }
  bool operator==(const Win32PressedRun &other) const
  {
    return this->first_ == other.first_ && this->last_ == other.last_;
  }

private:
  std::size_t first_, last_;
};

/** A placed press, consumed before native release begins. */
class Win32PressCandidate
{
public:
  Win32PressCandidate()
      : present_(false)
  {
  }
  void place(const Win32PressedRun &run)
  {
    this->run_ = run;
    this->present_ = true;
  }
  void drop()
  {
    this->present_ = false;
  }
  bool take(Win32PressedRun &out)
  {
    if (!this->present_)
      return false;
    out = this->run_;
    this->drop();
    return true;
  }
  bool holds(const Win32PressedRun &run) const
  {
    return this->present_ && this->run_ == run;
  }

private:
  Win32PressedRun run_;
  bool present_;
};
class Win32LinkPressProcedure;

/** Owns the table and couples every mutation to press invalidation. */
class Win32LinkPressTable
{
public:
  Win32LinkPressTable();
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
  friend class Win32LinkPressProcedure;
  friend class loka::testing::Win32AttributedTextAccess;
  bool enter(Win32LinkPressProcedure &);
  void leave(Win32LinkPressProcedure &);
  Win32AttributedTextTable table_;
  Win32PressCandidate candidate_;
  Win32LinkPressProcedure *active_;
  Win32LinkPressTable(const Win32LinkPressTable &);
  Win32LinkPressTable &operator=(const Win32LinkPressTable &);
};

/** Stack registration inside the input borrow; close before publishing. */
class Win32LinkPressProcedure
{
public:
  explicit Win32LinkPressProcedure(Win32LinkPressTable &);
  ~Win32LinkPressProcedure();
  void close();
  bool admitted() const
  {
    return this->links_ != 0;
  }
  bool invalidated() const
  {
    return this->invalidated_;
  }
  bool takeCandidate(Win32PressedRun &out);
  void placeCandidate(const Win32PressedRun &);
  bool candidateHolds(const Win32PressedRun &) const;

private:
  friend class Win32LinkPressTable;
  Win32LinkPressTable *links_;
  bool invalidated_;
  Win32LinkPressProcedure(const Win32LinkPressProcedure &);
  Win32LinkPressProcedure &operator=(const Win32LinkPressProcedure &);
};
#endif
