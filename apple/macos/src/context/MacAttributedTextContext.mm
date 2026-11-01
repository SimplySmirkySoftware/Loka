#include "MacAttributedTextContext.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
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

  /** Coalesce logical equals before decoding/ranging. NSString lengths are
      UTF-16 units, including two units for supplementary code points. */
  NSString *NextRun(const loka::app::AttributedString &value, std::size_t &index)
  {
    const loka::app::TextStyle &style = value.segment(index).style;
    std::string joined;
    do
    {
      std::string bytes;
      if (!loka::platform::CollectUtf8(value.segment(index).text, bytes))
        return nil;
      joined.append(bytes);
      ++index;
    } while (index < value.segmentCount() && value.segment(index).style == style);
    return [[[NSString alloc] initWithBytes:joined.data() length:joined.size()
                                   encoding:NSUTF8StringEncoding] autorelease];
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
  this->measurement_.invalidate();
  [(NSAttributedString *)this->value_ release];
  this->value_ = 0;
}

bool MacAttributedTextContext::Projection::build(const loka::app::AttributedString &value,
                                                 const loka::app::BlockStyle &block,
                                                 const MacScenePlatformController &controller,
                                                 const loka::macos::MacLength &constraint)
{
  // Miss only: walk this value's runs, then measure one whole native string.
  this->clear();
  if (!value.valid())
    return false;
  NSMutableString *joined = [[[NSMutableString alloc] init] autorelease];
  if (!joined)
    return false;
  for (std::size_t index = 0; index < value.segmentCount();)
  {
    NSString *run = NextRun(value, index);
    if (!run)
      return false;
    [joined appendString:run];
  }
  NSMutableParagraphStyle *paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
  if (!paragraph)
    return false;
  [paragraph setLineBreakMode:LineBreakMode(block)];
  // Twin: MacTextContext::applyStyle configures the plain label.
  switch (block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT)
  {
  case loka::app::TEXT_ALIGN_LEFT: [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_LEFT]; break;
  case loka::app::TEXT_ALIGN_CENTER: [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_CENTER]; break;
  case loka::app::TEXT_ALIGN_RIGHT: [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT]; break;
  }
  NSDictionary *defaults = [NSDictionary dictionaryWithObject:paragraph forKey:NSParagraphStyleAttributeName];
  if (!defaults)
    return false;
  NSMutableAttributedString *result = [[[NSMutableAttributedString alloc] initWithString:joined
                                                                              attributes:defaults] autorelease];
  if (!result)
    return false;
  NSUInteger offset = 0;
  for (std::size_t index = 0; index < value.segmentCount();)
  {
    const loka::app::TextStyle &style = value.segment(index).style;
    NSFont *font = (NSFont *)controller.textFont(style);
    // An unset run carries the label's native default explicitly, so it never
    // depends on how an AppKit version fills attributes the string leaves out.
    NSColor *color = nil;
    if (!loka::macos::QueryMacTextColor(style.color_, color))
      color = loka::macos::MacLabelTextColor();
    NSString *run = NextRun(value, index);
    if (!font || !run)
      return false;
    const NSUInteger length = [run length];
    [result addAttribute:NSFontAttributeName value:font range:NSMakeRange(offset, length)];
    [result addAttribute:NSForegroundColorAttributeName value:color range:NSMakeRange(offset, length)];
    bool on = false;
    // Same attribute as TextDraft in MacTextContext.mm; two builders for two node kinds (page 1c O-P4).
    if (style.queryUnderline(on) && on)
      [result addAttribute:NSUnderlineStyleAttributeName
                     value:[NSNumber numberWithInt:NSUnderlineStyleSingle]
                     range:NSMakeRange(offset, length)];
    offset += length;
  }
  NSTextFieldCell *measure = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  if (!measure)
    return false;
  [measure setAttributedStringValue:result];
  ConfigureCell(measure, block);
  const NSSize size = [measure cellSizeForBounds:loka::macos::MacMeasurementBounds(constraint)];
  // The builder never escapes as mutable storage. Commit only a complete owner.
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
