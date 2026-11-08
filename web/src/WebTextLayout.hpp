#ifndef LOKA_WEB_TEXT_LAYOUT_HPP
#define LOKA_WEB_TEXT_LAYOUT_HPP
#include "WebTextConstants.hpp"
#include "WebTextDescriptor.hpp"
#include "app/layout/TextLineBreaker.hpp"
#include "core/Frame.hpp"

/** Immutable metric constraint. Cost: leaf visit once, fixed own values only. */
class WebTextConstraint
{
public:
  WebTextConstraint(int key, const loka::app::BlockStyle &block, short width)
      : fontKey(key),
        block(block),
        availableWidth(width)
  {
  }
  /** Cost: leaf predicate once; compares only this value and its argument, O(1). */
  bool operator==(const WebTextConstraint &other) const
  {
    return this->fontKey == other.fontKey && this->block == other.block && this->availableWidth == other.availableWidth;
  }
  const int fontKey;
  const loka::app::BlockStyle block;
  const short availableWidth;
};
/** Completed projection owns its only coordinate system and all derived rows.
    Cost: layout constructs once on a miss; destruction visits its own buffers only.
    Read-only accessors are O(1) own-value reads by layout/render per visit. */
class WebTextSnapshot
{
public:
  ~WebTextSnapshot();
  const WebTextConstraint &constraint() const
  {
    return this->constraint_;
  }
  const char *prepared() const
  {
    return this->prepared_;
  }
  unsigned preparedLength() const
  {
    return this->preparedLength_;
  }
  const char *joined() const
  {
    return this->joined_;
  }
  unsigned joinedLength() const
  {
    return this->joinedLength_;
  }
  const loka::app::TextLineMetrics &metrics() const
  {
    return this->metrics_;
  }
  const loka::core::Frame &extent() const
  {
    return this->extent_;
  }
  /** Cost: owner predicate once per matching constraint; one normalized scan, no allocation on borrow. */
  int matches(const loka::core::String &, bool button, bool &out) const;

private:
  friend class WebTextLayout;
  friend class WebTextMeasure;
  struct Line
  {
    std::size_t start, end;
    int width;
  };
  explicit WebTextSnapshot(const WebTextConstraint &constraint);
  WebTextConstraint constraint_;
  char *prepared_, *joined_;
  loka::app::TextBreakCharacter *characters_;
  Line *lines_;
  unsigned preparedLength_, joinedLength_;
  std::size_t characterCount_, lineCount_;
  loka::app::TextLineMetrics metrics_;
  loka::core::Frame extent_;
  WebTextSnapshot(const WebTextSnapshot &);
  WebTextSnapshot &operator=(const WebTextSnapshot &);
};
/** One context-owned slot. Successful replacement publishes before releasing old storage.
    Cost: one pointer exchange per successful build; clear frees only this slot. */
class WebTextSlot
{
public:
  WebTextSlot()
      : value_(0)
  {
  }
  ~WebTextSlot()
  {
    this->clear();
  }
  /** Cost: layout/render once per visit; reads its own slot, O(1). */
  const WebTextSnapshot *get() const
  {
    return this->value_;
  }
  /** Cost: successful build once; exchanges this slot then frees its previous buffers. */
  void replace(WebTextSnapshot *value)
  {
    WebTextSnapshot *old = this->value_;
    this->value_ = value;
    delete old;
  }
  /** Cost: retire/destruction once; releases only this slot, with no callbacks. */
  void clear()
  {
    this->replace(0);
  }
  /** Cost: build success once; transfers its own value without releasing storage. */
  WebTextSnapshot *release()
  {
    WebTextSnapshot *value = this->value_;
    this->value_ = 0;
    return value;
  }

private:
  WebTextSnapshot *value_;
  WebTextSlot(const WebTextSlot &);
  WebTextSlot &operator=(const WebTextSlot &);
};
/** Synchronous construction; no application state or native resource is retained. */
class WebTextLayout
{
public:
  /** Cost: owner miss once per visit; prepares own bytes, metrics, two breaker passes and joined rows.
      Width memo lives only during this build; WORD lookahead can visit quadratic bytes. */
  static int build(const loka::core::String &, const WebTextConstraint &, bool button, WebTextSnapshot *&out);
  /** Cost: natural query once; one temporary normalized run and one service query, no publication. */
  static bool natural(const loka::core::String &, const WebTextConstraint &, bool button, bool height, short &out);
};
/** Cost: leaf constraint once per visit; six generated size comparisons; deliberate JS fontKey twin. */
int WebFontKey(const loka::app::TextStyle &);
/** Cost: render or measure once; nine stack words, own font plus current paint overlay. */
WebTextDescriptor WebMetricDescriptor(int key, const loka::app::TextStyle &paint = loka::app::TextStyle());
#endif
