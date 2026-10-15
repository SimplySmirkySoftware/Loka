#ifndef LOKA_APP_NODES_CONTROLS_CARD_HPP
#define LOKA_APP_NODES_CONTROLS_CARD_HPP

#include "app/nodes/nestable/RowColumn.hpp"
#include "app/style/CardStyle.hpp"

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
        const int bodyMinimum = CardStyle::kCornerRadius * 2 > CardStyle::kBorderBand * 2 ? CardStyle::kCornerRadius * 2
                                                                                          : CardStyle::kBorderBand * 2;
        return layout::FrameGeometry(layout::FRAME_PLACEMENT_CARD_BANDS,
                                     layout::FrameInsets(CardStyle::kShadowLeft + CardStyle::kBorderBand,
                                                         CardStyle::kShadowTop + CardStyle::kBorderBand,
                                                         CardStyle::kShadowRight + CardStyle::kBorderBand,
                                                         CardStyle::kShadowBottom + CardStyle::kBorderBand),
                                     CardStyle::kShadowLeft + CardStyle::kShadowRight + bodyMinimum,
                                     CardStyle::kShadowTop + CardStyle::kShadowBottom + bodyMinimum);
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
      CardDefinition &operator<<(const scene::NodeDefinitionBase &content)
      {
        scene::NodeDefinitionBase *column = this->childrenHead();
        if (column && column->asNestableDefinition())
          *column->asNestableDefinition() << content;
        return *this;
      }

    private:
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
