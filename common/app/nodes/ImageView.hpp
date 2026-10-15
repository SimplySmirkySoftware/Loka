#ifndef LOKA_APP2_IMAGE_VIEW_HPP
#define LOKA_APP2_IMAGE_VIEW_HPP

#include "app/scene/Node.hpp"
#include "core/resource/Image.hpp"
#include "core/State.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    enum ImageFit
    {
      IMAGE_FIT_NONE = 0,
      IMAGE_FIT_CONTAIN = 1,
      IMAGE_FIT_COVER = 2,
      IMAGE_FIT_STRETCH = 3
    };

    enum ImageViewSizePolicy
    {
      IMAGE_VIEW_SIZE_AUTO = 0,
      IMAGE_VIEW_SIZE_FILL_PARENT = 1,
      IMAGE_VIEW_SIZE_INTRINSIC = 2
    };

    struct ImageViewAttr
    {
      ImageViewAttr()
          : hasFitValue_(false),
            fitValue_(IMAGE_FIT_NONE),
            hasSizePolicyValue_(false),
            sizePolicyValue_(IMAGE_VIEW_SIZE_AUTO)
      {
      }

      ImageViewAttr &fit(ImageFit value)
      {
        this->hasFitValue_ = true;
        this->fitValue_ = value;
        return *this;
      }

      ImageViewAttr &sizePolicy(ImageViewSizePolicy value)
      {
        this->hasSizePolicyValue_ = true;
        this->sizePolicyValue_ = value;
        return *this;
      }

      bool operator==(const ImageViewAttr &other) const
      {
        return this->hasFitValue_ == other.hasFitValue_ && this->fitValue_ == other.fitValue_
               && this->hasSizePolicyValue_ == other.hasSizePolicyValue_
               && this->sizePolicyValue_ == other.sizePolicyValue_;
      }

      bool operator<(const ImageViewAttr &other) const
      {
        if (this->hasFitValue_ != other.hasFitValue_)
          return this->hasFitValue_ < other.hasFitValue_;
        if (this->fitValue_ != other.fitValue_)
          return this->fitValue_ < other.fitValue_;
        if (this->hasSizePolicyValue_ != other.hasSizePolicyValue_)
          return this->hasSizePolicyValue_ < other.hasSizePolicyValue_;
        return this->sizePolicyValue_ < other.sizePolicyValue_;
      }

      bool hasFitValue_;
      ImageFit fitValue_;
      bool hasSizePolicyValue_;
      ImageViewSizePolicy sizePolicyValue_;
    };

    // Backward-compatible alias. Keep until all call sites migrate.
    typedef ImageViewAttr ImageAttr;

    struct ImageViewTypeTag
    {
    };

    class ImageViewNode;
    struct ImageViewDefinitionWithAttr;

    struct ImageViewProps : public scene::NodePropsBase<ImageViewProps>
    {
      typedef ImageViewTypeTag TypeTag;
      typedef ImageViewNode NodeType;

      loka::core::State<loka::core::resource::Image> *image_;
      int width_;
      int height_;
      ImageViewAttr attr_;
      bool hasAttr_;

      ImageViewProps()
          : image_(0),
            width_(0),
            height_(0),
            attr_(),
            hasAttr_(false)
      {
      }

      ImageViewProps &image(loka::core::State<loka::core::resource::Image> *state)
      {
        this->image_ = state;
        return *this;
      }

      ImageViewProps &size(int width, int height)
      {
        this->width_ = width;
        this->height_ = height;
        return *this;
      }

      ImageViewProps &attr(const ImageViewAttr &value)
      {
        assert(!this->hasAttr_ && "ImageView.attr() can only be set once per node");
        this->attr_ = value;
        this->hasAttr_ = true;
        return *this;
      }

      void assertInitialized() const
      {
        assert(this->image_);
      }

      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        const ImageViewProps &other = static_cast<const ImageViewProps &>(rhs);
        if (image_ != other.image_)
          return image_ < other.image_;
        if (width_ != other.width_)
          return width_ < other.width_;
        if (height_ != other.height_)
          return height_ < other.height_;
        if (hasAttr_ != other.hasAttr_)
          return hasAttr_ < other.hasAttr_;
        return attr_ < other.attr_;
      }
    };

    /** Answers the undeclared INTRINSIC source width in pixels; refusal leaves width untouched. */
    inline bool queryIntrinsicImageWidth(const ImageViewProps &props, int &width)
    {
      if (props.width_ > 0 || !props.hasAttr_ || !props.attr_.hasSizePolicyValue_
          || props.attr_.sizePolicyValue_ != IMAGE_VIEW_SIZE_INTRINSIC || !props.image_)
        return false;
      const loka::core::resource::Image image = props.image_->get();
      if (!image.isValid() || image.width() <= 0)
        return false;
      width = image.width();
      return true;
    }

    class ImageViewNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef ImageViewTypeTag TypeTag;
      ImageViewProps props;
      ImageViewNode(const ImageViewProps &p)
          : props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_IMAGE_VIEW;
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<ImageViewNode>();
      }
      virtual ImageViewNode *asImageViewNode()
      {
        return this;
      }
      virtual short layoutProjected(scene::IPlatformController *controller, scene::LayoutState &state)
      {
        if (!controller)
        {
          return state.y;
        }
        if (!scene::PrepareProjectedLayout(controller, this, state))
        {
          return state.y;
        }
        return scene::Node::layout(controller, state);
      }
      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        if (this->props.image_)
        {
          registrar.markDirtyOnChange(this->props.image_, scene::NODE_DIRTY_PROPS);
        }
      }
    };

    struct ImageViewDefinition : public scene::NodeDefinition<ImageViewProps, ImageViewNode>,
                                 public scene::TestIdDslMixin<ImageViewDefinition>
    {
      ImageViewDefinition()
          : loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>()
      {
      }
      ImageViewDefinition(const ImageViewProps &p)
          : loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>(p)
      {
      }
      ImageViewDefinition &image(loka::core::State<loka::core::resource::Image> *state)
      {
        this->props.image(state);
        return *this;
      }
      ImageViewDefinition &size(int width, int height)
      {
        this->props.size(width, height);
        return *this;
      }
      ImageViewDefinitionWithAttr attr(const ImageViewAttr &value) const;

      using loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>::create;
    };

    struct ImageViewDefinitionWithAttr : public scene::NodeDefinition<ImageViewProps, ImageViewNode>,
                                         public scene::TestIdDslMixin<ImageViewDefinitionWithAttr>
    {
      ImageViewDefinitionWithAttr()
          : loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>()
      {
      }
      ImageViewDefinitionWithAttr(const ImageViewProps &p)
          : loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>(p)
      {
      }
      ImageViewDefinitionWithAttr(const ImageViewDefinition &def)
          : loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>(def.props)
      {
        this->copyTestIdPolicyFrom(def);
      }

      ImageViewDefinitionWithAttr &image(loka::core::State<loka::core::resource::Image> *state)
      {
        this->props.image(state);
        return *this;
      }

      ImageViewDefinitionWithAttr &size(int width, int height)
      {
        this->props.size(width, height);
        return *this;
      }
      using loka::app::scene::NodeDefinition<ImageViewProps, ImageViewNode>::create;
    };

    inline ImageViewDefinitionWithAttr ImageViewDefinition::attr(const ImageViewAttr &value) const
    {
      ImageViewProps p = this->props;
      p.attr(value);
      ImageViewDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(*this);
      return result;
    }

    typedef ImageViewDefinition ImageView;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_IMAGE_VIEW_HPP
