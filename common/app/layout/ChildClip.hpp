#ifndef LOKA_APP_LAYOUT_CHILD_CLIP_HPP
#define LOKA_APP_LAYOUT_CHILD_CLIP_HPP

namespace loka
{
  namespace app
  {
    namespace layout
    {
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
