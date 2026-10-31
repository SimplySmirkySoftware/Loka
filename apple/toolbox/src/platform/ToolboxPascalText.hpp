#ifndef LOKA_TOOLBOX_PASCAL_TEXT_HPP
#define LOKA_TOOLBOX_PASCAL_TEXT_HPP

#include <Quickdraw.h>
#include <cassert>
#include "core/String.hpp"
#include "app/layout/TextLineBreaker.hpp"

/** One strict UTF-8 scalar, or one malformed byte represented by '?'.
    consumed counts input bytes; native is the T1 Roman/ASCII projection. */
struct ToolboxTextUnit
{
  unsigned long scalar;
  std::size_t consumed;
  unsigned char native;
};

namespace loka
{
  namespace toolbox
  {
    namespace detail
    {
      ToolboxTextUnit DecodeToolboxTextUnit(const char *input, std::size_t remaining, bool roman);
    }
  } // namespace toolbox
} // namespace loka

/** Read one unit from a nonempty counted input; never join malformed bytes. */
#if defined(__GNUC__)
// Under -Os the encoder's loop otherwise calls an out-of-line copy (E1-2, maciix).
inline ToolboxTextUnit ToolboxNextTextUnit(const char *, std::size_t, bool) __attribute__((always_inline));
#endif
inline ToolboxTextUnit ToolboxNextTextUnit(const char *input, std::size_t remaining, bool roman)
{
  assert(input && remaining);
  const unsigned char byte = static_cast<unsigned char>(input[0]);
  // Keep all three fields aligned with DecodeScalar's ASCII branch in the .cpp.
  if (byte < 0x80)
  {
    const ToolboxTextUnit unit = {byte, 1, byte};
    return unit;
  }
  return loka::toolbox::detail::DecodeToolboxTextUnit(input, remaining, roman);
}

/** Owned, counted native projection. Presence certifies the complete logical
    snapshot. Geometry is deliberately outside this value. Boundary queries
    use native byte offsets; callers must not infer character counts from them.
    The system script is sampled once per build (Roman, otherwise ASCII). */
class ToolboxNativeText
{
public:
  bool build(const loka::core::String &value);
  void clear();
  bool valid() const { return this->bytes_.valid(); }
  bool matches(const loka::core::String &value) const
  { return this->valid() && this->source_.equals(value); }
  std::size_t size() const { return this->bytes_.size(); }
  const unsigned char *data() const { return this->size() ? &this->bytes_[0] : 0; }
  /** Adjacent unit boundaries, given a boundary within the completed value. */
  std::size_t next(std::size_t offset) const;
  std::size_t previous(std::size_t end) const;
  /** Largest whole-unit prefix within a native-byte budget. */
  std::size_t cappedEnd(std::size_t budget) const;
  /** Copy an already bounded native range to a Pascal scratch buffer. */
  void copyPascal(std::size_t start, std::size_t end, Str255 out) const;

private:
  loka::app::detail::TextMeasureTable<unsigned char> bytes_;
  loka::core::String source_;
};

/** Capped door over the counted projection, at most 255 native bytes.
    Collection/allocation refusal leaves out[0] zero.
    Input bytes and out must not overlap. */
bool ToolboxEncodePascal(const loka::core::String &value, Str255 out);

/** Decode counted native bytes literally, sampling the system script once.
    Roman accepts all bytes; other scripts accept ASCII only. Empty input
    succeeds; null nonempty input refuses. Refusal leaves out untouched.
    Embedded NULs are preserved; allocation failure follows the String contract. */
bool ToolboxDecodeNative(const unsigned char *bytes, std::size_t length, loka::core::String &out);

/** Display-only chosen-name projection: Roman decodes every byte; other scripts
    preserve ASCII and replace each high byte with question mark. Never identity. */
loka::core::String ToolboxChosenFileDisplayName(const unsigned char *bytes, std::size_t length);

#endif
