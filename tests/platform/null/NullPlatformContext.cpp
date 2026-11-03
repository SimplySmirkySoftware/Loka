#include "platform/null/NullPlatformContext.hpp"

#include "app/scene/projection/NativeNodeContext.hpp"
#include "core/resource/Blob.hpp"
#include "core/resource/Image.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullWindow.hpp"

NullPlatformContext::NullPlatformContext()
    : urlScript_(0),
      urlScriptUser_(0),
      openedURLs_(),
      applicationDirectory_(),
      hasApplicationDirectory_(false)
{
}

NullPlatformContext::~NullPlatformContext() {}

void NullPlatformContext::setApplicationDirectory(const loka::core::String &dir)
{
  std::string bytes;
  this->applicationDirectory_ = dir;
  this->hasApplicationDirectory_ =
      loka::platform::CollectUtf8(this->applicationDirectory_, bytes) && !bytes.empty();
}

App *NullPlatformContext::createApp(AppConfigurable *config, NativeModuleHandle hInstance, int nCmdShow) const
{
  (void)hInstance;
  (void)nCmdShow;
  return new NullApp(config);
}

Window *NullPlatformContext::createWindow(const WindowProps &props)
{
  return new NullWindow(this, props);
}

loka::app::scene::NodeContext *
NullPlatformContext::createNodeContext(loka::app::scene::Node *node) const
{
  loka::app::scene::NativeNodeContext *context = new loka::app::scene::NativeNodeContext();
  if (context)
  {
    context->setOwner(node);
  }
  return context;
}

bool NullPlatformContext::openFile(const loka::file::File &item,
                                   loka::platform::file::FileHandle &out) const
{
  out = loka::platform::file::FileHandle();
  if (item.base() != loka::file::File::BASE_APPLICATION)
  {
    return false;
  }
  if (!loka::platform::file::ApplicationRelativeIsOpenable(item)
      || !this->hasApplicationDirectory_)
  {
    return false;
  }
  out.displayPath =
      this->applicationDirectory_ + loka::core::String::Literal("/") + item.relativePath();
  out.kind = item.kind();
  return true;
}

bool NullPlatformContext::createImageFromBlob(const loka::core::resource::Blob &blob,
                                              std::size_t offset,
                                              std::size_t length,
                                              loka::core::resource::Image &out)
{
  // Unchanged in substance: the null platform decodes nothing, so it refuses
  // every request whatever the range. `BlobRangeIsUsable` is therefore pinned
  // directly rather than through this seam.
  (void)blob;
  (void)offset;
  (void)length;
  out = loka::core::resource::Image::Empty();
  return false;
}

loka::app::URLOpenKind NullPlatformContext::openURL(const loka::app::WebURL &url)
{
  this->openedURLs_.push_back(url);
  return this->urlScript_ ? this->urlScript_(url, this->urlScriptUser_) : loka::app::URL_OPENED;
}
