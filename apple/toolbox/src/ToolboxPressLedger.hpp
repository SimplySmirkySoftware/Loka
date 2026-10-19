#ifndef LOKA_TOOLBOX_PRESS_LEDGER_HPP
#define LOKA_TOOLBOX_PRESS_LEDGER_HPP
#include <Quickdraw.h>
#include <vector>
#include <cstddef>

class ToolboxScenePlatformController;
struct ResolvedPress;
/** Render-reach identity only; geometry and ownership stay in their existing rows. */
struct PressRow
{
  enum Kind
  {
    NATIVE_BUTTON,
    NATIVE_BAR,
    EDIT_CONTROL,
    EDIT_HIT,
    POPUP_HIT,
    CELL_HIT,
    BUTTON_HIT,
    UNIT,
    PRESS_ROW_KIND_COUNT
  };
  Kind kind;
  std::size_t index;
};
enum PressOwnerFact
{
  PRESS_OWNER_ATTACHED,
  PRESS_OWNER_PARKED,
  PRESS_OWNER_SHIELD
};

/** Controller-owned order. A refused render preserves LIVE and its indexed rows. */
class ToolboxPressLedger
{
public:
  void begin()
  {
    this->staging_.clear();
  }
  void commit()
  {
    this->live_.swap(this->staging_);
    this->staging_.clear();
  }
  void clear()
  {
    this->live_.clear();
    this->staging_.clear();
  }
  void append(PressRow::Kind kind, std::size_t index)
  {
    const PressRow row = {kind, index};
    this->staging_.push_back(row);
  }
  void forget(PressRow::Kind kind, std::size_t index)
  {
    forgetIn(this->live_, kind, index);
    forgetIn(this->staging_, kind, index);
  }
  bool topmostAt(const Point &, ToolboxScenePlatformController &, ResolvedPress &) const;

private:
  static void forgetIn(std::vector<PressRow> &rows, PressRow::Kind kind, std::size_t index)
  {
    std::size_t out = 0;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
      PressRow row = rows[i];
      if (row.kind == kind)
      {
        if (row.index == index)
          continue;
        if (row.index > index)
          --row.index;
      }
      rows[out++] = row;
    }
    rows.resize(out);
  }
  std::vector<PressRow> live_, staging_;
};
#endif
