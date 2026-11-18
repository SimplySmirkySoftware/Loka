#include "WebRangeWidthMemo.hpp"
#include "WebTextLayout.hpp"
#include "WebBoundary.hpp"
#include <limits>
#include <cassert>
WebRangeWidthMemo::WebRangeWidthMemo(const char *bytes, const loka::core::LokaAllocationSite &site)
    : bytes_(bytes),
      site_(site),
      rows_(0),
      capacity_(0),
      used_(0),
      status_(WEB_MEASURE_OK)
{
  if (!this->grow())
    this->status_ = WEB_MEASURE_REFUSED;
}
WebRangeWidthMemo::~WebRangeWidthMemo()
{
  loka::core::LokaFreeRaw(this->rows_, this->site_);
}
std::size_t WebRangeWidthMemo::find(std::size_t start, std::size_t end, int key, Row *rows, std::size_t capacity) const
{
  std::size_t index = (start * 33 + end + static_cast<unsigned>(key) * 17) & (capacity - 1);
  while (rows[index].occupied && (rows[index].start != start || rows[index].end != end || rows[index].key != key))
    index = (index + 1) & (capacity - 1);
  return index;
}
bool WebRangeWidthMemo::grow() const
{
  if (this->capacity_ > (std::numeric_limits<std::size_t>::max)() / 2)
    return false;
  const std::size_t capacity = this->capacity_ ? this->capacity_ * 2 : 64;
  if (capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(Row))
    return false;
  Row *rows = static_cast<Row *>(loka::core::LokaAllocRaw(capacity * sizeof(Row), this->site_));
  if (!rows)
    return false;
  for (std::size_t i = 0; i < capacity; ++i)
    rows[i].occupied = false;
  for (std::size_t i = 0; i < this->capacity_; ++i)
    if (this->rows_[i].occupied)
      rows[this->find(this->rows_[i].start, this->rows_[i].end, this->rows_[i].key, rows, capacity)] = this->rows_[i];
  loka::core::LokaFreeRaw(this->rows_, this->site_);
  this->rows_ = rows;
  this->capacity_ = capacity;
  return true;
}
bool WebRangeWidthMemo::width(std::size_t start, std::size_t end, int key, int &out) const
{
  if (this->status_ != WEB_MEASURE_OK)
    return false;
  if (this->used_ >= this->capacity_ / 2 && !this->grow())
  {
    this->status_ = WEB_MEASURE_REFUSED;
    return false;
  }
  const std::size_t index = this->find(start, end, key, this->rows_, this->capacity_);
  if (this->rows_[index].occupied)
  {
    out = this->rows_[index].width;
    return true;
  }
  const WebTextDescriptor descriptor = WebMetricDescriptor(key);
  int measured = 0;
  this->status_ = web_measure(descriptor.data(), this->bytes_ + start, static_cast<unsigned>(end - start), &measured);
  if (this->status_ == WEB_MEASURE_PROTOCOL)
    assert(false && "Web measurement protocol refusal");
  if (this->status_ != WEB_MEASURE_OK)
    return false;
  Row &row = this->rows_[index];
  row.start = start;
  row.end = end;
  row.key = key;
  row.width = measured;
  row.occupied = true;
  ++this->used_;
  out = measured;
  return true;
}
