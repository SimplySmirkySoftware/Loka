#ifndef LOKA_TEST_QUICKDRAW_H
#define LOKA_TEST_QUICKDRAW_H
#include <cstddef>
#include <vector>
#include <map>
#include <string>
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
  underline = 4,
  srcOr = 1,
  patCopy = 8,
  patOr = 9
};
struct Point
{
  short v, h;
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
void ValidRect(const Rect *);
void DiffRgn(RgnHandle, RgnHandle, RgnHandle);
bool EmptyRgn(RgnHandle);
bool EqualRgn(RgnHandle, RgnHandle);
void ScrollRect(const Rect *, short, short, RgnHandle);
namespace toolbox_host
{
  extern std::vector<Rect> scrollRects, repairUnions;
  extern std::vector<Point> scrollOffsets;
  extern bool refuseExposedUnion, refuseCopiedClip;
} // namespace toolbox_host
namespace toolbox_host
{
  struct RegionGeometry : Region
  {
    std::vector<Rect> pieces;
  };
  extern short regionError;
  extern std::vector<std::string> regionOperations;
  extern std::vector<RegionGeometry> scrollClips, exposedRegions, erasedClips;
  bool regionIntersects(const RegionGeometry &, const Rect &);
  typedef std::map<const Region *, RegionGeometry> RegionStore;
  inline RegionStore &regionStore()
  {
    static RegionStore storage;
    return storage;
  }
  inline void forgetRegion(Region &region)
  {
    regionStore().erase(&region);
  }
  inline void copyRegion(const Region &source, Region &destination)
  {
    const RegionStore::const_iterator stored = regionStore().find(&source);
    destination = source;
    if (stored == regionStore().end())
      forgetRegion(destination);
    else
      regionStore()[&destination] = stored->second;
  }
  void failRegionOperation(unsigned);
  void corruptGetClipOperation(unsigned);
} // namespace toolbox_host
inline short MemError()
{
  return toolbox_host::regionError;
}
void PaintRgn(RgnHandle);
typedef char *Ptr;
struct BitMap
{
  Ptr baseAddr;
  short rowBytes;
  Rect bounds;
};
void CopyBits(const BitMap *, const BitMap *, const Rect *, const Rect *, short, RgnHandle);
struct HostControl;
/** Per-port OS state; update rectangles retain disjoint damage. */
struct HostPortState
{
  Region clipRegion;
  Region visibleRegion;
  Region *visibleRegionPointer;
  Region *clipRegionPointer;
  Rect &clip;
  std::vector<Rect> update;
  std::vector<HostControl *> controls;
  HostPortState()
      : visibleRegionPointer(&visibleRegion),
        clipRegionPointer(&clipRegion),
        clip(clipRegion.rgnBBox)
  {
    toolbox_host::forgetRegion(visibleRegion);
    toolbox_host::forgetRegion(clipRegion);
    visibleRegion.rgnSize = 10;
    visibleRegion.rgnBBox = Rect();
    clipRegion.rgnSize = 10;
    clip.top = clip.left = -30000;
    clip.bottom = clip.right = 30000;
  }
  HostPortState(const HostPortState &other)
      : clipRegion(other.clipRegion),
        visibleRegion(other.visibleRegion),
        visibleRegionPointer(&visibleRegion),
        clipRegionPointer(&clipRegion),
        clip(clipRegion.rgnBBox),
        update(other.update),
        controls(other.controls)
  {
    toolbox_host::copyRegion(other.clipRegion, clipRegion);
    toolbox_host::copyRegion(other.visibleRegion, visibleRegion);
  }
  ~HostPortState()
  {
    toolbox_host::forgetRegion(clipRegion);
    toolbox_host::forgetRegion(visibleRegion);
  }

private:
  HostPortState &operator=(const HostPortState &);
};
typedef void (*QDGetPicUPP)(void *, short);
struct QDProcs
{
  QDGetPicUPP getPicProc;
};
typedef QDProcs *QDProcsPtr;
typedef QDProcs CQDProcs;
typedef CQDProcs *CQDProcsPtr;
inline QDGetPicUPP NewQDGetPicUPP(QDGetPicUPP proc)
{
  return proc;
}
inline void SetStdProcs(QDProcs *procs)
{
  procs->getPicProc = 0;
}
inline void SetStdCProcs(CQDProcs *procs)
{
  procs->getPicProc = 0;
}
struct GrafPort
{
  BitMap portBits;
  short txFont, txSize;
  Style txFace;
  long fgColor;
  HostPortState native;
  RgnHandle clipRgn;
  RgnHandle visRgn;
  QDProcsPtr grafProcs;
  long spExtra;
  short chExtra;
};
typedef GrafPort *GrafPtr;
typedef GrafPtr CGrafPtr;
bool LMGetFractEnable();
struct FontInfo
{
  short ascent, descent, widMax, leading;
};
void GetPort(GrafPtr *);
void SetPort(GrafPtr);
/** Test entry scope, mirroring a native window operation's selected port. */
class HostPortScope
{
  GrafPtr previous_;

public:
  explicit HostPortScope(GrafPtr port)
  {
    GetPort(&this->previous_);
    SetPort(port);
  }
  ~HostPortScope()
  {
    SetPort(this->previous_);
  }

private:
  HostPortScope(const HostPortScope &);
  HostPortScope &operator=(const HostPortScope &);
};
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
#ifdef LOKA_HOST_IMAGE_VIEW
void DrawPicture(PicHandle, const Rect *);
void LineTo(short, short);
typedef long Size;
char **NewHandle(Size);
#endif
#endif
