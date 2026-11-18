#ifndef LOKA_WEB_RANGE_WIDTH_MEMO_HPP
#define LOKA_WEB_RANGE_WIDTH_MEMO_HPP
#include "core/LokaAlloc.hpp"
#include "WebTextConstants.hpp"
/** Build-local standalone-range memo, shared by the Text and attributed twins.
    The supplied bytes outlive this memo; offsets never name packed display bytes. */
class WebRangeWidthMemo
{
public:
  /** Cost: builder once per miss; allocates this build's initial 64-row table. */
  WebRangeWidthMemo(const char *, const loka::core::LokaAllocationSite &);
  /** Cost: build exit once; releases only this memo's table, no callbacks. */
  ~WebRangeWidthMemo();
  /** Cost: builder/breaker per probe; O(1), reads this build's status. */
  int status() const
  {
    return this->status_;
  }
  /** Cost: breaker per range probe; expected O(1) lookup in own rows, one synchronous
      import on miss. Growth rehashes only this build's rows. */
  bool width(std::size_t, std::size_t, int key, int &out) const;

private:
  struct Row
  {
    std::size_t start, end;
    int key, width;
    bool occupied;
  };
  const char *bytes_;
  const loka::core::LokaAllocationSite site_;
  mutable Row *rows_;
  mutable std::size_t capacity_, used_;
  mutable int status_;
  /** Cost: probe/growth per row; probes only this memo's supplied table. */
  std::size_t find(std::size_t, std::size_t, int, Row *, std::size_t) const;
  /** Cost: construction or half-full probe; rehashes only this memo's rows. */
  bool grow() const;
  WebRangeWidthMemo(const WebRangeWidthMemo &);
  WebRangeWidthMemo &operator=(const WebRangeWidthMemo &);
};
#endif
