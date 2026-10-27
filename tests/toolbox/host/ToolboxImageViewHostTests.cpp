#include "ToolboxHost.hpp"
#include "ToolboxNativeImage.hpp"
#include "context/ToolboxImageViewContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
#include "ToolboxDirtyImageReplay.inc"
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka::core::resource;
  std::vector<Rect> pictureClips, pictureDestinations, lineClips;

  void same(const Rect &actual, const Rect &expected)
  {
    std::printf("rect actual=%d,%d,%d,%d expected=%d,%d,%d,%d\n",
                actual.left,
                actual.top,
                actual.right,
                actual.bottom,
                expected.left,
                expected.top,
                expected.right,
                expected.bottom);
    std::fflush(stdout);
    LOKA_VERIFY(EqualRect(&actual, &expected));
  }
  void clearDrawing()
  {
    toolbox_host::reset();
    toolbox_host::erasedRects.clear();
    toolbox_host::paintOperations.clear();
    pictureClips.clear();
    pictureDestinations.clear();
    lineClips.clear();
  }
  LayoutState seat(short y = 10)
  {
    LayoutState state;
    state.x = 10;
    state.y = y;
    state.width = 20;
    state.height = 20;
    state.lineHeight = 20;
    return state;
  }
  void
  place(ToolboxImageViewContext &context, ToolboxScenePlatformController &controller, const Rect &clip, int offsetY = 0)
  {
    ProjectionParentScope scrolled;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, offsetY, Frame(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top), scrolled));
    ProjectionViewportScopeGuard viewport(controller.projectionParentScopes_, scrolled);
    LOKA_VERIFY(viewport.isActive());
    LayoutState state = seat();
    LOKA_VERIFY(controller.projectLayoutState(state));
    context.layout(&controller, state);
    LOKA_VERIFY(controller.restoreProjectedLayoutState(state));
  }
  Image picture(bool bytes)
  {
    if (bytes)
    {
      Blob blob = Blob::Create();
      const std::vector<unsigned char> data(sizeof(Picture) + sizeof(long) * 8 + 8, 0);
      LOKA_VERIFY(blob.tryAssign(&data[0], data.size()));
      blob.sealBytes();
      return loka::toolbox::MakeImageFromPictBlob(blob, 0, data.size(), 80, 40);
    }
    static Picture pic;
    static PicPtr ptr = &pic;
    return loka::toolbox::MakeImageFromPicHandle(&ptr, 80, 40);
  }
} // namespace

char **NewHandle(Size size)
{
  char **handle = new char *;
  *handle = new char[size];
  return handle;
}
void KillPicture(PicHandle picture)
{
  delete[] reinterpret_cast<char *>(*picture);
  delete reinterpret_cast<char **>(picture);
}
void DrawPicture(PicHandle, const Rect *rect)
{
  pictureClips.push_back(toolbox_host::currentClip());
  pictureDestinations.push_back(*rect);
}
void LineTo(short x, short y)
{
  lineClips.push_back(toolbox_host::currentClip());
  MoveTo(x, y);
}

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const std::string mode(argv[1]);
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  const Rect ambient = {0, 0, 100, 100};
  const Rect frame = {10, 10, 30, 30};
  const Rect half = {10, 10, 20, 30};
  window.port.native.clip = ambient;
  MutableState<Image> image(picture(mode == "bytes-cover"));
  if (mode == "no-image")
    image.set(Image());
  loka::toolbox::ToolboxNativeImage unknown = {
      loka::toolbox::kToolboxNativeImageMagic, loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_UNKNOWN, 0};
  if (mode == "diagonal")
    image.set(Image::FromNative(&unknown, 80, 40, 0, 0));
  ImageViewProps props;
  props.image(&image).size(20, 20).attr(
      ImageViewAttr().fit(mode == "pict-cover" || mode == "bytes-cover" || mode == "refusal" ? IMAGE_FIT_COVER
                          : mode == "none"                                                   ? IMAGE_FIT_NONE
                                                                                             : IMAGE_FIT_STRETCH));
  if (mode == "no-state")
    props.image(0);
  ImageViewNode node(props);
  ToolboxImageViewContext &context = *new ToolboxImageViewContext(&node, &controller);
  node.setContext(&context);
  Rect projection = ambient;
  const Rect viewport = {0, 0, 20, 100};
  if (mode == "walk" || mode == "dirty" || mode == "ambient" || mode == "scroll" || mode == "pict-cover")
    projection = viewport;
  if (mode == "empty")
    SetRect(&projection, 50, 50, 60, 60);
  place(context, controller, projection);
  clearDrawing();
  if (mode == "dirty" || mode == "ambient")
  {
    const Rect dirty = mode == "dirty" ? ambient : Rect();
    Rect damage = dirty;
    if (mode == "ambient")
      SetRect(&damage, 15, 15, 40, 40);
    const Rect expected = mode == "dirty" ? half : Rect();
    Rect reached = expected;
    if (mode == "ambient")
      SetRect(&reached, 15, 15, 30, 20);
    {
      ToolboxPaintClip clip(damage);
      RenderDirtyImageViews(&node, &controller, damage);
    }
    LOKA_VERIFY(toolbox_host::erasedRects.size() == 1);
    same(toolbox_host::erasedRects[0], reached);
    LOKA_VERIFY(pictureClips.size() == 1);
    same(pictureClips[0], reached);
  }
  else if (mode == "clip-change" || mode == "scroll")
  {
    context.render(&controller);
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    projection = viewport;
    place(context, controller, projection, mode == "scroll" ? 5 : 0);
    clearDrawing();
    context.render(&controller);
    LOKA_VERIFY(pictureClips.size() == 1);
    const Rect scrolled = {5, 10, 20, 30};
    same(pictureClips[0], mode == "scroll" ? scrolled : half);
    if (mode == "scroll")
    {
      const Rect moved = {5, 10, 25, 30};
      same(pictureDestinations[0], moved);
    }
    // No history exists to invalidate: every placement remains conservative.
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  }
  else if (mode == "refusal")
  {
    for (int attempt = 1; attempt <= 2; ++attempt)
    {
      clearDrawing();
      toolbox_host::refuseRegionAttempt = attempt;
      context.render(&controller);
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(pictureClips.size() == 1);
      same(pictureClips[0], ambient);
      same(toolbox_host::currentClip(), ambient);
    }
  }
  else
  {
    context.render(&controller);
    if (mode == "empty")
    {
      LOKA_VERIFY(pictureClips.empty() && toolbox_host::draws.empty() && lineClips.empty());
      LOKA_VERIFY(toolbox_host::regions == 0);
      LOKA_VERIFY(toolbox_host::paintOperations.empty());
    }
    else if (mode == "no-image" || mode == "no-state" || mode == "diagonal")
    {
      LOKA_VERIFY(toolbox_host::draws.size() == 1);
      if (mode == "diagonal")
      {
        LOKA_VERIFY(lineClips.size() == 2);
        same(lineClips[0], frame);
        same(lineClips[1], frame);
      }
      same(toolbox_host::draws[0].clip, frame);
    }
    else
    {
      LOKA_VERIFY(pictureClips.size() == 1);
      same(pictureClips[0], mode == "walk" || mode == "pict-cover" ? half : frame);
      if (mode == "pict-cover" || mode == "bytes-cover" || mode == "none")
        LOKA_VERIFY(pictureDestinations[0].right > frame.right);
    }
  }
  same(toolbox_host::currentClip(), ambient);
  std::printf("PASS testToolboxImageView_%s\n", argv[1]);
}
