#include "app/style/Style.hpp"

#define LOKA_STYLE_VOCAB_DEFINE_STORAGE
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_STORAGE

namespace loka
{
  namespace app
  {
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
