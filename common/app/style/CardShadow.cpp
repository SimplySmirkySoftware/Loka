#include "app/style/CardShadow.hpp"
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
    unsigned char CardShadowDensity(const CardStyle &style, const core::Frame &body, int x, int y)
    {
      if (style.extent() == CARD_SHADOW_NONE || body.width <= 0 || body.height <= 0)
        return 0;
      const detail::CardShadowTable &g = style.geometry();
      const CardShadowGridLines grid = CardShadowGrid(style, body);
      if (x < grid.x[0] || y < grid.y[0] || x >= grid.x[3] || y >= grid.y[3])
        return 0;
      const int horizontal = x < grid.x[1] ? 0 : x >= grid.x[2] ? 1 : 2;
      const int vertical = y < grid.y[1] ? 0 : y >= grid.y[2] ? 1 : 2;
      x -= body.x;
      y -= body.y;
      if (horizontal != 2 && vertical != 2)
      {
        const int tx = horizontal == 0 ? x + g.allowance[0] : x - body.width + g.cut[2];
        const int ty = vertical == 0 ? y + g.allowance[1] : y - body.height + g.cut[3];
        const int width = horizontal == 0 ? g.allowance[0] + g.cut[0] : g.allowance[2] + g.cut[2];
        return detail::CardShadowDensities[g.offset[vertical * 2 + horizontal] + ty * width + tx];
      }
      if (x < 0)
        return detail::CardShadowDensities[g.offset[4] + x + g.allowance[0]];
      if (y < 0)
        return detail::CardShadowDensities[g.offset[5] + y + g.allowance[1]];
      if (x >= body.width)
        return detail::CardShadowDensities[g.offset[6] + x - body.width];
      if (y >= body.height)
        return detail::CardShadowDensities[g.offset[7] + y - body.height];
      return 0;
    }
    bool CardShadowStencilInk(unsigned char density, int windowX, int windowY)
    {
      return density > 4 * detail::CardShadowBayer[(windowY & 7) * 8 + (windowX & 7)] + 2;
    }
  } // namespace app
} // namespace loka
