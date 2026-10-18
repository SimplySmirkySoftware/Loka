#include "StackBlurTests.hpp"
#include "support/TestVerify.hpp"
#include "app/style/StackBlur.hpp"
#include <climits>
#include <cstring>
#include <vector>

namespace
{
  // Direct convolution, deliberately no running sums or production helpers.
  void reference(std::vector<unsigned char> &pixels, int w, int h, int stride, int radius)
  {
    const int r = radius <= 0 ? 1 : radius >= 32 ? 32 : radius;
    for (int axis = 0; axis != 2; ++axis)
    {
      const std::vector<unsigned char> input(pixels);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          for (int c = 0; c < 4; ++c)
          {
            unsigned int sum = 0;
            for (int k = -r; k <= r; ++k)
            {
              int sx = x + (axis == 0 ? k : 0);
              int sy = y + (axis == 1 ? k : 0);
              if (sx < 0)
                sx = 0;
              if (sx >= w)
                sx = w - 1;
              if (sy < 0)
                sy = 0;
              if (sy >= h)
                sy = h - 1;
              sum += input[sy * stride + sx * 4 + c] * (r + 1 - (k < 0 ? -k : k));
            }
            pixels[y * stride + x * 4 + c] = static_cast<unsigned char>(sum / ((r + 1) * (r + 1)));
          }
    }
  }
} // namespace

void testStackBlurIndependentReference()
{
  const int sizes[] = {1, 2, 3, 7, 35};
  const int radii[] = {INT_MIN, -1, 0, 1, 2, 3, 7, 16, 31, 32, 33, INT_MAX};
  for (unsigned wi = 0; wi < sizeof(sizes) / sizeof(sizes[0]); ++wi)
    for (unsigned hi = 0; hi < sizeof(sizes) / sizeof(sizes[0]); ++hi)
      for (unsigned ri = 0; ri < sizeof(radii) / sizeof(radii[0]); ++ri)
        for (int padding = 0; padding <= 5; padding += 5)
        {
          const int w = sizes[wi], h = sizes[hi], stride = w * 4 + padding;
          std::vector<unsigned char> pixels(stride * h + 1, 213);
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
              const unsigned char alpha = static_cast<unsigned char>((x * 37 + y * 73 + 19) % 256);
              for (int c = 0; c < 3; ++c)
                pixels[y * stride + x * 4 + c] = static_cast<unsigned char>((x * 91 + y * 43 + c * 61) % (alpha + 1));
              pixels[y * stride + x * 4 + 3] = alpha;
            }
          std::vector<unsigned char> expected(pixels);
          reference(expected, w, h, stride, radii[ri]);
          const size_t bytes = loka::app::StackBlurScratchBytes(w, h);
          LOKA_VERIFY(bytes == static_cast<size_t>(w * h * 4));
          std::vector<unsigned char> scratch(bytes + 2, 177);
          LOKA_VERIFY(loka::app::StackBlur(&pixels[0], w, h, stride, radii[ri], &scratch[1], bytes));
          LOKA_VERIFY(pixels == expected);
          LOKA_VERIFY(scratch.front() == 177 && scratch.back() == 177);
        }
}

void testStackBlurRefusals()
{
  using namespace loka::app;
  unsigned char pixels[16], scratch[16];
  std::memset(pixels, 91, sizeof(pixels));
  std::memset(scratch, 37, sizeof(scratch));
  const int bad[][2] = {{0, 1}, {1, 0}, {-1, 1}, {1, -1}, {INT_MAX, 1}, {1, INT_MAX}, {INT_MAX, INT_MAX}};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
  {
    LOKA_VERIFY(StackBlurScratchBytes(bad[i][0], bad[i][1]) == 0);
    LOKA_VERIFY(!StackBlur(pixels, bad[i][0], bad[i][1], 8, 2, scratch, sizeof(scratch)));
  }
  LOKA_VERIFY(!StackBlur(0, 2, 2, 8, 2, scratch, sizeof(scratch)));
  LOKA_VERIFY(!StackBlur(pixels, 2, 2, 8, 2, 0, sizeof(scratch)));
  LOKA_VERIFY(!StackBlur(pixels, 2, 2, 8, 2, scratch, 15));
  LOKA_VERIFY(!StackBlur(pixels, 2, 2, 7, 2, scratch, sizeof(scratch)));
  LOKA_VERIFY(!StackBlur(pixels, 2, 2, static_cast<size_t>(-1), 2, scratch, sizeof(scratch)));
  for (unsigned i = 0; i < sizeof(pixels); ++i)
  {
    LOKA_VERIFY(pixels[i] == 91);
    LOKA_VERIFY(scratch[i] == 37);
  }
}
