#ifndef LOKA_TEST_QUICKDRAW_H
#define LOKA_TEST_QUICKDRAW_H
#include <cstddef>
typedef unsigned char Style;
typedef unsigned char Str255[256];
enum
{
  whiteColor = 30,
  blackColor = 33,
  bold = 1,
  italic = 2
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
struct GrafPort
{
  short txFont, txSize;
  Style txFace;
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
