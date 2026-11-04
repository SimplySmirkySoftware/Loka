#ifndef LOKA_TOOLBOX_CHAR_WIDTH_CACHE_HPP
#define LOKA_TOOLBOX_CHAR_WIDTH_CACHE_HPP

#include "context/ToolboxLayoutUtil.hpp"

/** The two admitted byte domains, and a terminal refusal for this App run. */
enum ToolboxCharWidthKind
{
  TOOLBOX_CHAR_ROMAN,
  TOOLBOX_CHAR_JAPANESE_ASCII,
  TOOLBOX_CHAR_REFUSED
};

/** Completed character advances with no mutation or copying interface. */
class ToolboxCharWidths
{
public:
  /** Call only after the door has admitted every byte for this table's kind. */
  bool sum(const unsigned char *text, short &width) const;

private:
  ToolboxCharWidths(const ToolboxCharWidths &);
  ToolboxCharWidths &operator=(const ToolboxCharWidths &);
  friend class ToolboxCharWidthCache;
  ToolboxCharWidths() {}
  bool build(ToolboxCharWidthKind kind, const ToolboxTextMeasureScope &selected);
  // Non-admitted entries are never read: the door checks the whole byte range.
  short widths_[257];
};

/** App-owned completed tables; no eviction or resume clearing.
    Font resources and replacements, font-number aliases (0 and 1), font forcing,
    the system script, and the Font Manager/QuickDraw measuring implementation
    must stay unchanged for one App run. Guards protect queries, not previously
    derived geometry. Ambient selection changes still require an input mark.
    Used slots form one prefix. Each owns either one admitted Entry (key, kind
    and table together) or one small refused Slot, never both. The returned
    Slot is a read-only view; no owned pointer crosses the cache boundary.
    Allocation refusal records nothing; a later query may try again. */
class ToolboxCharWidthCache
{
public:
  struct Slot
  {
    const ToolboxTextSelection key;
    const ToolboxCharWidthKind kind;
    const ToolboxCharWidths *const widths;

    Slot(const ToolboxTextSelection &key, ToolboxCharWidthKind kind, const ToolboxCharWidths *widths);
  };
  ToolboxCharWidthCache();
  ~ToolboxCharWidthCache();
  const Slot *find(const ToolboxTextSelection &key) const;
  bool full() const
  {
    return this->used_ == 8;
  }
  /** Records a terminal refusal if absent and capacity/allocation permit. */
  void refuse(const ToolboxTextSelection &key);
  /** The scope must currently apply key. Validation refusal records REFUSED;
      allocation refusal records nothing. Returned slots are borrowed. */
  const Slot *
  build(const ToolboxTextSelection &key, ToolboxCharWidthKind kind, const ToolboxTextMeasureScope &selected);

private:
  ToolboxCharWidthCache(const ToolboxCharWidthCache &);
  ToolboxCharWidthCache &operator=(const ToolboxCharWidthCache &);
  /** Private construction phase; publication follows successful table build. */
  struct Entry
  {
    ToolboxCharWidths table;
    const Slot slot;
    Entry(const ToolboxTextSelection &key, ToolboxCharWidthKind kind);
  };
  /** Typed ownership avoids a kind-tagged downcast during lookup or deletion. */
  struct StoredSlot
  {
    Entry *entry;
    Slot *refusal;
    StoredSlot();
    ~StoredSlot();
    const Slot *view() const;

  private:
    StoredSlot(const StoredSlot &);
    StoredSlot &operator=(const StoredSlot &);
  };
  StoredSlot slots_[8];
  unsigned used_;
};

bool ToolboxCharWidthEnvironment(GrafPtr port);
ToolboxCharWidthKind ToolboxClassifyCharWidth(const ToolboxTextSelection &selection);
bool ToolboxCharWidthBytes(ToolboxCharWidthKind kind, const unsigned char *text);

#endif
