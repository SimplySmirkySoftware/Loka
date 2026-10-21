#include "ToolboxBlurKernel.hpp"
#include <climits>
#include <cassert>
#include <new>

namespace
{
  /** All workspace is acquired before enlargement can change the image. */
  class Scratch
  {
  public:
    Scratch(long gridSize, long rowSize)
        : grid(new(std::nothrow) unsigned char[gridSize]),
          rows(rowSize ? new(std::nothrow) unsigned char[rowSize] : 0)
    {
    }
    ~Scratch()
    {
      delete[] this->grid;
      delete[] this->rows;
    }
    unsigned char *const grid;
    unsigned char *const rows;

  private:
    Scratch(const Scratch &);
    Scratch &operator=(const Scratch &);
  };
  unsigned char Milk(unsigned value, unsigned resolution)
  {
    return static_cast<unsigned char>((value + (((255 - value) * 160) >> 8)) >> (8 - resolution));
  }
  /** In-place separable [1 2 1], retaining original neighbors, edge repeat.
      Indexes each sample, so no pointer is formed past the line. */
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
  int Neighbor(int i, int count, int d)
  {
    return d < 0 ? (i > 0 ? i - 1 : 0) : (i + 1 < count ? i + 1 : i);
  }
  unsigned Mix(unsigned own, unsigned neighbor, int d, unsigned cell)
  {
    const unsigned weight = static_cast<unsigned>(d < 0 ? -d : d);
    return ((2 * cell - weight) * own + weight * neighbor + cell) / (2 * cell);
  }
  void MixRow(const unsigned char *grid, unsigned char *out, int width, int columns, int cell)
  {
    for (int x = 0; x < width; ++x)
    {
      const int i = x / cell, d = 2 * (x % cell) + 1 - cell;
      const int n = Neighbor(i, columns, d);
      for (int channel = 0; channel < 3; ++channel)
        out[static_cast<long>(x) * 3 + channel] = static_cast<unsigned char>(
            Mix(grid[static_cast<long>(i) * 3 + channel], grid[static_cast<long>(n) * 3 + channel], d, cell));
    }
  }
} // namespace

namespace loka
{
  namespace toolbox
  {
    bool ToolboxBlurAcceptColorIndex(long answer, unsigned char &index)
    {
      if (answer < 0 || answer > 255)
        return false;
      index = static_cast<unsigned char>(answer);
      return true;
    }

    bool ToolboxBlurFillInverse(unsigned char *table, long size, unsigned resolution, ToolboxBlurColourQuery &query)
    {
      if (!table || resolution < 3 || resolution > 5 || size != (1L << (3 * resolution)))
        return false;
      for (long i = 0; i < size; ++i)
        table[i] = 0;
      // Milk maps every channel into Milk(0)..Milk(255); interpolation stays inside.
      const unsigned first = Milk(0, resolution), last = Milk(255, resolution);
      const unsigned shift = 16 - resolution, half = 1U << (15 - resolution);
      for (unsigned red = first; red <= last; ++red)
        for (unsigned green = first; green <= last; ++green)
          for (unsigned blue = first; blue <= last; ++blue)
          {
            unsigned char index;
            if (!query.indexFor(static_cast<unsigned short>((red << shift) + half),
                                static_cast<unsigned short>((green << shift) + half),
                                static_cast<unsigned short>((blue << shift) + half),
                                index))
              return false;
            table[(red << (2 * resolution)) | (green << resolution) | blue] = index;
          }
      return true;
    }

    ToolboxBlurEnlargement ToolboxBlurEnlargementFor(platform::CpuClass cpu)
    {
      switch (cpu)
      {
      case platform::CPU_CLASS_68K:
        return TOOLBOX_BLUR_MOSAIC;
      case platform::CPU_CLASS_PPC_60X:
      case platform::CPU_CLASS_G3_OR_FASTER:
        return TOOLBOX_BLUR_SMOOTH;
      case platform::CPU_CLASS_COUNT:
        break;
      }
      assert(false && "Invalid CPU class");
      return TOOLBOX_BLUR_MOSAIC;
    }

    bool ToolboxBlurKernel(unsigned char *pixels,
                           int width,
                           int height,
                           long stride,
                           const unsigned char *palette,
                           int cell,
                           ToolboxBlurEnlargement mode,
                           const unsigned char *inverse,
                           unsigned resolution)
    {
      if (!pixels || !palette || !inverse || width <= 0 || height <= 0 || stride < width || (cell != 4 && cell != 8)
          || resolution < 3 || resolution > 5 || (mode != TOOLBOX_BLUR_MOSAIC && mode != TOOLBOX_BLUR_SMOOTH))
        return false;
      const int columns = (width - 1) / cell + 1, rows = (height - 1) / cell + 1;
      if (stride > LONG_MAX / height || static_cast<long>(columns) > LONG_MAX / rows / 3
          || (mode == TOOLBOX_BLUR_SMOOTH && static_cast<long>(width) > LONG_MAX / 6))
        return false;
      const long gridSize = static_cast<long>(columns) * rows * 3;
      const long rowSize = mode == TOOLBOX_BLUR_SMOOTH ? static_cast<long>(width) * 6 : 0;
      Scratch scratch(gridSize, rowSize);
      if (!scratch.grid || (rowSize && !scratch.rows))
        return false;
      for (int by = 0; by < rows; ++by)
        for (int bx = 0; bx < columns; ++bx)
        {
          unsigned long sum[3] = {0, 0, 0};
          const int bw = width - bx * cell < cell ? width - bx * cell : cell;
          const int bh = height - by * cell < cell ? height - by * cell : cell;
          for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x)
            {
              const unsigned char *rgb = palette + pixels[(by * cell + y) * stride + bx * cell + x] * 3;
              for (int channel = 0; channel < 3; ++channel)
                sum[channel] += rgb[channel];
            }
          for (int channel = 0; channel < 3; ++channel)
            scratch.grid[(static_cast<long>(by) * columns + bx) * 3 + channel] =
                static_cast<unsigned char>(sum[channel] / (bw * bh));
        }
      for (int channel = 0; channel < 3; ++channel)
      {
        for (int y = 0; y < rows; ++y)
          FilterLine(scratch.grid + static_cast<long>(y) * columns * 3 + channel, columns, 3);
        for (int x = 0; x < columns; ++x)
          FilterLine(scratch.grid + static_cast<long>(x) * 3 + channel, rows, static_cast<long>(columns) * 3);
      }
      if (mode == TOOLBOX_BLUR_MOSAIC)
      {
        for (int by = 0; by < rows; ++by)
          for (int bx = 0; bx < columns; ++bx)
          {
            const unsigned char *rgb = scratch.grid + (static_cast<long>(by) * columns + bx) * 3;
            const unsigned index = (static_cast<unsigned>(Milk(rgb[0], resolution)) << (2 * resolution))
                                   | (static_cast<unsigned>(Milk(rgb[1], resolution)) << resolution)
                                   | Milk(rgb[2], resolution);
            const unsigned char value = inverse[index];
            const int bw = width - bx * cell < cell ? width - bx * cell : cell;
            const int bh = height - by * cell < cell ? height - by * cell : cell;
            for (int y = 0; y < bh; ++y)
              for (int x = 0; x < bw; ++x)
                pixels[(by * cell + y) * stride + bx * cell + x] = value;
          }
      }
      else
      {
        for (long i = 0; i < gridSize; ++i)
          scratch.grid[i] = Milk(scratch.grid[i], 8);
        unsigned char *own = scratch.rows, *neighbor = own + static_cast<long>(width) * 3;
        for (int y = 0; y < height; ++y)
        {
          const int j = y / cell, d = 2 * (y % cell) + 1 - cell;
          const int n = Neighbor(j, rows, d);
          MixRow(scratch.grid + static_cast<long>(j) * columns * 3, own, width, columns, cell);
          MixRow(scratch.grid + static_cast<long>(n) * columns * 3, neighbor, width, columns, cell);
          for (int x = 0; x < width; ++x)
          {
            unsigned index = 0;
            for (int channel = 0; channel < 3; ++channel)
            {
              const long offset = static_cast<long>(x) * 3 + channel;
              index = (index << resolution) | (Mix(own[offset], neighbor[offset], d, cell) >> (8 - resolution));
            }
            pixels[y * stride + x] = inverse[index];
          }
        }
      }
      return true;
    }
  } // namespace toolbox
} // namespace loka
