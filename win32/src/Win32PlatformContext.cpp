#include "Win32PlatformContext.hpp"
#include "app/internal/NativeResourceReservation.hpp"

#include <windows.h>
#include "core/resource/BlobRange.hpp"
#include "Win32Window.hpp"
#include "Win32DisplayDepth.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileHandle.hpp"
#include "app/core/Window.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "core/resource/Blob.hpp"
#include "core/resource/Image.hpp"
#include <wincodec.h>

Win32PlatformContext::Win32PlatformContext() {}
Win32PlatformContext::~Win32PlatformContext() {}

App *Win32PlatformContext::createApp(AppConfigurable *config, NativeModuleHandle hInstance, int nCmdShow) const
{
  App *app = new Win32App(config, static_cast<HINSTANCE>(hInstance), nCmdShow);
  return app;
}

Window *Win32PlatformContext::createWindow(const WindowProps &props)
{
  return new Win32Window(this, props);
}

loka::app::scene::NodeContext *Win32PlatformContext::createNodeContext(loka::app::scene::Node *node) const
{
  loka::app::scene::NativeNodeContext *context = new loka::app::scene::NativeNodeContext();
  if (context)
  {
    context->setOwner(node);
  }
  return context;
}

bool Win32PlatformContext::queryMainDisplayDepth(int &out) const
{
  // The screen device context is the primary display; a window asks its own
  // context instead (Win32Window::queryDisplayDepth), same extraction.
  HDC dc = GetDC(0);
  const bool answered = loka::win32::queryDeviceContextDepth(dc, out);
  if (dc)
  {
    ReleaseDC(0, dc);
  }
  return answered;
}

bool Win32PlatformContext::openFile(const loka::file::File &item, loka::platform::file::FileHandle &out) const
{
  if (item.base() == loka::file::File::BASE_APPLICATION)
  {
    return loka::platform::file::ResolveApplicationItem(item, out);
  }
  out.displayPath = item.toString();
  out.kind = item.kind();
  return !out.displayPath.empty();
}

namespace
{
  void ReleaseWin32Bitmap(void *handle)
  {
    if (handle)
    {
      DeleteObject(static_cast<HBITMAP>(handle));
    }
  }
} // namespace

bool Win32PlatformContext::createImageFromBlob(const loka::core::resource::Blob &blob,
                                               std::size_t offset,
                                               std::size_t length,
                                               loka::core::resource::Image &out)
{
  const loka::core::resource::Blob source = blob;
  out = loka::core::resource::Image::Empty();
  const unsigned char *bytes = source.data();
  if (!source.isValid())
  {
    return false;
  }
  if (!loka::core::resource::BlobRangeIsUsable(source.size(), offset, length))
  {
    return false;
  }
  // WIC's memory stream takes a DWORD extent, so a range this host could
  // describe but that API could not is refused here rather than silently
  // narrowed into a shorter image.
  if (length > static_cast<std::size_t>(0xFFFFFFFFul))
  {
    return false;
  }

  loka::app::internal::Reservation reservation(*this, &ReleaseWin32Bitmap);
  if (!reservation.isValid())
    return false;

  HRESULT initResult = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  const bool shouldUninit = SUCCEEDED(initResult);
  const bool comReady = SUCCEEDED(initResult) || initResult == RPC_E_CHANGED_MODE;
  if (!comReady)
  {
    return false;
  }

  IWICImagingFactory *factory = 0;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory, 0, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void **>(&factory));
  if (FAILED(hr) || !factory)
  {
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  IWICStream *stream = 0;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr) || !stream)
  {
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  hr = stream->InitializeFromMemory(const_cast<BYTE *>(&bytes[offset]), static_cast<DWORD>(length));
  if (FAILED(hr))
  {
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  IWICBitmapDecoder *decoder = 0;
  hr = factory->CreateDecoderFromStream(stream, 0, WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr) || !decoder)
  {
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  IWICBitmapFrameDecode *frame = 0;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr) || !frame)
  {
    decoder->Release();
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  IWICFormatConverter *converter = 0;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr) || !converter)
  {
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  hr = converter->Initialize(
      frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, 0, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr))
  {
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  UINT width = 0;
  UINT height = 0;
  converter->GetSize(&width, &height);
  if (width == 0 || height == 0)
  {
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    if (shouldUninit)
      CoUninitialize();
    return false;
  }

  BITMAPINFO bmi;
  ZeroMemory(&bmi, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = static_cast<LONG>(width);
  bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = 0;
  HBITMAP hbmp = CreateDIBSection(0, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
  if (hbmp && bits)
  {
    const UINT stride = width * 4;
    const UINT total = stride * height;
    hr = converter->CopyPixels(0, stride, total, reinterpret_cast<BYTE *>(bits));
    if (SUCCEEDED(hr))
    {
      reservation.publishImage(hbmp, static_cast<int>(width), static_cast<int>(height), out);
    }
    else
    {
      DeleteObject(hbmp);
    }
  }

  converter->Release();
  frame->Release();
  decoder->Release();
  stream->Release();
  factory->Release();
  if (shouldUninit)
    CoUninitialize();

  return out.isValid();
}
