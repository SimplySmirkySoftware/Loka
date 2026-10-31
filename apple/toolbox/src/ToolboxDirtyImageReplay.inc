// Dirty-image replay body. The controller includes it inside its anonymous
// namespace; the host image-view tests include the same body.
void RenderDirtyImageViews(loka::app::scene::Node *node,
                           ToolboxScenePlatformController *controller,
                           const Rect &dirtyRect)
{
  if (!node)
  {
    return;
  }
  if (loka::app::ImageViewNode *image = node->asImageViewNode())
  {
    ToolboxImageViewContext *ctx = static_cast<ToolboxImageViewContext *>(image->getContext());
    if (!ctx)
      LOKA_PAINT_REFUSE(ToolboxPaintRefusals(controller), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    Rect intersection;
    if (ctx && SectRect(&ctx->rect(), &dirtyRect, &intersection)
        && SectRect(&intersection, &ctx->paintRect(), &intersection))
    {
      // Same erase-before-render ritual as composition replay. Unit-touching
      // rects take that replay through the ledger gate; disjoint ImageViews
      // have only window ground beneath.
      EraseRect(&intersection);
      ctx->render(controller);
    }
    return;
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      RenderDirtyImageViews(child, controller, dirtyRect);
    }
  }
}
