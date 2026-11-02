#ifndef LOKA_TOOLBOX_UNICODE_PROJECTION_HPP
#define LOKA_TOOLBOX_UNICODE_PROJECTION_HPP
#include "app/nodes/controls/TextEditor.hpp"
#include "core/LokaAlloc.hpp"
#include "core/TextUnits.hpp"
#include <algorithm>

/** Context-owned, bounded logical bytes. No allocation during capture. */
class UnicodeLogicalText
{
public:
  UnicodeLogicalText()
      : bytes_(0),
        length_(0),
        capacity_(0)
  {
  }
  ~UnicodeLogicalText()
  {
    this->clear();
  }
  bool reserve(std::size_t capacity)
  {
    if (this->bytes_)
      return this->capacity_ == capacity;
    this->bytes_ = static_cast<char *>(loka::core::LokaAllocRaw(capacity ? capacity : 1, site()));
    if (!this->bytes_)
      return false;
    this->capacity_ = capacity;
    return true;
  }
  bool hasCapacity(std::size_t capacity) const
  {
    return this->bytes_ && this->capacity_ == capacity;
  }
  void clear()
  {
    loka::core::LokaFreeRaw(this->bytes_, site());
    this->bytes_ = 0;
    this->length_ = this->capacity_ = 0;
  }
  void swap(UnicodeLogicalText &other)
  {
    std::swap(this->bytes_, other.bytes_);
    std::swap(this->length_, other.length_);
    std::swap(this->capacity_, other.capacity_);
  }
  const char *bytes() const
  {
    return this->bytes_;
  }
  std::size_t length() const
  {
    return this->length_;
  }
  loka::app::EditorResult project(loka::app::TextEditorDocument &document)
  {
    return document.project(this->bytes_, this->capacity_, this->length_);
  }
  loka::app::EditorResult convert(const unsigned short *units, std::size_t count, std::size_t *offsets)
  {
    // Distinguish malformed input from a valid encoding exceeding the byte cap.
    // Use the same strict encoder for preflight; no second surrogate policy.
    std::size_t required = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
      std::size_t width = 1;
      if (units[i] >= 0xD800 && units[i] <= 0xDBFF && i + 1 < count)
        width = 2;
      else if (units[i] == '\r' && i + 1 < count && units[i + 1] == '\n')
        width = 2;
      char scratch[4];
      std::size_t written = 0;
      if (!loka::core::Utf16ToUtf8(units + i, width, scratch, sizeof(scratch), written, 0, 0))
        return loka::app::EDITOR_NON_ASCII;
      required += written;
      i += width - 1;
    }
    if (required > this->capacity_)
      return loka::app::EDITOR_CAPACITY;
    return loka::core::Utf16ToUtf8(units, count, this->bytes_, this->capacity_, this->length_, offsets, 4)
               ? loka::app::EDITOR_OK
               : loka::app::EDITOR_NON_ASCII;
  }

private:
  UnicodeLogicalText(const UnicodeLogicalText &);
  UnicodeLogicalText &operator=(const UnicodeLogicalText &);
  static loka::core::LokaAllocationSite site()
  {
    return loka::core::LokaAllocationSite("ToolboxUnicodeProjection", "LogicalText");
  }
  char *bytes_;
  std::size_t length_, capacity_;
};

/** Platform twin of Win32 Projection: native success alone publishes the stamp. */
class ToolboxUnicodeProjection
{
public:
  class Preparation;
  struct Stamp
  {
    loka::app::TextBuffer *owner;
    loka::core::ListRevision revision;
    unsigned long changes;
    Stamp()
        : owner(0),
          revision(),
          changes(0)
    {
    }
    explicit Stamp(const loka::app::TextEditorNode &node)
        : owner(node.props.buffer_),
          revision(owner->lines().revision().get()),
          changes(0)
    {
    }
  };
  bool hasCapacity(std::size_t capacity) const
  {
    return this->committed_.hasCapacity(capacity) && this->candidate_.hasCapacity(capacity);
  }
  /** Allocate an empty presentation's pair only; resizing requires Preparation. */
  bool reserve(std::size_t capacity)
  {
    if (this->committed_.bytes() || this->candidate_.bytes())
      return this->hasCapacity(capacity);
    UnicodeLogicalText committed, candidate;
    if (!committed.reserve(capacity) || !candidate.reserve(capacity))
      return false;
    this->committed_.swap(committed);
    this->candidate_.swap(candidate);
    return true;
  }
  void swap(ToolboxUnicodeProjection &other)
  {
    this->committed_.swap(other.committed_);
    this->candidate_.swap(other.candidate_);
    std::swap(this->stamp_, other.stamp_);
  }
  bool current(const loka::app::TextEditorNode &node) const
  {
    if (!this->stamp_.owner || this->stamp_.owner != node.props.buffer_
        || this->stamp_.revision != this->stamp_.owner->lines().revision().get())
      return false;
    loka::core::StateTracker *tracker = 0;
    const loka::core::ListEditResult ready = this->stamp_.owner->lines().queryMutationTracker(tracker);
    return (ready == loka::core::EDIT_OK || ready == loka::core::EDIT_REENTRANT)
           && node.props.cursorUsesTracker(tracker);
  }
  loka::app::EditorResult
  stage(loka::app::TextEditorNode &node, const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key, Stamp &taken)
  {
    taken = Stamp(node);
    return this->candidate_.project(node.seam(key));
  }
  void publish(const Stamp &stamp)
  {
    this->committed_.swap(this->candidate_);
    this->stamp_ = stamp;
  }
  void clear()
  {
    this->committed_.clear();
    this->candidate_.clear();
    this->stamp_ = Stamp();
  }
  const Stamp &stamp() const
  {
    return this->stamp_;
  }
  const UnicodeLogicalText &committed() const
  {
    return this->committed_;
  }
  const UnicodeLogicalText &candidate() const
  {
    return this->candidate_;
  }
  loka::app::EditorResult convert(const unsigned short *units, std::size_t count, std::size_t *offsets)
  {
    return this->candidate_.convert(units, count, offsets);
  }

private:
  UnicodeLogicalText committed_, candidate_;
  Stamp stamp_;
};
/** Stack-local projection transaction. Ordinary projection reuses the installed
    pair. A changed capacity prepares a complete replacement pair; native refusal
    drops it, and publication alone replaces the installed bytes and stamp. */
class ToolboxUnicodeProjection::Preparation
{
public:
  Preparation(ToolboxUnicodeProjection &installed, std::size_t capacity)
      : installed_(installed),
        replacement_(),
        target_(installed.hasCapacity(capacity) ? &installed : &this->replacement_)
  {
  }
  loka::app::EditorResult
  stage(loka::app::TextEditorNode &node, const loka::app::scene::SeamKey<loka::app::TextEditorNode> &key, Stamp &taken)
  {
    return this->target_->reserve(node.props.buffer_->capacity().bytes) ? this->target_->stage(node, key, taken)
                                                                        : loka::app::EDITOR_ALLOCATION;
  }
  const UnicodeLogicalText &candidate() const
  {
    return this->target_->candidate();
  }
  void publish(const Stamp &stamp)
  {
    this->target_->publish(stamp);
    if (this->target_ != &this->installed_)
      this->installed_.swap(*this->target_);
  }

private:
  Preparation(const Preparation &);
  Preparation &operator=(const Preparation &);
  ToolboxUnicodeProjection &installed_;
  ToolboxUnicodeProjection replacement_;
  ToolboxUnicodeProjection *const target_;
};
#endif
