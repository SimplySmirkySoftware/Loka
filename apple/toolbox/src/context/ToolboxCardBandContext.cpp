#include "context/ToolboxCardBandContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/style/CardShadow.hpp"
#include <cstring>
#include "core/Profiler.hpp"
#include "ToolboxAlignedPattern.hpp"
#include "ToolboxPaintGround.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  class ToolboxCardBandNodeHandler : public loka::app::scene::RetainedNodeHandler<ToolboxCardBandNodeHandler,
                                                                                  loka::app::CardBandNode,
                                                                                  ToolboxCardBandContext>
  {
  public:
    static loka::app::CardBandNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asCardBandNode() : 0;
    }
    static ToolboxCardBandContext *create(loka::app::CardBandNode *node,
                                          loka::app::scene::IPlatformController *controller,
                                          const loka::app::scene::LayoutState &)
    {
      return new ToolboxCardBandContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };
  ToolboxCardBandNodeHandler gToolboxCardBandNodeHandler;
} // namespace

ToolboxCardBandContext::ToolboxCardBandContext(loka::app::CardBandNode *node,
                                               ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_()
{
}

void ToolboxCardBandContext::readLifecycleFactOnAttach()
{
  if (this->controller())
    this->controller()->registerCompositionReplay(this->replay_);
}

void ToolboxCardBandContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                           loka::app::scene::NodeLifecycleFact next)
{
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
    this->readLifecycleFactOnAttach();
  else
  {
    this->replay_.clear();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

short ToolboxCardBandContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  SetRect(&this->rect_, state.x, state.y, state.x + state.width, state.y + state.height);
  this->paintRect_ = this->rect_;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(this->rect_, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  state.y += state.height;
  return state.width;
}

/** Collector: once per band per paint-only collection; O(1), no owned or foreign rows walked. */
loka::app::scene::PaintAnswer ToolboxCardBandContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope()
      || this->deliveredFact() != NODE_FACT_ATTACHED)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  return ToolboxExactPaint(this->paintRect_, true);
}

namespace
{
  /** Toolbox has no alpha transfer. Props retain the requested ink. */
  loka::app::CardStyle ToolboxStencilStyle(const loka::app::CardStyle &requested)
  {
    return loka::app::CardStyle(requested.extent(), loka::app::CARD_SHADOW_STENCIL);
  }

  /** QuickDraw's radius-6 frame quadrant, measured from FrameRoundRect(r, 12, 12)
      on maciix. The PaintDamage matrix keeps this equal to its FrameRoundRect oracle. */
  const unsigned char kQuickDrawRadius6FrameQuadrant[6] = {0x0c, 0x30, 0x40, 0x40, 0x80, 0x80};
  typedef char QuickDrawFrameRequiresRadius6[loka::app::CardStyle::kCornerRadius == 6 ? 1 : -1];
  // A corner row's border bits are gathered in one 32-bit word before the OR.
  typedef char CardBorderRowFitsOneLong[loka::app::detail::kCardShadowMaximumTileWidth <= 32 ? 1 : -1];

  /** Once per nonempty band render; walks only this band's nine cells. Corner
      tiles are at most 30x30, with one border computation per row. Edges read
      one profile per cell and issue one PaintRect per run; no FrameRoundRect.
      All storage dies with the call. */
  void PaintCardStencil(const loka::app::CardStyle &requested,
                        const loka::core::Frame &body,
                        const Rect &band,
                        const ToolboxPaintClip &clip)
  {
    using namespace loka::app;
    const CardStyle style = ToolboxStencilStyle(requested);
    assert(body.width >= 12 && body.height >= 12);
    if (body.width < 12 || body.height < 12)
      return;
    const CardShadowGridLines grid = CardShadowGrid(style, body);
    enum
    {
      rowBytes = 2 * ((detail::kCardShadowMaximumTileWidth + 15) / 16)
    };
    // CopyBits reads a BitMap by words, and the 68000 faults on a word access
    // at an odd address (#1398); the union gives the buffer word alignment by
    // type instead of by the frame layout the compiler happens to choose.
    union
    {
      unsigned char bytes[rowBytes * detail::kCardShadowMaximumTileHeight + detail::kCardShadowMaximumTileWidth + 8];
      long alignment;
    } bits;
    unsigned char *densities = bits.bytes + rowBytes * detail::kCardShadowMaximumTileHeight;
    unsigned char *thresholds = densities + detail::kCardShadowMaximumTileWidth;
    GrafPtr port;
    GetPort(&port);
    PenMode(patOr);
    for (int v = 0; v < 3; ++v)
      for (int h = 0; h < 3; ++h)
      {
        if (v == 1 && h == 1)
          continue;
        // Intersect in wide arithmetic before narrowing virtual body edges.
        const int x0 = grid.x[h] > band.left ? grid.x[h] : band.left;
        const int y0 = grid.y[v] > band.top ? grid.y[v] : band.top;
        const int x1 = grid.x[h + 1] < band.right ? grid.x[h + 1] : band.right;
        const int y1 = grid.y[v + 1] < band.bottom ? grid.y[v + 1] : band.bottom;
        if (x0 >= x1 || y0 >= y1)
          continue;
        Rect dest;
        SetRect(&dest, x0, y0, x1, y1);
        if (!clip.touches(dest))
          continue;
        if (v != 1 && h != 1)
        {
          const int width = x1 - x0, height = y1 - y0;
          const bool fits =
              width <= detail::kCardShadowMaximumTileWidth && height <= detail::kCardShadowMaximumTileHeight;
          assert(fits && "CardShadowGrid exceeds generated tile capacity");
          if (!fits)
            continue;
          {
            PROFILE_SECTION("card.buffer-clear");
            std::memset(bits.bytes, 0, sizeof(bits.bytes));
          }
          bool hasInk = false;
          {
            PROFILE_SECTION("card.threshold");
            for (int y = 0; y < height; ++y)
            {
              CardShadowDensityRun(style, body, x0, y0 + y, width, densities);
              CardShadowStencilThresholdRow(y0 + y, thresholds);
              unsigned char *row = bits.bytes + y * rowBytes;
              for (int x = 0; x < width; ++x)
                if (densities[x] > thresholds[(x0 + x) & 7])
                {
                  row[x / 8] |= 0x80 >> (x & 7);
                  hasInk = true;
                }
              const int ly = y0 + y - body.y;
              if (ly < 0 || ly >= body.height)
                continue;
              const int dy = ly < body.height - 1 - ly ? ly : body.height - 1 - ly;
              const unsigned char quadrant = dy < 6 ? kQuickDrawRadius6FrameQuadrant[dy] : 0x80;
              unsigned long border = 0;
              for (int dx = 0; dx < (dy < 6 ? 6 : 1); ++dx)
                if (quadrant & (0x80 >> dx))
                  for (int side = 0; side < 2; ++side)
                  {
                    const int lx = side == 0 ? dx : body.width - 1 - dx;
                    const int column = body.x + lx - x0;
                    if (column >= 0 && column < width)
                      border |= 0x80000000UL >> column;
                  }
              if (dy == 0)
              {
                const int first = body.x + 6 > x0 ? body.x + 6 - x0 : 0;
                const int end = body.x + body.width - 6 < x1 ? body.x + body.width - 6 - x0 : width;
                if (first < end)
                  border |= (0xffffffffUL >> first) & (0xffffffffUL << (32 - end));
              }
              if (border)
              {
                for (int byte = 0; byte < rowBytes; ++byte)
                  row[byte] |= static_cast<unsigned char>(border >> (24 - 8 * byte));
                hasInk = true;
              }
            }
          }
          if (!hasInk)
            continue;
          BitMap source;
          source.baseAddr = reinterpret_cast<Ptr>(bits.bytes);
          source.rowBytes = rowBytes;
          SetRect(&source.bounds, 0, 0, width, height);
          {
            PROFILE_SECTION("card.copy-bits");
            CopyBits(&source, &port->portBits, &source.bounds, &dest, srcOr, 0);
          }
        }
        else
        {
          const int count = v == 1 ? x1 - x0 : y1 - y0;
          enum
          {
            profileCapacity = detail::kCardShadowMaximumTileWidth > detail::kCardShadowMaximumTileHeight
                                  ? detail::kCardShadowMaximumTileWidth
                                  : detail::kCardShadowMaximumTileHeight
          };
          assert(count <= profileCapacity && "CardShadowGrid exceeds edge profile capacity");
          if (count > profileCapacity)
            continue;
          unsigned char values[profileCapacity];
          if (v == 1)
            CardShadowDensityRun(style, body, x0, y0, count, values);
          else
            for (int i = 0; i < count; ++i)
              values[i] = CardShadowDensity(style, body, x0, y0 + i);
          // Arc zones are contained by the corner cuts; an edge holds only a straight line.
          const int borderIndex = v == 1 ? (h == 0 ? body.x : body.x + body.width - 1) - x0
                                         : (v == 0 ? body.y : body.y + body.height - 1) - y0;
          for (int i = 0; i < count; ++i)
          {
            const bool border = i == borderIndex;
            const unsigned char density = values[i];
            if (!border && !density)
              continue;
            // PenPat copies the pattern with long moves; keep it word-aligned (#1398).
            loka::toolbox::ToolboxAlignedPattern pattern;
            unsigned char *bytes = reinterpret_cast<unsigned char *>(&pattern.pattern);
            {
              PROFILE_SECTION("card.pattern-prepare");
              if (border)
                std::memset(bytes, 0xff, 8);
              else
                CardShadowStencilPattern(density, bytes);
            }
            int end = i + 1;
            while (!border && end < count && end != borderIndex && values[end] == density)
              ++end;
            Rect run = dest;
            if (v == 1)
              run.left = x0 + i, run.right = x0 + end;
            else
              run.top = y0 + i, run.bottom = y0 + end;
            i = end - 1;
            {
              PROFILE_SECTION("card.paint-run");
              PenPat(&pattern.pattern);
              PaintRect(&run);
            }
          }
        }
      }
  }

  /** Restore the caller's pen, including pattern, mode, size and position. */
  class CardPenScope
  {
  public:
    CardPenScope()
    {
      GetPenState(&this->saved_);
      PenNormal();
    }
    ~CardPenScope()
    {
      SetPenState(&this->saved_);
    }

  private:
    PenState saved_;
    CardPenScope(const CardPenScope &);
    CardPenScope &operator=(const CardPenScope &);
  };

} // namespace

void ToolboxCardBandContext::render(loka::app::scene::IPlatformController *)
{
  PROFILE_SECTION("card.band");
  if (EmptyRect(&this->rect_) || EmptyRect(&this->paintRect_))
    return;
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive() || !clip.touches(this->paintRect_))
    return;
  CardPenScope pen;
  loka::toolbox::ToolboxTextInkScope ink(blackColor);
  const loka::core::Frame recovered = loka::app::CardBodyInBand(loka::core::Frame(this->rect_.left,
                                                                                  this->rect_.top,
                                                                                  this->rect_.right - this->rect_.left,
                                                                                  this->rect_.bottom - this->rect_.top),
                                                                this->node_->props);
  PaintCardStencil(this->node_->props.style, recovered, this->paintRect_, clip);
}

bool RegisterToolboxCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxCardBandNodeHandler);
}
