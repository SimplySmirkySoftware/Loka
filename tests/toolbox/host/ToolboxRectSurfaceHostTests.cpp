#include "ToolboxNodeDispatch.hpp"
#include "context/ToolboxRectSurfaceContext.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
  Rect clip = {SHRT_MIN, SHRT_MIN, SHRT_MAX, SHRT_MAX};
  std::vector<Rect> paints;
  std::vector<Rect> unions;
  unsigned refuseRegions = 0;
}
// Rectangular QuickDraw subset: these pins union only one visible sprite, so
// a bounding rectangle is an exact region. Record inputs before native clipping.
void SetRect(Rect *r, short l, short t, short right, short b)
{ r->left = l; r->top = t; r->right = right; r->bottom = b; }
bool EmptyRect(const Rect *r) { return r->left >= r->right || r->top >= r->bottom; }
bool EqualRect(const Rect *a, const Rect *b)
{ return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
bool SectRect(const Rect *a, const Rect *b, Rect *out)
{
  const Rect r = {std::max(a->top, b->top), std::max(a->left, b->left),
                  std::min(a->bottom, b->bottom), std::min(a->right, b->right)};
  *out = r;
  return !EmptyRect(out);
}
RgnHandle NewRgn()
{
  if (refuseRegions)
  {
    --refuseRegions;
    return 0;
  }
  RgnHandle r = new Region *;
  *r = new Region;
  (**r).rgnSize = sizeof(Region);
  SetRect(&(**r).rgnBBox, 0, 0, 0, 0);
  return r;
}
void DisposeRgn(RgnHandle r) { delete *r; delete r; }
void GetClip(RgnHandle r) { (**r).rgnBBox = clip; }
void SetClip(RgnHandle r) { clip = (**r).rgnBBox; }
void RectRgn(RgnHandle r, const Rect *rect) { (**r).rgnBBox = *rect; }
void SetEmptyRgn(RgnHandle r) { SetRect(&(**r).rgnBBox, 0, 0, 0, 0); }
void SectRgn(RgnHandle a, RgnHandle b, RgnHandle out)
{ SectRect(&(**a).rgnBBox, &(**b).rgnBBox, &(**out).rgnBBox); }
void UnionRgn(RgnHandle a, RgnHandle b, RgnHandle out)
{
  const Rect x = (**a).rgnBBox, y = (**b).rgnBBox;
  unions.push_back(y);
  const Rect r = {std::min(x.top, y.top), std::min(x.left, y.left),
                  std::max(x.bottom, y.bottom), std::max(x.right, y.right)};
  (**out).rgnBBox = EmptyRect(&x) ? y : EmptyRect(&y) ? x : r;
}
bool RectInRgn(const Rect *rect, RgnHandle r)
{ Rect out; return SectRect(rect, &(**r).rgnBBox, &out); }
void BackColor(long) {}
void EraseRect(const Rect *) {}
void PaintRect(const Rect *r) { if (!EmptyRect(r)) paints.push_back(*r); }

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  void checkRect(const Rect &r, short l, short t, short right, short bottom)
  {
    std::printf("rect: %d,%d,%d,%d expected: %d,%d,%d,%d\n",
                r.left, r.top, r.right, r.bottom, l, t, right, bottom);
    std::fflush(stdout);
    LOKA_VERIFY(r.left == l && r.top == t && r.right == right && r.bottom == bottom);
  }
  void layoutPin(const char *mode)
  {
    ToolboxScenePlatformController controller;
    loka::core::MutableState<RectSurfaceModel> model((RectSurfaceModel()));
    RectSurfaceNode node(RectSurfaceProps().model(&model));
    LayoutState state;
    state.x = 10; state.y = 20; state.width = 100; state.height = 50; state.spacing = 3;
    const bool normal = std::strcmp(mode, "normal") == 0;
    const bool limit = std::strcmp(mode, "limit") == 0;
    const bool scroll = std::strcmp(mode, "scroll-bottom") == 0;
    if (std::strcmp(mode, "right") == 0) state.width = 32760;
    else if (std::strcmp(mode, "spacing") == 0) state.height = SHRT_MAX - 20;
    else if (!normal) { state.height = limit ? SHRT_MAX - 20 : 32760; state.spacing = 0; }
    ProjectionParentScope scope;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, 0, loka::core::Frame(0, 0, 80, 80), scope));
    if (scroll)
      LOKA_VERIFY(controller.projectionParentScopes_.pushViewport(scope));
    const short width = LayoutNode(&node, state, &controller, 0);
    std::printf("layout: width=%d y=%d extent records=%u\n", width, state.y, controller.extentRecords);
    std::fflush(stdout);
    if (normal || limit || std::strcmp(mode, "spacing") == 0)
    {
      LOKA_VERIFY(width == 100 && state.y == (normal ? 70 : SHRT_MAX));
      LOKA_VERIFY(controller.extentRecords == 1);
      Rect bounds;
      LOKA_VERIFY(static_cast<ToolboxRectSurfaceContext *>(node.getContext())->dirtyRect(bounds));
      checkRect(bounds, 10, 20, 110, normal ? 70 : SHRT_MAX);
    }
    else
    {
      LOKA_VERIFY(width == 0 && state.y == 20 && controller.extentRecords == 0);
      if (node.getContext())
      {
        Rect bounds;
        LOKA_VERIFY(!static_cast<ToolboxRectSurfaceContext *>(node.getContext())->dirtyRect(bounds));
      }
      if (scroll) LOKA_VERIFY(controller.projectionParentScopes_.current().hasShortRangeRefusal());
    }
    if (scroll) controller.projectionParentScopes_.pop();
  }
  void paintRefusalPin(const char *mode)
  {
    ToolboxScenePlatformController controller;
    loka::core::MutableState<RectSurfaceModel> model((RectSurfaceModel()));
    RectSurfaceNode node(RectSurfaceProps().model(&model).useRegionClip(true));
    const bool fallback = std::strcmp(mode, "paint-fallback") == 0;
    if (fallback)
      refuseRegions = 3;
    node.setContext(new ToolboxRectSurfaceContext(&node, &controller));
    ToolboxRectSurfaceContext *context = static_cast<ToolboxRectSurfaceContext *>(node.getContext());
    LayoutState placement;
    placement.x = 20;
    placement.y = 20;
    placement.width = 100;
    placement.height = 100;
    context->layout(&controller, placement);
    context->render(&controller);
    controller.paintObservation.reset();
    const bool missing = std::strcmp(mode, "paint-missing") == 0;
    const bool dirtyMissing = std::strcmp(mode, "paint-dirty-missing") == 0;
    const bool disjoint = std::strcmp(mode, "paint-disjoint") == 0;
    const bool query = std::strcmp(mode, "paint-query") == 0;
    const bool clipRefused = std::strcmp(mode, "paint-clip") == 0;
    if (missing || dirtyMissing || disjoint || query)
      node.props.model_ = 0;
    Rect rect = {20, 20, 120, 120};
    if (disjoint)
      SetRect(&rect, 200, 200, 300, 300);
    if (clipRefused)
      refuseRegions = 2;
    if (query)
    {
      LOKA_VERIFY(!context->dirtyRect(rect));
    }
    else if (missing || clipRefused)
      context->render(&controller);
    else
      context->renderDirty(rect);
    LOKA_VERIFY(controller.paintObservation.outsideRefusal().reason
                == (clipRefused               ? TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION
                    : missing || dirtyMissing ? TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET
                                              : TOOLBOX_PAINT_REFUSAL_NONE));
  }
  void spritePin(const char *mode)
  {
    RectSurfaceModel model;
    model.rectCount = 1;
    model.rects[0] = RectSprite(0, 0, 32760, 32760);
    const bool normal = std::strcmp(mode, "sprite-normal") == 0;
    const bool outside = std::strcmp(mode, "sprite-outside") == 0;
    const bool negative = std::strcmp(mode, "sprite-negative") == 0;
    const bool current = std::strcmp(mode, "dirty-current") == 0;
    const bool history = std::strcmp(mode, "dirty-history") == 0;
    const bool region = std::strcmp(mode, "region") == 0;
    if (normal) model.rects[0] = RectSprite(3, 4, 5, 6);
    if (outside) model.rects[0] = RectSprite(32760, 32760, 32760, 32760);
    if (negative) model.rects[0] = RectSprite(-32760, -32760, 32767, 32767);
    loka::core::MutableState<RectSurfaceModel> fact(model);
    loka::core::PushStateTracker tracker;
    tracker.addState(&fact);
    RectSurfaceNode node(RectSurfaceProps().model(&fact).useRegionClip(true));
    // The node owns its real context. No private access or production test door.
    node.setContext(new ToolboxRectSurfaceContext(&node, 0));
    ToolboxRectSurfaceContext *ctx = static_cast<ToolboxRectSurfaceContext *>(node.getContext());
    LayoutState state;
    state.x = negative ? -100 : 20; state.y = state.x;
    state.width = 100; state.height = 100;
    LOKA_VERIFY(node.layout(0, state) == 100);
    if (current) { model.rectCount = 0; loka::core::StateTrackerGuard transaction(&tracker); fact.set(model); }
    ctx->render(0);
    if (current || history)
    {
      model.rectCount = current ? 1 : 0;
      { loka::core::StateTrackerGuard transaction(&tracker); fact.set(model); }
      Rect dirty;
      LOKA_VERIFY(ctx->dirtyRect(dirty));
      checkRect(dirty, 20, 20, 120, 120);
    }
    else if (region)
    {
      Rect seat = {20, 20, 120, 120};
      ctx->renderDirty(seat);
      std::printf("region unions=%lu\n", static_cast<unsigned long>(unions.size()));
      std::fflush(stdout);
      LOKA_VERIFY(!unions.empty());
      checkRect(unions[0], 20, 20, 120, 120);
    }
    else if (outside)
    {
      LOKA_VERIFY(paints.empty());
      Rect dirty;
      LOKA_VERIFY(!ctx->dirtyRect(dirty));
    }
    else
    {
      LOKA_VERIFY(paints.size() == 1);
      if (normal) checkRect(paints[0], 23, 24, 28, 30);
      else if (negative) checkRect(paints[0], -100, -100, -93, -93);
      else checkRect(paints[0], 20, 20, 120, 120);
    }
  }
}
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (std::strncmp(argv[1], "paint-", 6) == 0)
    paintRefusalPin(argv[1]);
  else if (std::strncmp(argv[1], "sprite-", 7) == 0 || std::strncmp(argv[1], "dirty-", 6) == 0
           || std::strcmp(argv[1], "region") == 0)
    spritePin(argv[1]);
  else layoutPin(argv[1]);
  return 0;
}
