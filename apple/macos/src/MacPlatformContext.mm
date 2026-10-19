#include "MacPlatformContext.hpp"
#include "app/internal/NativeResourceReservation.hpp"

#include "core/resource/BlobRange.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "MacScreenDepth.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileHandle.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "core/resource/Blob.hpp"
#include "core/resource/Image.hpp"
#include <AppKit/AppKit.h>

MacPlatformContext::MacPlatformContext() {}
MacPlatformContext::~MacPlatformContext() {}

App *MacPlatformContext::createApp(AppConfigurable *config, NativeModuleHandle, int) const
{
  return new MacApp(config);
}

Window *MacPlatformContext::createWindow(const WindowProps &props)
{
  return new MacWindow(this, props);
}

loka::app::scene::NodeContext *MacPlatformContext::createNodeContext(loka::app::scene::Node *node) const
{
  loka::app::scene::NativeNodeContext *context = new loka::app::scene::NativeNodeContext();
  if (context)
  {
    context->setOwner(node);
  }
  return context;
}

bool MacPlatformContext::queryMainDisplayDepth(int &out) const
{
  // The main screen is where a new window appears; a window asks the screen
  // it overlaps most instead (MacWindow::queryDisplayDepth), same extraction.
  return loka::macos::queryScreenDepth([NSScreen mainScreen], out);
}

bool MacPlatformContext::openFile(const loka::file::File &item, loka::platform::file::FileHandle &out) const
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
  void ReleaseNSImage(void *handle)
  {
    if (handle)
    {
      [(NSImage *)handle release];
    }
  }
} // namespace

bool MacPlatformContext::createImageFromBlob(const loka::core::resource::Blob &blob,
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

  loka::app::internal::Reservation reservation(*this, &ReleaseNSImage);
  if (!reservation.isValid())
    return false;

  NSData *data = [NSData dataWithBytes:&bytes[offset] length:length];
  if (!data)
  {
    return false;
  }
  NSImage *image = [[NSImage alloc] initWithData:data];
  if (!image)
  {
    return false;
  }
  NSSize size = [image size];
  return reservation.publishImage(
      image, static_cast<int>(size.width), static_cast<int>(size.height), out);
}
