#include "app/style/Style.hpp"

#define LOKA_STYLE_VOCAB_DEFINE_STORAGE
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_STORAGE

namespace loka
{
  namespace app
  {
    // One byte per optional axis keeps Classic Segment size identical on every ABI,
    // including PowerPC Darwin where bool is four bytes.
    typedef char TextStyleSizePin[sizeof(TextStyle) == 12 ? 1 : -1];
    // Page 1: one byte per toggle keeps the Classic Segment size budget.
    typedef char TextToggleValueSizePin[sizeof(TextToggleValue) == 1 ? 1 : -1];

    typedef char TextWeightValueSizePin[sizeof(TextWeightValue) == 1 ? 1 : -1];

    int detail::SnapTextSizePoints(int logicalUnits)
    {
      int selected = detail::StyleVocabularySizes[0];
      if (logicalUnits > selected)
      {
        for (int i = 1; i < detail::StyleVocabularySizeCount; ++i)
        {
          const int candidate = detail::StyleVocabularySizes[i];
          if (logicalUnits >= candidate)
          {
            selected = candidate;
            continue;
          }
          if (logicalUnits - selected > candidate - logicalUnits)
          {
            selected = candidate;
          }
          break;
        }
      }
      return selected;
    }

    TextStyle SizeOf(int logicalUnits)
    {
      return TextStyle(TextSizeValue::points(detail::SnapTextSizePoints(logicalUnits)));
    }

  } // namespace app
} // namespace loka
