#ifndef LOKA_APP_DOC_LOWERING_HPP
#define LOKA_APP_DOC_LOWERING_HPP
#include "app/nodes/AttributedText.hpp"
#include "app/style/AttributedTextRead.hpp"
#include <functional>
namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Segment-sensitive value ordering, with invalid values first.
            Cost: each props comparison walks only the compared values' segments and bytes.
            The existing text order preserves opaque refused String identities. */
        inline int CompareContent(const AttributedString &left, const AttributedString &right)
        {
          if (left.valid() != right.valid())
            return left.valid() ? 1 : -1;
          if (!left.valid())
            return 0;
          const std::size_t count =
              left.segmentCount() < right.segmentCount() ? left.segmentCount() : right.segmentCount();
          for (std::size_t i = 0; i < count; ++i)
          {
            const AttributedString::Segment &a = left.segment(i), &b = right.segment(i);
            int order = style_internal::CompareLinkTargets(LinkTarget(a.text), LinkTarget(b.text));
            if (order)
              return order;
            if (a.style < b.style)
              return -1;
            if (b.style < a.style)
              return 1;
            order = style_internal::CompareLinkTargets(a.link, b.link);
            if (order)
              return order;
          }
          if (left.segmentCount() == right.segmentCount())
            return 0;
          return left.segmentCount() < right.segmentCount() ? -1 : 1;
        }
        /** Applies a complete constant leaf, including its activation box.
            Cost: once per retained props application; checks one owned child, no row walk. */
        inline void ApplyLeaf(scene::Node *child, const AttributedTextDefinitionWithAttr &lowered)
        {
          if (!child)
            return;
          const bool expected = child->asAttributedTextNode() && !child->nextInComposition;
          assert(expected && "document leaf shape");
          if (expected)
            lowered.applyPropsToNode(child);
        }
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
