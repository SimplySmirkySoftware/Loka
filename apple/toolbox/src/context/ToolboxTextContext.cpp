#include "ToolboxPaintGround.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "app/layout/AlignedLineOffset.hpp"
#include "app/layout/TextLineBreaker.hpp"
#include <climits>
#include "ToolboxLayoutMetrics.hpp"
#include "context/ToolboxTextContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <cstring>

namespace
{
  /** Twin: attributed text aligns fitted lines from its cached native widths. */
  void DrawPascalAt(short x,
                    short y,
                    const unsigned char *text,
                    short availableWidth,
                    const loka::app::BlockStyle &block,
                    loka::app::TextColor color)
  {
    const loka::app::TextAlign align = block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT;
    const int aligned = x + (align == loka::app::TEXT_ALIGN_LEFT ? 0
        : loka::app::AlignedLineOffset(availableWidth, StringWidth(text), align));
    MoveTo(static_cast<short>(aligned > SHRT_MAX ? SHRT_MAX : aligned), y);
    loka::toolbox::ToolboxTextInkScope ink(loka::toolbox::ToolboxTextInk(color, loka::app::SURFACE_GROUND_WINDOW));
    DrawString(text);
  }
} // namespace

/** Immutable completed plain-text break. Extent owns native ranges; construction
    counts and fills through the same walk, using TextLineBreaker's fallible table. */
class ToolboxPlainTextLines
{
public:
  static loka::core::Managed<ToolboxPlainTextLines> Build(
      const ToolboxNativeText &native, short width, short pitch, loka::app::TextWrap wrap,
      const ToolboxTextMeasureScope &measure)
  {
    ToolboxPlainTextLines *lines = loka::core::LokaNew<ToolboxPlainTextLines>(Site());
    if (!lines)
      return loka::core::Managed<ToolboxPlainTextLines>();
    std::size_t count = 0;
    if (pitch <= 0 || !measure.valid() || !native.valid()
        || !lines->walk(native, width, wrap, false, count)
        || count > static_cast<std::size_t>(SHRT_MAX / pitch)
        || !lines->ranges_.allocate(count)
        || !lines->walk(native, width, wrap, true, count))
    {
      Release(lines, 0);
      return loka::core::Managed<ToolboxPlainTextLines>();
    }
    lines->pitch_ = pitch;
    const loka::core::Managed<ToolboxPlainTextLines> result =
        loka::core::Managed<ToolboxPlainTextLines>::TryWrap(lines, Release, 0);
    if (!result.isValid())
      Release(lines, 0);
    return result;
  }
  short height() const
  {
    return static_cast<short>(this->ranges_.size() * this->pitch_);
  }
  void draw(const ToolboxNativeText &native,
            short x,
            short baseline,
            short availableWidth,
            const loka::app::BlockStyle &block,
            loka::app::TextColor color) const
  {
    for (std::size_t i = 0; i < this->ranges_.size(); ++i)
    {
      const Range &range = this->ranges_[i];
      Str255 text;
      native.copyPascal(range.start, range.end, text);
      const int y = baseline + static_cast<int>(i) * this->pitch_;
      DrawPascalAt(x, static_cast<short>(y > SHRT_MAX ? SHRT_MAX : y), text, availableWidth, block, color);
    }
  }

private:
  /** Native byte endpoints into the context-owned projection. */
  struct Range
  {
    std::size_t start, end;
  };
  loka::app::detail::TextMeasureTable<Range> ranges_;
  short pitch_;
  static loka::core::LokaAllocationSite Site()
  {
    return loka::core::LokaAllocationSite("ToolboxPlainText", "Lines");
  }
  static void Release(ToolboxPlainTextLines *lines, void *)
  {
    loka::core::LokaDelete(lines, Site());
  }
  bool append(std::size_t start, std::size_t end, bool fill, std::size_t &count)
  {
    if (fill)
    {
      if (count >= this->ranges_.size())
        return false;
      this->ranges_[count].start = start;
      this->ranges_[count].end = end;
    }
    ++count;
    return true;
  }
  bool walk(const ToolboxNativeText &native, short maxWidth, loka::app::TextWrap wrap, bool fill, std::size_t &count)
  {
    count = 0;
    std::size_t start = 0, wordStart = 0;
    for (std::size_t i = 0; i < native.size();)
    {
      const std::size_t cpStart = i;
      i = native.next(i);
      if (i - cpStart == 1 && (native.data()[cpStart] == '\n' || native.data()[cpStart] == '\r'))
      {
        if (!this->append(start, cpStart, fill, count))
          return false;
        start = wordStart = i;
        continue;
      }
      // Rebase before narrowing: a later line may start beyond SHRT_MAX.
      const short width = i - start <= 255
          ? TextWidth(reinterpret_cast<const char *>(native.data() + start), 0, static_cast<short>(i - start)) : 0;
      const bool space = native.data()[cpStart] == ' ' || native.data()[cpStart] == '\t';
      // Twin: TextLineBreaker::build keeps fitting spaces/tabs, moves the
      // following word intact, and force-breaks a word at an empty line.
      // This native walk additionally bounds every range to Str255 and keeps
      // the plain path's independent CR/LF breaks.
      if ((width > maxWidth || i - start > 255) && cpStart != start)
      {
        const std::size_t end = wrap == loka::app::TEXT_WRAP_WORD && !space && wordStart > start
            ? wordStart : cpStart;
        if (!this->append(start, end, fill, count))
          return false;
        start = wordStart = end;
        i = end;
        continue;
      }
      if (i - start > 255)
        return false;
      if (space)
        wordStart = i;
    }
    return this->append(start, native.size(), fill, count);
  }
};

namespace
{
  class ToolboxTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxTextNodeHandler,
                                                     loka::app::TextNode,
                                                     ToolboxTextContext>
  {
  public:
    static loka::app::TextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asTextNode() : 0;
    }

    static ToolboxTextContext *create(loka::app::TextNode *node,
                                      loka::app::scene::IPlatformController *controller,
                                      const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxTextContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxTextNodeHandler gToolboxTextNodeHandler;

  /** Capped native display, including the dots inside the 255-byte budget. */
  void PlainDisplay(const ToolboxNativeText &native, short maxWidth,
                    loka::app::TextTruncation truncation, Str255 out)
  {
    native.copyPascal(0, native.cappedEnd(255), out);
    if (maxWidth <= 0 || truncation != loka::app::TEXT_TRUNCATION_ELLIPSIS
        || StringWidth(out) <= maxWidth)
      return;
    std::size_t end = native.cappedEnd(252);
    for (;;)
    {
      native.copyPascal(0, end, out);
      std::memcpy(out + 1 + out[0], "...", 3);
      out[0] = static_cast<unsigned char>(out[0] + 3);
      if (StringWidth(out) <= maxWidth)
        return;
      if (!end)
      {
        out[0] = 0;
        return;
      }
      end = native.previous(end);
    }
  }

  /** Completed text geometry; paint consumes the baseline captured by layout. */
  struct TextGeometry
  {
    TextGeometry(short boxHeight = 0, short baseline = 0, short pitch = 0)
        : height(boxHeight), baselineOffset(baseline), linePitch(pitch) {}

    short height;
    short baselineOffset;
    short linePitch;
    loka::core::Managed<ToolboxPlainTextLines> lines;
  };

  /** Unset size preserves legacy box geometry. Wrapped paint starts at the
      first baseline; ellipsis keeps the legacy terminal baseline. */
  bool ResolveTextGeometry(const loka::app::TextStyle &style,
                                   const loka::app::scene::LayoutState &state,
                                   const ToolboxNativeText &native,
                                   loka::app::TextWrap wrap,
                                   loka::app::TextTruncation truncation,
                                   const ToolboxTextMeasureScope &measure, TextGeometry &geometry)
  {
    if (!measure.valid())
      return false;
    const bool wraps = state.width > 0 && wrap != loka::app::TEXT_WRAP_NONE;
    if (!style.size_.isSet())
    {
      geometry = TextGeometry(
          static_cast<short>(state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset
                             + ToolboxLayoutMetrics::kControlDescent),
          static_cast<short>(state.lineHeight - ToolboxLayoutMetrics::kControlAscentInset),
          state.lineHeight > 0 ? state.lineHeight : ToolboxLayoutMetrics::kDefaultLineHeight);
    }
    else
    {
      FontInfo fontInfo;
      GetFontInfo(&fontInfo);
      const short pitch = static_cast<short>(fontInfo.ascent + fontInfo.descent + fontInfo.leading);
      geometry = TextGeometry(pitch, fontInfo.ascent, pitch);
    }
    if (wraps)
    {
      geometry.lines = ToolboxPlainTextLines::Build(native, state.width, geometry.linePitch, wrap, measure);
      if (!geometry.lines.isValid())
        return false;
      const int extra = geometry.lines->height() - (style.size_.isSet() ? geometry.linePitch : state.lineHeight);
      if (geometry.height + extra > SHRT_MAX)
        return false;
      geometry.height = static_cast<short>(geometry.height + extra);
      if (!style.size_.isSet() && truncation == loka::app::TEXT_TRUNCATION_ELLIPSIS)
        geometry.baselineOffset = static_cast<short>(geometry.baselineOffset + extra);
    }
    return true;
  }
} // namespace

ToolboxTextContext::ToolboxTextContext(loka::app::TextNode *node, ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_(),
      textX_(0),
      textY_(0),
      maxWidth_(0),
      wrapMode_(loka::app::TEXT_WRAP_NONE),
      truncationMode_(loka::app::TEXT_TRUNCATION_NONE),
      text_(0)
{
}

ToolboxTextContext::~ToolboxTextContext() {}

loka::app::scene::PaintAnswer ToolboxTextContext::queryPaintDamage(
    const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (query.placement != PLACEMENT_ELIGIBLE || query.scope != ToolboxPaintScope())
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || !this->text_ || this->node_->props.text_ != this->text_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  if (ToolboxPaintIsClippedOut(this->rect_, this->paintRect_, this->deliveredFact()))
    return ToolboxExactPaint(this->paintRect_, false);
  if (!this->presented_.isKnown())
    return PaintAnswer::refused(PAINT_REFUSED_HISTORY_UNKNOWN);
  loka::core::State<loka::core::String> *live = this->liveTextState();
  PaintAnswer answer = ToolboxExactPaint(this->paintRect_, live && !live->get().equals(this->presented_.value()));
  answer.damage.coverage = PAINT_COVERAGE_ERASE_AND_PAINT;
  return answer;
}

void ToolboxTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                      loka::app::scene::NodeLifecycleFact next)
{
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->presented_.invalidate();
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  }
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->clearMeasurement();
  ToolboxProjectedNodeContext::onFactChanged(previous, next);
}

void ToolboxTextContext::clearMeasurement()
{
  this->projection_.clear();
  this->measurement_ = loka::app::MeasurementResult<Constraint, Extent>();
  this->presented_.invalidate();
  SetRect(&this->rect_, 0, 0, 0, 0);
  this->paintRect_ = this->rect_;
}

void ToolboxTextContext::updateData(loka::core::State<loka::core::String> *text)
{
  text_ = text;
}

void ToolboxTextContext::updateRect(const Rect &rect, short textX, short textY)
{
  const Rect previousRect = this->rect_;
  const Rect previousPaintRect = this->paintRect_;

  rect_ = rect;
  this->paintRect_ = rect;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(rect, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  if (!EqualRect(&previousRect, &this->rect_) || !EqualRect(&previousPaintRect, &this->paintRect_) || this->textX_ != textX || this->textY_ != textY)
    this->presented_.invalidate();
  textX_ = textX;
  textY_ = textY;
}

bool ToolboxTextContext::reconcileProjection()
{
  if (!this->text_ || this->deliveredFact() == loka::app::scene::NODE_FACT_RETIRED)
  {
    this->clearMeasurement();
    return false;
  }
  const loka::core::String &value = this->text_->get();
  if (this->projection_.matches(value))
    return true;
  // Geometry ranges index the old bytes; paint history stays, because it
  // records the logical value last painted and drives exact damage (#518).
  this->measurement_ = loka::app::MeasurementResult<Constraint, Extent>();
  if (!this->projection_.build(value))
  {
    // Revoke readiness and history but keep placement: the scene's
    // non-wrapped text-change and redrawTextHit paths only measure and
    // repaint, so a later successful build must still have a rect to paint.
    this->projection_.clear();
    this->presented_.invalidate();
    return false;
  }
  return true;
}

short ToolboxTextContext::visibleWidth()
{
  if (!this->reconcileProjection() || !this->node_ || !this->controller())
    return 0;
  const ToolboxTextFontDescriptor descriptor(this->node_->props.resolvedTextStyle());
  ToolboxTextMeasureScope measure(*this->controller(), descriptor);
  if (!measure.valid())
  {
    this->clearMeasurement();
    return 0;
  }
  Str255 text;
  PlainDisplay(this->projection_, this->maxWidth_, this->truncationMode_, text);
  short width = StringWidth(text);
  const short maxWidth = static_cast<short>(rect_.right - rect_.left);
  if (maxWidth > 0 && width > maxWidth)
    width = maxWidth;
  return width;
}

void ToolboxTextContext::paint(bool erase)
{
  if (!this->node_ || !this->controller() || EmptyRect(&this->rect_))
    return;
  // Same early exit as AttributedText: a placement clipped out by layout
  // owes no pixels, so no port switch and no clip regions.
  if (EmptyRect(&this->paintRect_))
    return;
  if (!this->reconcileProjection())
    return;
  const ToolboxTextFontDescriptor descriptor(this->node_->props.resolvedTextStyle());
  ToolboxTextMeasureScope measure(*this->controller(), descriptor);
  ToolboxPaintClip clip(this->paintRect_);
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  const bool completes = ToolboxPaintCompletes(clip, this->paintRect_, this->presented_,
      this->presented_.isKnown() && this->text_ && this->text_->get().equals(this->presented_.value()));
  this->presented_.invalidate();
  if (!this->text_)
    return;
  if (!measure.valid())
  {
    this->clearMeasurement();
    return;
  }
  if (erase && clip.isActive())
    EraseRect(&this->paintRect_);
  bool painted = false;
  if (this->truncationMode_ != loka::app::TEXT_TRUNCATION_ELLIPSIS
      && this->measurement_.extent().lines.isValid())
  {
    this->measurement_.extent().lines->draw(this->projection_,
                                            this->textX_,
                                            this->textY_,
                                            this->maxWidth_,
                                            this->node_->props.blockStyle_,
                                            this->node_->props.resolvedTextStyle().color_);
    painted = true;
  }
  else if (this->wrapMode_ == loka::app::TEXT_WRAP_NONE || this->maxWidth_ <= 0
           || this->truncationMode_ == loka::app::TEXT_TRUNCATION_ELLIPSIS)
  {
    Str255 text;
    PlainDisplay(this->projection_, this->maxWidth_, this->truncationMode_, text);
    DrawPascalAt(this->textX_,
                 this->textY_,
                 text,
                 this->maxWidth_,
                 this->node_->props.blockStyle_,
                 this->node_->props.resolvedTextStyle().color_);
    painted = true;
  }
  if (painted && completes)
    this->presented_.commit(this->text_->get(), ToolboxPaintScope());
}

void ToolboxTextContext::repaint()
{
  this->paint(true);
}

void ToolboxTextContext::draw(ToolboxScenePlatformController *controller)
{
  if (!this->node_ || !this->controller() || EmptyRect(&this->rect_))
    return;
  // A placement clipped out by layout owes no pixels and no hit row
  // (recordTextHit rejects it too): leave before the measure scope, the
  // paint clip and the hit-width measurement (S1 lane).
  if (EmptyRect(&this->paintRect_))
    return;
  const ToolboxTextFontDescriptor descriptor(this->node_->props.resolvedTextStyle());
  // Paint and the hit-width measurement are one non-yielding transaction.
  ToolboxTextMeasureScope measure(*this->controller(), descriptor);
  this->paint(false);
  if (controller && this->text_)
    controller->recordTextHit(this->rect_, this->textX_, this->textY_, this->text_, this->boundary_,
                              this->wrapMode_ != loka::app::TEXT_WRAP_NONE, this->visibleWidth(), this);
}

short ToolboxTextContext::layout(loka::app::scene::IPlatformController *controller,
                                 loka::app::scene::LayoutState &state)
{
  ToolboxScenePlatformController *toolbox =
      static_cast<ToolboxScenePlatformController *>(controller);
  this->captureProps();
  if (!toolbox || !node_ || !node_->props.text_)
  {
    this->clearMeasurement();
    return 0;
  }
  if (!this->reconcileProjection())
  {
    controller->refuseTextMeasurement(this->node_, state);
    return 0;
  }
  const Constraint constraint(state.width, state.lineHeight);
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->measurement_.reusable(constraint))
  {
    const loka::app::TextStyle style = this->node_->props.resolvedTextStyle();
    const ToolboxTextFontDescriptor descriptor(style);
    ToolboxTextMeasureScope measure(*toolbox, descriptor);
    TextGeometry geometry;
    if (!ResolveTextGeometry(style, state, this->projection_, this->wrapMode_, this->truncationMode_, measure, geometry))
    {
      controller->refuseTextMeasurement(this->node_, state);
      this->clearMeasurement();
      return 0;
    }
    Str255 text;
    PlainDisplay(this->projection_, state.width, this->truncationMode_, text);
    Extent extent(geometry.height, geometry.baselineOffset, StringWidth(text));
    extent.lines = geometry.lines;
    this->measurement_.commit(constraint, extent);
  }
  const Extent &geometry = this->measurement_.extent();
  this->maxWidth_ = state.width > 0 ? state.width : 0;
  const short width = this->maxWidth_ > 0 ? this->maxWidth_ : geometry.measuredWidth;
  Rect rect;
  rect.left = state.x;
  rect.top = state.y;
  rect.right = static_cast<short>(state.x + width);
  rect.bottom = static_cast<short>(state.y + geometry.height);
  updateRect(rect, state.x, static_cast<short>(state.y + geometry.baselineOffset));
  // Advance by the painted box, as the other rails do: y is the top edge.
  state.y = static_cast<short>(rect.bottom);
  return width;
}

void ToolboxTextContext::render(loka::app::scene::IPlatformController *controller)
{
  ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
  draw(toolbox);
}

bool RegisterToolboxTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxTextNodeHandler);
}

bool ToolboxTextContext::captureProps()
{
  loka::core::State<loka::core::String> *text = this->node_ ? this->node_->props.text_ : 0;
  const bool changed = ToolboxTextProjectionChanged(this->text_, text);
  this->updateData(text);
  if (!this->node_)
    return changed;
  wrapMode_ =
      node_->props.blockStyle_.hasWrap_ ? node_->props.blockStyle_.wrap_ : loka::app::TEXT_WRAP_NONE;
  truncationMode_ = node_->props.blockStyle_.hasTruncation_
                        ? node_->props.blockStyle_.truncation_
                        : loka::app::TEXT_TRUNCATION_NONE;
  return changed;
}

void ToolboxTextContext::onPropsApplied()
{
  this->presented_.invalidate();
  const bool changed = this->captureProps();
  if (changed && this->controller() && this->node_)
  {
    this->controller()->refreshContextProps(this->node_);
  }
}
