#include "context/ToolboxClipBoxContext.hpp"
#include "context/ToolboxPaintSupport.hpp"

/** One flagged Box/Card paint visit reads its own latest placement in O(1).
    The existing guards own both clips; refusal restores either successful half
    before the ordinary child walk. No placement or child geometry is changed. */
void ToolboxScenePlatformController::renderClipBox(loka::app::scene::Node *node)
{
  using namespace loka::app::scene;
  NodeContext *installed = node->getContext();
  IClipBoxContext *clipContext = installed ? installed->asClipBoxContext() : 0;
  const ToolboxClipBoxContext *context = clipContext ? static_cast<ToolboxClipBoxContext *>(clipContext) : 0;
  Rect rect;
  if (context && context->placement().query(rect))
  {
    Rect clipped = rect;
    if (this->projectionParentScopes_.activeDepth() != 0)
      this->intersectWithProjectionClip(rect, clipped);
    ProjectionParentScope frame = this->projectionParentScopes_.current();
    frame.clipRect =
        loka::core::Frame(clipped.left, clipped.top, clipped.right - clipped.left, clipped.bottom - clipped.top);
    ToolboxPaintClip clip(rect, ToolboxPaintClip::INTERSECT);
    ProjectionClipScopeGuard guard(this->projectionParentScopes_, frame);
    if (clip.isActive() && guard.isActive())
    {
      RenderChildren(node->asNestable(), this);
      return;
    }
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  }
  else
    LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
  RenderChildren(node->asNestable(), this);
}
