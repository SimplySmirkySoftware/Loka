/** Shared by production and the host: the walker owns missing-context refusal. */
void RenderDirtyRectSurfaces(loka::app::scene::Node *node,
                             ToolboxScenePlatformController *controller,
                             const Rect &dirtyRect)
{
  if (!node)
  {
    return;
  }
  if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
  {
    ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(surface->getContext());
    if (ctx)
    {
      ctx->renderDirty(dirtyRect);
    }
    else
      LOKA_PAINT_REFUSE(ToolboxPaintRefusals(controller), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    return;
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      RenderDirtyRectSurfaces(child, controller, dirtyRect);
    }
  }
  (void)controller;
}
