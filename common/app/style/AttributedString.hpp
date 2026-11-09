#ifndef LOKA_APP_STYLE_ATTRIBUTED_STRING_HPP
#define LOKA_APP_STYLE_ATTRIBUTED_STRING_HPP

#include "app/style/Style.hpp"
#include "app/style/LinkTarget.hpp"
#include "core/String.hpp"
#include "core/Frame.hpp"

namespace loka
{
  namespace app
  {
    /** Immutable styled content with shared segment storage. */
    class AttributedString
    {
    public:
      class Builder;

      /** One text contribution, its declared character style and optional link. */
      struct Segment
      {
        core::String text;
        TextStyle style;
        LinkTarget link;
      };

      /** Inclusive endpoints of nonempty participants in one link run. */
      struct LinkRun
      {
        std::size_t first;
        std::size_t last;
        LinkTarget target;
      };
      /** Finds the maximal run of equal, non-none targets containing segmentIndex.
          Bridges empty segments regardless of target or style; refused bodies
          participate. Out of range, empty body or none target leaves out untouched. */
      bool queryLinkRun(std::size_t segmentIndex, LinkRun &out) const;

      /** Constructs a valid, empty value without allocating. */
      AttributedString();
      bool valid() const;
      /** Refused text buffers are conservatively considered nonempty. */
      bool empty() const;
      std::size_t segmentCount() const;
      /** Borrows a segment for this value's lifetime; index must be in range. */
      const Segment &segment(std::size_t index) const;
      /** Rail-free deterministic estimate for future off-window seats, never
          layoutProjected's rail truth and not intended for the keystroke path.
          Uses Null's synthetic advance ceil(size/3), line height size, and
          BlockStyle wrapping/truncation. Nonpositive width is unbounded.
          On success writes an origin-(0,0) extent; on invalid input or allocation
          refusal returns false and leaves out unchanged. No state is retained. */
      bool estimateExtent(short availableWidth, const BlockStyle &block, core::Frame &out) const;
      bool equals(const AttributedString &other) const;
      bool operator==(const AttributedString &other) const;
      bool operator!=(const AttributedString &other) const;
      /**
       * Lexicographic styled UTF-8 ordering, independent of segmentation.
       * Equal body bytes compare style then link at either segment start.
       * Targets order empty, refused (by handle identity), then readable bytes.
       * Invalid values precede valid ones. A refused buffer is an opaque token
       * before readable bytes, ordered by segment identity among refusals.
       * Distinct refused segments never compare equal; shared storage does.
       */
      int compare(const AttributedString &other) const;

    private:
      struct Storage;
      core::Managed<Storage> storage_;
      bool valid_;

      static AttributedString Allocate(std::size_t count);
      AttributedString fold(const TextStyle &defaults, const TextStyle &overrides) const;
      friend AttributedString Linked(const core::String &, const TextStyle &, const LinkTarget &);
      friend AttributedString operator+(const AttributedString &, const AttributedString &);
      friend AttributedString operator+(const TextStyle &, const AttributedString &);
      friend AttributedString operator+(const AttributedString &, const TextStyle &);
    };

    /**
     * Builds one immutable line with growing, privately owned segment storage.
     * A sufficient capacity hint needs one array and one control block.
     * Initial allocation refusal leaves an invalid (unusable) builder.
     */
    class AttributedString::Builder
    {
    public:
      explicit Builder(std::size_t capacityHint);
      /** Appends a shared String handle and copied style. Allocation refusal
          preserves the successful prefix; a consumed builder also refuses. */
      bool append(const core::String &text, const TextStyle &style);
      /** Appends a target without changing style; same refusal contract as append. */
      bool appendLink(const core::String &text, const TextStyle &style, const LinkTarget &target);
      /** Transfers the prefix, including a valid empty prefix, and consumes
          this builder. An invalid/consumed builder returns an invalid value. */
      AttributedString build();

    private:
      core::Managed<Storage> storage_;
      std::size_t used_;
      Builder(const Builder &);
      Builder &operator=(const Builder &);
    };

    /**
     * Shares the String's existing handle and copies its style. Refusal covers
     * only the segment array and its Managed control block; construction of the
     * supplied String (including the literal overload) keeps String's contract.
     */
    AttributedString Styled(const core::String &text, const TextStyle &style);
    /** Literal convenience form with the same storage-refusal contract. */
    AttributedString Styled(const char *text, const TextStyle &style);
    /** One linked segment; the same storage-refusal contract as Styled. */
    AttributedString Linked(const core::String &text, const TextStyle &style, const LinkTarget &target);
    /** Literal convenience form with the same storage-refusal contract. */
    AttributedString Linked(const char *text, const TextStyle &style, const LinkTarget &target);
    /** Concatenates segments atomically; an invalid operand propagates refusal. */
    AttributedString operator+(const AttributedString &left, const AttributedString &right);
    /** Supplies defaults; each segment's declared style wins. */
    AttributedString operator+(const TextStyle &left, const AttributedString &right);
    /** Overrides every segment; the right style wins. */
    AttributedString operator+(const AttributedString &left, const TextStyle &right);
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_ATTRIBUTED_STRING_HPP
