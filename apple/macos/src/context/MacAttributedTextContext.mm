#include "MacAttributedTextContext.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include "core/Utf8Decode.hpp"
#include <AppKit/AppKit.h>
#include <cassert>
#include <climits>
#include <new>

namespace
{
  NSLineBreakMode LineBreakMode(const loka::app::BlockStyle &block)
  {
    if (block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE)
      return block.wrap_ == loka::app::TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping;
    return block.hasTruncation_ && block.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS
               ? NSLineBreakByTruncatingTail
               : NSLineBreakByClipping;
  }

  /** Both the measuring cell and the installed cell cross this same door. */
  void ConfigureCell(NSTextFieldCell *cell, const loka::app::BlockStyle &block)
  {
    const BOOL wraps = block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE;
    if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)])
      [cell setUsesSingleLineMode:NO];
    [cell setWraps:wraps];
    // NONE disables soft wrapping; explicit LF still separates paragraphs.
    [cell setScrollable:NO];
    [cell setLineBreakMode:LineBreakMode(block)];
  }

  /** Byte bounds are construction-local and relative to the current style run. */
  struct ByteBound
  {
    std::size_t begin, end;
  };

  /** Validate the whole run before publishing any source interval. NSString may
      discard a leading BOM; the display remains native even when sources differ. */
  bool AgreesWithUtf16(const std::string &bytes, NSString *run)
  {
    const NSUInteger length = [run length];
    NSUInteger unit = 0;
    for (std::size_t byte = 0; byte < bytes.size();)
    {
      unsigned int scalar = 0;
      if (!loka::core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), byte, scalar))
        return false;
      // Twin: core/TextUnits.hpp's scalar-to-UTF-16 pair formula.
      const NSUInteger count = scalar > 0xFFFF ? 2 : 1;
      const unsigned int pair = scalar > 0xFFFF ? scalar - 0x10000 : 0;
      for (NSUInteger k = 0; k < count; ++k)
      {
        const unichar expected = static_cast<unichar>(count == 1 ? scalar
                                                      : k == 0   ? 0xD800 + (pair >> 10)
                                                                 : 0xDC00 + (pair & 0x3FF));
        if (unit >= length || [run characterAtIndex:unit] != expected)
          return false;
        ++unit;
      }
    }
    return unit == length;
  }

  class MacAttributedTextNodeHandler : public loka::app::scene::RetainedNodeHandler<MacAttributedTextNodeHandler,
                                                                                    loka::app::AttributedTextNode,
                                                                                    MacAttributedTextContext>
  {
  public:
    static loka::app::AttributedTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asAttributedTextNode() : 0;
    }
    static MacAttributedTextContext *create(loka::app::AttributedTextNode *node,
                                            loka::app::scene::IPlatformController *controller,
                                            const loka::app::scene::LayoutState &)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      MacAttributedTextContext *context =
          new (std::nothrow) MacAttributedTextContext(mac, mac->projectionParentView(), node);
      // A context whose only native view could not be created would be
      // installed forever and never retried; refuse it so a later ensure
      // can build a complete one (AGENTS "Failure-atomicity policy").
      if (context && !context->hasNativeLabel())
      {
        delete context;
        return 0;
      }
      return context;
    }
  };
  MacAttributedTextNodeHandler handler;

  short Coordinate(int value)
  {
    return static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value < SHRT_MIN ? SHRT_MIN : value);
  }
} // namespace

bool MacAttributedTextContext::MacTextSourceTable::queryProvenance(NSUInteger start,
                                                                   NSUInteger end,
                                                                   loka::app::TextSegmentRange &out) const
{
  if (start >= end)
    return false;
  std::size_t first = 0, last = 0;
  for (NSUInteger unit = start; unit < end;)
  {
    // Upper bound: the last start <= unit wins ties with preceding empties.
    // Search again at each covered boundary; never walk intervening empty slots.
    std::size_t low = 0, high = this->slots_.size();
    while (low < high)
    {
      const std::size_t middle = low + (high - low) / 2;
      if (this->slots_[middle].start <= unit)
        low = middle + 1;
      else
        high = middle;
    }
    if (!low || unit >= this->slots_[low - 1].end)
      return false;
    last = low - 1;
    if (unit == start)
      first = last;
    unit = this->slots_[last].end;
  }
  out = loka::app::TextSegmentRange(first).through(last);
  return true;
}

bool MacAttributedTextContext::Projection::queryProvenance(NSUInteger start,
                                                           NSUInteger end,
                                                           loka::app::TextSegmentRange &out) const
{
  if (!this->value_ || start >= end)
    return false;
  // Coverage also enforces end <= native length: no slot can extend beyond it.
  // This keeps the query native-call-free without retaining a duplicate length.
  return this->sources_.queryProvenance(start, end, out);
}

MacAttributedTextContext::Projection::Projection()
    : value_(0)
{
}
MacAttributedTextContext::Projection::~Projection()
{
  this->clear();
}

void MacAttributedTextContext::Projection::clear()
{
  this->sources_.clear();
  this->measurement_.invalidate();
  [(NSAttributedString *)this->value_ release];
  this->value_ = 0;
}

bool MacAttributedTextContext::Projection::build(const loka::app::AttributedString &value,
                                                 const loka::app::BlockStyle &block,
                                                 const MacScenePlatformController &controller,
                                                 const loka::macos::MacLength &constraint)
{
  // One failure exit: fill's locals (bounds, byte buffers) are released when it
  // returns, and a refused fill leaves no partial value, measurement or slots.
  this->clear();
  if (this->fill(value, block, controller, constraint))
    return true;
  this->clear();
  return false;
}

bool MacAttributedTextContext::Projection::fill(const loka::app::AttributedString &value,
                                                const loka::app::BlockStyle &block,
                                                const MacScenePlatformController &controller,
                                                const loka::macos::MacLength &constraint)
{
  if (!value.valid() || !this->sources_.reserve(value.segmentCount()))
    return false;
  loka::app::detail::TextMeasureTable<ByteBound> bounds;
  if (!bounds.allocate(value.segmentCount()))
    return false;
  NSMutableAttributedString *result = [[[NSMutableAttributedString alloc] initWithString:@""] autorelease];
  if (!result)
    return false;
  std::size_t index = 0;
  for (; index < value.segmentCount();)
  {
    const std::size_t first = index;
    const loka::app::TextStyle &style = value.segment(first).style;
    std::string bytes;
    do
    {
      bounds[index].begin = bytes.size();
      std::string part;
      if (!loka::platform::CollectUtf8(value.segment(index).text, part))
        return false;
      bytes.append(part);
      bounds[index].end = bytes.size();
      ++index;
    } while (index < value.segmentCount() && value.segment(index).style == style);
    NSString *run = [[[NSString alloc] initWithBytes:bytes.data() length:bytes.size()
                                            encoding:NSUTF8StringEncoding] autorelease];
    NSFont *font = (NSFont *)controller.textFont(style);
    // Missing fonts refuse even a zero-length run, as in the original builder.
    if (!run || !font)
      return false;
    const NSUInteger base = [result length];
    NSColor *color = nil;
    if (!loka::macos::QueryMacTextColor(style.color_, color))
      color = loka::macos::MacLabelTextColor();
    NSMutableDictionary *attributes = [NSMutableDictionary
        dictionaryWithObjectsAndKeys:font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    if (!attributes)
      return false;
    bool on = false;
    // Twin: TextDraft in MacTextContext.mm uses the same underline attribute.
    if (style.queryUnderline(on) && on)
      [attributes setObject:[NSNumber numberWithInt:NSUnderlineStyleSingle] forKey:NSUnderlineStyleAttributeName];
    NSAttributedString *part = [[[NSAttributedString alloc] initWithString:run attributes:attributes] autorelease];
    if (!part)
      return false;
    [result appendAttributedString:part];
    if (!AgreesWithUtf16(bytes, run))
    {
      for (std::size_t segment = first; segment < index; ++segment)
        this->sources_.slot(segment).start = this->sources_.slot(segment).end = base;
    }
    else
    {
      std::size_t byte = 0;
      NSUInteger unit = 0;
      for (std::size_t segment = first; segment < index; ++segment)
      {
        MacTextSourceSlot &slot = this->sources_.slot(segment);
        slot.start = slot.end = base + unit;
        while (byte < bounds[segment].end)
        {
          const std::size_t begin = byte;
          unsigned int scalar = 0;
          if (!loka::core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), byte, scalar))
            return false;
          const NSUInteger count = scalar > 0xFFFF ? 2 : 1;
          // A scalar crossing any segment edge has no owner (both surrogates).
          if (begin >= bounds[segment].begin && byte <= bounds[segment].end)
            slot.end = base + unit + count;
          unit += count;
        }
      }
    }
    // The next base is result.length, including the native decoder's BOM rule.
  }
  NSMutableParagraphStyle *paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
  if (!paragraph)
    return false;
  [paragraph setLineBreakMode:LineBreakMode(block)];
  // Twin: MacTextContext::applyStyle configures the plain label.
  switch (block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT)
  {
  case loka::app::TEXT_ALIGN_LEFT:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_LEFT];
    break;
  case loka::app::TEXT_ALIGN_CENTER:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_CENTER];
    break;
  case loka::app::TEXT_ALIGN_RIGHT:
    [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
    break;
  }
  [result addAttribute:NSParagraphStyleAttributeName value:paragraph range:NSMakeRange(0, [result length])];
  NSTextFieldCell *measure = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  if (!measure)
    return false;
  [measure setAttributedStringValue:result];
  ConfigureCell(measure, block);
  const NSSize size = [measure cellSizeForBounds:loka::macos::MacMeasurementBounds(constraint)];
#ifdef LOKA_LIFECYCLE_AUDIT
  for (std::size_t segment = 0; segment < value.segmentCount(); ++segment)
  {
    const MacTextSourceSlot &slot = this->sources_.slot(segment);
    assert(slot.start <= slot.end && slot.end <= [result length]);
    if (segment)
      assert(this->sources_.slot(segment - 1).end <= slot.start);
  }
#endif
  this->value_ = (void *)[result copy];
  if (!this->value_)
    return false;
  this->measurement_.commit(constraint.pt, controller.projection().measurementToLu(size.height));
  return true;
}

MacAttributedTextContext::MacAttributedTextContext(MacScenePlatformController *controller,
                                                   void *parentView,
                                                   loka::app::AttributedTextNode *node)
    : MacRetirableContext(controller),
      node_(node),
      label_(0)
{
  assert(controller && controller->textShaping() == loka::app::WHOLE_LINE);
  NSTextField *label = [[NSTextField alloc] initWithFrame:NSZeroRect];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  this->label_ = (void *)label;
  if (parentView && label)
    [(NSView *)parentView addSubview:label];
}

MacAttributedTextContext::~MacAttributedTextContext()
{
  assert(!this->label_ && "retirement must queue the native view before reclaim");
  assert(!this->projection_.value() && "retirement must drop derived text before reclaim");
}

void MacAttributedTextContext::clearProjection()
{
  [(NSTextField *)this->label_ setStringValue:@""];
  this->projection_.clear();
}

void MacAttributedTextContext::onPropsApplied()
{
  // The #965 props mark also forces a rebuild, including baked paragraph alignment.
  this->clearProjection();
  this->controller()->requestRelayout();
}

void MacAttributedTextContext::readLifecycleFactOnAttach()
{
  [(NSTextField *)this->label_ setHidden:this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED];
}

void MacAttributedTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact,
                                             loka::app::scene::NodeLifecycleFact next)
{
  [(NSTextField *)this->label_ setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
  if (next == loka::app::scene::NODE_FACT_RETIRED)
  {
    this->clearProjection();
    [(NSTextField *)this->label_ removeFromSuperview];
    this->retireNativeObject(this->label_);
    this->node_ = 0;
  }
}

short MacAttributedTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const loka::macos::MacLength constraint = projection.projectLength(state.x, state.x + state.width);
  NSTextField *label = (NSTextField *)this->label_;
  // Per visited leaf: O(1) hit, no run walk or native measurement; always place.
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->projection_.reusable(constraint))
  {
    this->clearProjection();
    if (label && this->node_ && this->node_->props.text_
        && this->projection_.build(this->node_->props.text_->get(), this->node_->props.blockStyle_,
                                   *this->controller(), constraint))
    {
      [label setAttributedStringValue:(NSAttributedString *)this->projection_.value()];
      ConfigureCell([label cell], this->node_->props.blockStyle_);
    }
    else
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearProjection();
    }
  }
  state.height = Coordinate(this->projection_.height());
  loka::macos::SetMacFrame(
      label,
      this->controller()->projection().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height)));
  [label setNeedsDisplay:YES];
  return Coordinate(state.y + state.height);
}

void RegisterMacAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}
