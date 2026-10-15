#ifndef LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_HPP
#define LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_HPP

#include "app/scene/Node.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "core/State.hpp"
#include "core/Frame.hpp"
#include <climits>

namespace loka
{
  namespace app
  {
    struct ScrollViewTypeTag
    {
    };

    class ScrollViewNode;

    /** Stable inputs for a scrollable projection-parent scope. viewport_ is the
        rail-published fact; scrollTo_ is application intent, stored by the
        contract but deliberately not yet consumed by any rail — wiring it
        (clamping, zoom focal behavior) is its own mechanism in a later PR,
        and zoom is its first named consumer (#537). */
    struct ScrollViewProps : public scene::NodePropsBase<ScrollViewProps>
    {
      typedef ScrollViewTypeTag TypeTag;
      typedef ScrollViewNode NodeType;

      /** Visible content rect in CONTENT coordinates, not the seat rect.
          x/y are the scroll origin. Rails publish only origin in this phase:
          width/height are carried through unchanged; !hasSize() means not
          reported yet. Initialise apps with Frame(0, 0, 0, 0); a nonzero
          origin publishes a clamp on first layout, as the int door did.
          The whole WriteSeat retains the owner's tracked transaction door. */
      scene::WriteSeat<loka::core::Frame> viewport_;
      scene::WriteSeat<int> scrollTo_;

      ScrollViewProps()
          : viewport_(),
            scrollTo_(0)
      {
      }

      explicit ScrollViewProps(const scene::NodeState<loka::core::Frame> &viewport)
          : viewport_(viewport.writeSeat()),
            scrollTo_(0)
      {
      }

      /** Binds the rail-published visible content rect. */
      ScrollViewProps &viewport(const scene::NodeState<loka::core::Frame> &value)
      {
        this->viewport_ = value.writeSeat();
        return *this;
      }

      /** Binds application-authored scroll intent. Stored only for now: no
          rail consumes it yet — the intent pipe is wired in a later PR with
          zoom as its first consumer (#537), not as a side effect of a rail
          landing. */
      ScrollViewProps &scrollTo(const scene::WriteSeat<int> &value)
      {
        this->scrollTo_ = value;
        return *this;
      }

      ScrollViewProps &scrollTo(const scene::NodeState<int> &value)
      {
        this->scrollTo_ = value.writeSeat();
        return *this;
      }

      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
        {
          return false;
        }
        const ScrollViewProps &other = static_cast<const ScrollViewProps &>(rhs);
        loka::core::State<loka::core::Frame> *mine = this->viewport_.state();
        loka::core::State<loka::core::Frame> *theirs = other.viewport_.state();
        if (mine != theirs)
        {
          return mine < theirs;
        }
        return this->scrollTo_.state() < other.scrollTo_.state();
      }
    };

    class ScrollViewNode : public scene::NestableNode
    {
    public:
      typedef ScrollViewTypeTag TypeTag;

      explicit ScrollViewNode(const ScrollViewProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }

      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_SCROLL_VIEW;
      }

      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<ScrollViewNode>();
      }

      virtual ScrollViewNode *asScrollViewNode()
      {
        return this;
      }

      /** Requested origin, safely representable by every rail. An unbound
          seat or Frame()'s negative origin requests zero. */
      loka::core::Frame requestedOrigin() const
      {
        const loka::core::Frame current =
            this->props.viewport_.isValid() ? this->props.viewport_.state()->get() : loka::core::Frame(0, 0, 0, 0);
        return loka::core::Frame(current.x < 0 ? 0 : (current.x > SHRT_MAX ? SHRT_MAX : current.x),
                                 current.y < 0 ? 0 : (current.y > SHRT_MAX ? SHRT_MAX : current.y),
                                 current.width,
                                 current.height);
      }

      /** Publishes one changed origin through the owner's tracker, preserving
          the current size. An unbound seat has no fact to publish. */
      void publishOrigin(int x, int y)
      {
        if (!this->props.viewport_.isValid())
          return;
        const loka::core::Frame current = this->props.viewport_.state()->get();
        if (current.x != x || current.y != y)
          this->props.viewport_.set(loka::core::Frame(x, y, current.width, current.height));
      }

      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        if (this->props.viewport_.isValid())
        {
          registrar.markDirtyOnChange(this->props.viewport_.state(), scene::NODE_DIRTY_LAYOUT);
        }
      }

      ScrollViewProps props;
    };

    struct ScrollViewDefinition
        : public scene::NestableNodeDefinition<ScrollViewProps, ScrollViewNode, ScrollViewDefinition>,
          public scene::TestIdDslMixin<ScrollViewDefinition>
    {
      typedef scene::NestableNodeDefinition<ScrollViewProps, ScrollViewNode, ScrollViewDefinition> BaseType;
      using BaseType::operator<<;

      ScrollViewDefinition()
          : BaseType()
      {
      }

      explicit ScrollViewDefinition(const ScrollViewProps &p)
          : BaseType(p)
      {
      }

      explicit ScrollViewDefinition(const scene::NodeState<loka::core::Frame> &viewport)
          : BaseType(ScrollViewProps(viewport))
      {
      }

      ScrollViewDefinition(const ScrollViewDefinition &other)
          : BaseType(other)
      {
      }

      ScrollViewDefinition &viewport(const scene::NodeState<loka::core::Frame> &value)
      {
        this->props.viewport(value);
        return *this;
      }

      ScrollViewDefinition &scrollTo(const scene::WriteSeat<int> &value)
      {
        this->props.scrollTo(value);
        return *this;
      }

      ScrollViewDefinition &scrollTo(const scene::NodeState<int> &value)
      {
        this->props.scrollTo(value);
        return *this;
      }
    };

    typedef ScrollViewDefinition ScrollView;

    inline ScrollViewNode *createNode(const ScrollViewProps &props)
    {
      return new ScrollViewNode(props);
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_HPP
