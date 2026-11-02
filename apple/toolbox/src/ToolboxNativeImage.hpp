#ifndef LOKA_TOOLBOX_NATIVE_IMAGE_HPP
#define LOKA_TOOLBOX_NATIVE_IMAGE_HPP

#include <Quickdraw.h>
#include "ToolboxPictAdmission.hpp"
#include "core/resource/Blob.hpp"
#include "core/resource/Image.hpp"

namespace loka
{
  namespace toolbox
  {
    enum ToolboxNativeImageKind
    {
      TOOLBOX_NATIVE_IMAGE_KIND_UNKNOWN = 0,
      TOOLBOX_NATIVE_IMAGE_KIND_PICT = 1,
      TOOLBOX_NATIVE_IMAGE_KIND_PICT_BYTES = 2
    };

    struct ToolboxPictBytesPayload
    {
      // Shares the source Blob's refcounted buffer instead of copying the PICT
      // bytes a second time; the streaming getPicProc reads straight from it.
      loka::core::resource::Blob blob;
      std::size_t pictureOffset;
      /** One past the picture's last byte, absolute within the blob. The blob
          may hold more than this picture -- a whole LRPK bag, or a file with
          trailing bytes -- so the end is carried rather than taken to be the
          end of the buffer. */
      std::size_t pictureEnd;
      ToolboxPictAdmission admission;
      ToolboxPictBytesPayload()
          : blob(),
            pictureOffset(0),
            pictureEnd(0)
      {
      }
    };

    struct ToolboxNativeImage
    {
      unsigned long magic;
      ToolboxNativeImageKind kind;
      void *payload;
    };

    /** Borrows picture. The caller must keep it alive past every Image copy
        and dispose of it itself. A future owning producer must reserve through
        loka::app::internal::Reservation before acquiring the native resource;
        see docs/NativeResourceRetirementDesign.md. */
    loka::core::resource::Image MakeImageFromPicHandle(PicHandle picture, int width, int height);
    loka::core::resource::Image
    MakeImageFromPictBlob(const loka::core::resource::Blob &blob,
                          std::size_t pictureOffset,
                          std::size_t pictureEnd,
                          int width,
                          int height);

    const unsigned long kToolboxNativeImageMagic = 0x4C4F4B41UL; // 'LOKA'

    inline const ToolboxNativeImage *TryGetToolboxNativeImage(const loka::core::resource::Image &image)
    {
      if (!image.isValid())
      {
        return 0;
      }
      const ToolboxNativeImage *native = static_cast<const ToolboxNativeImage *>(image.nativeHandle());
      if (!native || native->magic != kToolboxNativeImageMagic)
      {
        return 0;
      }
      return native;
    }
  } // namespace toolbox
} // namespace loka

#endif // LOKA_TOOLBOX_NATIVE_IMAGE_HPP
