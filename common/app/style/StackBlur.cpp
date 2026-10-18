#include "app/style/StackBlur.hpp"
#include <climits>

namespace loka
{
  namespace app
  {
    namespace
    {
      static const unsigned char *BlurPixel(const unsigned char *line, int index, int length, size_t step)
      {
        const int clamped = index < 0 ? 0 : index >= length ? length - 1 : index;
        return line + static_cast<size_t>(clamped) * step;
      }

      // Original implementation of Klingemann's StackBlur triangular running sums.
      // The source pass stays immutable, so it supplies the moving stack's samples
      // directly. Each axis uses weights r+1-|offset| and replicated edge pixels.
      static void StackBlurPass(const unsigned char *source,
                                unsigned char *destination,
                                int length,
                                int lines,
                                size_t sourceStep,
                                size_t sourceLineStep,
                                size_t destinationStep,
                                size_t destinationLineStep,
                                int radius)
      {
        const int divisor = (radius + 1) * (radius + 1);
        for (int line = 0; line < lines; ++line)
        {
          const unsigned char *input = source + static_cast<size_t>(line) * sourceLineStep;
          unsigned char *output = destination + static_cast<size_t>(line) * destinationLineStep;
          int weighted[4] = {0, 0, 0, 0};
          int outgoing[4] = {0, 0, 0, 0};
          int incoming[4] = {0, 0, 0, 0};
          for (int offset = -radius; offset <= radius + 1; ++offset)
          {
            const unsigned char *pixel = BlurPixel(input, offset, length, sourceStep);
            const int weight = radius + 1 - (offset < 0 ? -offset : offset);
            for (int channel = 0; channel < 4; ++channel)
            {
              weighted[channel] += pixel[channel] * weight;
              if (offset <= 0)
                outgoing[channel] += pixel[channel];
              else
                incoming[channel] += pixel[channel];
            }
          }
          for (int position = 0; position < length; ++position)
          {
            for (int channel = 0; channel < 4; ++channel)
              output[channel] = static_cast<unsigned char>(weighted[channel] / divisor);
            if (position + 1 == length)
              break;
            const unsigned char *departing = BlurPixel(input, position - radius, length, sourceStep);
            const unsigned char *middle = BlurPixel(input, position + 1, length, sourceStep);
            const unsigned char *arriving = BlurPixel(input, position + radius + 2, length, sourceStep);
            for (int channel = 0; channel < 4; ++channel)
            {
              weighted[channel] += incoming[channel] - outgoing[channel];
              outgoing[channel] += middle[channel] - departing[channel];
              incoming[channel] += arriving[channel] - middle[channel];
            }
            output += destinationStep;
          }
        }
      }

    } // namespace

    size_t StackBlurScratchBytes(int width, int height)
    {
      if (width <= 0 || height <= 0 || width > INT_MAX / 4 || height > INT_MAX - 64)
        return 0;
      const size_t row = static_cast<size_t>(width) * 4;
      if (row > static_cast<size_t>(-1) / static_cast<size_t>(height))
        return 0;
      return row * static_cast<size_t>(height);
    }

    bool StackBlur(unsigned char *pixels,
                   int width,
                   int height,
                   size_t rowBytes,
                   int radius,
                   unsigned char *scratch,
                   size_t scratchBytes)
    {
      const size_t required = StackBlurScratchBytes(width, height);
      if (!required || !pixels || !scratch || scratchBytes < required)
        return false;
      const size_t packedRow = static_cast<size_t>(width) * 4;
      if (rowBytes < packedRow || rowBytes > static_cast<size_t>(-1) / static_cast<size_t>(height))
        return false;
      radius = radius < 1 ? 1 : radius > 32 ? 32 : radius;
      StackBlurPass(pixels, scratch, width, height, 4, rowBytes, 4, packedRow, radius);
      StackBlurPass(scratch, pixels, height, width, packedRow, 4, rowBytes, 4, radius);
      return true;
    }
  } // namespace app
} // namespace loka
