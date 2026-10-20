#include "ToolboxBlurKernel.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace loka::toolbox;
namespace
{
  // Mosaic oracle: ToolboxStaticBlur.cpp at 5e8f5a65, lines 127-143,
  // 214-240 and 258-266. Original loops; only CLUT access becomes RGB bytes,
  // and FilterLine indexes each sample instead of stepping past the line
  // (the same arithmetic).
  unsigned char Milk(unsigned value, unsigned resolution)
  {
    return static_cast<unsigned char>((value + (((255 - value) * 160) >> 8)) >> (8 - resolution));
  }
  /** In-place separable [1 2 1], retaining original neighbors, edge repeat. */
  void FilterLine(unsigned char *first, int count, long step)
  {
    unsigned previous = *first;
    for (int i = 0; i < count; ++i)
    {
      unsigned char *const sample = first + i * step;
      const unsigned current = *sample;
      const unsigned next = i + 1 < count ? sample[step] : current;
      *sample = static_cast<unsigned char>((previous + 2 * current + next) / 4);
      previous = current;
    }
  }

  void Legacy(unsigned char *base,
              int width,
              int height,
              long stride,
              const unsigned char *palette,
              const unsigned char *inverse,
              unsigned resolution)
  {
    const int columns = (width + 7) / 8, rows = (height + 7) / 8;
    std::vector<unsigned char> storage(columns * rows * 3);
    unsigned char *reduced = &storage[0];
    for (int by = 0; by < rows; ++by)
      for (int bx = 0; bx < columns; ++bx)
      {
        unsigned long sum[3] = {0, 0, 0};
        const int bw = width - bx * 8 < 8 ? width - bx * 8 : 8;
        const int bh = height - by * 8 < 8 ? height - by * 8 : 8;
        for (int y = 0; y < bh; ++y)
          for (int x = 0; x < bw; ++x)
          {
            const unsigned char *color = palette + 3 * base[(by * 8 + y) * stride + bx * 8 + x];
            sum[0] += color[0];
            sum[1] += color[1];
            sum[2] += color[2];
          }
        for (int c = 0; c < 3; ++c)
          reduced[(static_cast<long>(by) * columns + bx) * 3 + c] = static_cast<unsigned char>(sum[c] / (bw * bh));
      }
    for (int c = 0; c < 3; ++c)
    {
      for (int y = 0; y < rows; ++y)
        FilterLine(reduced + static_cast<long>(y) * columns * 3 + c, columns, 3);
      for (int x = 0; x < columns; ++x)
        FilterLine(reduced + x * 3 + c, rows, static_cast<long>(columns) * 3);
    }
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
      {
        const unsigned char *rgb = reduced + (static_cast<long>(y / 8) * columns + x / 8) * 3;
        const unsigned index = (static_cast<unsigned>(Milk(rgb[0], resolution)) << (2 * resolution))
                               | (static_cast<unsigned>(Milk(rgb[1], resolution)) << resolution)
                               | Milk(rgb[2], resolution);
        base[y * stride + x] = inverse[index];
      }
  }
  // Independent oracle written from the specification: direct cell sums,
  // out-of-place filtering, then four milked grid samples for each output pixel.
  void Smooth(unsigned char *base,
              int w,
              int h,
              long stride,
              const unsigned char *palette,
              int cell,
              const unsigned char *inverse,
              unsigned r)
  {
    const int W = (w + cell - 1) / cell, H = (h + cell - 1) / cell;
    std::vector<unsigned> raw(W * H * 3), horizontal(raw.size()), grid(raw.size());
    for (int j = 0; j < H; ++j)
      for (int i = 0; i < W; ++i)
        for (int k = 0; k < 3; ++k)
        {
          unsigned sum = 0, count = 0;
          for (int y = j * cell; y < std::min(h, (j + 1) * cell); ++y)
            for (int x = i * cell; x < std::min(w, (i + 1) * cell); ++x)
            {
              sum += palette[3 * base[y * stride + x] + k];
              ++count;
            }
          raw[(j * W + i) * 3 + k] = sum / count;
        }
    for (int j = 0; j < H; ++j)
      for (int i = 0; i < W; ++i)
        for (int k = 0; k < 3; ++k)
          horizontal[(j * W + i) * 3 + k] = (raw[(j * W + std::max(0, i - 1)) * 3 + k] + 2 * raw[(j * W + i) * 3 + k]
                                             + raw[(j * W + std::min(W - 1, i + 1)) * 3 + k])
                                            / 4;
    for (int j = 0; j < H; ++j)
      for (int i = 0; i < W; ++i)
        for (int k = 0; k < 3; ++k)
        {
          unsigned v = (horizontal[(std::max(0, j - 1) * W + i) * 3 + k] + 2 * horizontal[(j * W + i) * 3 + k]
                        + horizontal[(std::min(H - 1, j + 1) * W + i) * 3 + k])
                       / 4;
          grid[(j * W + i) * 3 + k] = v + ((255 - v) * 160 / 256);
        }
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
      {
        const int i = x / cell, j = y / cell;
        const int dx = 2 * (x % cell) + 1 - cell, dy = 2 * (y % cell) + 1 - cell;
        const int nx = std::max(0, std::min(W - 1, i + (dx < 0 ? -1 : 1)));
        const int ny = std::max(0, std::min(H - 1, j + (dy < 0 ? -1 : 1)));
        const unsigned wx = dx < 0 ? -dx : dx, wy = dy < 0 ? -dy : dy;
        const unsigned scale = 2 * cell;
        unsigned index = 0;
        for (int k = 0; k < 3; ++k)
        {
          const unsigned top =
              ((scale - wx) * grid[(j * W + i) * 3 + k] + wx * grid[(j * W + nx) * 3 + k] + cell) / scale;
          const unsigned bottom =
              ((scale - wx) * grid[(ny * W + i) * 3 + k] + wx * grid[(ny * W + nx) * 3 + k] + cell) / scale;
          const unsigned v = ((scale - wy) * top + wy * bottom + cell) / scale;
          index = index * (1U << r) + (v >> (8 - r));
        }
        base[y * stride + x] = inverse[index];
      }
  }
  unsigned char Random(unsigned long &seed)
  {
    seed = (seed * 1664525UL + 1013904223UL) & 0xFFFFFFFFUL;
    return static_cast<unsigned char>(seed >> 24);
  }
  struct FillSpec
  {
    unsigned resolution, low, high, firstCentre, lastCentre, step, count;
  };
  const FillSpec fillSpecs[] = {{3, 4, 7, 0x9000, 0xF000, 0x2000, 64},
                                {4, 9, 15, 0x9800, 0xF800, 0x1000, 343},
                                {5, 19, 31, 0x9C00, 0xFC00, 0x0800, 2197}};
  class RecordingQuery : public ToolboxBlurColourQuery
  {
  public:
    RecordingQuery(const FillSpec &spec, unsigned fail, unsigned pass)
        : spec_(spec),
          fail_(fail),
          pass_(pass),
          calls_(0)
    {
    }
    virtual bool indexFor(unsigned short red, unsigned short green, unsigned short blue, unsigned char &index)
    {
      const unsigned n = this->spec_.high - this->spec_.low + 1;
      const unsigned ordinal = this->calls_++;
      LOKA_VERIFY(ordinal < this->spec_.count);
      LOKA_VERIFY(red == this->spec_.firstCentre + ordinal / (n * n) * this->spec_.step);
      LOKA_VERIFY(green == this->spec_.firstCentre + (ordinal / n) % n * this->spec_.step);
      LOKA_VERIFY(blue == this->spec_.firstCentre + ordinal % n * this->spec_.step);
      if (ordinal + 1 == this->spec_.count)
        LOKA_VERIFY(red == this->spec_.lastCentre && green == red && blue == red);
      // Two ordinal digits discriminate destination-channel wiring. The third
      // pass exercises both endpoint answers even at resolution 3.
      index = static_cast<unsigned char>(this->pass_ == 0   ? ordinal % 256
                                         : this->pass_ == 1 ? ordinal / 256
                                         : ordinal % 2      ? 255
                                                            : 0);
      return ordinal != this->fail_;
    }
    unsigned calls() const
    {
      return this->calls_;
    }

  private:
    const FillSpec &spec_;
    const unsigned fail_, pass_;
    unsigned calls_;
  };
  void FillPins()
  {
    unsigned char answer = 17;
    LOKA_VERIFY(!ToolboxBlurAcceptColorIndex(-1, answer) && answer == 17);
    LOKA_VERIFY(ToolboxBlurAcceptColorIndex(0, answer) && answer == 0);
    LOKA_VERIFY(ToolboxBlurAcceptColorIndex(255, answer) && answer == 255);
    LOKA_VERIFY(!ToolboxBlurAcceptColorIndex(256, answer) && answer == 255);
    for (unsigned s = 0; s < 3; ++s)
    {
      const FillSpec &spec = fillSpecs[s];
      const unsigned side = 1U << spec.resolution;
      const long size = side * side * side;
      std::vector<unsigned char> table(size, 0xA7), previous;
      for (unsigned pass = 0; pass < 3; ++pass)
      {
        RecordingQuery query(spec, spec.count, pass);
        LOKA_VERIFY(ToolboxBlurFillInverse(&table[0], size, spec.resolution, query));
        LOKA_VERIFY(query.calls() == spec.count);
        unsigned ordinal = 0;
        for (unsigned r = 0; r < side; ++r)
          for (unsigned g = 0; g < side; ++g)
            for (unsigned b = 0; b < side; ++b)
            {
              const bool reachable = r >= spec.low && g >= spec.low && b >= spec.low;
              unsigned char expected = 0;
              if (reachable)
              {
                expected = static_cast<unsigned char>(pass == 0     ? ordinal % 256
                                                      : pass == 1   ? ordinal / 256
                                                      : ordinal % 2 ? 255
                                                                    : 0);
                ++ordinal;
              }
              LOKA_VERIFY(table[(r * side + g) * side + b] == expected);
            }
        if (pass)
          LOKA_VERIFY(table != previous);
        previous = table;
      }
      const unsigned failures[] = {0, spec.count / 2, spec.count - 1};
      for (unsigned f = 0; f < 3; ++f)
      {
        RecordingQuery query(spec, failures[f], 0);
        LOKA_VERIFY(!ToolboxBlurFillInverse(&table[0], size, spec.resolution, query));
        LOKA_VERIFY(query.calls() == failures[f] + 1);
      }
      for (unsigned bad = 0; bad < 7; ++bad)
      {
        RecordingQuery query(spec, spec.count, 0);
        std::fill(table.begin(), table.end(), 0xA7);
        const std::vector<unsigned char> saved(table);
        const unsigned r = bad == 0 ? 2 : bad == 1 ? 6 : bad == 2 ? UINT_MAX : spec.resolution;
        const long length = bad == 3 ? size - 1 : bad == 4 ? size + 1 : bad == 5 ? -1 : size;
        LOKA_VERIFY(!ToolboxBlurFillInverse(bad == 6 ? 0 : &table[0], length, r, query));
        LOKA_VERIFY(table == saved && query.calls() == 0);
      }
    }
  }
  unsigned ReachabilityPins()
  {
    unsigned cases = 0;
    for (unsigned s = 0; s < 3; ++s)
    {
      const FillSpec &spec = fillSpecs[s];
      const unsigned side = 1U << spec.resolution;
      unsigned char representatives[32] = {0};
      for (unsigned bucket = spec.low; bucket <= spec.high; ++bucket)
      {
        unsigned v = 0;
        while (v < 256 && (v + (255 - v) * 160 / 256) / (256 / side) != bucket)
          ++v;
        LOKA_VERIFY(v < 256);
        representatives[bucket] = static_cast<unsigned char>(v);
      }
      for (unsigned pass = 0; pass < 2; ++pass)
      {
        // Two base-255 digits uniquely identify the key; 255 is never an answer.
        std::vector<unsigned char> inverse(side * side * side, 255);
        for (unsigned r = spec.low; r <= spec.high; ++r)
          for (unsigned g = spec.low; g <= spec.high; ++g)
            for (unsigned b = spec.low; b <= spec.high; ++b)
            {
              const unsigned key = (r * side + g) * side + b;
              inverse[key] = static_cast<unsigned char>(pass ? key / 255 : key % 255);
            }
        for (int mode = 0; mode < 2; ++mode)
          for (int cell = 4; cell <= 8; cell += 4)
          {
            const int shapes[][2] = {
                {1, 1}, {1, 2 * cell + 1}, {2 * cell + 1, 1}, {cell, cell}, {2 * cell + 1, cell + 1}};
            for (unsigned shape = 0; shape < 5; ++shape)
            {
              const int w = shapes[shape][0], h = shapes[shape][1], stride = w + 3;
              for (unsigned r = spec.low; r <= spec.high; ++r)
                for (unsigned g = spec.low; g <= spec.high; ++g)
                  for (unsigned b = spec.low; b <= spec.high; ++b)
                  {
                    unsigned char palette[768] = {0};
                    palette[0] = representatives[r];
                    palette[1] = representatives[g];
                    palette[2] = representatives[b];
                    std::vector<unsigned char> pixels(stride * h, 0xA7);
                    for (int y = 0; y < h; ++y)
                      std::fill(pixels.begin() + y * stride, pixels.begin() + y * stride + w, 0);
                    LOKA_VERIFY(ToolboxBlurKernel(&pixels[0],
                                                  w,
                                                  h,
                                                  stride,
                                                  palette,
                                                  cell,
                                                  static_cast<ToolboxBlurEnlargement>(mode),
                                                  &inverse[0],
                                                  spec.resolution));
                    const unsigned char expected = inverse[(r * side + g) * side + b];
                    for (int y = 0; y < h; ++y)
                      for (int x = 0; x < stride; ++x)
                        LOKA_VERIFY(pixels[y * stride + x] == (x < w ? expected : 0xA7));
                    ++cases;
                  }
              // All eight constant RGB corners (including black/white), then
              // alternating extreme blocks and a spatial mix of all corners.
              for (unsigned pattern = 0; pattern < 10; ++pattern)
              {
                unsigned char palette[768] = {0};
                for (unsigned corner = 0; corner < 8; ++corner)
                  for (unsigned channel = 0; channel < 3; ++channel)
                    palette[3 * corner + channel] = (corner & (1U << channel)) ? 255 : 0;
                std::vector<unsigned char> pixels(stride * h, 0xA7);
                for (int y = 0; y < h; ++y)
                  for (int x = 0; x < w; ++x)
                    pixels[y * stride + x] = static_cast<unsigned char>(pattern < 8    ? pattern
                                                                        : pattern == 8 ? ((x / cell + y / cell) % 2) * 7
                                                                                       : (x + 3 * y) % 8);
                std::vector<unsigned char> expected(pixels);
                // The existing smooth oracle supports both cell sizes. For
                // mosaic use a scaled copy of each pixel so Legacy's c=8 agrees.
                if (mode == TOOLBOX_BLUR_SMOOTH)
                  Smooth(&expected[0], w, h, stride, palette, cell, &inverse[0], spec.resolution);
                else
                {
                  const int scale = 8 / cell, sw = w * scale, sh = h * scale;
                  std::vector<unsigned char> expanded(sw * sh);
                  for (int y = 0; y < sh; ++y)
                    for (int x = 0; x < sw; ++x)
                      expanded[y * sw + x] = pixels[(y / scale) * stride + x / scale];
                  Legacy(&expanded[0], sw, sh, sw, palette, &inverse[0], spec.resolution);
                  for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                      expected[y * stride + x] = expanded[(y * scale) * sw + x * scale];
                }
                LOKA_VERIFY(ToolboxBlurKernel(&pixels[0],
                                              w,
                                              h,
                                              stride,
                                              palette,
                                              cell,
                                              static_cast<ToolboxBlurEnlargement>(mode),
                                              &inverse[0],
                                              spec.resolution));
                LOKA_VERIFY(pixels == expected);
                for (int y = 0; y < h; ++y)
                  for (int x = 0; x < w; ++x)
                    LOKA_VERIFY(pixels[y * stride + x] != 255);
                ++cases;
              }
            }
          }
      }
    }
    return cases;
  }
} // namespace

int main()
{
  FillPins();
  const unsigned reachableCases = ReachabilityPins();
  std::printf("Reachability: %u deterministic cases passed\n", reachableCases);
  unsigned long seed = 0x18B40UL;
  unsigned char palette[768], inverse[32768];
  for (int i = 0; i < 32768; ++i)
    inverse[i] = Random(seed);
  const int shapes[][2] = {{1, 1}, {1, 29}, {29, 1}, {3, 3}, {5, 7}, {17, 9}, {220, 40}};
  unsigned cases = 0;
  for (int grey = 0; grey < 2; ++grey)
  {
    for (int i = 0; i < 768; ++i)
      palette[i] = grey ? static_cast<unsigned char>(i / 3) : Random(seed);
    for (unsigned shape = 0; shape < sizeof(shapes) / sizeof(shapes[0]); ++shape)
      for (unsigned r = 3; r <= 5; ++r)
      {
        const int w = shapes[shape][0], h = shapes[shape][1], stride = w + 7;
        std::vector<unsigned char> input(stride * h, 0xA7);
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x)
            input[y * stride + x] = Random(seed);
        std::vector<unsigned char> expected(input), actual(input);
        Legacy(&expected[0], w, h, stride, palette, inverse, r);
        LOKA_VERIFY(ToolboxBlurKernel(&actual[0], w, h, stride, palette, 8, TOOLBOX_BLUR_MOSAIC, inverse, r));
        LOKA_VERIFY(actual == expected);
        ++cases;
        for (int c = 4; c <= 8; c += 4)
        {
          actual = input;
          expected = input;
          Smooth(&expected[0], w, h, stride, palette, c, inverse, r);
          LOKA_VERIFY(ToolboxBlurKernel(&actual[0], w, h, stride, palette, c, TOOLBOX_BLUR_SMOOTH, inverse, r));
          LOKA_VERIFY(actual == expected);
          ++cases;
        }
      }
  }
  // Hand arithmetic: reduced [0,255] -> filtered [63,191] -> milk [183,231].
  // x=4 at c=4 mixes (5*231 + 3*183 + 4)/8 = 213, quantized to 26 at r=5.
  for (int i = 0; i < 768; ++i)
    palette[i] = static_cast<unsigned char>(i / 3);
  unsigned char shortRow[] = {0, 0, 0, 0, 255, 0xA7};
  LOKA_VERIFY(ToolboxBlurKernel(shortRow, 5, 1, 6, palette, 4, TOOLBOX_BLUR_SMOOTH, inverse, 5));
  LOKA_VERIFY(shortRow[4] == inverse[(26 << 10) | (26 << 5) | 26]);
  LOKA_VERIFY(shortRow[5] == 0xA7);
  unsigned char mosaic[] = {0, 0, 0, 0, 255, 0xA7};
  LOKA_VERIFY(ToolboxBlurKernel(mosaic, 5, 1, 6, palette, 4, TOOLBOX_BLUR_MOSAIC, inverse, 5));
  LOKA_VERIFY(mosaic[0] == inverse[(22 << 10) | (22 << 5) | 22]);
  LOKA_VERIFY(mosaic[4] == inverse[(28 << 10) | (28 << 5) | 28]);
  LOKA_VERIFY(mosaic[5] == 0xA7);
  for (unsigned r = 3; r <= 5; ++r)
    for (int white = 0; white < 2; ++white)
    {
      std::fill(palette, palette + 768, static_cast<unsigned char>(white ? 255 : 71));
      std::vector<unsigned char> a(17 * 9, 0), b(a);
      LOKA_VERIFY(ToolboxBlurKernel(&a[0], 17, 9, 17, palette, 8, TOOLBOX_BLUR_MOSAIC, inverse, r));
      LOKA_VERIFY(ToolboxBlurKernel(&b[0], 17, 9, 17, palette, 8, TOOLBOX_BLUR_SMOOTH, inverse, r));
      LOKA_VERIFY(a == b);
      if (white)
      {
        const unsigned t = 255 >> (8 - r);
        for (unsigned i = 0; i < a.size(); ++i)
          LOKA_VERIFY(a[i] == inverse[(t << (2 * r)) | (t << r) | t]);
      }
    }
  // Invalid inputs are refused before any write, including all sentinel bytes.
  for (int test = 0; test < 13; ++test)
  {
    std::vector<unsigned char> bytes(32, 0xA7), saved(bytes);
    int w = 4, h = 2, c = 8;
    unsigned r = 3;
    long stride = 8;
    unsigned char *base = &bytes[0];
    const unsigned char *pal = palette, *inv = inverse;
    switch (test)
    {
    case 0:
      w = 0;
      break;
    case 1:
      h = 0;
      break;
    case 2:
      stride = 3;
      break;
    case 3:
      c = 5;
      break;
    case 4:
      r = 2;
      break;
    case 5:
      r = 6;
      break;
    case 6:
      base = 0;
      break;
    case 7:
      pal = 0;
      break;
    case 8:
      inv = 0;
      break;
    case 9:
      stride = LONG_MAX;
      break;
    case 10:
      w = -1;
      break;
    case 11:
      h = -1;
      break;
    case 12:
      stride = -1;
      break;
    }
    LOKA_VERIFY(!ToolboxBlurKernel(base, w, h, stride, pal, c, TOOLBOX_BLUR_MOSAIC, inv, r));
    LOKA_VERIFY(bytes == saved);
  }
  LOKA_VERIFY(ToolboxBlurEnlargementFor(loka::platform::CPU_CLASS_68K) == TOOLBOX_BLUR_MOSAIC);
  LOKA_VERIFY(ToolboxBlurEnlargementFor(loka::platform::CPU_CLASS_PPC_60X) == TOOLBOX_BLUR_SMOOTH);
  LOKA_VERIFY(ToolboxBlurEnlargementFor(loka::platform::CPU_CLASS_G3_OR_FASTER) == TOOLBOX_BLUR_SMOOTH);
  std::printf("Blur kernel: %u oracle cases plus hand, validation and policy pins passed\n", cases);
  return 0;
}
