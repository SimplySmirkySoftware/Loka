#include "context/ToolboxPaintSupport.hpp"
#include "platform/ToolboxPascalText.hpp"

/** Shared kind-ordered text repaint; preserve the caller's dirty clip. */
void ToolboxScenePlatformController::redrawTextHit(TextHit &hit)
{
  if (!window_ || !window_->window())
  {
    return;
  }
  if (hit.context)
  {
    GrafPtr previous;
    GetPort(&previous);
    SetPort(this->window_->window());
    hit.context->repaint();
    hit.lastMeasuredWidth = hit.context->visibleWidth();
    SetPort(previous);
    return;
  }
  short measuredWidth = hit.text ? this->measureTextWidth(hit.text->get()) : 0;
  const short maxWidth = static_cast<short>(hit.rect.right - hit.rect.left);
  if (maxWidth > 0 && measuredWidth > maxWidth)
  {
    measuredWidth = maxWidth;
  }
  Rect dirtyRect = hit.rect;
  short redrawWidth = hit.lastMeasuredWidth;
  if (measuredWidth > redrawWidth)
  {
    redrawWidth = measuredWidth;
  }
  if (maxWidth > 0 && redrawWidth > maxWidth)
  {
    redrawWidth = maxWidth;
  }
  dirtyRect.right = static_cast<short>(dirtyRect.left + redrawWidth);
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_->window());
  {
    ToolboxPaintClip clip(dirtyRect, ToolboxPaintClip::INTERSECT);
    if (clip.isActive())
    {
      EraseRect(&dirtyRect);
      Str255 text;
      if (hit.text && ToolboxEncodePascal(hit.text->get(), text))
      {
        MoveTo(hit.x, hit.y);
        DrawString(text);
      }
      else
        LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_TEXT_CONVERSION);
      // A refused paint must retain the old presented extent for the retry.
      hit.lastMeasuredWidth = measuredWidth;
    }
    else if (!EmptyRect(&dirtyRect))
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  }
  SetPort(oldPort);
}
