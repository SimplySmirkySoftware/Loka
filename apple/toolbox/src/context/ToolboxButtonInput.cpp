/** Included by the native context and the host input fixture. */
bool ToolboxButtonContext::handleMouseDown(const Point &point, ToolboxScenePlatformController *controller)
{
  if (enabled_ && !enabled_->get())
  {
    return false;
  }
  if (!PtInRect(point, &rect_))
  {
    return false;
  }
  if (controller)
  {
    controller->emitHitEmitter(emitter_);
  }
  return true;
}
