#include "ToolboxNativeImage.hpp"

namespace
{
  loka::core::LokaAllocationSite NativeImageSite()
  {
    return loka::core::LokaAllocationSite("ToolboxNativeImage", "Image");
  }

  loka::core::LokaAllocationSite PictBytesSite()
  {
    return loka::core::LokaAllocationSite("ToolboxNativeImage", "PictBytes");
  }

  void ReleaseToolboxNativeImage(void *handle, void *)
  {
    loka::toolbox::ToolboxNativeImage *native = static_cast<loka::toolbox::ToolboxNativeImage *>(handle);
    if (!native)
    {
      return;
    }

    switch (native->kind)
    {
    case loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_UNKNOWN:
    case loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_PICT:
      break;
    case loka::toolbox::TOOLBOX_NATIVE_IMAGE_KIND_PICT_BYTES:
      loka::core::LokaDelete(static_cast<loka::toolbox::ToolboxPictBytesPayload *>(native->payload), PictBytesSite());
      break;
    }

    loka::core::LokaDelete(native, NativeImageSite());
  }
} // namespace

namespace loka
{
  namespace toolbox
  {
    loka::core::resource::Image MakeImageFromPicHandle(PicHandle picture, int width, int height)
    {
      if (!picture)
      {
        return loka::core::resource::Image::Empty();
      }

      ToolboxNativeImage *native = loka::core::LokaNew<ToolboxNativeImage>(NativeImageSite());
      if (!native)
      {
        return loka::core::resource::Image::Empty();
      }
      native->magic = kToolboxNativeImageMagic;
      native->kind = TOOLBOX_NATIVE_IMAGE_KIND_PICT;
      native->payload = picture;

      return loka::core::resource::Image::FromNative(native, width, height, &ReleaseToolboxNativeImage, 0);
    }

    loka::core::resource::Image
    MakeImageFromPictBlob(const loka::core::resource::Blob &blob,
                          std::size_t pictureOffset,
                          std::size_t pictureEnd,
                          int width,
                          int height)
    {
      if (pictureOffset >= pictureEnd || pictureEnd > blob.size() || width <= 0 || height <= 0)
      {
        return loka::core::resource::Image::Empty();
      }

      ToolboxPictBytesPayload *payload = loka::core::LokaNew<ToolboxPictBytesPayload>(PictBytesSite());
      if (!payload)
      {
        return loka::core::resource::Image::Empty();
      }
      // Share the source buffer only when the Blob is a stable snapshot
      // (completed and immutable); the streamed bytes must stay consistent
      // with the width/height parsed here. For a mutable or still-loading
      // Blob, take an owned copy so later storage writes can't
      // desync the rendered picture from its reported size — matching the
      // snapshot behavior of the macOS/Win32 decoders.
      if (blob.isCompleted())
      {
        payload->blob = blob;
        payload->pictureOffset = pictureOffset;
        payload->pictureEnd = pictureEnd;
      }
      else
      {
        // Only the picture's range. The API's contract is that no
        // implementation reads outside the supplied range, and a bag-sized
        // mutable blob copied whole per image would be the doubling this
        // seam exists to avoid. The snapshot is its own coordinate system --
        // rebasing to zero here is not the cross-boundary double-count the
        // design guards against, because the payload stores blob and offsets
        // as one consistent pair.
        const unsigned char *source = blob.data();
        loka::core::resource::Blob snapshot = loka::core::resource::Blob::Create();
        if (!snapshot.isValid() || !snapshot.tryAssign(source + pictureOffset, pictureEnd - pictureOffset))
        {
          loka::core::LokaDelete(payload, PictBytesSite());
          return loka::core::resource::Image::Empty();
        }
        snapshot.sealBytes();
        payload->blob = snapshot;
        payload->pictureOffset = 0;
        payload->pictureEnd = pictureEnd - pictureOffset;
      }

      payload->admission = ScanToolboxPictAdmission(payload->blob.data() + payload->pictureOffset,
                                                    payload->pictureEnd - payload->pictureOffset);

      ToolboxNativeImage *native = loka::core::LokaNew<ToolboxNativeImage>(NativeImageSite());
      if (!native)
      {
        loka::core::LokaDelete(payload, PictBytesSite());
        return loka::core::resource::Image::Empty();
      }
      native->magic = kToolboxNativeImageMagic;
      native->kind = TOOLBOX_NATIVE_IMAGE_KIND_PICT_BYTES;
      native->payload = payload;

      return loka::core::resource::Image::FromNative(native, width, height, &ReleaseToolboxNativeImage, 0);
    }
  } // namespace toolbox
} // namespace loka
