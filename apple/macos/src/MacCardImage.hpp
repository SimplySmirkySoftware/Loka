#ifndef LOKA_MAC_CARD_IMAGE_HPP
#define LOKA_MAC_CARD_IMAGE_HPP
#include "MacObjCCompat.hpp"
#include <cstdlib>
#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacCardBitmap();
  }
} // namespace loka
#endif
namespace
{
  /** Lexical ownership of a Tiger-compatible RGBA premultiplied bitmap.
      Explicit storage is required: NULL bitmap data allocation is newer than Tiger. */
  class CardImage
  {
  public:
    CardImage(int width, int height)
        : bytes_(0),
          space_(0),
          context_(0),
          image_(0)
    {
#ifdef TEST_BUILD
      if (loka::testing::declineMacCardBitmap())
        return;
#endif
      this->bytes_ = static_cast<unsigned char *>(std::calloc(static_cast<size_t>(width) * height, 4));
      if (!this->bytes_)
        return;
      this->space_ = CGColorSpaceCreateDeviceRGB();
      if (this->space_)
        this->context_ = CGBitmapContextCreate(
            this->bytes_, width, height, 8, width * 4, this->space_, kCGImageAlphaPremultipliedLast);
    }
    ~CardImage()
    {
      if (this->image_)
        CGImageRelease(this->image_);
      if (this->context_)
        CGContextRelease(this->context_);
      if (this->space_)
        CGColorSpaceRelease(this->space_);
      std::free(this->bytes_);
    }
    unsigned char *bytes() const
    {
      return this->context_ ? this->bytes_ : 0;
    }
    /** Borrowed during one paint; O(1), no pixel walk. */
    CGContextRef context() const
    {
      return this->context_;
    }
    /** One seal per completed paint; Core Graphics may copy this bitmap. */
    CGImageRef seal()
    {
      this->image_ = CGBitmapContextCreateImage(this->context_);
      return this->image_;
    }

  private:
    unsigned char *bytes_;
    CGColorSpaceRef space_;
    CGContextRef context_;
    CGImageRef image_;
    CardImage(const CardImage &);
    CardImage &operator=(const CardImage &);
  };

} // namespace
#endif
