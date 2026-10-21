#ifndef LOKA_TOOLBOX_BLUR_KERNEL_HPP
#define LOKA_TOOLBOX_BLUR_KERNEL_HPP
#include "platform/CpuClass.hpp"

namespace loka
{
  namespace toolbox
  {
    enum ToolboxBlurEnlargement
    {
      TOOLBOX_BLUR_MOSAIC,
      TOOLBOX_BLUR_SMOOTH
    };
    /** Expression policy; a caller with no CPU answer supplies CPU_CLASS_68K. */
    ToolboxBlurEnlargement ToolboxBlurEnlargementFor(platform::CpuClass cpu);

    /** Borrowed colour query; failure has no answer. Never owned through this base. */
    class ToolboxBlurColourQuery
    {
    public:
      virtual bool indexFor(unsigned short red, unsigned short green, unsigned short blue, unsigned char &index) = 0;

    protected:
      ~ToolboxBlurColourQuery() {}
    };

    /** Fill caller-owned storage with centre answers for reachable milked colours.
        Invalid inputs leave storage untouched; query failure stops immediately.
        Other slots are zero, which is also a valid answer, not a failure sentinel. */
    bool ToolboxBlurFillInverse(unsigned char *table, long size, unsigned resolution, ToolboxBlurColourQuery &query);

    /** Accept only byte-sized QuickDraw answers, leaving index unchanged on refusal. */
    bool ToolboxBlurAcceptColorIndex(long answer, unsigned char &index);

    /** Rewrite one indexed image, borrowing all inputs for this call only.
        Palette contains 256 RGB byte triples; inverse has 1 << (3*resolution)
        bytes. Stride is decoded bytes, not QuickDraw rowBytes. Invalid sizes or
        allocation refusal leave all pixels and padding unchanged. Padding is
        never read. Only the final enlargement writes pixels; no hairline. */
    bool ToolboxBlurKernel(unsigned char *pixels,
                           int width,
                           int height,
                           long stride,
                           const unsigned char *palette,
                           int cell,
                           ToolboxBlurEnlargement mode,
                           const unsigned char *inverse,
                           unsigned resolution);
  } // namespace toolbox
} // namespace loka
#endif
