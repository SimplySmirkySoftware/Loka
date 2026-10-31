/** Included by the production window and the Toolbox host fixture. */
#include "context/ToolboxPaintSupport.hpp"

void ToolboxWindow::drawDirty(const Rect &rect)
{
  if (!window_ || !scenePlatformController_)
  {
    return;
  }
  scenePlatformController_->noteWindowDirtyDraw();
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);
  const Rect extent = scenePlatformController_->dirtyExtent(rect);
  Rect clip = extent;
  const Rect corner = ToolboxWindowSizeBoxRect(window_->portRect);
  Rect growIntersection;
  const bool drawsGrowBox = SectRect(&clip, &corner, &growIntersection) != 0;
  RgnHandle oldClip = NewRgn();
  if (oldClip)
  {
    GetClip(oldClip);
    ClipRect(&clip);
    ToolboxInputDoor::renderDirty(*scenePlatformController_, extent);
    if (drawsGrowBox)
    {
      SetClip(oldClip);
      ClipRect(&growIntersection);
      this->drawGrowBox();
    }
    SetClip(oldClip);
    DisposeRgn(oldClip);
  }
  else
  {
    ToolboxInputDoor::renderDirty(*scenePlatformController_, extent);
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(scenePlatformController_), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  }
  SetPort(oldPort);
}

void ToolboxWindow::drawGrowBox()
{
  if (!this->window_)
  {
    return;
  }
  RgnHandle oldClip = NewRgn();
  if (!oldClip)
  {
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->scenePlatformController_), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    return;
  }
  GetClip(oldClip);
  const Rect corner = ToolboxWindowSizeBoxRect(this->window_->portRect);
  ClipRect(&corner);
  DrawGrowIcon(this->window_);
  SetClip(oldClip);
  DisposeRgn(oldClip);
}

void ToolboxWindow::drawWhole(const Rect &evidence)
{
  if (!window_)
    return;

  if (scenePlatformController_)
  {
    scenePlatformController_->noteWindowDraw();
  }
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);

  // This draw repaints the whole window, so an OS update already pending
  // would repaint it again (#1104). Validate before the walk, never after it:
  // whatever the OS invalidates during the walk (a modal closing inside it)
  // stays pending.
  ValidRect(&window_->portRect);
  {
    ToolboxPaintClip clip(window_->portRect, ToolboxPaintClip::REPLACE);
    ToolboxPortErasePresenter presenter(window_->portRect);
    if (scenePlatformController_)
    {
      Rect captureEvidence = {0, 0, 0, 0};
      if (clip.isActive())
        captureEvidence = ToolboxCaptureEvidence(window_->portRect, evidence);
      ToolboxErasedPass::Guard erased(scenePlatformController_->erasedPass(), captureEvidence);
      ToolboxInputDoor::render(*scenePlatformController_, &presenter);
    }
    else
      presenter.eraseBeforePaint();
    this->drawGrowBox();
#ifdef TEST_BUILD
    presenter.finishDrawForTesting();
#endif
  }

  SetPort(oldPort);
}
