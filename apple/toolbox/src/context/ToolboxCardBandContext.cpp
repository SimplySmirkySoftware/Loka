#include "context/ToolboxCardBandContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/style/CardShadow.hpp"
#include <cstring>
#include "core/Profiler.hpp"
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

  /** Once per nonempty band paint; walks only this band's intersecting tiles
      and constant-density edge rows. All storage dies with the call. */
  void PaintCardStencil(const loka::app::CardStyle &requested,
                        const loka::core::Frame &body,
                        const Rect &band,
                        const ToolboxPaintClip &clip)
  {
    using namespace loka::app;
    const CardStyle style = ToolboxStencilStyle(requested);
    if (style.extent() == CARD_SHADOW_NONE)
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
      unsigned char bytes[rowBytes * detail::kCardShadowMaximumTileHeight];
      long alignment;
    } bits;
    GrafPtr port;
    GetPort(&port);
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
          {
            PROFILE_SECTION("card.threshold");
            for (int y = 0; y < height; ++y)
              for (int x = 0; x < width; ++x)
                if (CardShadowStencilInk(CardShadowDensity(style, body, x0 + x, y0 + y), x0 + x, y0 + y))
                  bits.bytes[y * rowBytes + x / 8] |= 0x80 >> (x & 7);
          }
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
          for (int i = 0; i < count; ++i)
          {
            const unsigned char density = CardShadowDensity(style, body, x0 + (v == 1 ? i : 0), y0 + (v == 1 ? 0 : i));
            if (!density)
              continue;
            Pattern pattern;
            unsigned char *bytes = reinterpret_cast<unsigned char *>(&pattern);
            {
              PROFILE_SECTION("card.pattern-prepare");
              for (int y = 0; y < 8; ++y)
              {
                bytes[y] = 0;
                for (int x = 0; x < 8; ++x)
                  if (CardShadowStencilInk(density, x, y))
                    bytes[y] |= 0x80 >> x;
              }
            }
            Rect run = dest;
            if (v == 1)
              run.left = x0 + i, run.right = run.left + 1;
            else
              run.top = y0 + i, run.bottom = run.top + 1;
            {
              PROFILE_SECTION("card.paint-run");
              PenPat(&pattern);
              PenMode(patOr);
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
  Rect body;
  SetRect(&body, recovered.x, recovered.y, recovered.x + recovered.width, recovered.y + recovered.height);
  const short oval = 2 * this->node_->props.style.kCornerRadius;
  PaintCardStencil(this->node_->props.style, recovered, this->paintRect_, clip);
  PenNormal();
  FrameRoundRect(&body, oval, oval);
}

bool RegisterToolboxCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxCardBandNodeHandler);
}
