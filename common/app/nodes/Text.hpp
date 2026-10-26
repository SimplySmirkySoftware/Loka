#ifndef LOKA_APP2_TEXT_HPP
#define LOKA_APP2_TEXT_HPP

#include "core/State.hpp"
#include "app/scene/Node.hpp"
#include "app/style/Style.hpp"
#include "core/String.hpp"

namespace loka
{
  namespace app
  {
    struct TextTypeTag
    {
    };

    class TextNode;
    struct TextDefinitionWithAttr;

    struct TextProps : public scene::NodePropsBase<TextProps>
    {
      typedef TextTypeTag TypeTag;
      typedef TextNode NodeType;
      loka::core::State<loka::core::String> *text_;
      loka::core::MutableState<loka::core::String> ownedText;
      TextStyle textStyle_;
      BlockStyle blockStyle_;
      loka::core::State<TextStyle> *textStyleState_;
      TextProps()
          : text_(0),
            ownedText(),
            textStyle_(),
            blockStyle_(),
            textStyleState_(0)
      {
      }
      TextProps(loka::core::State<loka::core::String> *state)
          : text_(state),
            ownedText(),
            textStyle_(),
            blockStyle_(),
            textStyleState_(0)
      {
      }
      TextProps(const loka::core::String &value)
          : text_(0),
            ownedText(value),
            textStyle_(),
            blockStyle_(),
            textStyleState_(0)
      {
        text_ = &ownedText;
      }
      TextProps(const char *value)
          : text_(0),
            ownedText(loka::core::String::Literal(value)),
            textStyle_(),
            blockStyle_(),
            textStyleState_(0)
      {
        text_ = &ownedText;
      }
      TextProps(const TextProps &other)
          : scene::NodePropsBase<TextProps>(other),
            text_(other.text_),
            ownedText(other.ownedText),
            textStyle_(other.textStyle_),
            blockStyle_(other.blockStyle_),
            textStyleState_(other.textStyleState_)
      {
        if (other.ownsText())
        {
          text_ = &ownedText;
        }
      }
      TextProps &operator=(const TextProps &other)
      {
        if (this != &other)
        {
          text_ = other.text_;
          ownedText = other.ownedText;
          textStyle_ = other.textStyle_;
          blockStyle_ = other.blockStyle_;
          textStyleState_ = other.textStyleState_;
          if (other.ownsText())
          {
            text_ = &ownedText;
          }
        }
        return *this;
      }
      TextProps &text(loka::core::State<loka::core::String> *state)
      {
        this->text_ = state;
        return *this;
      }
      TextProps &text(const loka::core::String &value)
      {
        ownedText = loka::core::MutableState<loka::core::String>(value);
        this->text_ = &ownedText;
        return *this;
      }
      TextProps &text(const char *value)
      {
        return text(loka::core::String::Literal(value));
      }

      /** Whether text points at this props value's own constant storage. */
      bool ownsText() const
      {
        return this->text_ == &this->ownedText;
      }

      /** True when a field changing font or block geometry was declared, or a
          style State is bound. Color is applied by each rail from color_, where
          UNSET means the component's native default. */
      bool hasDeclaredLayoutStyle() const
      {
        return this->textStyleState_ != 0 || this->textStyle_.declaresFont() || this->blockStyle_.hasWrap_
               || this->blockStyle_.hasTruncation_ || this->blockStyle_.hasAlign_;
      }
      TextStyle resolvedTextStyle() const
      {
        return this->textStyleState_ ? this->textStyle_ + this->textStyleState_->get() : this->textStyle_;
      }

      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        const TextProps &other = static_cast<const TextProps &>(rhs);
        if (this->ownsText() != other.ownsText())
          return this->ownsText() < other.ownsText();
        if (this->ownsText())
        {
          const int textCompare =
              ownedText.get().compare(other.ownedText.get());
          if (textCompare != 0)
            return textCompare < 0;
        }
        else if (text_ != other.text_)
          return text_ < other.text_;
        if (!(textStyle_ == other.textStyle_))
          return textStyle_ < other.textStyle_;
        if (!(blockStyle_ == other.blockStyle_))
          return blockStyle_ < other.blockStyle_;
        return textStyleState_ < other.textStyleState_;
      }
    };

    class TextNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef TextTypeTag TypeTag;
      TextProps props;
      TextNode(const TextProps &p)
          : props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_TEXT;
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<TextNode>();
      }
      virtual TextNode *asTextNode()
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
        if (this->props.text_ && !this->props.ownsText())
        {
          scene::NodeDirtyFlags textFlags = scene::NODE_DIRTY_PROPS;
          if (this->props.blockStyle_.hasWrap_ && this->props.blockStyle_.wrap_ != TEXT_WRAP_NONE)
          {
            textFlags = static_cast<scene::NodeDirtyFlags>(textFlags | scene::NODE_DIRTY_LAYOUT);
          }
          registrar.markDirtyOnChange(this->props.text_, textFlags);
        }
        if (this->props.textStyleState_)
        {
          registrar.markDirtyOnChange(this->props.textStyleState_, scene::NODE_DIRTY_LAYOUT);
        }
      }
    };

    struct TextDefinition : public scene::NodeDefinition<TextProps, TextNode>,
                            public scene::TestIdDslMixin<TextDefinition>
    {
      TextDefinition()
          : loka::app::scene::NodeDefinition<TextProps, TextNode>()
      {
      }
      TextDefinition(const TextProps &p)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(p)
      {
      }
      TextDefinition(const char *value)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(TextProps(value))
      {
      }
      TextDefinition(const loka::core::String &value)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(TextProps(value))
      {
      }
      TextDefinition(loka::core::State<loka::core::String> *state)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(TextProps(state))
      {
      }
    };

    struct TextDefinitionWithAttr : public scene::NodeDefinition<TextProps, TextNode>,
                                    public scene::TestIdDslMixin<TextDefinitionWithAttr>
    {
      TextDefinitionWithAttr()
          : loka::app::scene::NodeDefinition<TextProps, TextNode>()
      {
      }
      TextDefinitionWithAttr(const TextProps &p)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(p)
      {
      }
      TextDefinitionWithAttr(const TextDefinition &def)
          : loka::app::scene::NodeDefinition<TextProps, TextNode>(def.props)
      {
        this->copyTestIdPolicyFrom(def);
      }
    };

    inline TextDefinitionWithAttr operator+(const TextDefinition &definition, const TextStyle &style)
    {
      TextProps p = definition.props;
      p.textStyle_ = p.textStyle_ + style;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline TextDefinitionWithAttr operator+(const TextDefinition &definition, const BlockStyle &style)
    {
      TextProps p = definition.props;
      p.blockStyle_ = p.blockStyle_ + style;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline TextDefinitionWithAttr operator+(const TextDefinition &definition, loka::core::State<TextStyle> *state)
    {
      TextProps p = definition.props;
      p.textStyleState_ = state;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline TextDefinitionWithAttr operator+(const TextDefinitionWithAttr &definition, const TextStyle &style)
    {
      TextProps p = definition.props;
      p.textStyle_ = p.textStyle_ + style;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline TextDefinitionWithAttr operator+(const TextDefinitionWithAttr &definition, const BlockStyle &style)
    {
      TextProps p = definition.props;
      p.blockStyle_ = p.blockStyle_ + style;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline TextDefinitionWithAttr operator+(const TextDefinitionWithAttr &definition,
                                             loka::core::State<TextStyle> *state)
    {
      TextProps p = definition.props;
      p.textStyleState_ = state;
      TextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    typedef TextDefinition Text;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_TEXT_HPP
