#include "platform/ToolboxPascalText.hpp"
#include "context/ToolboxImageViewContext.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxNativeImage.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
  class ToolboxImageViewNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxImageViewNodeHandler,
                                                     loka::app::ImageViewNode,
                                                     ToolboxImageViewContext>
  {
  public:
    static loka::app::ImageViewNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asImageViewNode() : 0;
    }

    static ToolboxImageViewContext *create(loka::app::ImageViewNode *node,
                                           loka::app::scene::IPlatformController *controller,
                                           const loka::app::scene::LayoutState &state)
    {
      (void)state;
      return new ToolboxImageViewContext(node, static_cast<ToolboxScenePlatformController *>(controller));
    }
  };

  ToolboxImageViewNodeHandler gToolboxImageViewNodeHandler;

  static const loka::toolbox::ToolboxPictBytesPayload *gActivePictBytes = 0;
  static std::size_t gActivePictReadPos = 0;
  static QDGetPicUPP gReadPictFromBytesUPP = 0;
  static CQDProcs gStreamPictProcs;
  static bool gStreamPictProcsInitialized = false;
  static QDProcs gBWStreamPictProcs;
  static bool gBWStreamPictProcsInitialized = false;

  static pascal void ReadPictDataFromActiveBytes(void *dataPtr, short byteCount)
  {
    if (!gActivePictBytes || !dataPtr || byteCount <= 0)
    {
      return;
    }
    const unsigned char *bytes = gActivePictBytes->blob.data();
    // The picture's end, not the buffer's. A blob can hold a whole LRPK bag,
    // so streaming to the end of it would feed DrawPicture the asset stored
    // after this one once the size field or terminator was unreliable.
    std::size_t end = gActivePictBytes->pictureEnd;
    if (end > gActivePictBytes->blob.size())
    {
      end = gActivePictBytes->blob.size();
    }
    unsigned char *dst = static_cast<unsigned char *>(dataPtr);
    long remain = byteCount;
    while (remain > 0)
    {
      if (gActivePictReadPos >= end)
      {
        std::memset(dst, 0, static_cast<std::size_t>(remain));
        return;
      }
      std::size_t available = end - gActivePictReadPos;
      std::size_t chunk = static_cast<std::size_t>(remain);
      if (chunk > available)
      {
        chunk = available;
      }
      std::memcpy(dst, &bytes[gActivePictReadPos], chunk);
      dst += chunk;
      gActivePictReadPos += chunk;
      remain -= static_cast<long>(chunk);
    }
  }

  static bool DrawPictBytes(const loka::toolbox::ToolboxPictBytesPayload *payload,
                            const Rect &dstRect,
                            ToolboxPaintRefusal &failure)
  {
    if (!payload)
    {
      return false;
    }
    const unsigned char *bytes = payload->blob.data();
    const std::size_t headerSize = sizeof(Picture) + sizeof(long) * 8;
    if (payload->pictureEnd > payload->blob.size() ||
        payload->pictureOffset + headerSize > payload->pictureEnd)
    {
      return false;
    }

    PicHandle picHandle = (PicHandle)NewHandle(static_cast<Size>(headerSize));
    if (!picHandle || !*picHandle)
    {
      failure = TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION;
      return false;
    }

    HLock((Handle)picHandle);
    std::memcpy(*picHandle, &bytes[payload->pictureOffset], headerSize);
    HUnlock((Handle)picHandle);

    if (!gReadPictFromBytesUPP)
    {
      gReadPictFromBytesUPP = NewQDGetPicUPP(ReadPictDataFromActiveBytes);
    }

    GrafPtr port = 0;
    GetPort(&port);

    // Detect B&W GrafPort vs CGrafPort.
    // In a CGrafPort the first field (portVersion) has bits 14-15 set;
    // in a classic B&W GrafPort those bits are the high bits of portBits.baseAddr
    // which is always in low memory (bits clear).
    bool isColorPort = false;
    if (port)
    {
      short firstWord = *reinterpret_cast<short *>(port);
      isColorPort = (firstWord & static_cast<short>(0xC000)) != 0;
    }

    QDProcsPtr oldBWProcs = 0;
    CQDProcsPtr oldColorProcs = 0;

    if (isColorPort)
    {
      oldColorProcs = ((CGrafPtr)port)->grafProcs;
      if (oldColorProcs)
      {
        gStreamPictProcs = *oldColorProcs;
        gStreamPictProcsInitialized = true;
      }
      else if (!gStreamPictProcsInitialized)
      {
        SetStdCProcs(&gStreamPictProcs);
        gStreamPictProcsInitialized = true;
      }
      gStreamPictProcs.getPicProc = gReadPictFromBytesUPP;
      ((CGrafPtr)port)->grafProcs = &gStreamPictProcs;
    }
    else if (port)
    {
      oldBWProcs = port->grafProcs;
      if (oldBWProcs)
      {
        gBWStreamPictProcs = *oldBWProcs;
        gBWStreamPictProcsInitialized = true;
      }
      else if (!gBWStreamPictProcsInitialized)
      {
        SetStdProcs(&gBWStreamPictProcs);
        gBWStreamPictProcsInitialized = true;
      }
      gBWStreamPictProcs.getPicProc = gReadPictFromBytesUPP;
      port->grafProcs = &gBWStreamPictProcs;
    }

    gActivePictBytes = payload;
    gActivePictReadPos = payload->pictureOffset + sizeof(Picture);
    DrawPicture(picHandle, &dstRect);
    gActivePictBytes = 0;
    gActivePictReadPos = 0;

    if (isColorPort)
    {
      ((CGrafPtr)port)->grafProcs = oldColorProcs;
    }
    else if (port)
    {
      port->grafProcs = oldBWProcs;
    }
    KillPicture(picHandle);
    return true;
  }

  static void DrawPascalStringAt(short x, short y, const char *utf8)
  {
    if (!utf8)
    {
      return;
    }
    Str255 text;
    if (!ToolboxEncodePascal(loka::core::String::Literal(utf8), text))
      return;
    MoveTo(x, y);
    DrawString(text);
  }

  static Rect ComputeImageDrawRect(const Rect &frameRect, int fitMode, int imageWidth, int imageHeight)
  {
    Rect out = frameRect;
    const int frameWidth = frameRect.right - frameRect.left;
    const int frameHeight = frameRect.bottom - frameRect.top;
    if (imageWidth <= 0 || imageHeight <= 0 || frameWidth <= 0 || frameHeight <= 0)
    {
      return out;
    }

    if (fitMode == loka::app::IMAGE_FIT_NONE)
    {
      out.right = static_cast<short>(out.left + imageWidth);
      out.bottom = static_cast<short>(out.top + imageHeight);
      return out;
    }

    if (fitMode == loka::app::IMAGE_FIT_STRETCH)
    {
      return out;
    }

    // contain / cover
    const double sx = static_cast<double>(frameWidth) / static_cast<double>(imageWidth);
    const double sy = static_cast<double>(frameHeight) / static_cast<double>(imageHeight);
    const double scale = (fitMode == loka::app::IMAGE_FIT_COVER) ? ((sx > sy) ? sx : sy) : ((sx < sy) ? sx : sy);

    const int dstW = static_cast<int>(imageWidth * scale);
    const int dstH = static_cast<int>(imageHeight * scale);
    const int offsetX = (frameWidth - dstW) / 2;
    const int offsetY = (frameHeight - dstH) / 2;
    out.left = static_cast<short>(frameRect.left + offsetX);
    out.top = static_cast<short>(frameRect.top + offsetY);
    out.right = static_cast<short>(out.left + dstW);
    out.bottom = static_cast<short>(out.top + dstH);
    return out;
  }
  /** True when any pixel of a picture drawn into `destination` can reach the
      port through the enclosing viewport. Outside one, every pixel can. The
      frame line and placeholder text are cheap; the shared paint clip bounds
      them without a separate cost skip. */
  bool PictureReachesPort(const ToolboxScenePlatformController *controller, const Rect &destination)
  {
    Rect reached;
    return !controller || controller->intersectWithProjectionClip(destination, reached);
  }
} // namespace

ToolboxImageViewContext::ToolboxImageViewContext(loka::app::ImageViewNode *node,
                                                 ToolboxScenePlatformController *controller)
    : ToolboxProjectedNodeContext(controller),
      node_(node),
      rect_(),
      paintRect_()
{
  SetRect(&rect_, 0, 0, 0, 0);
}

ToolboxImageViewContext::~ToolboxImageViewContext() {}

short ToolboxImageViewContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  int sizePolicy = loka::app::IMAGE_VIEW_SIZE_AUTO;
  int width = state.width;
  int height = state.lineHeight > 0 ? state.lineHeight : ToolboxLayoutMetrics::kImageFallbackHeight;
  if (node_)
  {
    const bool hasExplicitWidth = node_->props.width_ > 0;
    const bool hasExplicitHeight = node_->props.height_ > 0;
    if (node_->props.hasAttr_ && node_->props.attr_.hasSizePolicyValue_)
    {
      sizePolicy = static_cast<int>(node_->props.attr_.sizePolicyValue_);
    }

    if (hasExplicitWidth)
    {
      width = node_->props.width_;
    }
    if (hasExplicitHeight)
    {
      height = node_->props.height_;
    }

    int srcWidth = 0;
    int srcHeight = 0;
    if (node_->props.image_)
    {
      const loka::core::resource::Image current = node_->props.image_->get();
      srcWidth = current.width();
      srcHeight = current.height();
    }

    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_INTRINSIC && !hasExplicitWidth && srcWidth > 0)
    {
      // Keep 1:1 source width aligned with ToolboxScenePlatformController::queryNaturalWidth.
      width = srcWidth;
    }
    else if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT && !hasExplicitWidth)
    {
      width = state.width;
    }

    if (!hasExplicitHeight)
    {
      if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT && state.height > 0)
      {
        height = state.height;
      }
      else if (srcWidth > 0 && srcHeight > 0 && width > 0)
      {
        height = (width * srcHeight) / srcWidth;
      }
      else if (srcHeight > 0)
      {
        height = srcHeight;
      }
    }
  }
  const short rectWidth = std::max<short>(0, static_cast<short>(width));
  const short rectHeight = std::max<short>(0, static_cast<short>(height));
  const Rect rect = {
      state.y, state.x, static_cast<short>(state.y + rectHeight), static_cast<short>(state.x + rectWidth)};
  const ToolboxLeafGeometry previous = {
      this->rect_, {this->rect_.top, this->rect_.left}, static_cast<short>(this->rect_.right - this->rect_.left)};
  const ToolboxLeafGeometry next = {rect, {rect.top, rect.left}, rectWidth};
  this->comparePlacement(previous, next);
  updateRect(state.x, state.y, static_cast<short>(width), static_cast<short>(height));
  this->paintRect_ = this->rect_;
  if (this->controller() && !this->controller()->intersectWithProjectionClip(this->rect_, this->paintRect_))
    SetRect(&this->paintRect_, 0, 0, 0, 0);
  this->completePlacement();
  // ImageView has no presented fact: its inherited unsupported paint answer
  // remains conservative across frame and clip changes.
  state.y = static_cast<short>(state.y + static_cast<short>(height));
  return static_cast<short>(width);
}

void ToolboxImageViewContext::render(loka::app::scene::IPlatformController *)
{
  draw();
}

void ToolboxImageViewContext::updateRect(short x, short y, short width, short height)
{
  if (width < 0)
  {
    width = 0;
  }
  if (height < 0)
  {
    height = 0;
  }
  rect_.left = x;
  rect_.top = y;
  rect_.right = static_cast<short>(x + width);
  rect_.bottom = static_cast<short>(y + height);
}

void ToolboxImageViewContext::draw()
{
  if (EmptyRect(&this->paintRect_))
    return;
  ToolboxPaintClip clip(this->paintRect_, ToolboxPaintClip::INTERSECT);
  if (!clip.isActive())
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
  // Like Text, region refusal still draws under the caller's ambient clip.
  if (clip.isActive() && !clip.touches(this->paintRect_))
    return;
  // The caller restores the pixels beneath this transparent kind.
  FrameRect(&rect_);

  if (!node_ || !node_->props.image_)
  {
    DrawPascalStringAt(static_cast<short>(rect_.left + 6), static_cast<short>(rect_.top + 14), "Image: (no state)");
    return;
  }

  const loka::core::resource::Image image = node_->props.image_->get();
  if (!image.isValid())
  {
    DrawPascalStringAt(static_cast<short>(rect_.left + 6), static_cast<short>(rect_.top + 14), "Image: (empty)");
    return;
  }

  int fitMode = loka::app::IMAGE_FIT_STRETCH;
  if (node_->props.hasAttr_ && node_->props.attr_.hasFitValue_)
  {
    fitMode = static_cast<int>(node_->props.attr_.fitValue_);
  }

  const loka::toolbox::ToolboxNativeImage *native = loka::toolbox::TryGetToolboxNativeImage(image);
  if (native && native->kind == loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_PICT && native->payload)
  {
    PicHandle picture = static_cast<PicHandle>(native->payload);
    Rect dstRect = ComputeImageDrawRect(rect_, fitMode, image.width(), image.height());
    if (fitMode == loka::app::IMAGE_FIT_COVER)
    {
      // Keep the branch-specific cost skip; the shared clip bounds the draw.
      Rect framed;
      if (SectRect(&dstRect, &rect_, &framed) && PictureReachesPort(this->controller(), framed))
        DrawPicture(picture, &dstRect);
    }
    else if (PictureReachesPort(this->controller(), dstRect))
    {
      // Outside the viewport DrawPicture would parse the whole picture for
      // pixels the clip discards (#1043 B4-1); skip it instead.
      DrawPicture(picture, &dstRect);
    }
    return;
  }

  if (native && native->kind == loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_PICT_BYTES && native->payload)
  {
    const loka::toolbox::ToolboxPictBytesPayload *payload =
        static_cast<const loka::toolbox::ToolboxPictBytesPayload *>(native->payload);
    Rect dstRect = ComputeImageDrawRect(rect_, fitMode, image.width(), image.height());
    if (!PictureReachesPort(this->controller(), dstRect))
    {
      // Same skip as above: the streamed picture draws exactly dstRect.
      return;
    }
    ToolboxPaintRefusal failure = TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW;
    if (DrawPictBytes(payload, dstRect, failure))
    {
      return;
    }
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), failure);
  }
  else
    LOKA_PAINT_REFUSE(ToolboxPaintRefusals(this->controller()), TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);

  MoveTo(rect_.left, rect_.top);
  LineTo(rect_.right, rect_.bottom);
  MoveTo(rect_.right, rect_.top);
  LineTo(rect_.left, rect_.bottom);

  char label[64];
  ::snprintf(label, sizeof(label), "Image(native?): %dx%d", image.width(), image.height());
  DrawPascalStringAt(static_cast<short>(rect_.left + 6), static_cast<short>(rect_.top + 14), label);
}

bool RegisterToolboxImageViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxImageViewNodeHandler);
}
