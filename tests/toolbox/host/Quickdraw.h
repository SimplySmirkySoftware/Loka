#ifndef LOKA_TEST_QUICKDRAW_H
#define LOKA_TEST_QUICKDRAW_H
#include <cstddef>
typedef unsigned char Style;
struct Pattern
{
  unsigned char pat[8];
};
void FillRect(const struct Rect *, const Pattern *);
typedef unsigned char Str255[256];
enum
{
  whiteColor = 30,
  blackColor = 33,
  redColor = 205, greenColor = 341, blueColor = 409, cyanColor = 273, magentaColor = 137, yellowColor = 69,
  bold = 1,
  italic = 2,
  srcOr = 1,
  patCopy = 8,
  patOr = 9
};
struct Rect
{
  short top, left, bottom, right;
};
struct Region
{
  short rgnSize;
  Rect rgnBBox;
};
typedef Region **RgnHandle;
struct PenState
{
  short mode, width, height;
  Pattern pattern;
};
void GetPenState(PenState *);
void SetPenState(const PenState *);
void PenNormal();
void PenPat(const Pattern *);
void PenMode(short);
void OpenRgn();
void CloseRgn(RgnHandle);
void FrameRoundRect(const Rect *, short, short);
void OffsetRect(Rect *, short, short);
void DiffRgn(RgnHandle, RgnHandle, RgnHandle);
void PaintRgn(RgnHandle);
typedef char *Ptr;
struct BitMap
{
  Ptr baseAddr;
  short rowBytes;
  Rect bounds;
};
void CopyBits(const BitMap *, const BitMap *, const Rect *, const Rect *, short, RgnHandle);
struct GrafPort
{
  BitMap portBits;
  short txFont, txSize;
  Style txFace;
  long fgColor;
};
typedef GrafPort *GrafPtr;
struct FontInfo
{
  short ascent, descent, widMax, leading;
};
void GetPort(GrafPtr *);
void SetPort(GrafPtr);
void TextFont(short);
void TextSize(short);
void TextFace(Style);
short GetDefFontSize();
short GetSysFont();
void GetFontInfo(FontInfo *);
void MeasureText(short, const void *, void *);
short TextWidth(const void *, short, short);
short StringWidth(const unsigned char *);
void DrawString(const unsigned char *);
void SetWTitle(GrafPtr, const unsigned char *);
void DrawText(const void *, short, short);
void MoveTo(short, short);
void SetRect(Rect *, short, short, short, short);
bool EmptyRect(const Rect *);
RgnHandle NewRgn();
void DisposeRgn(RgnHandle);
void GetClip(RgnHandle);
void RectRgn(RgnHandle, const Rect *);
void SectRgn(RgnHandle, RgnHandle, RgnHandle);
void SetClip(RgnHandle);
bool RectInRgn(const Rect *, RgnHandle);
void EraseRect(const Rect *);
bool EqualRect(const Rect *, const Rect *);
bool SectRect(const Rect *, const Rect *, Rect *);
void SetEmptyRgn(RgnHandle);
void UnionRgn(RgnHandle, RgnHandle, RgnHandle);
void PaintRect(const Rect *);
void BackColor(long);
void ForeColor(long);
void ClipRect(const Rect *);
struct Picture
{
  short picSize;
  Rect picFrame;
};
typedef Picture *PicPtr;
typedef PicPtr *PicHandle;
void KillPicture(PicHandle);
#endif
