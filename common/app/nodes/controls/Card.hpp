#ifndef LOKA_APP_NODES_CONTROLS_CARD_HPP
#define LOKA_APP_NODES_CONTROLS_CARD_HPP

#include "app/nodes/nestable/RowColumn.hpp"
#include "app/style/CardStyle.hpp"
#include <vector>
#include "core/Frame.hpp"

namespace loka
{
  namespace app
  {
    /** Clockwise ring pieces; each leaf owns a copy of the complete style. */
    enum CardBandRole
    {
      CARD_BAND_TOP,
      CARD_BAND_LEFT,
      CARD_BAND_RIGHT,
      CARD_BAND_BOTTOM,
      CARD_BAND_COUNT
    };
    struct CardBandTypeTag
    {
    };
    struct CardTypeTag
    {
    };
    class CardNode;

    /** Closed projected-leaf input, independent of its parent and siblings. */
    struct CardBandProps : public scene::NodePropsBase<CardBandProps>
    {
      typedef CardBandTypeTag TypeTag;
      typedef CardBandNode NodeType;
      CardBandRole role;
      CardStyle style;
      explicit CardBandProps(CardBandRole value = CARD_BAND_TOP, const CardStyle &look = CardStyle())
          : role(value),
            style(look)
      {
      }
      int hash() const
      {
        return this->role + 31 * this->style.hash();
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const CardBandProps &other = static_cast<const CardBandProps &>(rhs);
        return this->role < other.role || (this->role == other.role && this->style < other.style);
      }
    };

    /** Recover three real body edges and one virtual opposite edge in layout
        units. The virtual span is 2r: its opposite arc cannot reach this
        band's inset-wide sample. Call only for nonempty admitted bands.
        Cost: O(1) per band paint; no tree or ledger walk. */
    inline core::Frame CardBodyInBand(const core::Frame &band, const CardBandProps &props)
    {
      const CardStyle &style = props.style;
      const int b = style.kBorderBand;
      const int diameter = 2 * style.kCornerRadius;
      int left = band.x;
      int top = band.y;
      int right = band.x + band.width;
      int bottom = band.y + band.height;
      switch (props.role)
      {
      case CARD_BAND_TOP:
        left += style.kShadowLeft;
        top += style.kShadowTop;
        right += b;
        bottom = top + diameter;
        break;
      case CARD_BAND_RIGHT:
        top += style.kShadowTop;
        right -= style.kShadowRight;
        bottom += b;
        left = right - diameter;
        break;
      case CARD_BAND_BOTTOM:
        left -= b;
        right -= style.kShadowRight;
        bottom -= style.kShadowBottom;
        top = bottom - diameter;
        break;
      case CARD_BAND_LEFT:
        left += style.kShadowLeft;
        top -= b;
        bottom -= style.kShadowBottom;
        right = left + diameter;
        break;
      case CARD_BAND_COUNT:
        assert(false && "CardBand requires a role");
        return core::Frame(0, 0, 0, 0);
      }
      return core::Frame(left, top, right - left, bottom - top);
    }

    /** A projected strip; native rails explicitly refuse it until their painters land. */
    class CardBandNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef CardBandTypeTag TypeTag;
      CardBandProps props;
      explicit CardBandNode(const CardBandProps &p)
          : props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_CARD_BAND;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<CardBandNode>();
      }
      virtual CardBandNode *asCardBandNode()
      {
        return this;
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual short layoutProjected(scene::IPlatformController *controller, scene::LayoutState &state)
      {
        if (!controller || !scene::PrepareProjectedLayout(controller, this, state))
          return state.y;
        return scene::Node::layout(controller, state);
      }
    };

    /** Complete card input; style participates in identity and retained application. */
    struct CardProps : public scene::NodePropsBase<CardProps>
    {
      typedef CardTypeTag TypeTag;
      typedef CardNode NodeType;
      CardStyle style;
      explicit CardProps(const CardStyle &look = CardStyle())
          : style(look)
      {
      }
      int hash() const
      {
        return this->style.hash();
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        return rhs.propsTypeId() == this->propsTypeId() && this->style < static_cast<const CardProps &>(rhs).style;
      }
    };

    /** Closed frame: one content column followed by four transparent bands. */
    class CardNode : public scene::NestableNode, public scene::IFramedNode
    {
    public:
      typedef CardTypeTag TypeTag;
      CardProps props;
      explicit CardNode(const CardProps &p)
          : props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_CARD;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<CardNode>();
      }
      virtual scene::IFramedNode *asFramedNode()
      {
        return this;
      }
      virtual layout::FrameGeometry frameGeometry() const
      {
        return layout::FrameGeometry(
            layout::FRAME_PLACEMENT_CARD_BANDS,
            layout::FrameInsets(CardStyle::kShadowLeft + CardStyle::kBorderBand,
                                CardStyle::kShadowTop + CardStyle::kBorderBand,
                                CardStyle::kShadowRight + CardStyle::kBorderBand,
                                CardStyle::kShadowBottom + CardStyle::kBorderBand),
            CardStyle::kShadowLeft + CardStyle::kShadowRight
                + CardStyle::minimumBodyExtent(CardStyle::kShadowLeft, CardStyle::kShadowRight),
            CardStyle::kShadowTop + CardStyle::kShadowBottom
                + CardStyle::minimumBodyExtent(CardStyle::kShadowTop, CardStyle::kShadowBottom));
      }
    };

    /** App-facing closed container. Appended definitions belong to its content column. */
    struct CardDefinition : public scene::NestableNodeDefinition<CardProps, CardNode, CardDefinition>,
                            public scene::TestIdDslMixin<CardDefinition>
    {
      typedef scene::NestableNodeDefinition<CardProps, CardNode, CardDefinition> BaseType;
      explicit CardDefinition(const CardStyle &style = CardStyle())
          : BaseType(CardProps(style))
      {
        BaseType::operator<<(VStack());
        if (this->childrenCount() != 1)
          return;
        for (int role = 0; role < CARD_BAND_COUNT; ++role)
        {
          BaseType::operator<<(BandDefinition(static_cast<CardBandRole>(role), style));
          if (this->childrenCount() != static_cast<size_t>(role + 2))
          {
            this->clearChildren();
            return;
          }
        }
      }
      virtual scene::NodeDefinitionBase *clone() const
      {
        if (this->childrenCount() != 1 + CARD_BAND_COUNT)
          return 0;
        CardDefinition *copy = new CardDefinition(EMPTY_SHELL);
        if (!copy)
          return 0;
        if (!copy->assignFromDefinition(*this))
        {
          delete copy;
          return 0;
        }
        return copy;
      }
      // Every insertion form a nestable definition accepts goes to the content
      // column; declaring these here hides the inherited forms, which would append
      // to the card itself.
      CardDefinition &operator<<(scene::NodeDefinitionBase &content)
      {
        if (scene::INestableDefinition *column = this->contentColumn())
          *column << content;
        return *this;
      }
      CardDefinition &operator<<(const scene::NodeDefinitionBase &content)
      {
        if (scene::INestableDefinition *column = this->contentColumn())
          *column << content;
        return *this;
      }
      /** Takes ownership like every nestable definition; a refused card shell deletes it. */
      CardDefinition &operator<<(scene::NodeDefinitionBase *ownedContent)
      {
        if (scene::INestableDefinition *column = this->contentColumn())
          *column << ownedContent;
        else
          delete ownedContent;
        return *this;
      }
      CardDefinition &operator<<(const std::vector<scene::NodeDefinitionBase *> &contents)
      {
        if (scene::INestableDefinition *column = this->contentColumn())
          *column << contents;
        return *this;
      }
      template <class ItemT, class FactoryT, class KeyExprT>
      CardDefinition &operator<<(const ::loka::app::ForBuilder<ItemT, FactoryT, KeyExprT> &builder)
      {
        if (scene::INestableDefinition *column = this->contentColumn())
          builder.appendTo(*column);
        return *this;
      }

    private:
      scene::INestableDefinition *contentColumn()
      {
        scene::NodeDefinitionBase *column = this->childrenHead();
        return column ? column->asNestableDefinition() : 0;
      }
      /** Cloning fills the shell from completed source children, without re-expansion. */
      enum EmptyShell
      {
        EMPTY_SHELL
      };
      explicit CardDefinition(EmptyShell)
          : BaseType()
      {
      }
      struct BandDefinition : public scene::NodeDefinition<CardBandProps, CardBandNode>
      {
        BandDefinition(CardBandRole role, const CardStyle &style)
            : scene::NodeDefinition<CardBandProps, CardBandNode>(CardBandProps(role, style))
        {
        }
      };
    };
    typedef CardDefinition Card;
  } // namespace app
} // namespace loka
#endif
