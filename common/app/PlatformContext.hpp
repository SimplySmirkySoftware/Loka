#ifndef LOKA_PLATFORMCONTEXT_HPP
#define LOKA_PLATFORMCONTEXT_HPP

#include <cstddef>
#include "app/NativeResourceRetirement.hpp"

#if defined(_WIN32) || defined(WIN32)
#if !defined(UNICODE) || !defined(_UNICODE)
#error "Loka Win32 targets require UNICODE and _UNICODE"
#endif
#endif

/** Borrowed native module identity for bootstrap; interpreted only by the platform. */
typedef void *NativeModuleHandle;

class AppConfigurable;
class PlatformContext;
class App;
class Window;
struct WindowProps;

namespace loka
{
  namespace file
  {
    struct File;
  }

  namespace platform
  {
    namespace file
    {
      struct FileHandle;
    }
  } // namespace platform

  namespace core
  {
    namespace resource
    {
      class Blob;
      class Image;
    } // namespace resource
  } // namespace core

  namespace app
  {
    namespace scene
    {
      class Scene;
      class Node;
      struct NodeContext;
    } // namespace scene
  } // namespace app
} // namespace loka

class PlatformContext
{
public:
  PlatformContext() {}
  virtual ~PlatformContext() {}

  // Creates the platform-specific App instance.
  virtual App *createApp(AppConfigurable *config, NativeModuleHandle hInstance, int nCmdShow) const = 0;

  // Creates a platform-specific Window. Implementations should assert on invalid owner state.
  virtual Window *createWindow(const WindowProps &props) = 0;

  virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *node) const = 0;
  virtual bool openFile(const loka::file::File &item, loka::platform::file::FileHandle &out) const = 0;
  /** Estimates what one contiguous allocation could obtain now, including
      compaction, purging and growth the allocation itself would perform.
      The query may perform that memory-manager work and may report a
      conservative lower bound. It does not reserve memory or guarantee a
      later allocation. Targets without a meaningful answer decline. */
  virtual bool queryLargestContiguousAllocation(std::size_t &out) const
  {
    (void)out;
    return false;
  }
  /** Main display bits per pixel at the moment of the call; targets that
      cannot answer decline and leave out untouched. Twin of
      Window::queryDisplayDepth, which asks about the display a window overlaps
      most; this query supports choosing resources before or without a window.
      A plain value, not State: an app that must react writes its own State
      from its own callback (#194). */
  virtual bool queryMainDisplayDepth(int &out) const
  {
    (void)out;
    return false;
  }
  /** Builds an image from `[offset, offset + length)` of `blob`.
      A range rather than the whole buffer, because an LRPK asset is a range
      inside its bag's blob and copying it out to be decoded would put the
      bytes in memory twice -- once in the bag and once per asset drawn.
      Implementations must not read outside the supplied range. */
  virtual bool createImageFromBlob(const loka::core::resource::Blob &blob,
                                   std::size_t offset,
                                   std::size_t length,
                                   loka::core::resource::Image &out) = 0;
private:
  friend class App;
  friend class loka::app::internal::Reservation;
  friend class loka::app::testing::NativeResourceRetirementTestAccess;
  loka::app::NativeResourceRetirement nativeResourceRetirement_;
  PlatformContext(const PlatformContext &);
  PlatformContext &operator=(const PlatformContext &);
};

#endif // LOKA_PLATFORMCONTEXT_HPP
