#include "MacTextContext.hpp"
#include <cassert>
#include <cmath>
#include <climits>
#include <algorithm>
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "Utf8String.hpp"
#include <AppKit/AppKit.h>
#include "app/nodes/Text.hpp"
#include "core/State.hpp"
#include "platform/StringUTF8.hpp"
#include "core/resource/Image.hpp"

MacTextVersion::Cell::Cell()
    : wraps(false),
      scrollable(false),
      singleLine(false),
      lineBreakMode(0),
      alignment(0),
      font(0),
      colour(0)
{
}

MacTextVersion::MacTextVersion()
    : value_(0),
      cell_(),
      wraps_(false),
      measure_()
{
}

MacTextVersion::MacTextVersion(void *value, const Cell &cell, bool wraps, const Measure &measure)
    : value_((void *)[(NSAttributedString *)value retain]),
      cell_(cell),
      wraps_(wraps),
      measure_(measure)
{
  [(NSFont *)this->cell_.font retain];
  [(NSColor *)this->cell_.colour retain];
}

MacTextVersion::MacTextVersion(const MacTextVersion &other)
    : value_((void *)[(NSAttributedString *)other.value_ retain]),
      cell_(other.cell_),
      wraps_(other.wraps_),
      measure_(other.measure_)
{
  [(NSFont *)this->cell_.font retain];
  [(NSColor *)this->cell_.colour retain];
}

MacTextVersion::~MacTextVersion()
{
  [(NSAttributedString *)this->value_ release];
  [(NSFont *)this->cell_.font release];
  [(NSColor *)this->cell_.colour release];
}

void MacTextVersion::swap(MacTextVersion &other)
{
  std::swap(this->value_, other.value_);
  std::swap(this->cell_, other.cell_);
  std::swap(this->wraps_, other.wraps_);
  std::swap(this->measure_, other.measure_);
}

MacTextVersion &MacTextVersion::operator=(const MacTextVersion &other)
{
  MacTextVersion copy(other);
  this->swap(copy);
  return *this;
}

namespace
{
  /** Null font preserves the historical unstyled paths. Styled single lines
      keep height 20 unless ceil(ascender + |descender| + leading) exceeds it. */
  struct TextGeometry
  {
    TextGeometry(NSFont *selectedFont, int height)
        : font(selectedFont),
          minimumHeight(height)
    {
    }
    NSFont *const font;
    const int minimumHeight;
  };

  bool HasTextFontStyle(const loka::app::TextStyle &style)
  {
    return style.declaresFont();
  }

  TextGeometry ResolveTextGeometry(const loka::app::TextStyle &style, const MacScenePlatformController *controller)
  {
    const int fallback = loka::app::layout::FallbackControlMetrics::kTextHeight;
    if (!controller || !HasTextFontStyle(style))
      return TextGeometry(nil, fallback);
    NSFont *font = (NSFont *)controller->textFont(style);
    const int lineHeight =
        controller->projection().measurementToLu([font ascender] + std::fabs([font descender]) + [font leading]);
    return TextGeometry(font, lineHeight > fallback ? lineHeight : fallback);
  }

  bool SingleLine(NSCell *cell)
  {
    return [cell respondsToSelector:@selector(usesSingleLineMode)]
               ? (BOOL)(long)[cell performSelector:@selector(usesSingleLineMode)] != NO
               : false;
  }

  /** The native door is shared by draft construction and presentation. Setting
      cell fields can change the attributed value; callers restore it afterward. */
  bool ApplyCell(NSTextFieldCell *cell, const MacTextVersion::Cell &c)
  {
    bool changed = false;
    if (SingleLine(cell) != c.singleLine && [cell respondsToSelector:@selector(setUsesSingleLineMode:)])
    {
      [cell setUsesSingleLineMode:c.singleLine];
      changed = true;
    }
    if ([cell wraps] != c.wraps)
    {
      [cell setWraps:c.wraps];
      changed = true;
    }
    if ([cell isScrollable] != c.scrollable)
    {
      [cell setScrollable:c.scrollable];
      changed = true;
    }
    if ([cell lineBreakMode] != static_cast<NSLineBreakMode>(c.lineBreakMode))
    {
      [cell setLineBreakMode:static_cast<NSLineBreakMode>(c.lineBreakMode)];
      changed = true;
    }
    if ([cell alignment] != static_cast<NSTextAlignment>(c.alignment))
    {
      [cell setAlignment:static_cast<NSTextAlignment>(c.alignment)];
      changed = true;
    }
    if ([cell font] != (NSFont *)c.font)
    {
      [cell setFont:(NSFont *)c.font];
      changed = true;
    }
    if (![[cell textColor] isEqual:(NSColor *)c.colour])
    {
      [cell setTextColor:(NSColor *)c.colour];
      changed = true;
    }
    return changed;
  }

  /** Every exit releases the draft, its copied value, and then its pool. */
  class TextDraft
  {
  public:
    TextDraft()
        : pool_([[NSAutoreleasePool alloc] init]),
          field_([[NSTextField alloc] initWithFrame:NSZeroRect]),
          value_(nil)
    {
      [this->field_ setEditable:NO];
      [this->field_ setSelectable:NO];
      [this->field_ setBezeled:NO];
      [this->field_ setDrawsBackground:NO];
    }
    ~TextDraft()
    {
      [this->value_ release];
      [this->field_ release];
      [this->pool_ release];
    }
    NSTextFieldCell *cell() const
    {
      return [this->field_ cell];
    }
    NSAttributedString *copyValue()
    {
      assert(!this->value_);
      this->value_ = [[this->cell() attributedStringValue] copy];
      return this->value_;
    }

  private:
    NSAutoreleasePool *pool_;
    NSTextField *field_;
    NSAttributedString *value_;
    TextDraft(const TextDraft &);
    TextDraft &operator=(const TextDraft &);
  };

  /** Reads only this Text's current inputs. Natural width uses the same draft
      with its historical three-field policy, leaving singleLine/alignment fresh. */
  MacTextVersion BuildTextVersion(const loka::app::TextProps &props,
                                  const MacScenePlatformController *controller,
                                  const loka::macos::MacLength *constraint,
                                  const MacTextVersion::Measure &oldMeasure,
                                  NSSize *naturalSize)
  {
    TextDraft draft;
    NSTextFieldCell *cell = draft.cell();
    if (!cell || !controller)
      return MacTextVersion();
    const loka::app::TextStyle style = props.resolvedTextStyle();
    const loka::app::BlockStyle block = props.blockStyle_;
    const bool declared = props.hasDeclaredLayoutStyle();
    const TextGeometry geometry = ResolveTextGeometry(style, controller);
    MacTextVersion::Cell c;
    c.wraps = [cell wraps];
    c.scrollable = [cell isScrollable];
    c.singleLine = SingleLine(cell);
    c.lineBreakMode = [cell lineBreakMode];
    c.alignment = [cell alignment];
    c.font = style.declaresFont() ? (void *)geometry.font : (void *)[cell font];
    NSColor *colour = nil;
    if (!loka::macos::QueryMacTextColor(style.color_, colour))
      colour = loka::macos::MacLabelTextColor();
    c.colour = (void *)colour;
    const bool wraps = block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE;
    if (naturalSize)
    {
      c.wraps = false;
      c.scrollable = true;
      c.lineBreakMode = NSLineBreakByClipping;
    }
    else if (declared)
    {
      c.wraps = wraps;
      c.scrollable = !wraps;
      c.singleLine = !wraps;
      c.lineBreakMode =
          wraps ? (block.wrap_ == loka::app::TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping)
                : (block.hasTruncation_ && block.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS
                       ? NSLineBreakByTruncatingTail
                       : NSLineBreakByClipping);
      switch (block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT)
      {
      case loka::app::TEXT_ALIGN_LEFT:
        c.alignment = LOKA_MAC_TEXT_ALIGNMENT_LEFT;
        break;
      case loka::app::TEXT_ALIGN_CENTER:
        c.alignment = LOKA_MAC_TEXT_ALIGNMENT_CENTER;
        break;
      case loka::app::TEXT_ALIGN_RIGHT:
        c.alignment = LOKA_MAC_TEXT_ALIGNMENT_RIGHT;
        break;
      }
    }
    else
      c.alignment = LOKA_MAC_TEXT_ALIGNMENT_NATURAL;
    if (!c.font || !c.colour)
      return MacTextVersion();
    ApplyCell(cell, c);
    std::string utf8;
    if (props.text_ && !loka::platform::CollectUtf8(props.text_->get(), utf8))
      return MacTextVersion();
    if (naturalSize && (utf8.empty() || utf8.find_first_of("\n\r") != std::string::npos))
      return MacTextVersion();
    NSString *string = [NSString stringWithUTF8String:utf8.c_str()];
    if (!string)
      return MacTextVersion();
    [cell setStringValue:string];
    NSAttributedString *value = draft.copyValue();
    if (!value)
      return MacTextVersion();
    MacTextVersion::Measure measure;
    int height = geometry.minimumHeight;
    if (naturalSize || (constraint && wraps && !utf8.empty() && constraint->pt > 0))
    {
      [cell setAttributedStringValue:value];
      const NSRect bounds =
          naturalSize ? loka::macos::MacUnboundedMeasurementBounds() : loka::macos::MacMeasurementBounds(*constraint);
      const NSSize size = [cell cellSizeForBounds:bounds];
      if (naturalSize)
        *naturalSize = size;
      else
      {
        const int padded = controller->projection().measurementToLu(size.height) + 2;
        if (padded > height)
          height = padded;
      }
    }
    if (constraint)
      measure.commit(constraint->pt, height);
    else if (!wraps && !naturalSize)
    {
      CGFloat previousConstraint = 0;
      if (oldMeasure.queryConstraint(previousConstraint))
        measure.commit(previousConstraint, height);
    }
    return MacTextVersion((void *)value, c, wraps, measure);
  }

  bool ShapeNeedsRelayout(const MacTextVersion &old, const MacTextVersion &next)
  {
    if (!next.value() || !old.value())
      return true;
    const MacTextVersion::Cell &a = old.cell(), &b = next.cell();
    if (a.font != b.font || a.wraps != b.wraps || a.scrollable != b.scrollable || a.singleLine != b.singleLine
        || a.lineBreakMode != b.lineBreakMode || a.alignment != b.alignment || old.wraps() != next.wraps())
      return true;
    return next.wraps()
           && ![[(NSAttributedString *)old.value() string] isEqualToString:[(NSAttributedString *)next.value() string]];
  }

  class MacTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacTextNodeHandler, loka::app::TextNode, MacTextContext>
  {
  public:
    static loka::app::TextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asTextNode() : 0;
    }

    static MacTextContext *create(loka::app::TextNode *text,
                                  loka::app::scene::IPlatformController *controller,
                                  const loka::app::scene::LayoutState &state)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return new MacTextContext(mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, text);
    }
  };

  MacTextNodeHandler gMacTextNodeHandler;
} // namespace

bool MacTextContext::queryNaturalWidth(const loka::app::TextNode *text,
                                       const MacScenePlatformController *controller,
                                       short &width)
{
  if (!controller || !text || !text->props.text_)
    return false;
  NSSize size = NSZeroSize;
  const MacTextVersion version = BuildTextVersion(text->props, controller, 0, MacTextVersion::Measure(), &size);
  if (!version.value())
    return false;
  // Match the button answer: bound points before narrowing, including NaN refusal.
  const loka::app::Ratio &scale = controller->projection().railMetrics().spaceScale;
  if (!(size.width > 0 && size.width <= static_cast<double>(SHRT_MAX) * scale.num / scale.den))
    return false;
  width = static_cast<short>(controller->projection().measurementToLu(size.width));
  return true;
}

bool MacTextContext::queryNaturalHeight(const loka::app::TextNode *text,
                                        const MacScenePlatformController *controller,
                                        short &height)
{
  if (!controller || !text || !text->props.text_
      || (text->props.blockStyle_.hasWrap_ && text->props.blockStyle_.wrap_ != loka::app::TEXT_WRAP_NONE))
    return false;
  std::string utf8;
  if (!loka::platform::CollectUtf8(text->props.text_->get(), utf8) || utf8.find_first_of("\n\r") != std::string::npos)
    return false;
  const TextGeometry geometry = ResolveTextGeometry(text->props.resolvedTextStyle(), controller);
  if (geometry.minimumHeight <= 0 || geometry.minimumHeight > SHRT_MAX)
    return false;
  height = static_cast<short>(geometry.minimumHeight);
  return true;
}

MacTextContext::MacTextContext(MacScenePlatformController *controller,
                               void *parentView,
                               int x,
                               int y,
                               int width,
                               int height,
                               loka::app::TextNode *node)
    : MacRetirableContext(controller),
      node_(node),
      label_(0),
      textState_(0),
      textStateBound_(false)
{
  NSView *parent = (NSView *)parentView;
  const loka::macos::MacRect frame =
      this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height));
  NSTextField *label = [[NSTextField alloc] initWithFrame:frame.r];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  this->label_ = (void *)label;
  this->version_ = this->buildVersion(0);
  this->present(true);

  if (parent)
  {
    [parent addSubview:label];
  }

  bindText();
}

MacTextContext::~MacTextContext()
{
  assert(!label_ && "terminal fact delivery must queue the native view before context reclaim");
  assert(!this->version_.value() && "terminal fact delivery must release the version before context reclaim");
}

MacTextVersion MacTextContext::buildVersion(const loka::macos::MacLength *constraint) const
{
  if (!this->node_ || !this->label_)
    return MacTextVersion();
  return BuildTextVersion(this->node_->props, this->controller(), constraint, this->version_.measure(), 0);
}

void MacTextContext::present(bool newVersion)
{
  NSTextField *label = (NSTextField *)this->label_;
  if (!label)
    return;
  if (!this->version_.value())
  {
    [label setStringValue:@""];
    return;
  }
  const bool changed = ApplyCell([label cell], this->version_.cell());
  if (changed || newVersion)
  {
    [label setAttributedStringValue:(NSAttributedString *)this->version_.value()];
    [label setNeedsDisplay:YES];
  }
}

void MacTextContext::replaceFromProps()
{
  const MacTextVersion next = this->buildVersion(0);
  const bool relayout = ShapeNeedsRelayout(this->version_, next);
  this->version_ = next;
  this->present(true);
  if (relayout)
    this->controller()->requestRelayout();
}

void MacTextContext::onPropsApplied()
{
  if (!this->node_)
    return;
  // Platform twin: Win32TextContext::onPropsApplied classifies literals and
  // changed live sources identically; macOS keeps deferred binding delivery.
  if (this->node_->props.ownsText())
    this->unbindText();
  else if (this->node_->props.text_ != this->textState_)
  {
    this->unbindText();
    this->bindText();
  }
  this->replaceFromProps();
}

void MacTextContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void MacTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                   loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // DETACHED_RETAINED hides; terminal RETIRED keeps the same policy
    // (hide before the ritual destroys the native pair).
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindText();
      this->version_ = MacTextVersion();
      this->present(true);
      this->relayout(0, 0, 0, 0);
      [(NSTextField *)this->label_ removeFromSuperview];
      this->retireNativeObject(this->label_);
      this->node_ = 0;
    }
  }
}

void MacTextContext::applyAttachedPresentation()
{
  NSTextField *label = (NSTextField *)label_;
  if (label)
  {
    [label setHidden:NO];
  }
}

void MacTextContext::applyDetachedPresentation()
{
  NSTextField *label = (NSTextField *)label_;
  if (label)
  {
    [label setHidden:YES];
  }
}

bool MacTextContext::captureBitmap(loka::core::resource::Image &out) const
{
  return this->version_.value() && this->controller()->captureViewBitmap(this->label_, out);
}

short MacTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const loka::macos::MacLength constraint = this->controller()->projection().projectLength(0, state.width);
  const bool miss = state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->version_.value()
                    || !this->version_.measure().reusable(constraint.pt);
  if (miss)
    this->version_ = this->buildVersion(&constraint);
  this->present(miss);
  if (!this->version_.value())
  {
    this->controller()->refuseTextMeasurement(this->node_, state);
    this->relayout(0, 0, 0, 0);
    state.height = 0;
    return static_cast<short>(state.y);
  }
  const int textHeight = this->version_.measure().extent();
  this->relayout(state.x, state.y, state.width, textHeight);
  state.height = static_cast<short>(textHeight);
  return static_cast<short>(state.y + textHeight);
}

void MacTextContext::relayout(int x, int y, int width, int height)
{
  NSTextField *label = (NSTextField *)label_;
  if (!label)
  {
    return;
  }
  if (loka::macos::ApplyMacFrame(label,
                                 this->controller()->projection().projectFrame(loka::core::Frame(x, y, width, height))))
    [label setNeedsDisplay:YES];
}

void MacTextContext::bindText()
{
  if (!node_)
  {
    return;
  }
  textState_ = static_cast<loka::core::State<loka::core::String> *>(node_->props.text_);
  textStateBound_ = false;
  if (textState_)
  {
    if (!node_->props.ownsText())
    {
      textState_->deferBind(&MacTextContext::TextChangedThunk, this);
      textStateBound_ = true;
    }
  }
}

void MacTextContext::unbindText()
{
  if (textState_)
  {
    if (textStateBound_)
    {
      textState_->deferUnbind(&MacTextContext::TextChangedThunk, this);
    }
    textState_ = 0;
    textStateBound_ = false;
  }
}

void MacTextContext::TextChangedThunk(void *userData)
{
  MacTextContext *self = static_cast<MacTextContext *>(userData);
  if (self)
  {
    self->replaceFromProps();
  }
}

void RegisterMacTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacTextNodeHandler);
}
