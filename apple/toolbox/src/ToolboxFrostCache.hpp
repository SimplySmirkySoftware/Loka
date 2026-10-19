#ifndef LOKA_TOOLBOX_FROST_CACHE_HPP
#define LOKA_TOOLBOX_FROST_CACHE_HPP
#include <QDOffscreen.h>

/** Toolbox-only hardware fact; unknown answers are declined, not guessed. */
enum ToolboxFpuType
{
  TOOLBOX_FPU_NONE,
  TOOLBOX_FPU_68881,
  TOOLBOX_FPU_68882,
  TOOLBOX_FPU_68040,
  TOOLBOX_FPU_POWERPC
};
bool queryToolboxFpuType(ToolboxFpuType &out);

/** Controller-owned scratch capacity, never retained backdrop content.
    Each paint captures every pixel it reads. No callbacks or clock residents. */
class ToolboxFrostCache
{
public:
  ToolboxFrostCache();
  ~ToolboxFrostCache();
  /** Called once per eligible unit per erased composition walk; O(w*h) over
      this owner's scratch pixels. False asks the caller to paint window ground. */
  bool paint(WindowPtr window, const Rect &rect);

private:
  friend class ToolboxFrostCacheInspection;
  bool prepare(WindowPtr window, short width, short height);
  GWorldPtr world_;
  unsigned char milkIndex_[256];
  unsigned char hairline_;
  ToolboxFpuType fpu_;
  ToolboxFrostCache(const ToolboxFrostCache &);
  ToolboxFrostCache &operator=(const ToolboxFrostCache &);
};
#endif
