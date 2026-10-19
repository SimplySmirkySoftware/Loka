#ifndef LOKA_MAC_PLATFORM_CONTEXT_HPP
#define LOKA_MAC_PLATFORM_CONTEXT_HPP

#include "app/PlatformContext.hpp"

class MacPlatformContext : public PlatformContext
{
public:
  MacPlatformContext();
  virtual ~MacPlatformContext();

  virtual App *createApp(AppConfigurable *config, NativeModuleHandle hInstance, int nCmdShow) const;
  virtual Window *createWindow(const WindowProps &props);
  virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *node) const;
  virtual bool openFile(const loka::file::File &item, loka::platform::file::FileHandle &out) const;
  virtual bool queryMainDisplayDepth(int &out) const;
  virtual bool createImageFromBlob(const loka::core::resource::Blob &blob,
                                   std::size_t offset,
                                   std::size_t length,
                                   loka::core::resource::Image &out);
};

#endif // LOKA_MAC_PLATFORM_CONTEXT_HPP
