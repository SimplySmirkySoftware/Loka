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

      /** A pair of authored dimensions in CSS pixels. */
      class ImageSize
      {
        unsigned width_;
        unsigned height_;

      public:
        ImageSize(unsigned width, unsigned height)
            : width_(width),
              height_(height)
        {
        }
        unsigned width() const
        {
          return this->width_;
        }
        unsigned height() const
        {
          return this->height_;
        }
        bool operator<(const ImageSize &other) const
        {
          return this->width_ == other.width_ ? this->height_ < other.height_ : this->width_ < other.width_;
        }
      };

      namespace detail
      {
        /** Owns the optional pair, including authored dimensions an export may refuse. */
        class AuthoredImageSize
        {
          ImageSize size_;
          bool present_;

        public:
          AuthoredImageSize()
              : size_(0, 0),
                present_(false)
          {
          }
          explicit AuthoredImageSize(const ImageSize &size)
              : size_(size),
                present_(true)
          {
          }
          const ImageSize *get() const
          {
            return this->present_ ? &this->size_ : 0;
          }
          bool operator<(const AuthoredImageSize &other) const
          {
            if (this->present_ != other.present_)
              return !this->present_;
            return this->present_ && this->size_ < other.size_;
          }
        };
      } // namespace detail

      /** An authored image source and its alternative text. */
      struct ImageProps : scene::NodePropsBase<ImageProps>
      {
        typedef ImageNode NodeType;
        struct TypeTag
        {
        };
        core::String source;
        core::String alt;
        detail::AuthoredImageSize size;

        ImageProps() {}
        ImageProps(const core::String &source, const core::String &alt)
            : source(source),
              alt(alt)
        {
        }
        ImageProps(const core::String &source, const core::String &alt, const ImageSize &size)
            : source(source),
              alt(alt),
              size(size)
        {
        }
        /** Cost: retained reconciliation compares source, label and dimensions until a difference. */
        bool operator<(const scene::PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
            return false;
          const ImageProps &other = static_cast<const ImageProps &>(rhs);
          const int order = style_internal::CompareLinkTargets(LinkTarget(this->source), LinkTarget(other.source));
          if (order)
            return order < 0;
          const int label = style_internal::CompareLinkTargets(LinkTarget(this->alt), LinkTarget(other.alt));
          return label ? label < 0 : this->size < other.size;
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
      /** Authors a block image with both export dimensions. */
      inline ImageDefinition Image(const core::String &source, const core::String &alt, const ImageSize &size)
      {
        return scene::Component(ImageProps(source, alt, size));
      }
      inline ImageDefinition Image(const char *source, const char *alt, const ImageSize &size)
      {
        return Image(core::String(source), core::String(alt), size);
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
