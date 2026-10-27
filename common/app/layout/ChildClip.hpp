#ifndef LOKA_APP_LAYOUT_CHILD_CLIP_HPP
#define LOKA_APP_LAYOUT_CHILD_CLIP_HPP

#include <cassert>

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

      /** Applies retained props while keeping the clip flag chosen at node creation.
          Use separate Match / Show arms or Keyed to create a node with another flag.
          Audit builds diagnose attempts to change it; release applies every other field
          and succeeds without requesting replacement. Called by applyPropsToNode once
          per retained application; walks no rows. */
      template <class NodeT, class PropsT> bool ApplyPropsKeepingChildClip(NodeT *node, const PropsT &props)
      {
        const ChildClip childClip = node->props.childClip_;
#ifdef LOKA_LIFECYCLE_AUDIT
        assert(props.childClip_ == childClip && "clipChildren is fixed at creation; use Match / Show arms or Keyed");
#endif
        node->props = props;
        node->props.childClip_ = childClip;
        return true;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_CHILD_CLIP_HPP
