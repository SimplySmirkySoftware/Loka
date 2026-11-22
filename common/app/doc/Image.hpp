#ifndef LOKA_APP_DOC_IMAGE_HPP
#define LOKA_APP_DOC_IMAGE_HPP

#include "app/doc/BlockReader.hpp"
#include "app/doc/Lowering.hpp"
#include "app/style/AttributedTextRead.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      class ImageNode;

      /** An authored image source and its alternative text. */
      struct ImageProps : scene::NodePropsBase<ImageProps>
      {
        typedef ImageNode NodeType;
        struct TypeTag
        {
        };
        core::String source;
        core::String alt;

        ImageProps() {}
        ImageProps(const core::String &source, const core::String &alt)
            : source(source),
              alt(alt)
        {
        }
        /** Cost: retained reconciliation compares the sources, then the labels on a tie. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const ImageProps &other = static_cast<const ImageProps &>(rhs);
          const int order = style_internal::CompareLinkTargets(LinkTarget(this->source), LinkTarget(other.source));
          return order ? order < 0
                       : style_internal::CompareLinkTargets(LinkTarget(this->alt), LinkTarget(other.alt)) < 0;
        }
      };

      /** Cost: one styled segment per composition or retained projection. */
      inline AttributedTextDefinitionWithAttr Lower(const ImageProps &props)
      {
        return AttributedText(Styled(props.alt, Italic)) + BlockStyle().wrap(TEXT_WRAP_WORD);
      }

      /** An image projects its alternative text into one wrapped text child. */
      class ImageNode : public BlockNode<ImageProps>
      {
      public:
        explicit ImageNode(const ImageProps &props)
            : BlockNode<ImageProps>(props)
        {
        }

      protected:
        virtual void composeChildren(scene::NodeComposition &composition)
        {
          composition.declare(Lower(this->props));
        }
      };
    } // namespace doc
    namespace scene
    {
      /** Cost: each retained apply updates one owned text child and stores the authored props. */
      template <> struct NodePropsApplier<doc::ImageNode, doc::ImageProps>
      {
        static bool apply(doc::ImageNode *node, const doc::ImageProps &props)
        {
          if (AttributedTextNode *leaf = doc::detail::ProjectionLeaf(node->childrenHead()))
            doc::Lower(props).applyPropsToNode(leaf);
          node->props = props;
          return true;
        }
      };
    } // namespace scene
    namespace doc
    {
      typedef scene::NodeDefinition<ImageProps, ImageNode> ImageDefinition;

      /** Authors a block image. Pass empty alternative text for a decorative image. */
      inline ImageDefinition Image(const core::String &source, const core::String &alt)
      {
        return scene::Component(ImageProps(source, alt));
      }

      /** Cost: constructs two strings per literal declaration. */
      inline ImageDefinition Image(const char *source, const char *alt)
      {
        return Image(core::String(source), core::String(alt));
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
