#ifndef LOKA_TESTS_PLATFORM_NULL_PLATFORM_CONTEXT_HPP
#define LOKA_TESTS_PLATFORM_NULL_PLATFORM_CONTEXT_HPP

#include "app/PlatformContext.hpp"
#include <climits>
#include "core/String.hpp"

class NullPlatformContext : public PlatformContext
{
public:
  virtual bool queryTextEditorCapacity(loka::app::TextCapacity &out) const
  {
    out = loka::app::TextCapacity::of(65535, INT_MAX);
    return true;
  }
  NullPlatformContext();
  virtual ~NullPlatformContext();

  void setApplicationDirectory(const loka::core::String &dir);

  virtual App *createApp(AppConfigurable *config, NativeModuleHandle hInstance, int nCmdShow) const;
  virtual Window *createWindow(const WindowProps &props);
  virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *node) const;
  virtual bool openFile(const loka::file::File &item,
                        loka::platform::file::FileHandle &out) const;
  virtual bool createImageFromBlob(const loka::core::resource::Blob &blob,
                                   std::size_t offset,
                                   std::size_t length,
                                   loka::core::resource::Image &out);

private:
  loka::core::String applicationDirectory_;
  bool hasApplicationDirectory_;
};

#endif // LOKA_TESTS_PLATFORM_NULL_PLATFORM_CONTEXT_HPP
