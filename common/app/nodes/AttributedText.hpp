#ifndef LOKA_APP_ATTRIBUTED_TEXT_HPP
#define LOKA_APP_ATTRIBUTED_TEXT_HPP

#include "core/State.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/Node.hpp"
#include "app/style/Style.hpp"
#include "app/style/AttributedString.hpp"

namespace loka
{
  namespace app
  {
    struct AttributedTextTypeTag
    {
    };

    class AttributedTextNode;
    struct AttributedTextDefinitionWithAttr;

    /** Borrowed live content or a props-owned constant, plus block layout. */
    struct AttributedTextProps : public scene::NodePropsBase<AttributedTextProps>
    {
      typedef AttributedTextTypeTag TypeTag;
      typedef AttributedTextNode NodeType;
      loka::core::State<AttributedString> *text_;
      loka::core::MutableState<AttributedString> ownedText;
      bool ownsText;
      BlockStyle blockStyle_;
      scene::NodeState<LinkTarget> activatedLink_;
      AttributedTextProps()
          : text_(0),
            ownedText(),
            ownsText(false),
            blockStyle_()
      {
      }
      AttributedTextProps(loka::core::State<AttributedString> *state)
          : text_(state),
            ownedText(),
            ownsText(false),
            blockStyle_()
      {
      }
      AttributedTextProps(const AttributedString &value)
          : text_(0),
            ownedText(value),
            ownsText(true),
            blockStyle_()
      {
        this->text_ = &this->ownedText;
      }
      AttributedTextProps(const AttributedTextProps &other)
          : scene::NodePropsBase<AttributedTextProps>(other),
            text_(other.text_),
            ownedText(other.ownedText),
            ownsText(other.ownsText),
            blockStyle_(other.blockStyle_),
            activatedLink_(other.activatedLink_)
      {
        if (this->ownsText)
        {
          this->text_ = &this->ownedText;
        }
      }
      AttributedTextProps &operator=(const AttributedTextProps &other)
      {
        if (this != &other)
        {
          this->text_ = other.text_;
          this->ownedText = other.ownedText;
          this->ownsText = other.ownsText;
          this->blockStyle_ = other.blockStyle_;
          this->activatedLink_ = other.activatedLink_;
          if (this->ownsText)
          {
            this->text_ = &this->ownedText;
          }
        }
        return *this;
      }
      AttributedTextProps &text(loka::core::State<AttributedString> *state)
      {
        this->text_ = state;
        this->ownsText = false;
        return *this;
      }
      AttributedTextProps &text(const AttributedString &value)
      {
        this->ownedText = loka::core::MutableState<AttributedString>(value);
        this->text_ = &this->ownedText;
        this->ownsText = true;
        return *this;
      }

      /** Borrows the app-owned result box; repeated activations notify. */
      AttributedTextProps &activatedLink(const scene::NodeState<LinkTarget> &box)
      {
        this->activatedLink_ = box;
        return *this;
      }

      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const AttributedTextProps &other = static_cast<const AttributedTextProps &>(rhs);
        if (this->ownsText != other.ownsText)
          return this->ownsText < other.ownsText;
        if (this->ownsText)
        {
          const int textCompare = this->ownedText.get().compare(other.ownedText.get());
          if (textCompare != 0)
            return textCompare < 0;
        }
        else if (this->text_ != other.text_)
          return this->text_ < other.text_;
        if (!(this->blockStyle_ == other.blockStyle_))
          return this->blockStyle_ < other.blockStyle_;
        return this->activatedLink_.state() < other.activatedLink_.state();
      }
    };

    class AttributedTextNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef AttributedTextTypeTag TypeTag;
      AttributedTextProps props;
      AttributedTextNode(const AttributedTextProps &p)
          : props(p)
      {
      }
      /** Called only from an accepted rail input inside the turn's Operation.
          The caller proves the index addresses the current exact segment
          representation and eligible presentation, without intervening work.
          Returns whether the app box was written; never opens a turn. */
      bool writeActivatedLink(std::size_t segmentIndex)
      {
        if (this->lifecycleFact() != scene::NODE_FACT_ATTACHED)
          return false;
        if (!this->props.activatedLink_.state() || !this->props.text_)
          return false;
        AttributedString::LinkRun run;
        if (!this->props.text_->get().queryLinkRun(segmentIndex, run))
          return false;
        const LinkTarget target = run.target;
        scene::NodeState<LinkTarget> box = this->props.activatedLink_;
        box.set(target, true);
        return true;
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_ATTRIBUTED_TEXT;
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<AttributedTextNode>();
      }
      virtual AttributedTextNode *asAttributedTextNode()
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
        if (this->props.text_ && !this->props.ownsText)
        {
          registrar.markDirtyOnChange(
              this->props.text_,
              static_cast<scene::NodeDirtyFlags>(scene::NODE_DIRTY_PROPS | scene::NODE_DIRTY_LAYOUT));
        }
      }
    };

    struct AttributedTextDefinition : public scene::NodeDefinition<AttributedTextProps, AttributedTextNode>,
                                      public scene::TestIdDslMixin<AttributedTextDefinition>
    {
      /** Borrows the app-owned activation result box. */
      AttributedTextDefinition &activatedLink(const scene::NodeState<LinkTarget> &box)
      {
        this->props.activatedLink(box);
        return *this;
      }
      AttributedTextDefinition()
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>()
      {
      }
      AttributedTextDefinition(const AttributedTextProps &p)
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>(p)
      {
      }
      AttributedTextDefinition(const AttributedString &value)
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>(AttributedTextProps(value))
      {
      }
      AttributedTextDefinition(loka::core::State<AttributedString> *state)
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>(AttributedTextProps(state))
      {
      }
    };

    struct AttributedTextDefinitionWithAttr : public scene::NodeDefinition<AttributedTextProps, AttributedTextNode>,
                                              public scene::TestIdDslMixin<AttributedTextDefinitionWithAttr>
    {
      /** Borrows the app-owned activation result box. */
      AttributedTextDefinitionWithAttr &activatedLink(const scene::NodeState<LinkTarget> &box)
      {
        this->props.activatedLink(box);
        return *this;
      }
      AttributedTextDefinitionWithAttr()
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>()
      {
      }
      AttributedTextDefinitionWithAttr(const AttributedTextProps &p)
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>(p)
      {
      }
      AttributedTextDefinitionWithAttr(const AttributedTextDefinition &def)
          : loka::app::scene::NodeDefinition<AttributedTextProps, AttributedTextNode>(def.props)
      {
        this->copyTestIdPolicyFrom(def);
      }
    };

    inline AttributedTextDefinitionWithAttr operator+(const AttributedTextDefinition &definition,
                                                      const BlockStyle &style)
    {
      AttributedTextProps p = definition.props;
      p.blockStyle_ = p.blockStyle_ + style;
      AttributedTextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    inline AttributedTextDefinitionWithAttr operator+(const AttributedTextDefinitionWithAttr &definition,
                                                      const BlockStyle &style)
    {
      AttributedTextProps p = definition.props;
      p.blockStyle_ = p.blockStyle_ + style;
      AttributedTextDefinitionWithAttr result(p);
      result.copyTestIdPolicyFrom(definition);
      return result;
    }

    typedef AttributedTextDefinition AttributedText;
  } // namespace app
} // namespace loka

#endif // LOKA_APP_ATTRIBUTED_TEXT_HPP
