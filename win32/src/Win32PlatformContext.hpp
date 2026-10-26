#ifndef LOKA_WIN32PLATFORMCONTEXT_HPP
#define LOKA_WIN32PLATFORMCONTEXT_HPP
#include "app/PlatformContext.hpp"
#include "app/core/Window.hpp"
#include "Win32App.hpp"

class Win32Window;
class AppConfigurable;

class Win32PlatformContext : public PlatformContext
{
public:
  virtual bool queryTextEditorCapacity(loka::app::TextCapacity &out) const
  {
    out = loka::app::TextCapacity::of(256, 8192);
    return true;
  }
  Win32PlatformContext();
  ~Win32PlatformContext();
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

#endif // LOKA_WIN32PLATFORMCONTEXT_HPP
