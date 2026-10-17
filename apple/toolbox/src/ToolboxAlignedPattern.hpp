#ifndef LOKA_TOOLBOX_ALIGNED_PATTERN_HPP
#define LOKA_TOOLBOX_ALIGNED_PATTERN_HPP

#include <Quickdraw.h>
#include <cstddef>

namespace loka
{
  namespace toolbox
  {
    /** A QuickDraw Pattern whose address is even. The 68000 ROM copies patterns
        with long moves and faults on odd addresses; Pattern alone is byte-aligned (#1398). */
    union ToolboxAlignedPattern
    {
      Pattern pattern;
      long alignment; // Never read; supplies long alignment (2 on 68K).
    };

    struct ToolboxPatternAlignmentProbe
    {
      char c;
      ToolboxAlignedPattern pattern;
    };
    typedef char ToolboxPatternMustBeWordAligned[offsetof(ToolboxPatternAlignmentProbe, pattern) >= 2 ? 1 : -1];
  } // namespace toolbox
} // namespace loka

#endif
