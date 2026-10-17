#include "CardShadowTableTests.hpp"
#include "app/style/CardShadow.hpp"
#include "app/nodes/controls/Card.hpp"
#include "support/TestVerify.hpp"
#include <vector>
#include <cstdio>

namespace
{
  using namespace loka::app;
  using loka::core::Frame;
  bool inside(const Frame &body, int x, int y)
  {
    x -= body.x;
    y -= body.y;
    if (x < 0 || y < 0 || x >= body.width || y >= body.height)
      return false;
    if ((x >= 6 && x < body.width - 6) || (y >= 6 && y < body.height - 6))
      return true;
    const int cx = x < 6 ? 6 : body.width - 6;
    const int cy = y < 6 ? 6 : body.height - 6;
    const int xx = 2 * x + 1 - 2 * cx;
    const int yy = 2 * y + 1 - 2 * cy;
    return xx * xx + yy * yy <= 144;
  }
  /** Independent whole-card oracle: enumerate triples to obtain the kernel,
      then convolve complete rows and columns. No generated geometry or bytes. */
  class Oracle
  {
    Frame body_;
    std::vector<int> horizontal_;
    int weights_[25];

  public:
    explicit Oracle(const Frame &body)
        : body_(body),
          horizontal_(static_cast<std::vector<int>::size_type>(body.width + 24) * body.height, 0)
    {
      for (int i = 0; i < 25; ++i)
        this->weights_[i] = 0;
      for (int a = 0; a < 9; ++a)
        for (int b = 0; b < 9; ++b)
          for (int c = 0; c < 9; ++c)
            ++this->weights_[a + b + c];
      for (int y = 0; y < body.height; ++y)
        for (int x = -12; x < body.width + 12; ++x)
          for (int tap = -12; tap <= 12; ++tap)
            if (inside(body, body.x + x - tap, body.y + y))
              this->horizontal_[y * (body.width + 24) + x + 12] += this->weights_[tap + 12];
    }
    int density(CardShadowExtent extent, int x, int y) const
    {
      if (inside(this->body_, x, y) || extent == CARD_SHADOW_NONE)
        return 0;
      if (extent == CARD_SHADOW_DROP)
        return inside(this->body_, x - 3, y - 3) ? 128 : 0;
      const int localX = x - this->body_.x;
      if (localX < -12 || localX >= this->body_.width + 12)
        return 0;
      long sum = 0;
      for (int tap = -12; tap <= 12; ++tap)
      {
        const int row = y - this->body_.y - 4 - tap;
        if (row >= 0 && row < this->body_.height)
          sum += static_cast<long>(this->weights_[tap + 12])
                 * this->horizontal_[row * (this->body_.width + 24) + localX + 12];
      }
      return static_cast<int>((255L * sum + 531441L / 2) / 531441L);
    }
  };
  /** Historical sampler choice is an independent discriminator for the new
      transfer grid, including near-only pixels past a small body's midline. */
  int referenceSide(int value, int size, int nearCut, int farCut)
  {
    const bool near = value < nearCut, far = value >= size - farCut;
    return near && far ? (value < size / 2 ? 0 : 1) : near ? 0 : far ? 1 : 2;
  }
  void verifyGridSample(const CardStyle &style, const Frame &body, const CardShadowGridLines &grid, int x, int y)
  {
    const detail::CardShadowTable &g = style.geometry();
    LOKA_VERIFY(grid.x[0] == body.x - g.allowance[0] && grid.x[3] == body.x + body.width + g.allowance[2]);
    LOKA_VERIFY(grid.y[0] == body.y - g.allowance[1] && grid.y[3] == body.y + body.height + g.allowance[3]);
    const int horizontal = x < grid.x[1] ? 0 : x >= grid.x[2] ? 1 : 2;
    const int vertical = y < grid.y[1] ? 0 : y >= grid.y[2] ? 1 : 2;
    LOKA_VERIFY(horizontal == referenceSide(x - body.x, body.width, g.cut[0], g.cut[2]));
    LOKA_VERIFY(vertical == referenceSide(y - body.y, body.height, g.cut[1], g.cut[3]));
    LOKA_VERIFY(grid.x[1] <= grid.x[2] && grid.y[1] <= grid.y[2]);
    LOKA_VERIFY(grid.x[1] - grid.x[0] <= detail::kCardShadowMaximumTileWidth);
    LOKA_VERIFY(grid.x[3] - grid.x[2] <= detail::kCardShadowMaximumTileWidth);
    LOKA_VERIFY(grid.y[1] - grid.y[0] <= detail::kCardShadowMaximumTileHeight);
    LOKA_VERIFY(grid.y[3] - grid.y[2] <= detail::kCardShadowMaximumTileHeight);
  }
  void verifyBody(CardShadowExtent extent, const Frame &body)
  {
    const Oracle oracle(body);
    const CardStyle style(extent);
    const CardShadowGridLines grid = CardShadowGrid(style, body);
    for (int y = body.y - 9; y < body.y + body.height + 17; ++y)
      for (int x = body.x - 13; x < body.x + body.width + 13; ++x)
      {
        verifyGridSample(style, body, grid, x, y);
        const int expected = oracle.density(extent, x, y);
        LOKA_VERIFY(CardShadowDensity(style, body, x, y) == expected);
        LOKA_VERIFY(CardShadowDensity(CardStyle(extent, CARD_SHADOW_ALPHA), body, x, y) == expected);
      }
  }
  int verifyBands(CardShadowExtent extent, const Frame &body)
  {
    const CardStyle style(extent);
    const detail::CardShadowTable &g = style.geometry();
    const int left = body.x - g.allowance[0], top = body.y - g.allowance[1];
    const int right = body.x + body.width + g.allowance[2], bottom = body.y + body.height + g.allowance[3];
    const Frame bands[] = {Frame(left, top, body.x + body.width - 3 - left, g.allowance[1] + 3),
                           Frame(left, body.y + 3, g.allowance[0] + 3, bottom - body.y - 3),
                           Frame(body.x + body.width - 3, top, g.allowance[2] + 3, body.y + body.height - 3 - top),
                           Frame(body.x + 3, body.y + body.height - 3, right - body.x - 3, g.allowance[3] + 3)};
    int mismatches = 0;
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      const Frame &band = bands[role];
      const Frame local = CardBodyInBand(band, CardBandProps(static_cast<CardBandRole>(role), style));
      const CardShadowGridLines grid = CardShadowGrid(style, local);
      for (int y = band.y; y < band.y + band.height; ++y)
        for (int x = band.x; x < band.x + band.width; ++x)
        {
          verifyGridSample(style, local, grid, x, y);
          const int expected = CardShadowDensity(style, body, x, y);
          const int actual = CardShadowDensity(style, local, x, y);
          if (expected != actual)
            ++mismatches;
          if (extent == CARD_SHADOW_WIDE)
            LOKA_VERIFY(actual == expected);
        }
      if (extent == CARD_SHADOW_WIDE)
      {
        // Enumerate the changed far-side caster, expanded by finite support
        // and the additional two-unit rounding clearance, against this band.
        for (int y = body.y; y < body.y + body.height; ++y)
          for (int x = body.x; x < body.x + body.width; ++x)
            if (inside(body, x, y) != inside(local, x, y))
            {
              const int l = x - 12 - 2, r = x + 12 + 2;
              const int t = y + 4 - 12 - 2, b = y + 4 + 12 + 2;
              LOKA_VERIFY(r < band.x || l >= band.x + band.width || b < band.y || t >= band.y + band.height);
            }
      }
    }
    return mismatches;
  }
} // namespace

void testCardShadowIndependentOracle()
{
  for (int extent = CARD_SHADOW_DROP; extent <= CARD_SHADOW_WIDE; ++extent)
  {
    const CardShadowExtent e = static_cast<CardShadowExtent>(extent);
    const int mw = e == CARD_SHADOW_DROP ? 14 : 36;
    const int mh = e == CARD_SHADOW_DROP ? 14 : 44;
    for (int delta = 0; delta < 18; ++delta)
    {
      verifyBody(e, Frame(0, 0, mw + delta, mh));
      verifyBody(e, Frame(0, 0, mw, mh + delta));
    }
    verifyBody(e, Frame(0, 0, 151, 113));
    verifyBody(e, Frame(-31, -18, mw + 7, mh + 11));
  }
}

void testCardShadowBandLocality()
{
  // A virtual DROP body is only promised to reconstruct its own band. Pin the
  // sampler's corner choice separately outside that band: the final corner
  // column is zero here, while the adjacent bottom profile would ink it.
  LOKA_VERIFY(CardShadowDensity(CardStyle(CARD_SHADOW_DROP), Frame(-31, -18, 12, 12), -26, -4) == 0);
  int dropMismatch = 0;
  for (int extent = CARD_SHADOW_DROP; extent <= CARD_SHADOW_WIDE; ++extent)
  {
    const CardShadowExtent e = static_cast<CardShadowExtent>(extent);
    const int mw = e == CARD_SHADOW_DROP ? 14 : 36;
    const int mh = e == CARD_SHADOW_DROP ? 14 : 44;
    for (int delta = 0; delta < 18; ++delta)
    {
      dropMismatch += verifyBands(e, Frame(0, 0, mw + delta, mh));
      dropMismatch += verifyBands(e, Frame(0, 0, mw, mh + delta));
    }
    dropMismatch += verifyBands(e, Frame(0, 0, 151, 113));
    dropMismatch += verifyBands(e, Frame(-31, -18, mw + 7, mh + 11));
  }
  LOKA_VERIFY(dropMismatch == 0);
  std::printf("DROP band-local mismatches: %d\n", dropMismatch);
}

void testCardShadowStencilLattice()
{
  const unsigned char expected[64] = {0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31,
                                      8,  56, 4,  52, 11, 59, 7,  55, 40, 24, 36, 20, 43, 27, 39, 23,
                                      2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30, 33, 17, 45, 29,
                                      10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21};
  for (int y = -8; y < 16; ++y)
    for (int x = -8; x < 16; ++x)
    {
      LOKA_VERIFY(CardShadowStencilInk(128, x, y) == ((x + y) % 2 == 0));
      LOKA_VERIFY(!CardShadowStencilInk(0, x, y));
      LOKA_VERIFY(CardShadowDensity(CardStyle(CARD_SHADOW_NONE, CARD_SHADOW_ALPHA), Frame(0, 0, 12, 12), x, y) == 0);
      LOKA_VERIFY(CardShadowStencilInk(255, x, y));
      const int threshold = 4 * expected[((y % 8 + 8) % 8) * 8 + (x % 8 + 8) % 8] + 2;
      LOKA_VERIFY(!CardShadowStencilInk(static_cast<unsigned char>(threshold), x, y));
      LOKA_VERIFY(CardShadowStencilInk(static_cast<unsigned char>(threshold + 1), x, y));
    }
}
