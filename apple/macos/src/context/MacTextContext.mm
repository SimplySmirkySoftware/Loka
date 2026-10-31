#include "MacTextContext.hpp"
#include <cassert>
#include <cmath>
#include <climits>
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

namespace
{
  /** Null font preserves the historical unstyled paths. Styled single lines
      keep height 20 unless ceil(ascender + |descender| + leading) exceeds it. */
  struct TextGeometry
  {
    TextGeometry(NSFont *selectedFont, int height) : font(selectedFont), minimumHeight(height) {}
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
    const int lineHeight = controller->projection().measurementToLu(
        [font ascender] + std::fabs([font descender]) + [font leading]);
    return TextGeometry(font, lineHeight > fallback ? lineHeight : fallback);
  }

  /** One cell measurement home for constrained layout and unbounded width.
      A fresh label supplies the same native default font as the live context. */
  bool MeasureTextCell(NSString *string, NSFont *selectedFont, loka::app::TextWrap wrap, NSRect bounds, NSSize &size)
  {
    NSTextField *label = [[[NSTextField alloc] initWithFrame:NSZeroRect] autorelease];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    NSTextFieldCell *cell = [label cell];
    NSFont *font = selectedFont ? selectedFont : [cell font];
    if (!cell || !font)
      return false;
    [cell setStringValue:string];
    [cell setFont:font];
    [cell setWraps:wrap != loka::app::TEXT_WRAP_NONE];
    [cell setScrollable:wrap == loka::app::TEXT_WRAP_NONE];
    // Twin: applyStyle configures the presented cell with these wrapping modes.
    [cell setLineBreakMode:wrap == loka::app::TEXT_WRAP_NONE   ? NSLineBreakByClipping
                           : wrap == loka::app::TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping
                                                               : NSLineBreakByWordWrapping];
    size = [cell cellSizeForBounds:bounds];
    return true;
  }

  // Miss only: materialize this leaf's text and measure one temporary cell if wrapped.
  bool MeasureTextHeightForWidth(const loka::app::TextNode *text,
                                 const loka::macos::MacLength &width,
                                 int defaultHeight, NSFont *selectedFont,
                                 const loka::macos::MacProjection &projection,
                                 int &height, NSString *&string)
  {
    height = defaultHeight;
    string = @"";
    if (!text || !text->props.text_)
      return true;
    std::string utf8;
    if (!loka::platform::CollectUtf8(text->props.text_->get(), utf8))
      return false;
    string = [NSString stringWithUTF8String:utf8.c_str()];
    if (!string)
      return false;
    if (utf8.empty() || width.pt <= 0 || !text->props.blockStyle_.hasWrap_
        || text->props.blockStyle_.wrap_ == loka::app::TEXT_WRAP_NONE)
      return true;

    NSFont *font = selectedFont ? selectedFont : [NSFont systemFontOfSize:[NSFont systemFontSize]];
    NSSize size;
    if (!font
        || !MeasureTextCell(
            string, font, text->props.blockStyle_.wrap_, loka::macos::MacMeasurementBounds(width), size))
      return false;
    const int measuredWithPadding = projection.measurementToLu(size.height) + 2;
    if (measuredWithPadding > height)
      height = measuredWithPadding;
    return true;
  }

  static void SetUsesSingleLineModeCompat(NSTextField *label, BOOL value)
  {
    if (!label)
    {
      return;
    }
    NSCell *cell = [label cell];
    if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)])
    {
      [cell setUsesSingleLineMode:value];
    }
  }

  class MacTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<MacTextNodeHandler,
                                                     loka::app::TextNode,
                                                     MacTextContext>
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
      return new MacTextContext(
          mac, mac->projectionParentView(), state.x, state.y, state.width, state.height, text);
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
  std::string utf8;
  if (!loka::platform::CollectUtf8(text->props.text_->get(), utf8) || utf8.empty()
      || utf8.find_first_of("\n\r") != std::string::npos)
    return false;
  NSString *string = [NSString stringWithUTF8String:utf8.c_str()];
  if (!string)
    return false;
  const TextGeometry geometry = ResolveTextGeometry(text->props.resolvedTextStyle(), controller);
  if (HasTextFontStyle(text->props.resolvedTextStyle()) && !geometry.font)
    return false;
  NSSize size;
  if (!MeasureTextCell(
          string, geometry.font, loka::app::TEXT_WRAP_NONE, loka::macos::MacUnboundedMeasurementBounds(), size))
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
      parentView_(parentView),
      label_(0),
      originalFont_(0),
      textState_(0),
      textStateBound_(false),
      didInitialApply_(false)
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
  this->originalFont_ = (void *)[[[label cell] font] retain];
  this->applyStyle(true);

  if (parent)
  {
    [parent addSubview:label];
  }

  bindText();
}

MacTextContext::~MacTextContext()
{
  assert(!label_ && "terminal fact delivery must queue the native view before context reclaim");
  [(NSFont *)this->originalFont_ release];
}

void MacTextContext::presentLabel(NSString *text, const loka::app::TextStyle &resolved, bool initial)
{
  NSTextField *label = (NSTextField *)this->label_;
  if (!label)
    return;
  const TextGeometry geometry = ResolveTextGeometry(resolved, this->controller());
  NSFont *font = geometry.font ? geometry.font : (NSFont *)this->originalFont_;
  NSColor *color = nil;
  if (!loka::macos::QueryMacTextColor(resolved.color_, color))
  {
    // A fresh initWithFrame label already has this native default. Retained
    // set -> unset must restore it (fresh-label equality is pinned in tests).
    color = initial ? [label textColor] : loka::macos::MacLabelTextColor();
  }
  // applyStyle may lend the current native string; keep it alive across the
  // clearing attributed write before the plain setters consume it.
  text = [[text retain] autorelease];
  bool underline = false;
  resolved.queryUnderline(underline);
  bool leftover = false;
  if (!underline)
  {
    NSAttributedString *current = [label attributedStringValue];
    const NSUInteger length = [current length];
    // Empty values must never be indexed. Inspect this one attribute over the
    // whole value: an underline may begin after an undecorated first run.
    for (NSUInteger index = 0; index < length;)
    {
      NSRange range;
      NSNumber *value = [current attribute:NSUnderlineStyleAttributeName atIndex:index effectiveRange:&range];
      if ([value intValue] != 0)
      {
        leftover = true;
        break;
      }
      index = NSMaxRange(range);
    }
  }
  // Per presentation pass: one value when on, or one clearing write only when
  // native state still carries underline. No remembered presentation flag.
  // The cell font is set first on both paths, so applyStyle's geometry check
  // compares against the resolved font whether or not the value is attributed.
  [[label cell] setFont:font];
  if (underline || leftover)
  {
    NSMutableAttributedString *value = [[NSMutableAttributedString alloc] initWithString:text];
    const NSRange range = NSMakeRange(0, [value length]);
    [value addAttribute:NSFontAttributeName value:font range:range];
    [value addAttribute:NSForegroundColorAttributeName value:color range:range];
    if (underline)
      [value addAttribute:NSUnderlineStyleAttributeName
                    value:[NSNumber numberWithInt:NSUnderlineStyleSingle]
                    range:range];
    // Twin of the cell's line-break mode and alignment: an attributed value
    // without a paragraph style lays out with AppKit's defaults (word wrap),
    // not the cell's, and would no longer match the measuring cell.
    NSMutableParagraphStyle *paragraph = [[NSMutableParagraphStyle alloc] init];
    [paragraph setLineBreakMode:[[label cell] lineBreakMode]];
    [paragraph setAlignment:[label alignment]];
    [value addAttribute:NSParagraphStyleAttributeName value:paragraph range:range];
    [paragraph release];
    [label setAttributedStringValue:value];
    [value release];
  }
  if (!underline)
  {
    [label setStringValue:text];
    [label setTextColor:color];
  }
  [label setNeedsDisplay:YES];
}

bool MacTextContext::applyStyle(bool initial)
{
  NSTextField *label = (NSTextField *)this->label_;
  if (!this->node_ || !label)
    return false;
  // Presentation is reconciled even when geometry is unchanged.
  NSTextFieldCell *cell = [label cell];
  const loka::app::TextStyle resolved = this->node_->props.resolvedTextStyle();
  const TextGeometry geometry = ResolveTextGeometry(resolved, this->controller());
  NSFont *font = geometry.font ? geometry.font : (NSFont *)this->originalFont_;
  bool changed = [cell font] != font;
  const loka::app::BlockStyle &attr = this->node_->props.blockStyle_;
  const bool wrapWord = attr.hasWrap_ && attr.wrap_ == loka::app::TEXT_WRAP_WORD;
  const bool wrapChar = attr.hasWrap_ && attr.wrap_ == loka::app::TEXT_WRAP_CHAR;
  const BOOL wraps = wrapWord || wrapChar ? YES : NO;
  NSLineBreakMode mode = NSLineBreakByClipping;
  if (wraps)
    mode = wrapChar ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping;
  else if (attr.hasTruncation_ && attr.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS)
    mode = NSLineBreakByTruncatingTail;
  // An undeclared Text keeps AppKit's defaults (#814); the block configuration
  // below runs only once a style field was declared.
  if (this->node_->props.hasDeclaredLayoutStyle()
      && (initial || [cell wraps] != wraps || [cell isScrollable] != !wraps || [cell lineBreakMode] != mode))
  {
    SetUsesSingleLineModeCompat(label, !wraps);
    [cell setWraps:wraps];
    [cell setScrollable:!wraps];
    [cell setLineBreakMode:mode];
    changed = true;
  }
  {
    // Twin: MacAttributedTextContext::Projection::build sets paragraph alignment.
    // Reconciled on every apply: an undeclared style asks for AppKit's default
    // (natural), so a retained style removal restores it.
    NSTextAlignment alignment = LOKA_MAC_TEXT_ALIGNMENT_NATURAL;
    if (this->node_->props.hasDeclaredLayoutStyle())
    {
      switch (attr.hasAlign_ ? attr.align_ : loka::app::TEXT_ALIGN_LEFT)
      {
      case loka::app::TEXT_ALIGN_LEFT: alignment = LOKA_MAC_TEXT_ALIGNMENT_LEFT; break;
      case loka::app::TEXT_ALIGN_CENTER: alignment = LOKA_MAC_TEXT_ALIGNMENT_CENTER; break;
      case loka::app::TEXT_ALIGN_RIGHT: alignment = LOKA_MAC_TEXT_ALIGNMENT_RIGHT; break;
      }
    }
    if ([label alignment] != alignment)
    {
      [label setAlignment:alignment];
      changed = true;
    }
  }
  // After the cell's wrapping and alignment are current, so an attributed
  // value copies the settings this pass applied.
  this->presentLabel([label stringValue], resolved, initial);
  return changed;
}

void MacTextContext::onPropsApplied()
{
  if (this->applyStyle())
  {
    [(NSTextField *)this->label_ setNeedsDisplay:YES];
    if (this->controller())
      this->controller()->requestRelayout();
  }
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
      this->clearMeasurement();
      this->unbindText();
      [(NSTextField *)this->label_ removeFromSuperview];
      this->retireNativeObject(this->label_);
      this->node_ = 0;
      this->parentView_ = 0;
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
  return this->controller()->captureViewBitmap(this->label_, out);
}

void MacTextContext::clearMeasurement()
{
  // Refusal/retirement: revoke presentation and placement together.
  this->measurement_.invalidate();
  this->presentLabel(@"", this->node_ ? this->node_->props.resolvedTextStyle() : loka::app::TextStyle());
  this->relayout(0, 0, 0, 0);
}

short MacTextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  // Per visited leaf: reconcile native style even on the O(1) measurement hit.
  this->applyStyle();
  const loka::macos::MacProjection &projection = this->controller()->projection();
  const loka::macos::MacLength constraint = projection.projectLength(0, state.width);
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->measurement_.reusable(constraint.pt))
  {
    const TextGeometry geometry = ResolveTextGeometry(
        this->node_ ? this->node_->props.resolvedTextStyle() : loka::app::TextStyle(), this->controller());
    int height = 0;
    NSString *string = nil;
    if (!this->label_ || !MeasureTextHeightForWidth(
        this->node_, constraint, geometry.minimumHeight, geometry.font, projection, height, string))
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearMeasurement();
      state.height = 0;
      return static_cast<short>(state.y);
    }
    // Also restores presentation after refusal without needing a new State publication.
    this->presentLabel(string, this->node_ ? this->node_->props.resolvedTextStyle() : loka::app::TextStyle());
    this->measurement_.commit(constraint.pt, height);
  }
  const int textHeight = this->measurement_.extent();
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
    applyText();
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

void MacTextContext::applyText()
{
  NSTextField *label = (NSTextField *)label_;
  if (!label || !textState_)
  {
    return;
  }
  std::string utf8;
  if (loka::platform::CollectUtf8(textState_->get(), utf8))
  {
    this->presentLabel(loka::macos::CreateNSStringFromUtf8(utf8), this->node_->props.resolvedTextStyle());
  }
  requestRelayoutIfNeeded();
  if (!didInitialApply_)
  {
    didInitialApply_ = true;
  }
}

void MacTextContext::requestRelayoutIfNeeded()
{
  if (!didInitialApply_ || !node_ || !node_->props.blockStyle_.hasWrap_)
  {
    return;
  }
  if (node_->props.blockStyle_.wrap_ == loka::app::TEXT_WRAP_NONE)
  {
    return;
  }
  NSView *view = (NSView *)parentView_;
  if (!view)
  {
    return;
  }
  // Projected text may be parented to a ScrollView document. Find the first
  // registered ancestor without storing a second root pointer in the context
  // or assuming that an embedded controller root is the topmost native view.
  MacScenePlatformController *controller = 0;
  while (view && !controller)
  {
    controller = MacScenePlatformController::findForRootView((void *)view);
    view = [view superview];
  }
  if (!controller)
  {
    return;
  }
  // Defer relayout to app flush tick to avoid re-entering layout on notify stack.
  controller->requestRelayout();
}

void MacTextContext::TextChangedThunk(void *userData)
{
  MacTextContext *self = static_cast<MacTextContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterMacTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gMacTextNodeHandler);
}
