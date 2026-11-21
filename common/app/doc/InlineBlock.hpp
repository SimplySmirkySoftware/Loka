#ifndef LOKA_APP_DOC_INLINE_BLOCK_HPP
#define LOKA_APP_DOC_INLINE_BLOCK_HPP
#include "app/doc/Lowering.hpp"
namespace loka
{
  namespace app
  {
    namespace doc
    {
      /** Complete semantic block input; no live content source.
          Cost: construction copies shared values; literal content allocates a segment. */
      template <class Props> struct InlineBlockProps : scene::NodePropsBase<Props>
      {
        struct TypeTag
        {
        };
        AttributedString content;
        scene::NodeState<LinkTarget> activatedLink_;

      protected:
        /** Cost: default construction initializes one empty input. */
        InlineBlockProps() {}
        /** Cost: authoring copies one shared content value. */
        InlineBlockProps(const AttributedString &content)
            : content(content)
        {
        }
        /** Cost: authoring constructs one string and one segment. */
        InlineBlockProps(const char *content)
            : content(Styled(content, TextStyle()))
        {
        }

      public:
        /** Value order for retained reconciliation. Cost: walks this pair's content only. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const Props &other = static_cast<const Props &>(rhs);
          const int order = this->content.compareSegmentValues(other.content);
          if (order)
            return order < 0;
          return std::less<core::State<LinkTarget> *>()(this->activatedLink_.state(), other.activatedLink_.state());
        }
      };
      /** Shared leaf lowering for composition and retained application.
          Cost: once per materialization attempt or apply with a destination; copies shared content.
          Refusal remains an invalid child value; no eager body read or retry. */
      template <class Props> inline AttributedTextDefinitionWithAttr Lower(const InlineBlockProps<Props> &props)
      {
        return AttributedText(props.content).activatedLink(props.activatedLink_) + BlockStyle().wrap(TEXT_WRAP_WORD);
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
