#include "ToolboxPictAdmissionTests.hpp"
#include "support/PictAdmissionFixture.hpp"
#include "support/TestVerify.hpp"
#include "../apple/toolbox/src/ToolboxPictAdmission.hpp"
#include <cstdio>
#include <string>

namespace
{
  using namespace pict_fixture;
  bool admitted(const Bytes &b)
  {
    return loka::toolbox::ScanToolboxPictAdmission(b.empty() ? 0 : &b[0], b.size()).admitsTranslation();
  }
} // namespace

void testPictAdmissionRasterVersions()
{
  LOKA_VERIFY(admitted(picture(false, 6, 0x90)));
  LOKA_VERIFY(admitted(picture(false, 6)));
  LOKA_VERIFY(admitted(picture(true, 8, 0x98, false)));
  LOKA_VERIFY(admitted(picture(true)));
  Bytes b = preamble(false);
  opcode(b, 0, false);
  raster(b, false, 8);
  raster(b, false, 8, 0x90);
  opcode(b, 0xFF, false);
  LOKA_VERIFY(admitted(b));
}

void testPictAdmissionPackBitsBoundaries()
{
  const unsigned widths[] = {6, 8, 250, 252};
  for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
  {
    Bytes b = picture(false, widths[i]);
    LOKA_VERIFY(admitted(b));
    b.erase(b.end() - 2); // Missing last row byte, not just the terminator.
    LOKA_VERIFY(!admitted(b));
  }
  Bytes b = picture();
  b[42] = 250; // repeat 7, expected 8
  LOKA_VERIFY(!admitted(b));
  b[42] = 248; // repeat 9, expected 8
  LOKA_VERIFY(!admitted(b));
  b = picture();
  b[42] = 7; // Literal eight bytes, count and data must agree.
  b[41] = 9;
  b.insert(b.begin() + 44, 7, 0x55);
  LOKA_VERIFY(admitted(b));
  b[41] = 10;
  b.insert(b.end() - 1, 128); // PackBits no-op consumes input but expands nothing.
  LOKA_VERIFY(admitted(b));
  b[41] = 8;
  LOKA_VERIFY(!admitted(b));
  b = picture();
  b[41] = 1;
  b[42] = 128;
  LOKA_VERIFY(!admitted(b));
  b = picture();
  b[41] = 3; // Count consumes the End byte, then the next opcode is missing.
  LOKA_VERIFY(!admitted(b));
}

void testPictAdmissionGeometryAndModes()
{
  // Signed coordinates are valid; preserve the dimensions while crossing zero.
  Bytes signedPicture = picture();
  const unsigned rectangles[] = {2, 15, 23, 31};
  for (unsigned i = 0; i < 4; ++i)
  {
    const unsigned at = rectangles[i];
    putWord(signedPicture, at, 0xFFFF);
    putWord(signedPicture, at + 2, 0xFFFE);
    putWord(signedPicture, at + 4, 0);
    putWord(signedPicture, at + 6, i == 1 ? 62 : 14);
  }
  LOKA_VERIFY(admitted(signedPicture));
  const unsigned invalidRows[] = {0, 7, 0x8008};
  for (unsigned i = 0; i < 3; ++i)
  {
    Bytes b = picture();
    putWord(b, 13, invalidRows[i]);
    LOKA_VERIFY(!admitted(b));
  }
  const unsigned modes[] = {0, 1, 2, 0x40, 0x41};
  for (unsigned i = 0; i < 5; ++i)
    LOKA_VERIFY(admitted(picture(false, 8, 0x98, true, 1, modes[i])) == (i < 2));
  // Each rectangle must be nonempty and ordered.
  for (unsigned at = 15; at <= 31; at += 8)
  {
    Bytes b = picture();
    putWord(b, at, 2);
    LOKA_VERIFY(!admitted(b));
    b = picture();
    putWord(b, at + 6, 0);
    LOKA_VERIFY(!admitted(b));
  }
  Bytes b = picture();
  putWord(b, 29, 65); // source beyond bitmap bounds
  LOKA_VERIFY(!admitted(b));
  b = picture();
  putWord(b, 37, 17); // destination beyond picture frame
  LOKA_VERIFY(!admitted(b));
  b = picture();
  putWord(b, 21, 65); // bitmap width exceeds row storage
  LOKA_VERIFY(!admitted(b));
}

void testPictAdmissionClipAndOpcodes()
{
  for (unsigned version = 0; version < 2; ++version)
  {
    const bool v2 = version != 0;
    Bytes b = preamble(v2);
    clip(b, v2);
    const Bytes clipped = b;
    raster(b, v2, 8);
    opcode(b, 0xFF, v2);
    LOKA_VERIFY(admitted(b));
    b = clipped;
    clip(b, v2);
    opcode(b, 0xFF, v2);
    LOKA_VERIFY(!admitted(b));
    b = preamble(v2);
    raster(b, v2, 8);
    clip(b, v2);
    opcode(b, 0xFF, v2);
    LOKA_VERIFY(!admitted(b));
    b = clipped;
    putWord(b, v2 ? 42 : 13, 12);
    opcode(b, 0xFF, v2);
    LOKA_VERIFY(!admitted(b));
    b = clipped;
    putWord(b, v2 ? 48 : 19, 0); // Empty Clip rectangle.
    opcode(b, 0xFF, v2);
    LOKA_VERIFY(!admitted(b));
    const unsigned refused[] = {0xA1, 0x9A, 0x91, 0x99, 0x81, 0x20, 0x30, 0x42, 0x77};
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
      b = preamble(v2);
      opcode(b, refused[i], v2);
      if (refused[i] == 0xA1)
      {
        word(b, 0);
        word(b, 0);
      }
      opcode(b, 0xFF, v2);
      LOKA_VERIFY(!admitted(b));
    }
  }
}

void testPictAdmissionTruncationAndAlignment()
{
  for (unsigned version = 0; version < 2; ++version)
  {
    const Bytes complete = picture(version != 0, 252);
    for (std::size_t end = 0; end < complete.size(); ++end)
    {
      const Bytes truncated(complete.begin(), complete.begin() + end);
      LOKA_VERIFY(!admitted(truncated));
    }
  }
  Bytes b = picture(true); // odd raster length requires padding before End
  b.erase(b.end() - 3);
  LOKA_VERIFY(!admitted(b));
  b = picture(true);
  putWord(b, 16, 0);
  LOKA_VERIFY(!admitted(b));
  LOKA_VERIFY(!loka::toolbox::ScanToolboxPictAdmission(0, 100).admitsTranslation());
}

void testPictAdmissionWrappedSizeAndRange()
{
  Bytes b = picture(true, 252, 0x90, true, 261);
  LOKA_VERIFY(b.size() > 65535);
  b.insert(b.end(), 17, 0xA5);
  LOKA_VERIFY(admitted(b));
  // A permissive size and a terminator outside the supplied range cannot save it.
  LOKA_VERIFY(!loka::toolbox::ScanToolboxPictAdmission(&b[0], b.size() - 19).admitsTranslation());
  Bytes bag(7, 0xA5);
  const Bytes small = picture();
  bag.insert(bag.end(), small.begin(), small.end());
  bag.insert(bag.end(), 20, 0xA5);
  LOKA_VERIFY(loka::toolbox::ScanToolboxPictAdmission(&bag[7], small.size()).admitsTranslation());
  LOKA_VERIFY(!loka::toolbox::ScanToolboxPictAdmission(&bag[7], small.size() - 1).admitsTranslation());
}

void testPictAdmissionSmirkyThumbnails()
{
  // The thirteen LAPTOP.JS machines; ICON is an additional generator asset.
  const char *names[] = {"NEO",
                         "MBP2023",
                         "MBP2019",
                         "MBP2015",
                         "MBP2011",
                         "MB2006",
                         "IBOOKG3",
                         "TIBOOK",
                         "PISMO",
                         "PDQ",
                         "PB550C",
                         "PB180C",
                         "MACPLUS"};
  const std::string source(__FILE__);
  const std::string root = source.substr(0, source.find_last_of("/\\")) + "/../example/SmirkyCard/assets/";
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
  {
    const std::string path = root + names[i] + "MS.PICT";
    FILE *file = std::fopen(path.c_str(), "rb");
    LOKA_VERIFY(file != 0);
    Bytes b;
    int byte;
    while ((byte = std::fgetc(file)) != EOF)
      b.push_back(static_cast<unsigned char>(byte));
    LOKA_VERIFY(!std::ferror(file));
    LOKA_VERIFY(std::fclose(file) == 0);
    LOKA_VERIFY(b.size() > 512);
    LOKA_VERIFY(loka::toolbox::ScanToolboxPictAdmission(&b[512], b.size() - 512).admitsTranslation());
  }
}
