#include "ToolboxCharWidthCache.hpp"
#include "core/LokaAlloc.hpp"
#include <Script.h>
#include <LowMem.h>
#include <climits>
#include <cassert>

namespace
{
  // Japanese ASCII is the first 95 bytes of the same canonical sequence.
  const unsigned char AdmittedBytes[] = {
      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
      0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
      0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
      0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,
      0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92,
      0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
      0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
      0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
      0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
      0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
      0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
  };

  loka::core::LokaAllocationSite EntrySite()
  {
    return loka::core::LokaAllocationSite("ToolboxCharWidthCache", "Entry");
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

bool ToolboxCharWidths::build(ToolboxCharWidthKind kind, const ToolboxTextMeasureScope &selected)
{
  assert(selected.valid());
  (void)selected;
  const unsigned count = kind == TOOLBOX_CHAR_ROMAN ? sizeof(AdmittedBytes) : 0x7E - 0x20 + 1;
  short *locs = this->widths_ + 0x20;
  for (unsigned i = 0; i <= count; ++i)
    locs[i] = -1;
  // Classic's Ptr signature is not const-correct; MeasureText only reads text.
  MeasureText(static_cast<short>(count),
              reinterpret_cast<char *>(const_cast<unsigned char *>(AdmittedBytes)),
              reinterpret_cast<char *>(locs));
  if (locs[0] != 0)
    return false;
  // Descend: byte[i] is 0x20+i before DEL, 0x21+i after it. Each
  // destination is at or above pos[i]; all still-unread positions are below
  // pos[i]. Carry pos[i] for the next delta before overwriting it. Neither
  // ASCII nor Roman's DEL gap destroys input.
  short next = locs[count];
  for (unsigned remaining = count; remaining > 0; --remaining)
  {
    const unsigned i = remaining - 1;
    const short current = locs[i];
    const long delta = static_cast<long>(next) - current;
    if (current == -1 || next == -1 || delta < 0 || delta > SHRT_MAX)
      return false;
    this->widths_[AdmittedBytes[i]] = static_cast<short>(delta);
    next = current;
  }
  return true;
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
ToolboxCharWidthCache::Entry::Entry(const ToolboxTextSelection &key, ToolboxCharWidthKind kind)
    : slot(key, kind, &this->table)
{
}
ToolboxCharWidthCache::StoredSlot::StoredSlot()
    : entry(0),
      refusal(0)
{
}
ToolboxCharWidthCache::StoredSlot::~StoredSlot()
{
  loka::core::LokaDelete(this->entry, EntrySite());
  loka::core::LokaDelete(this->refusal, SlotSite());
}
const ToolboxCharWidthCache::Slot *ToolboxCharWidthCache::StoredSlot::view() const
{
  return this->entry ? &this->entry->slot : this->refusal;
}
ToolboxCharWidthCache::~ToolboxCharWidthCache() {}
const ToolboxCharWidthCache::Slot *ToolboxCharWidthCache::find(const ToolboxTextSelection &key) const
{
  for (unsigned i = 0; i < this->used_; ++i)
    if (this->slots_[i].view()->key == key)
      return this->slots_[i].view();
  return 0;
}
void ToolboxCharWidthCache::refuse(const ToolboxTextSelection &key)
{
  if (this->full() || this->find(key))
    return;
  Slot *entry =
      loka::core::LokaNew<Slot>(SlotSite(), key, TOOLBOX_CHAR_REFUSED, static_cast<const ToolboxCharWidths *>(0));
  if (entry)
    this->slots_[this->used_++].refusal = entry;
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
  Entry *entry = loka::core::LokaNew<Entry>(EntrySite(), key, kind);
  if (!entry)
    return 0;
  if (!entry->table.build(kind, selected))
  {
    loka::core::LokaDelete(entry, EntrySite());
    this->refuse(key);
    return 0;
  }
  this->slots_[this->used_++].entry = entry;
  return &entry->slot;
}
