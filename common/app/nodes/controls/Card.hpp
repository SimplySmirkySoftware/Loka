#ifndef LOKA_APP_NODES_CONTROLS_CARD_HPP
#define LOKA_APP_NODES_CONTROLS_CARD_HPP

#include "app/layout/ChildClip.hpp"
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
        units. DROP retains its v1 2r span; other extents use their minimum
        body span to keep the opposite arc beyond finite kernel reach. Call only for nonempty admitted bands.
        Cost: O(1) per band paint; no tree or ledger walk. */
    inline core::Frame CardBodyInBand(const core::Frame &band, const CardBandProps &props)
    {
      const CardStyle &style = props.style;
      const int b = style.kBorderBand;
      const detail::CardShadowTable &geometry = style.geometry();
      int left = band.x;
      int top = band.y;
      int right = band.x + band.width;
      int bottom = band.y + band.height;
      switch (props.role)
      {
      case CARD_BAND_TOP:
        left += geometry.allowance[0];
        top += geometry.allowance[1];
        right += b;
        bottom = top + geometry.virtualHeight;
        break;
      case CARD_BAND_RIGHT:
        top += geometry.allowance[1];
        right -= geometry.allowance[2];
        bottom += b;
        left = right - geometry.virtualWidth;
        break;
      case CARD_BAND_BOTTOM:
        left -= b;
        right -= geometry.allowance[2];
        bottom -= geometry.allowance[3];
        top = bottom - geometry.virtualHeight;
        break;
      case CARD_BAND_LEFT:
        left += geometry.allowance[0];
        top -= b;
        bottom -= geometry.allowance[3];
        right = left + geometry.virtualWidth;
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
      layout::ChildClip childClip_;
      explicit CardProps(const CardStyle &look = CardStyle())
          : style(look),
            childClip_(layout::CHILD_CLIP_NONE)
      {
      }
      int hash() const
      {
        return this->style.hash() + this->childClip_;
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const CardProps &other = static_cast<const CardProps &>(rhs);
        if (this->style < other.style)
          return true;
        if (other.style < this->style)
          return false;
        return this->childClip_ < other.childClip_;
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
        const detail::CardShadowTable &g = this->props.style.geometry();
        return layout::FrameGeometry(layout::FRAME_PLACEMENT_CARD_BANDS,
                                     layout::FrameInsets(g.allowance[0] + CardStyle::kBorderBand,
                                                         g.allowance[1] + CardStyle::kBorderBand,
                                                         g.allowance[2] + CardStyle::kBorderBand,
                                                         g.allowance[3] + CardStyle::kBorderBand),
                                     g.allowance[0] + g.allowance[2] + g.minimumWidth,
                                     g.allowance[1] + g.allowance[3] + g.minimumHeight);
      }
    };

    namespace scene
    {
      template <> struct NodePropsApplier<CardNode, CardProps>
      {
        static bool apply(CardNode *node, const CardProps &props)
        {
          return layout::ApplyPropsKeepingChildClip(node, props);
        }
      };
    } // namespace scene

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
      /** Clips children at this card's own rect. With a positive width offer,
          reports no more than that width; a nonpositive offer keeps its natural width.
          Fixed when the card is created; switch with Match / Show arms or Keyed.
          Toolbox, macOS and Win32 clip. */
      CardDefinition &clipChildren()
      {
        this->props.childClip_ = layout::CHILD_CLIP_OWN_RECT;
        return *this;
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
