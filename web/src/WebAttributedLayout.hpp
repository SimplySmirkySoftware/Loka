#ifndef LOKA_WEB_ATTRIBUTED_LAYOUT_HPP
#define LOKA_WEB_ATTRIBUTED_LAYOUT_HPP
#include "app/style/AttributedString.hpp"
#include "WebTextConstants.hpp"
#include <new>

/** Completed PER_RUN visual projection. No controller, session, or State is retained.
    Deliberate twin of WebTextSnapshot; source segmentation and paint participate in reuse. */
class WebAttributedSnapshot
{
public:
  /** Packed source-ordered visual box. Cost: consumers read one completed row, O(1). */
  struct Fragment
  {
    int descriptor, left, top, width, height, byteStart, byteEnd;
  };
  /** Resolved font plus paint. Cost: consumers read nine completed words, O(1). */
  struct Descriptor
  {
    int words[9];
  };
  /** Cost: owner replacement/destruction once; frees only this completed value's buffers. */
  ~WebAttributedSnapshot();
  /** Cost: eventual layout once per visit; compares its constraint and supplied segment values.
      Shared handles reuse accepted content, not proof of a fresh readable input. */
  bool matches(const loka::app::AttributedString &, const loka::app::BlockStyle &, short width) const;
  /** Cost: eventual render once per visit; borrows this value's display bytes, O(1). */
  const char *bytes() const
  {
    return this->bytes_;
  }
  /** Cost: eventual render once per visit; reads this value, O(1). */
  unsigned byteLength() const
  {
    return this->byteLength_;
  }
  /** Cost: eventual render once per visit; borrows this value's serialized words, O(1). */
  const int *words() const
  {
    return this->words_;
  }
  /** Cost: eventual render once per visit; reads this value, O(1). */
  unsigned wordCount() const
  {
    return this->wordCount_;
  }
  /** Cost: eventual layout once per visit; reads this value, O(1). */
  short height() const
  {
    return this->height_;
  }
  /** Cost: projection consumer once per row; borrows this value's fragment, O(1). */
  const Fragment &fragment(std::size_t index) const;
  /** Cost: projection consumer once per descriptor; borrows this value's descriptor, O(1). */
  const Descriptor &descriptor(std::size_t index) const;
  /** Cost: projection consumer once per visit; reads own wire header, O(1). */
  unsigned fragmentCount() const
  {
    return static_cast<unsigned>(this->words_[1]);
  }
  /** Cost: projection consumer once per visit; reads own wire header, O(1). */
  unsigned descriptorCount() const
  {
    return static_cast<unsigned>(this->words_[2]);
  }
  /** Cost: build success once; nullable allocation through the snapshot's gate, no rows. */
  static void *operator new(std::size_t, const std::nothrow_t &) throw();
  /** Cost: owner deletion once; returns this object's storage to its gate, no rows. */
  static void operator delete(void *) throw();
  /** Cost: matching nothrow construction cleanup; no rows. Exceptions are disabled. */
  static void operator delete(void *, const std::nothrow_t &) throw();

private:
  friend class WebAttributedLayout;
  /** Cost: successful build once; adopts completed local buffers without walking them. */
  WebAttributedSnapshot(const loka::app::AttributedString &,
                        const loka::app::BlockStyle &,
                        short,
                        char *,
                        unsigned,
                        Fragment *,
                        Descriptor *,
                        int *,
                        unsigned,
                        short);
  const loka::app::AttributedString sourceValue_;
  const loka::app::BlockStyle block_;
  const short width_;
  char *const bytes_;
  Fragment *const fragments_;
  Descriptor *const descriptors_;
  int *const words_;
  const unsigned byteLength_, wordCount_;
  const short height_;
  WebAttributedSnapshot(const WebAttributedSnapshot &);
  WebAttributedSnapshot &operator=(const WebAttributedSnapshot &);
};
/** Synchronous producer only; WebTextLayout is the deliberate plain-Text twin. */
class WebAttributedLayout
{
public:
  /** Cost: eventual layout once per memo miss; walks supplied content and build-local tables.
      Breaker counts/fills; WORD lookahead can visit quadratic bytes. Metrics once per
      required key (at most 48), width import once per memo miss. No service is created.
      Success transfers a new value through out; caller releases its old value. Failure
      leaves out untouched. PROTOCOL asserts in debug and propagates in release. */
  static int
  build(const loka::app::AttributedString &, const loka::app::BlockStyle &, short width, WebAttributedSnapshot *&out);
};
#endif
