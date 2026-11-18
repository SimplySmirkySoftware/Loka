#ifndef LOKA_WEB_ATTRIBUTED_SLOT_HPP
#define LOKA_WEB_ATTRIBUTED_SLOT_HPP
#include "WebAttributedLayout.hpp"
/** One context-owned slot. Successful replacement publishes before releasing old storage.
    Cost: one pointer exchange per successful build; clear frees only this slot. */
class WebAttributedSlot
{
public:
  /** Cost: context admission once; initializes its own empty slot, O(1). */
  WebAttributedSlot()
      : value_(0)
  {
  }
  /** Cost: context destruction once; frees only its completed value buffers. */
  ~WebAttributedSlot()
  {
    this->clear();
  }
  /** Cost: layout/render once per visit; reads its own slot, O(1). */
  const WebAttributedSnapshot *get() const
  {
    return this->value_;
  }
  /** Cost: successful build once; exchanges this slot then frees its previous buffers. */
  void replace(WebAttributedSnapshot *value)
  {
    WebAttributedSnapshot *old = this->value_;
    this->value_ = value;
    delete old;
  }
  /** Cost: retire/destruction once; releases only this slot, with no callbacks. */
  void clear()
  {
    this->replace(0);
  }

private:
  WebAttributedSnapshot *value_;
  WebAttributedSlot(const WebAttributedSlot &);
  WebAttributedSlot &operator=(const WebAttributedSlot &);
};
#endif
