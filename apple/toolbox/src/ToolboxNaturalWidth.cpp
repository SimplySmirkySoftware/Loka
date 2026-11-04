#include "ToolboxScenePlatformController.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "platform/ToolboxUtf8Bytes.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include <climits>
#include "ToolboxCharWidthCache.hpp"
#include "platform/ToolboxPascalText.hpp"

bool ToolboxScenePlatformController::measureTextAdvance(const loka::app::TextStyle &style,
                                                        const loka::core::String &text,
                                                        short &width) const
{
  GrafPtr port = this->window_ ? reinterpret_cast<GrafPtr>(this->window_->window()) : 0;
  Str255 bytes;
  if (!port || !ToolboxEncodePascal(text, bytes))
    return false;
  const ToolboxTextSelection selection =
      ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(style), ToolboxTextPortFont::read(port));
  ToolboxCharWidthCache *cache = this->charWidthCache();
  do
  {
    if (!ToolboxCharWidthEnvironment(port) || !cache)
      break;
    const ToolboxCharWidthCache::Slot *slot = cache->find(selection);
    if (!slot && cache->full())
      break;
    const ToolboxCharWidthKind kind = slot ? slot->kind : ToolboxClassifyCharWidth(selection);
    if (kind == TOOLBOX_CHAR_REFUSED)
    {
      if (!slot)
        cache->refuse(selection);
      break;
    }
    if (!ToolboxCharWidthBytes(kind, bytes))
      break;
    if (slot)
    {
      if (slot->widths->sum(bytes, width))
        return true;
      break;
    }
    ToolboxTextMeasureScope scope(*this, selection);
    slot = cache->build(selection, kind, scope);
    if (slot && slot->widths->sum(bytes, width))
      return true;
    width = StringWidth(bytes);
    return true;
  } while (false);

  ToolboxTextMeasureScope scope(*this, selection);
  width = StringWidth(bytes);
  return true;
}

bool ToolboxScenePlatformController::measurePushButtonNaturalWidth(const loka::core::String &label,
                                                                   short &width) const
{
  short text = 0;
  ToolboxTextMeasureScope scope(*this);
  if (!scope.measure(label, text))
    return false;
  const long total = static_cast<long>(text) + 2 * ToolboxLayoutMetrics::kPushButtonTitleInset;
  if (total <= 0 || total > SHRT_MAX)
    return false;
  width = static_cast<short>(total);
  return true;
}

short ToolboxScenePlatformController::measurePushButtonNaturalWidth(const loka::core::String &label) const
{
  short width = 0;
  if (this->measurePushButtonNaturalWidth(label, width))
    return width;
  // A refused measurement keeps the standalone layout's inset-only width.
  return static_cast<short>(2 * ToolboxLayoutMetrics::kPushButtonTitleInset);
}

namespace
{
  /** CR/LF check on the borrowed UTF-8 bytes; copies only when the String cannot lend them.
      Measured on maciix (Toolbar rally 1, page 1c §10): the copy cost about 0.1 ms per query. */
  bool QuerySingleLine(const loka::core::String &value, bool &singleLine)
  {
    const ToolboxUtf8Bytes view(value);
    if (!view.valid())
      return false;
    singleLine = true;
    for (std::size_t i = 0; i < view.length(); ++i)
      if (view.bytes()[i] == '\r' || view.bytes()[i] == '\n')
      {
        singleLine = false;
        break;
      }
    return true;
  }
} // namespace

bool ToolboxScenePlatformController::queryNaturalWidth(loka::app::scene::Node *child, short &width) const
{
  if (loka::app::ImageViewNode *image = child ? child->asImageViewNode() : 0)
  {
    // No GrafPort or measurement scope is needed; portless controllers answer too.
    // Keep 1:1 source width aligned with ToolboxImageViewContext::layout's INTRINSIC branch.
    int pixels = 0;
    if (!loka::app::queryIntrinsicImageWidth(image->props, pixels) || pixels > SHRT_MAX)
      return false;
    width = static_cast<short>(pixels);
    return true;
  }
  loka::app::ButtonNode *button = child ? child->asButtonNode() : 0;
  if (button)
  {
    const loka::core::String label =
        button->props.text_ ? button->props.text_->get() : loka::core::String::Literal("Button");
    // A refused measurement declines, so the Row keeps a shared seat.
    return this->measurePushButtonNaturalWidth(label, width);
  }
  loka::app::TextNode *text = child ? child->asTextNode() : 0;
  if (!text || !text->props.text_)
    return false;
  const loka::core::String value = text->props.text_->get();
  // One line only: a line break declines, so multi-line Text keeps a shared seat.
  bool singleLine = false;
  if (!QuerySingleLine(value, singleLine) || !singleLine)
    return false;
  short measured = 0;
  if (!this->measureTextAdvance(text->props.resolvedTextStyle(), value, measured) || measured <= 0)
    return false;
  width = measured;
  return true;
}

bool ToolboxScenePlatformController::queryNaturalHeight(loka::app::scene::Node *child, short &height) const
{
  loka::app::TextNode *text = child ? child->asTextNode() : 0;
  if (!text || !text->props.text_)
    return false;
  if (text->props.blockStyle_.hasWrap_ && text->props.blockStyle_.wrap_ != loka::app::TEXT_WRAP_NONE)
    return false;
  const loka::app::TextStyle style = text->props.resolvedTextStyle();
  if (!style.size_.isSet())
    return false;
  bool singleLine = false;
  if (!QuerySingleLine(text->props.text_->get(), singleLine) || !singleLine)
    return false;
  ToolboxTextMeasureScope scope(*this, ToolboxTextFontDescriptor(style));
  long pitch = 0;
  short baseline = 0;
  if (!scope.queryLineMetrics(pitch, baseline) || pitch < 1 || pitch > SHRT_MAX)
    return false;
  height = static_cast<short>(pitch);
  return true;
}
