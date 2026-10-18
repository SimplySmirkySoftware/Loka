#ifndef LOKA_APP_STYLE_STACK_BLUR_HPP
#define LOKA_APP_STYLE_STACK_BLUR_HPP
#include <cstddef>
namespace loka
{
  namespace app
  {
    /** Scratch bytes for width x height, or zero for nonpositive dimensions,
        dimensions exceeding safe int indexing, or byte-count overflow. O(1). */
    size_t StackBlurScratchBytes(int width, int height);

    /** Blur premultiplied RGBA8 channel-wise in place, with replicated edges
        and triangular weights. Radius in pixels clamps to 1..32. Each axis
        truncates to bytes. Caller owns rowBytes * height writable pixel bytes
        and a separate, non-overlapping scratch buffer of the queried size.
        Refuses invalid dimensions, null buffers, short scratch, short stride,
        or stride-size overflow without writing. Padding bytes are untouched.
        Allocates nothing and retains nothing. Per caller-requested blur,
        O(width * height + radius * (width + height)), walking caller pixels. */
    bool StackBlur(unsigned char *pixels,
                   int width,
                   int height,
                   size_t rowBytes,
                   int radius,
                   unsigned char *scratch,
                   size_t scratchBytes);
  } // namespace app
} // namespace loka
#endif
