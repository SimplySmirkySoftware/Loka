#ifndef LOKA_TESTS_PICT_ADMISSION_FIXTURE_HPP
#define LOKA_TESTS_PICT_ADMISSION_FIXTURE_HPP

#include <vector>

namespace pict_fixture
{
  typedef std::vector<unsigned char> Bytes;
  inline void word(Bytes &b, unsigned value)
  {
    b.push_back(static_cast<unsigned char>(value >> 8));
    b.push_back(static_cast<unsigned char>(value));
  }
  inline void putWord(Bytes &b, std::size_t at, unsigned value)
  {
    b[at] = static_cast<unsigned char>(value >> 8);
    b[at + 1] = static_cast<unsigned char>(value);
  }
  inline void rect(Bytes &b, unsigned width, unsigned height)
  {
    word(b, 0);
    word(b, 0);
    word(b, height);
    word(b, width);
  }
  inline void opcode(Bytes &b, unsigned value, bool v2)
  {
    if (v2)
    {
      if (b.size() & 1)
        b.push_back(0);
      word(b, value);
    }
    else
      b.push_back(static_cast<unsigned char>(value));
  }
  inline Bytes preamble(bool v2, bool extended = true, unsigned width = 16, unsigned height = 1)
  {
    Bytes b;
    word(b, 0);
    rect(b, width, height);
    if (v2)
    {
      word(b, 0x11);
      word(b, 0x02FF);
      word(b, 0x0C00);
      word(b, extended ? 0xFFFE : 0xFFFF);
      b.resize(40, 0);
    }
    else
    {
      b.push_back(0x11);
      b.push_back(1);
    }
    return b;
  }
  inline void clip(Bytes &b, bool v2)
  {
    opcode(b, 1, v2);
    word(b, 10);
    rect(b, 16, 1);
  }
  inline void raster(Bytes &b, bool v2, unsigned rowBytes, unsigned op = 0x98, unsigned height = 1, unsigned mode = 0)
  {
    opcode(b, op, v2);
    word(b, rowBytes);
    rect(b, rowBytes * 8, height);
    rect(b, 16, height);
    rect(b, 16, height);
    word(b, mode);
    for (unsigned y = 0; y < height; ++y)
    {
      if (op == 0x90 || rowBytes < 8)
        b.insert(b.end(), rowBytes, 0x55);
      else
      {
        Bytes packed;
        unsigned remaining = rowBytes;
        while (remaining)
        {
          const unsigned run = remaining > 128 ? 128 : remaining;
          packed.push_back(static_cast<unsigned char>(257 - run));
          packed.push_back(0x55);
          remaining -= run;
        }
        if (rowBytes > 250)
          word(b, static_cast<unsigned>(packed.size()));
        else
          b.push_back(static_cast<unsigned char>(packed.size()));
        b.insert(b.end(), packed.begin(), packed.end());
      }
    }
  }
  inline Bytes picture(bool v2 = false,
                       unsigned rowBytes = 8,
                       unsigned op = 0x98,
                       bool extended = true,
                       unsigned height = 1,
                       unsigned mode = 0)
  {
    Bytes b = preamble(v2, extended, 16, height);
    raster(b, v2, rowBytes, op, height, mode);
    opcode(b, 0xFF, v2);
    putWord(b, 0, static_cast<unsigned>(b.size() & 0xFFFF));
    return b;
  }
} // namespace pict_fixture
#endif
