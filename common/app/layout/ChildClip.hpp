#ifndef LOKA_APP_LAYOUT_CHILD_CLIP_HPP
#define LOKA_APP_LAYOUT_CHILD_CLIP_HPP

#include <climits>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Resolves a positive clip width: prefer a positive declaration, otherwise
          the offer, and cap at a positive offer. Card passes 0 for its declaration.
          Returns false without writing width when neither resolves positively. */
      inline bool ResolveClipBoxWidth(int declaredWidth, int offeredWidth, int &width)
      {
        const int declaredOrOffer = declaredWidth > 0 ? declaredWidth : offeredWidth;
        const int resolved = offeredWidth > 0 && declaredOrOffer > offeredWidth ? offeredWidth : declaredOrOffer;
        if (resolved <= 0)
          return false;
        width = resolved;
        return true;
      }

      /** LayoutState positions and widths (scene/Node.hpp) are short, so a
          descendant's right edge x + width is at most 65534 logical units.
          A rail without a short coordinate ceiling projects this value as the
          right edge of a clip box whose width is unresolved. Toolbox does not
          use it: QuickDraw coordinates already stop at SHRT_MAX. */
      const int kClipFarRightLu = 65535;
      typedef char ClipFarRightMustExceedLayoutEdge[kClipFarRightLu > SHRT_MAX + SHRT_MAX ? 1 : -1];

      /** How a container treats children that paint outside its rect. */
      enum ChildClip
      {
        CHILD_CLIP_NONE,
        CHILD_CLIP_OWN_RECT
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_CHILD_CLIP_HPP
