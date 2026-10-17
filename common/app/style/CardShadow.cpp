#include "app/style/CardShadow.hpp"
#include <cstring>
namespace loka
{
  namespace app
  {
    namespace
    {
      // Preserve near-only and far-only samples; split their overlap at the
      // midline clamped to that overlap. This is the single partition rule.
      int nearEnd(int size, int nearCut, int farCut)
      {
        if (nearCut <= size - farCut)
          return nearCut;
        const int middle = size / 2;
        return middle < size - farCut ? size - farCut : middle > nearCut ? nearCut : middle;
      }
    } // namespace
    CardShadowGridLines CardShadowGrid(const CardStyle &style, const core::Frame &body)
    {
      const detail::CardShadowTable &g = style.geometry();
      const int nearX = nearEnd(body.width, g.cut[0], g.cut[2]);
      const int nearY = nearEnd(body.height, g.cut[1], g.cut[3]);
      const int farX = body.width - g.cut[2] > nearX ? body.width - g.cut[2] : nearX;
      const int farY = body.height - g.cut[3] > nearY ? body.height - g.cut[3] : nearY;
      const CardShadowGridLines grid = {
          {body.x - g.allowance[0], body.x + nearX, body.x + farX, body.x + body.width + g.allowance[2]},
          {body.y - g.allowance[1], body.y + nearY, body.y + farY, body.y + body.height + g.allowance[3]}};
      return grid;
    }
    void
    CardShadowDensityRun(const CardStyle &style, const core::Frame &body, int x, int y, int count, unsigned char *out)
    {
      assert(count >= 0 && (out || count == 0));
      if (count <= 0)
        return;
      const detail::CardShadowTable &g = style.geometry();
      const CardShadowGridLines grid = CardShadowGrid(style, body);
      if (style.extent() == CARD_SHADOW_NONE || body.width <= 0 || body.height <= 0 || y < grid.y[0] || y >= grid.y[3])
      {
        std::memset(out, 0, count);
        return;
      }
      const int vertical = y < grid.y[1] ? 0 : y >= grid.y[2] ? 1 : 2;
      while (count > 0)
      {
        int length = count;
        const unsigned char *source = 0;
        unsigned char value = 0;
        int end = x;
        if (x < grid.x[0])
          end = grid.x[0];
        else if (x >= grid.x[3])
        {
          std::memset(out, 0, count);
          return;
        }
        else
        {
          const int horizontal = x < grid.x[1] ? 0 : x >= grid.x[2] ? 1 : 2;
          end = horizontal == 0 ? grid.x[1] : horizontal == 1 ? grid.x[3] : grid.x[2];
          const int localX = x - body.x, localY = y - body.y;
          if (horizontal != 2 && vertical != 2)
          {
            const int tx = horizontal == 0 ? localX + g.allowance[0] : localX - body.width + g.cut[2];
            const int ty = vertical == 0 ? localY + g.allowance[1] : localY - body.height + g.cut[3];
            const int width = horizontal == 0 ? g.allowance[0] + g.cut[0] : g.allowance[2] + g.cut[2];
            source = detail::CardShadowDensities + g.offset[vertical * 2 + horizontal] + ty * width + tx;
          }
          else if (localX < 0)
          {
            source = detail::CardShadowDensities + g.offset[4] + localX + g.allowance[0];
            if (end > body.x)
              end = body.x;
          }
          else if (localX >= body.width)
            source = detail::CardShadowDensities + g.offset[6] + localX - body.width;
          else
          {
            if (end > body.x + body.width)
              end = body.x + body.width;
            if (localY < 0)
              value = detail::CardShadowDensities[g.offset[5] + localY + g.allowance[1]];
            else if (localY >= body.height)
              value = detail::CardShadowDensities[g.offset[7] + localY - body.height];
          }
        }
        if (length > end - x)
          length = end - x;
        assert(length > 0);
        if (source)
          std::memcpy(out, source, length);
        else
          std::memset(out, value, length);
        x += length;
        out += length;
        count -= length;
      }
    }
    unsigned char CardShadowDensity(const CardStyle &style, const core::Frame &body, int x, int y)
    {
      unsigned char result;
      CardShadowDensityRun(style, body, x, y, 1, &result);
      return result;
    }
    namespace
    {
      inline unsigned char threshold(unsigned char bayer)
      {
        return 4 * bayer + 2;
      }
    } // namespace
    void CardShadowStencilThresholdRow(int windowY, unsigned char *out)
    {
      for (int x = 0; x < 8; ++x)
        out[x] = threshold(detail::CardShadowBayer[(windowY & 7) * 8 + x]);
    }
    void CardShadowStencilPattern(unsigned char density, unsigned char *out)
    {
      for (int y = 0; y < 8; ++y)
      {
        unsigned char bits = 0;
        for (int x = 0; x < 8; ++x)
        {
          bits = static_cast<unsigned char>(bits << 1);
          if (density > threshold(detail::CardShadowBayer[y * 8 + x]))
            bits |= 1;
        }
        out[y] = bits;
      }
    }
    bool CardShadowStencilInk(unsigned char density, int windowX, int windowY)
    {
      return density > threshold(detail::CardShadowBayer[(windowY & 7) * 8 + (windowX & 7)]);
    }
  } // namespace app
} // namespace loka
