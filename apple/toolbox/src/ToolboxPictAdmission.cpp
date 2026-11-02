#include "ToolboxPictAdmission.hpp"

namespace
{
  class Reader
  {
  public:
    Reader(const unsigned char *bytes, std::size_t size)
        : bytes_(bytes),
          size_(size),
          position_(0)
    {
    }
    bool skip(std::size_t count)
    {
      if (count > this->size_ - this->position_)
        return false;
      this->position_ += count;
      return true;
    }
    bool byte(unsigned &value)
    {
      if (this->position_ == this->size_)
        return false;
      value = this->bytes_[this->position_++];
      return true;
    }
    bool word(unsigned &value)
    {
      unsigned high, low;
      if (!this->byte(high) || !this->byte(low))
        return false;
      value = (high << 8) | low;
      return true;
    }
    bool align()
    {
      return !(this->position_ & 1) || this->skip(1);
    }

  private:
    const unsigned char *bytes_;
    std::size_t size_;
    std::size_t position_;
  };

  struct PictureRect
  {
    long top, left, bottom, right;
    bool read(Reader &reader)
    {
      unsigned t, l, b, r;
      if (!reader.word(t) || !reader.word(l) || !reader.word(b) || !reader.word(r))
        return false;
      this->top = signedWord(t);
      this->left = signedWord(l);
      this->bottom = signedWord(b);
      this->right = signedWord(r);
      return this->top < this->bottom && this->left < this->right;
    }
    bool contains(const PictureRect &other) const
    {
      return this->top <= other.top && this->left <= other.left && this->bottom >= other.bottom
             && this->right >= other.right;
    }
    static long signedWord(unsigned value)
    {
      return value < 0x8000U ? static_cast<long>(value) : static_cast<long>(value) - 65536L;
    }
  };

  bool packedRow(Reader &reader, unsigned rowBytes)
  {
    unsigned count;
    if (!(rowBytes > 250 ? reader.word(count) : reader.byte(count)))
      return false;
    unsigned expanded = 0;
    while (count)
    {
      unsigned code;
      if (!reader.byte(code))
        return false;
      --count;
      const unsigned input = code < 128 ? code + 1 : code == 128 ? 0 : 1;
      const unsigned output = code < 128 ? code + 1 : code == 128 ? 0 : 257 - code;
      if (input > count || output > rowBytes - expanded || !reader.skip(input))
        return false;
      count -= input;
      expanded += output;
    }
    return expanded == rowBytes;
  }

  bool raster(Reader &reader, unsigned opcode, const PictureRect &frame)
  {
    unsigned rowBytes, mode;
    PictureRect bounds, source, destination;
    if (!reader.word(rowBytes) || !rowBytes || (rowBytes & 0x8001U) || !bounds.read(reader) || !source.read(reader)
        || !destination.read(reader) || !bounds.contains(source) || !frame.contains(destination)
        || bounds.right - bounds.left > static_cast<long>(rowBytes) * 8 || !reader.word(mode) || mode > 1)
      return false;
    for (long row = bounds.top; row < bounds.bottom; ++row)
    {
      if (opcode == 0x98 && rowBytes >= 8)
      {
        if (!packedRow(reader, rowBytes))
          return false;
      }
      else if (!reader.skip(rowBytes))
        return false;
    }
    return true;
  }

  bool scan(const unsigned char *bytes, std::size_t size)
  {
    if (!bytes)
      return false;
    Reader reader(bytes, size);
    PictureRect frame;
    unsigned version, opcode;
    if (!reader.skip(2) || !frame.read(reader) || !reader.byte(version))
      return false;
    const bool v2 = version == 0;
    if (v2)
    {
      unsigned header;
      if (!reader.byte(version) || version != 0x11 || !reader.word(version) || version != 0x02FF || !reader.word(opcode)
          || opcode != 0x0C00 || !reader.word(header) || (header != 0xFFFF && header != 0xFFFE) || !reader.skip(22))
        return false;
    }
    else if (version != 0x11 || !reader.byte(version) || version != 1)
      return false;

    bool clipped = false;
    bool rasterSeen = false;
    for (;;)
    {
      if (v2 && !reader.align())
        return false;
      if (!(v2 ? reader.word(opcode) : reader.byte(opcode)))
        return false;
      switch (opcode)
      {
      case 0:
        break;
      case 1:
      {
        unsigned regionSize;
        PictureRect clip;
        if (clipped || rasterSeen || !reader.word(regionSize) || regionSize != 10 || !clip.read(reader))
          return false;
        clipped = true;
        break;
      }
      case 0x90:
      case 0x98:
        if (!raster(reader, opcode, frame))
          return false;
        rasterSeen = true;
        break;
      case 0xFF:
        return true;
      default:
        return false;
      }
    }
  }
} // namespace

loka::toolbox::ToolboxPictAdmission loka::toolbox::ScanToolboxPictAdmission(const unsigned char *bytes,
                                                                            std::size_t size)
{
  return ToolboxPictAdmission(scan(bytes, size));
}
