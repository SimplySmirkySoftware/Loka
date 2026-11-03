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
  ToolboxCharWidths(const unsigned char *bytes, const short *positions, unsigned count);
  bool sum(const unsigned char *text, short &width) const;

private:
  ToolboxCharWidths(const ToolboxCharWidths &);
  ToolboxCharWidths &operator=(const ToolboxCharWidths &);
  short widths_[256];
};

/** App-owned completed tables; no eviction or resume clearing.
    Font resources and replacements, font-number aliases (0 and 1), font forcing,
    the system script, and the Font Manager/QuickDraw measuring implementation
    must stay unchanged for one App run. Guards protect queries, not previously
    derived geometry. Ambient selection changes still require an input mark.
    Used slots form one prefix: admitted kinds have a table, REFUSED has none.
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
  const Slot *slots_[8];
  unsigned used_;
};

bool ToolboxCharWidthEnvironment(GrafPtr port);
ToolboxCharWidthKind ToolboxClassifyCharWidth(const ToolboxTextSelection &selection);
bool ToolboxCharWidthBytes(ToolboxCharWidthKind kind, const unsigned char *text);

#endif
