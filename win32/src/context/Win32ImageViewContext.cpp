#include "Win32ImageViewContext.hpp"
#include <cassert>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  const wchar_t kImageViewClassName[] = L"LOKA_IMAGE_VIEW";

  class Win32ImageViewNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32ImageViewNodeHandler,
                                                     loka::app::ImageViewNode,
                                                     Win32ImageViewContext>
  {
  public:
    static loka::app::ImageViewNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asImageViewNode() : 0;
    }

    static Win32ImageViewContext *create(loka::app::ImageViewNode *image,
                                         loka::app::scene::IPlatformController *controller,
                                         const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32ImageViewContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, image);
    }

    static void refresh(Win32ImageViewContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32ImageViewNodeHandler gWin32ImageViewNodeHandler;

  struct BlitRect
  {
    int dstX;
    int dstY;
    int dstW;
    int dstH;
    int srcX;
    int srcY;
    int srcW;
    int srcH;
  };

  static BlitRect ComputeBlitRect(int fitMode, const RECT &rect, int srcWidth, int srcHeight)
  {
    BlitRect out;
    out.dstX = rect.left;
    out.dstY = rect.top;
    out.dstW = rect.right - rect.left;
    out.dstH = rect.bottom - rect.top;
    out.srcX = 0;
    out.srcY = 0;
    out.srcW = srcWidth;
    out.srcH = srcHeight;

    if (srcWidth <= 0 || srcHeight <= 0 || out.dstW <= 0 || out.dstH <= 0)
    {
      return out;
    }

    if (fitMode == loka::app::IMAGE_FIT_STRETCH)
    {
      return out;
    }

    if (fitMode == loka::app::IMAGE_FIT_NONE)
    {
      out.dstW = srcWidth;
      out.dstH = srcHeight;
      return out;
    }

    const double srcAspect = static_cast<double>(srcWidth) / static_cast<double>(srcHeight);
    const double dstAspect = static_cast<double>(out.dstW) / static_cast<double>(out.dstH);

    if (fitMode == loka::app::IMAGE_FIT_CONTAIN)
    {
      if (srcAspect > dstAspect)
      {
        out.dstH = static_cast<int>(out.dstW / srcAspect);
      }
      else
      {
        out.dstW = static_cast<int>(out.dstH * srcAspect);
      }
      out.dstX = rect.left + ((rect.right - rect.left) - out.dstW) / 2;
      out.dstY = rect.top + ((rect.bottom - rect.top) - out.dstH) / 2;
      return out;
    }

    if (fitMode == loka::app::IMAGE_FIT_COVER)
    {
      if (srcAspect > dstAspect)
      {
        const int cropWidth = static_cast<int>(srcHeight * dstAspect);
        out.srcX = (srcWidth - cropWidth) / 2;
        out.srcW = cropWidth;
      }
      else
      {
        const int cropHeight = static_cast<int>(srcWidth / dstAspect);
        out.srcY = (srcHeight - cropHeight) / 2;
        out.srcH = cropHeight;
      }
      return out;
    }

    return out;
  }

  int ResolveImageLayoutWidth(const loka::app::ImageViewNode *node,
                              const loka::win32::Win32DisplayScale &scale,
                              int fallbackWidth)
  {
    if (!node)
    {
      return fallbackWidth;
    }
    int sizePolicy = loka::app::IMAGE_VIEW_SIZE_AUTO;
    if (node->props.hasAttr_ && node->props.attr_.hasSizePolicyValue_)
    {
      sizePolicy = static_cast<int>(node->props.attr_.sizePolicyValue_);
    }
    const bool hasExplicitWidth = node->props.width_ > 0;
    int srcWidth = 0;
    if (node->props.image_)
    {
      const loka::core::resource::Image current = node->props.image_->get();
      srcWidth = current.width();
    }
    if (hasExplicitWidth)
    {
      return node->props.width_;
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_INTRINSIC && srcWidth > 0)
    {
      // Source rectangles remain decoded pixels (DPI only, no space scale).
      // Only the intrinsic layout reservation converts to lu here.
      // Keep conversion aligned with Win32ScenePlatformController::queryNaturalWidth.
      return scale.intrinsicPixelsToLu(srcWidth);
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT)
    {
      return fallbackWidth;
    }
    return fallbackWidth;
  }

  int ResolveImageLayoutHeight(const loka::app::ImageViewNode *node,
                               const loka::win32::Win32DisplayScale &scale,
                               int resolvedWidth,
                               int fallbackHeight)
  {
    if (!node)
    {
      return fallbackHeight > 0 ? fallbackHeight : loka::app::layout::FallbackControlMetrics::kImageFallbackHeight;
    }
    int sizePolicy = loka::app::IMAGE_VIEW_SIZE_AUTO;
    if (node->props.hasAttr_ && node->props.attr_.hasSizePolicyValue_)
    {
      sizePolicy = static_cast<int>(node->props.attr_.sizePolicyValue_);
    }
    const bool hasExplicitHeight = node->props.height_ > 0;
    int srcWidth = 0;
    int srcHeight = 0;
    if (node->props.image_)
    {
      const loka::core::resource::Image current = node->props.image_->get();
      srcWidth = current.width();
      srcHeight = current.height();
    }
    if (hasExplicitHeight)
    {
      return node->props.height_;
    }
    if (sizePolicy == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT && fallbackHeight > 0)
    {
      return fallbackHeight;
    }
    if (srcWidth > 0 && srcHeight > 0 && resolvedWidth > 0)
    {
      return (resolvedWidth * srcHeight) / srcWidth;
    }
    if (srcHeight > 0)
    {
      return scale.intrinsicPixelsToLu(srcHeight);
    }
    if (fallbackHeight > 0)
    {
      return fallbackHeight;
    }
    return loka::app::layout::FallbackControlMetrics::kImageFallbackHeight;
  }
} // namespace

Win32ImageViewContext::Win32ImageViewContext(Win32ScenePlatformController *controller,
                                             HWND parent,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             loka::app::ImageViewNode *node)
    : Win32RetirableContext(controller),
      node_(node),
      imageState_(0),
      image_()
{
  EnsureClassRegistered();
  this->createNativeChildWindow(
      0,
      kImageViewClassName,
      L"",
      WS_CHILD | WS_VISIBLE,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      0,
      GetModuleHandleW(NULL),
      this);
  bindImage();
}

Win32ImageViewContext::~Win32ImageViewContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** The image observer requests the parent rectangle and its children, without erase.
    Like Text, ground is restored before this transparent foreground is drawn.
    deferBind runs in State notification before the tracker invalidates the Boundary. */
loka::app::scene::PaintAnswer Win32ImageViewContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  if (query.placement != PLACEMENT_ELIGIBLE)
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || this->node_->props.image_ != this->imageState_)
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  return PaintAnswer::nativeScheduled();
}

void Win32ImageViewContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32ImageViewContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
                                         loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // DETACHED_RETAINED hides; terminal RETIRED keeps the same policy
    // (hide before the ritual destroys the native pair).
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->unbindImage();
      this->node_ = 0;
    }
  }
}

void Win32ImageViewContext::applyAttachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32ImageViewContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

short Win32ImageViewContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  const loka::win32::Win32DisplayScale &scale = this->controller()->displayScale();
  const int imageWidth = ResolveImageLayoutWidth(this->node_, scale, state.width);
  const int imageHeight = ResolveImageLayoutHeight(this->node_, scale, imageWidth, state.height);
  this->relayout(state.x, state.y, imageWidth, imageHeight);
  state.width = static_cast<short>(imageWidth);
  state.height = static_cast<short>(imageHeight);
  return static_cast<short>(state.y + imageHeight);
}

void Win32ImageViewContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
  Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
}

void Win32ImageViewContext::EnsureClassRegistered()
{
  static bool registered = false;
  if (registered)
  {
    return;
  }
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = Win32ImageViewContext::WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = kImageViewClassName;
  RegisterClassW(&wc);
  registered = true;
}

LRESULT CALLBACK Win32ImageViewContext::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Win32ImageViewContext *self =
      static_cast<Win32ImageViewContext *>(reinterpret_cast<void *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Win32ImageViewContext *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  switch (msg)
  {
  case WM_ERASEBKGND:
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_IMAGE, true);
    return 1;
  case WM_PAINT:
  {
    Win32ScenePlatformController::noteNativePaint(hwnd, Win32ScenePlatformController::NATIVE_PAINT_IMAGE, false);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (self)
    {
      RECT rect;
      GetClientRect(hwnd, &rect);
      self->drawImage(hdc, rect);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Win32ImageViewContext::bindImage()
{
  if (!node_)
  {
    return;
  }
  imageState_ = node_->props.image_;
  if (imageState_)
  {
    imageState_->deferBind(&Win32ImageViewContext::ImageChangedThunk, this);
    applyImage();
  }
}

void Win32ImageViewContext::unbindImage()
{
  if (imageState_)
  {
    imageState_->deferUnbind(&Win32ImageViewContext::ImageChangedThunk, this);
    imageState_ = 0;
  }
}

void Win32ImageViewContext::applyImage()
{
  if (!imageState_)
  {
    return;
  }
  image_ = imageState_->get();
  if (this->hwnd())
  {
    Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd());
  }
}

void Win32ImageViewContext::drawImage(HDC hdc, const RECT &rect)
{
  // The parent repaint supplies transparent letterbox and placeholder pixels.
  if (!image_.isValid())
  {
    return;
  }

  HBITMAP bitmap = static_cast<HBITMAP>(image_.nativeHandle());
  if (!bitmap)
  {
    return;
  }

  HDC memdc = CreateCompatibleDC(hdc);
  if (!memdc)
  {
    return;
  }
  HGDIOBJ old = SelectObject(memdc, bitmap);
  int dstWidth = rect.right - rect.left;
  int dstHeight = rect.bottom - rect.top;
  int srcWidth = image_.width();
  int srcHeight = image_.height();
  if (srcWidth > 0 && srcHeight > 0 && dstWidth > 0 && dstHeight > 0)
  {
    int fitMode = loka::app::IMAGE_FIT_STRETCH;
    if (node_ && node_->props.hasAttr_ && node_->props.attr_.hasFitValue_)
    {
      fitMode = static_cast<int>(node_->props.attr_.fitValue_);
    }
    BlitRect blit = ComputeBlitRect(fitMode, rect, srcWidth, srcHeight);
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(
        hdc, blit.dstX, blit.dstY, blit.dstW, blit.dstH, memdc, blit.srcX, blit.srcY, blit.srcW, blit.srcH, SRCCOPY);
  }
  SelectObject(memdc, old);
  DeleteDC(memdc);
}

void Win32ImageViewContext::ImageChangedThunk(void *userData)
{
  Win32ImageViewContext *self = static_cast<Win32ImageViewContext *>(userData);
  if (self)
  {
    self->applyImage();
  }
}

void RegisterWin32ImageViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32ImageViewNodeHandler);
}
