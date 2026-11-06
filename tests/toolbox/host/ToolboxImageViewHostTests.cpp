#include "app/nodes/boundary/StdComposition.hpp"
#include "ToolboxHost.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxNativeImage.hpp"
#include "support/PictAdmissionFixture.hpp"
#include "context/ToolboxImageViewContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#ifdef LOKA_HOST_GAP_DISPATCH
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "context/ToolboxClipBoxContext.hpp"
#include "app/nodes/nestable/Box.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
#include "ToolboxDirtyImageReplay.inc"
  class ImageObservationOwner : public loka::app::scene::BoundaryNodeFor<ImageObservationOwner>
  {
  public:
    ImageObservationOwner(const loka::app::scene::BoundaryPropsFor<ImageObservationOwner> &props =
                              loka::app::scene::BoundaryPropsFor<ImageObservationOwner>())
        : loka::app::scene::BoundaryNodeFor<ImageObservationOwner>(props)
    {
    }
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
  };
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

namespace loka
{
  namespace testing
  {
    unsigned refuseImageDrawHandles = 0;
  }
} // namespace loka
char **NewHandle(Size size)
{
  if (loka::testing::refuseImageDrawHandles)
  {
    --loka::testing::refuseImageDrawHandles;
    return 0;
  }
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

#ifdef LOKA_HOST_GAP_DISPATCH
namespace
{
  class ClipImageController : public ToolboxScenePlatformController
  {
  public:
    explicit ClipImageController(ToolboxWindow *window)
        : ToolboxScenePlatformController(window)
    {
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (!this->projectLayoutState(state))
        return false;
      return this->nodeHandlerRegistry_.find(node)->ensureContext(node, this, state) != 0;
    }
  };
  void clippedImagePlacement(const std::string &mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ClipImageController controller(&window);
    LOKA_VERIFY(RegisterToolboxImageViewNodeHandler(controller.nodeHandlerRegistry_));
    LOKA_VERIFY(RegisterToolboxClipBoxNodeHandlers(controller.nodeHandlerRegistry_));
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    MutableState<Frame> origin((Frame()));
    NodeState<Frame> viewport(&origin, 0);
    ScrollViewNode scroll((ScrollViewProps(viewport)));
    BoxNode *box = new BoxNode(Box().size(60, 600).clipChildren().props);
    MutableState<Image> imageState;
    if (mode != "clip-placement")
    {
      if (mode == "admission-raw")
        imageState.set(picture(false));
      else if (mode != "admission-empty")
      {
        pict_fixture::Bytes bytes = pict_fixture::picture();
        if (mode == "admission-color")
          pict_fixture::putWord(bytes, 13, 0x8008);
        Blob blob = Blob::Create();
        // A nonzero range tests the installed snapshot/rebase as well as sharing.
        bytes.insert(bytes.begin(), 7, 0xA5);
        const std::size_t end = bytes.size();
        bytes.insert(bytes.end(), 13, 0xA5);
        LOKA_VERIFY(blob.tryAssign(&bytes[0], bytes.size()));
        if (mode != "admission-snapshot")
          blob.sealBytes();
        imageState.set(loka::toolbox::MakeImageFromPictBlob(blob, 7, end, 16, 1));
        LOKA_VERIFY(imageState.get().isValid());
        const loka::toolbox::ToolboxPictBytesPayload *payload =
            static_cast<const loka::toolbox::ToolboxPictBytesPayload *>(
                loka::toolbox::TryGetToolboxNativeImage(imageState.get())->payload);
        LOKA_VERIFY(payload->admission.admitsTranslation() == (mode != "admission-color"));
        if (mode == "admission-snapshot")
        {
          blob.mutableData()[7 + 12] = 0xA1;
          LOKA_VERIFY(payload->pictureOffset == 0);
          LOKA_VERIFY(payload->admission.admitsTranslation());
        }
      }
    }
    ImageViewProps props;
    props.image(&imageState).size(20, 20);
    if (mode == "admission-cover")
      props.attr(ImageViewAttr().fit(IMAGE_FIT_COVER));
    ImageViewNode *image = new ImageViewNode(props);
    box->addChild(image);
    scroll.addChild(box);
    LayoutState state = seat();
    state.width = 100;
    state.height = 100;
    LayoutNode(&scroll, state, &controller, 0);
    Rect clip;
    LOKA_VERIFY(static_cast<ToolboxClipBoxContext *>(box->getContext())->placement().query(clip));
    LOKA_VERIFY(image->getContext());
    if (mode == "clip-placement")
      image->props.size(21, 20);
    origin.set(Frame(0, 16, 0, 0));
    if (mode != "clip-placement")
    {
      // Seed the prior completed-pass fact; this pin exercises layout admission.
      ToolboxPresentObservation::Interval previous(
          controller.observation_, true, controller.debugStats_, controller.scrollBarLedger_);
      controller.observation_.complete(0);
      controller.observation_.markPaintFinished();
    }
    controller.observation_.reset();
    controller.observation_.noteReason(TOOLBOX_FULL_DRAW_VIEWPORT_INPUT);
    state = seat();
    state.width = 100;
    state.height = 100;
    LayoutNode(&scroll, state, &controller, 0);
    LOKA_VERIFY(controller.observation_.viewports() == 1);
    const bool refused = mode == "admission-raw" || mode == "admission-color" || mode == "admission-cover";
    LOKA_VERIFY(controller.observation_.unsupportedKinds() == (refused ? 1U : 0U));
    if (mode == "clip-placement")
      LOKA_VERIFY(controller.observation_.notTranslated() == 1);
    else if (refused)
    {
      controller.observation_.complete(0);
      LOKA_VERIFY(controller.observation_.refusal() == TOOLBOX_PRESENT_UNSUPPORTED_KIND);
    }
    controller.clearControls();
    controller.releaseNodeContexts(&scroll);
  }
} // namespace
#endif

void imagePreflightPins()
{
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  MutableState<Image> image(picture(false));
  ImageViewProps props;
  props.image(&image).size(20, 20);
  ImageViewNode node(props);
  ToolboxImageViewContext context(&node, &controller);
  LayoutState state = seat();
  context.layout(&controller, state);
  const Rect broad = {0, 0, 300, 300}, miss = {200, 200, 210, 210};
  ClipRect(&broad);
  context.render(&controller);
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  const PaintAnswer history = context.queryPaintDamage(query);
  for (int hit = 0; hit < 2; ++hit)
  {
    ClipRect(hit ? &broad : &miss);
    controller.observation_.reset();
    const int before = toolbox_host::regions;
    const std::size_t draws = pictureClips.size();
    context.render(&controller);
    LOKA_VERIFY(toolbox_host::regions - before == (hit ? 2 : 0));
    LOKA_VERIFY(pictureClips.size() - draws == static_cast<unsigned>(hit));
    LOKA_VERIFY(EqualRect(&window.port.native.clip, hit ? &broad : &miss));
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason != TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    const PaintAnswer after = context.queryPaintDamage(query);
    LOKA_VERIFY(after.kind == history.kind && after.damage.width == history.damage.width);
  }
  std::puts("clip preflight ImageView pair/history passed");
}
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const std::string mode(argv[1]);
  if (mode == "ambient")
    imagePreflightPins();
#ifdef LOKA_HOST_GAP_DISPATCH
  if (mode == "clip-placement" || mode.find("admission-") == 0)
  {
    clippedImagePlacement(mode);
    return 0;
  }
#endif
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  const Rect ambient = {0, 0, 100, 100};
  const Rect frame = {10, 10, 30, 30};
  const Rect half = {10, 10, 20, 30};
  window.port.native.clip = ambient;
  MutableState<Image> image(picture(mode == "bytes-cover" || mode == "paint-handle-refused" || mode == "paint-valid"));
  if (mode == "no-image" || mode == "paint-empty" || mode == "paint-arrival")
    image.set(Image());
  loka::toolbox::ToolboxNativeImage unknown = {
      loka::toolbox::kToolboxNativeImageMagic, loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_UNKNOWN, 0};
  if (mode == "diagonal" || mode == "paint-unsupported")
    image.set(Image::FromNative(&unknown, 80, 40, 0, 0));
  ImageViewProps props;
  props.image(&image).size(20, 20).attr(
      ImageViewAttr().fit(mode == "pict-cover" || mode == "bytes-cover" || mode == "refusal" ? IMAGE_FIT_COVER
                          : mode == "none"                                                   ? IMAGE_FIT_NONE
                                                                                             : IMAGE_FIT_STRETCH));
  if (mode == "no-state" || mode == "paint-no-state")
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
  if (mode == "missing-context")
  {
    ImageViewNode missing(props);
    controller.observation_.reset();
    RenderDirtyImageViews(&missing, &controller, ambient);
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    return 0;
  }
  if (mode.find("paint-") == 0)
  {
    controller.rootNode_ = &node;
    controller.renderContext = &context;
    const bool refused = mode == "paint-handle-refused" || mode == "paint-unsupported";
    // Refuse only draw-time acquisition, after the valid payload exists.
    loka::testing::refuseImageDrawHandles = mode == "paint-handle-refused" ? 1 : 0;
    {
      ToolboxErasedPass::Guard erased(controller.erasedPass(), window.port.portRect);
      ToolboxPortErasePresenter presenter(window.port.portRect);
      presenter.setErasedGuard(erased);
      ToolboxInputDoor::render(controller, &presenter);
    }
    LOKA_VERIFY(loka::testing::refuseImageDrawHandles == 0);
    const ToolboxPresentObservation &result = controller.debugStats_.lastPresentObservation;
    LOKA_VERIFY(result.paintFinished());
    LOKA_VERIFY(result.eligibility() == (refused ? TOOLBOX_PAINT_REFUSED : TOOLBOX_PAINT_CLEAN));
    LOKA_VERIFY(result.firstRefusal().reason
                == (mode == "paint-unsupported" ? TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET
                    : refused                   ? TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION
                                                : TOOLBOX_PAINT_REFUSAL_NONE));
    if (mode == "paint-arrival")
    {
      ImageObservationOwner owner;
      BoundaryNode::declareBoundaryDirtySources(&node, &owner);
      node.takeLayoutInputs();
      image.set(picture(true));
      LOKA_VERIFY((node.takeLayoutInputs() & NODE_DIRTY_PROPS) != 0);
      LOKA_VERIFY(controller.observation_.eligibility() == TOOLBOX_PAINT_CLEAN);
      LOKA_VERIFY(!controller.observation_.outsideRefusal().refused());
      owner.clearObservedStateEntries();
    }
    controller.rootNode_ = 0;
    controller.renderContext = 0;
    return 0;
  }
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
      controller.observation_.reset();
      toolbox_host::refuseRegionAttempt = attempt;
      context.render(&controller);
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
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
    else if (mode == "no-image" || mode == "no-state" || mode == "diagonal" || mode == "paint-unsupported")
    {
      LOKA_VERIFY(toolbox_host::draws.size() == 1);
      if (mode == "diagonal" || mode == "paint-unsupported")
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
  if (mode == "none")
  {
    const Frame origin(0, 0, 0, 0);
    ToolboxViewportContentObservation moved(ToolboxExpectedMotion(&origin, Frame(0, 16, 0, 0)), false);
    {
      ToolboxViewportContentObservation::Scope scope(controller.activeViewportContent_, moved);
      place(context, controller, viewport, 16);
    }
    LOKA_VERIFY(moved.complete().notTranslated == 0);
    node.props.size(21, 20);
    ToolboxViewportContentObservation resized(ToolboxExpectedMotion(&origin, origin), false);
    {
      ToolboxViewportContentObservation::Scope scope(controller.activeViewportContent_, resized);
      place(context, controller, viewport, 16);
    }
    LOKA_VERIFY(resized.complete().notTranslated == 1);
  }
  std::printf("PASS testToolboxImageView_%s\n", argv[1]);
}
