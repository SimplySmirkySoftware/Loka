#include "ToolboxCharWidthCache.hpp"
#include "core/LokaAlloc.hpp"
#include <Script.h>
#include <LowMem.h>
#include <climits>
#include <cassert>

namespace
{
  loka::core::LokaAllocationSite WidthSite()
  {
    return loka::core::LokaAllocationSite("ToolboxCharWidthCache", "ToolboxCharWidths");
  }
  loka::core::LokaAllocationSite SlotSite()
  {
    return loka::core::LokaAllocationSite("ToolboxCharWidthCache", "Slot");
  }
  bool Admits(ToolboxCharWidthKind kind, unsigned byte)
  {
    switch (kind)
    {
    case TOOLBOX_CHAR_ROMAN:
      return byte >= 0x20 && byte <= 0xFF && byte != 0x7F;
    case TOOLBOX_CHAR_JAPANESE_ASCII:
      return byte >= 0x20 && byte <= 0x7E;
    case TOOLBOX_CHAR_REFUSED:
      return false;
    }
    return false;
  }
} // namespace

bool ToolboxCharWidthEnvironment(GrafPtr port)
{
  return port && port->spExtra == 0 && !LMGetFractEnable()
         && ((port->portBits.rowBytes & 0xC000) != 0xC000 || reinterpret_cast<CGrafPtr>(port)->chExtra == 0)
         && port->grafProcs == 0;
}

ToolboxCharWidthKind ToolboxClassifyCharWidth(const ToolboxTextSelection &selection)
{
  const short script = FontToScript(selection.nativeFont());
  if (GetScriptManagerVariable(smFontForce))
    return TOOLBOX_CHAR_REFUSED;
  if (script == smRoman)
    return TOOLBOX_CHAR_ROMAN;
  if (script == smJapanese)
    return TOOLBOX_CHAR_JAPANESE_ASCII;
  return TOOLBOX_CHAR_REFUSED;
}

bool ToolboxCharWidthBytes(ToolboxCharWidthKind kind, const unsigned char *text)
{
  for (unsigned i = 1; i <= text[0]; ++i)
    if (!Admits(kind, text[i]))
      return false;
  return true;
}

ToolboxCharWidths::ToolboxCharWidths(const unsigned char *bytes, const short *positions, unsigned count)
{
  for (unsigned i = 0; i < 256; ++i)
    this->widths_[i] = 0;
  for (unsigned i = 0; i < count; ++i)
    this->widths_[bytes[i]] = static_cast<short>(static_cast<long>(positions[i + 1]) - positions[i]);
}

bool ToolboxCharWidths::sum(const unsigned char *text, short &width) const
{
  long total = 0;
  for (unsigned i = 1; i <= text[0]; ++i)
    total += this->widths_[text[i]];
  if (total < SHRT_MIN || total > SHRT_MAX)
    return false;
  width = static_cast<short>(total);
  return true;
}

ToolboxCharWidthCache::Slot::Slot(const ToolboxTextSelection &key,
                                  ToolboxCharWidthKind kind,
                                  const ToolboxCharWidths *widths)
    : key(key),
      kind(kind),
      widths(widths)
{
}
ToolboxCharWidthCache::ToolboxCharWidthCache()
    : used_(0)
{
}
ToolboxCharWidthCache::~ToolboxCharWidthCache()
{
  for (unsigned i = 0; i < this->used_; ++i)
  {
    const Slot *entry = this->slots_[i];
    loka::core::LokaDelete(const_cast<ToolboxCharWidths *>(entry->widths), WidthSite());
    loka::core::LokaDelete(const_cast<Slot *>(entry), SlotSite());
  }
}
const ToolboxCharWidthCache::Slot *ToolboxCharWidthCache::find(const ToolboxTextSelection &key) const
{
  for (unsigned i = 0; i < this->used_; ++i)
    if (this->slots_[i]->key == key)
      return this->slots_[i];
  return 0;
}
void ToolboxCharWidthCache::refuse(const ToolboxTextSelection &key)
{
  if (this->full() || this->find(key))
    return;
  const Slot *entry =
      loka::core::LokaNew<Slot>(SlotSite(), key, TOOLBOX_CHAR_REFUSED, static_cast<const ToolboxCharWidths *>(0));
  if (entry)
    this->slots_[this->used_++] = entry;
}
const ToolboxCharWidthCache::Slot *ToolboxCharWidthCache::build(const ToolboxTextSelection &key,
                                                                ToolboxCharWidthKind kind,
                                                                const ToolboxTextMeasureScope &selected)
{
  assert(kind == TOOLBOX_CHAR_ROMAN || kind == TOOLBOX_CHAR_JAPANESE_ASCII);
  assert(selected.valid());
  assert(selected.applies(key));
  (void)selected;
  const Slot *existing = this->find(key);
  if (existing)
    return existing->widths ? existing : 0;
  if (this->full())
    return 0;
  unsigned char bytes[255];
  unsigned count = 0;
  for (unsigned byte = 0; byte < 256; ++byte)
    if (Admits(kind, byte))
      bytes[count++] = static_cast<unsigned char>(byte);
  short locs[256];
  for (unsigned i = 0; i < 256; ++i)
    locs[i] = -1;
  MeasureText(static_cast<short>(count), reinterpret_cast<char *>(bytes), reinterpret_cast<char *>(locs));
  if (locs[0] != 0)
  {
    this->refuse(key);
    return 0;
  }
  for (unsigned i = 0; i < count; ++i)
  {
    const long delta = static_cast<long>(locs[i + 1]) - locs[i];
    if (locs[i] == -1 || locs[i + 1] == -1 || delta < 0 || delta > SHRT_MAX)
    {
      this->refuse(key);
      return 0;
    }
  }
  ToolboxCharWidths *widths = loka::core::LokaNew<ToolboxCharWidths>(WidthSite(), bytes, locs, count);
  if (!widths)
    return 0;
  const Slot *entry = loka::core::LokaNew<Slot>(SlotSite(), key, kind, widths);
  if (!entry)
  {
    loka::core::LokaDelete(widths, WidthSite());
    return 0;
  }
  this->slots_[this->used_++] = entry;
  return entry;
}
