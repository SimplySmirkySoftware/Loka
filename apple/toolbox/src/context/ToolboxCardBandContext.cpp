#include "context/ToolboxCardBandContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxPaintGradient.hpp"
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
  /** Owns both temporary masks; no region survives a draw or a refused allocation. */
  class CardInkRegions
  {
  public:
    CardInkRegions()
        : body(NewRgn()),
          shadow(NewRgn())
    {
    }
    ~CardInkRegions()
    {
      if (this->shadow)
        DisposeRgn(this->shadow);
      if (this->body)
        DisposeRgn(this->body);
    }
    RgnHandle body;
    RgnHandle shadow;

  private:
    CardInkRegions(const CardInkRegions &);
    CardInkRegions &operator=(const CardInkRegions &);
  };

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

  /** Three real edges and one virtual opposite edge. The minimum body is 2r;
      the opposite arc cannot reach this band's inset-wide sample. All virtual
      edges stay inside the real body, including near the short-coordinate limit.
      Cost: one call per visible band draw, constant work, no tree/ledger walk. */
  Rect CardBodyInBand(const Rect &band, const loka::app::CardBandProps &props)
  {
    using namespace loka::app;
    const CardStyle &style = props.style;
    const int b = style.kBorderBand;
    const int diameter = 2 * style.kCornerRadius;
    Rect body = band;
    switch (props.role)
    {
    case CARD_BAND_TOP:
      body.left += style.kShadowLeft;
      body.top += style.kShadowTop;
      body.right += b;
      body.bottom = body.top + diameter;
      break;
    case CARD_BAND_RIGHT:
      body.top += style.kShadowTop;
      body.right -= style.kShadowRight;
      body.bottom += b;
      body.left = body.right - diameter;
      break;
    case CARD_BAND_BOTTOM:
      body.left -= b;
      body.right -= style.kShadowRight;
      body.bottom -= style.kShadowBottom;
      body.top = body.bottom - diameter;
      break;
    case CARD_BAND_LEFT:
      body.left += style.kShadowLeft;
      body.top -= b;
      body.bottom -= style.kShadowBottom;
      body.right = body.left + diameter;
      break;
    case CARD_BAND_COUNT:
      assert(false && "CardBand requires a role");
      SetRect(&body, 0, 0, 0, 0);
      break;
    }
    return body;
  }
} // namespace

void ToolboxCardBandContext::render(loka::app::scene::IPlatformController *)
{
  if (EmptyRect(&this->rect_) || EmptyRect(&this->paintRect_))
    return;
  ToolboxPaintClip clip(this->paintRect_);
  if (!clip.isActive() || !clip.touches(this->paintRect_))
    return;
  CardInkRegions regions;
  if (!regions.body || !regions.shadow)
    return;
  CardPenScope pen;
  loka::toolbox::ToolboxTextInkScope ink(blackColor);
  Rect body = CardBodyInBand(this->rect_, this->node_->props);
  const short oval = 2 * this->node_->props.style.kCornerRadius;
  OpenRgn();
  FrameRoundRect(&body, oval, oval);
  CloseRgn(regions.body);
  Rect shadow = body;
  OffsetRect(&shadow, this->node_->props.style.kShadowRight, this->node_->props.style.kShadowBottom);
  OpenRgn();
  FrameRoundRect(&shadow, oval, oval);
  CloseRgn(regions.shadow);
  DiffRgn(regions.shadow, regions.body, regions.shadow);
  PenPat(&loka::toolbox::kToolboxGradientPatterns[4]);
  PenMode(patOr);
  PaintRgn(regions.shadow);
  PenNormal();
  FrameRoundRect(&body, oval, oval);
}

bool RegisterToolboxCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxCardBandNodeHandler);
}
