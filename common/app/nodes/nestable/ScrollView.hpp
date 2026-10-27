#ifndef LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_HPP
#define LOKA_APP_NODES_NESTABLE_SCROLL_VIEW_HPP

#include <cassert>
#include <climits>

#include "app/scene/Node.hpp"
#include "app/nodes/nestable/ScrollViewRequests.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "core/Frame.hpp"
#include "core/State.hpp"

namespace loka
{
  namespace app
  {
    struct ScrollViewTypeTag
    {
    };

    class ScrollViewNode;

    /** Completed content insets: the extent grows by top + bottom and content is
        placed top below its start. Checked on construction like LayoutGap::of. */
    struct ScrollViewContentInsets
    {
      ScrollViewContentInsets()
          : top_(0),
            bottom_(0)
      {
      }

      /** Checks each logical inset in [0, SHRT_MAX], clamping in release.
          The default constructor and this door are the only writers. */
      static ScrollViewContentInsets of(int top, int bottom)
      {
        assert(top >= 0 && top <= SHRT_MAX);
        assert(bottom >= 0 && bottom <= SHRT_MAX);
        ScrollViewContentInsets result;
        result.top_ = static_cast<short>(top < 0 ? 0 : top > SHRT_MAX ? SHRT_MAX : top);
        result.bottom_ = static_cast<short>(bottom < 0 ? 0 : bottom > SHRT_MAX ? SHRT_MAX : bottom);
        return result;
      }

      int top() const
      {
        return this->top_;
      }
      int bottom() const
      {
        return this->bottom_;
      }

      bool operator==(const ScrollViewContentInsets &other) const
      {
        return this->top_ == other.top_ && this->bottom_ == other.bottom_;
      }
      bool operator!=(const ScrollViewContentInsets &other) const
      {
        return !(*this == other);
      }
      bool operator<(const ScrollViewContentInsets &other) const
      {
        return this->top_ != other.top_ ? this->top_ < other.top_ : this->bottom_ < other.bottom_;
      }
      /** Extra extent in int arithmetic, without narrowing the sum to short. */
      int extentExtra() const
      {
        return this->top_ + this->bottom_;
      }

    private:
      short top_;
      short bottom_;
    };

    /** Stable inputs for a scrollable projection-parent scope. viewport_ is the
        rail-published fact; scrollTo_ borrows application-owned requests. */
    struct ScrollViewProps : public scene::NodePropsBase<ScrollViewProps>
    {
      typedef ScrollViewTypeTag TypeTag;
      typedef ScrollViewNode NodeType;

      /** Visible content rect in CONTENT coordinates, not the seat rect.
          x/y are the scroll origin; width/height are the visible content size.
          hasSize() becomes true for a positive size after the first layout
          on rails that report it. Zero width and height mean not reported yet.
          Initialise apps with Frame(0, 0, 0, 0), or with topOrigin() to start
          with the top inset visible. Rails normalize the origin on layout;
          origin.y may be negative down to -top while the gap shows.
          The whole WriteSeat retains the owner's tracked transaction door. */
      scene::WriteSeat<loka::core::Frame> viewport_;
      scene::RequestBinding<ScrollTarget> scrollTo_;
      ScrollViewContentInsets contentInsets_;

      ScrollViewProps()
          : viewport_(),
            scrollTo_(),
            contentInsets_()
      {
      }

      explicit ScrollViewProps(const scene::NodeState<loka::core::Frame> &viewport)
          : viewport_(viewport.writeSeat()),
            scrollTo_(),
            contentInsets_()
      {
      }

      /** Requests the extent start with the top gap visible for every inset. */
      static loka::core::Frame topOrigin()
      {
        return loka::core::Frame(0, SHRT_MIN, 0, 0);
      }

      /** Declares the completed vertical content insets. */
      ScrollViewProps &contentInsets(const ScrollViewContentInsets &value)
      {
        this->contentInsets_ = value;
        return *this;
      }

      /** Binds the rail-published visible content rect. */
      ScrollViewProps &viewport(const scene::NodeState<loka::core::Frame> &value)
      {
        this->viewport_ = value.writeSeat();
        return *this;
      }

      /** Takes one post at its first eligible visit. A refused visit takes and
          drops it; a retained re-entry's first visit may jump. Null delivers
          requests; native rail delivery follows in subsequent PRs. */
      ScrollViewProps &scrollTo(scene::Request<ScrollTarget> &value)
      {
        this->scrollTo_ = scene::RequestBinding<ScrollTarget>(this->requestSeat(value));
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
        if (!this->scrollTo_.same(other.scrollTo_))
          return this->scrollTo_ < other.scrollTo_;
        return this->contentInsets_ < other.contentInsets_;
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

      /** Requested extent offset, safely representable by every rail. An unbound
          seat requests zero on both axes. Content y below -top reads as -top;
          clamp before adding top so even an INT_MAX request is safe. */
      loka::core::Frame requestedOrigin() const
      {
        if (!this->props.viewport_.isValid())
          return loka::core::Frame(0, 0, 0, 0);
        const loka::core::Frame current = this->props.viewport_.state()->get();
        const loka::core::Frame origin = this->normalizedOrigin(current.x, current.y);
        return loka::core::Frame(origin.x, origin.y, current.width, current.height);
      }

      /** Converts content coordinates to the representable extent origin,
          clamping before addition so even INT_MAX is safe. */
      loka::core::Frame normalizedOrigin(int x, int contentY) const
      {
        const int top = this->props.contentInsets_.top();
        const int y = contentY < -top ? -top : (contentY > SHRT_MAX - top ? SHRT_MAX - top : contentY);
        return loka::core::Frame(x < 0 ? 0 : (x > SHRT_MAX ? SHRT_MAX : x), y + top, 0, 0);
      }

      /** Shared request conversion, including the top gap destination. */
      loka::core::Frame normalizedOrigin(const ScrollTarget &target) const
      {
        return this->normalizedOrigin(target.x(), target.contentY(this->props.contentInsets_.top()));
      }

      /** Translation subtracted from content coordinates by projection rails. */
      loka::core::Frame contentTranslation() const
      {
        const loka::core::Frame origin = this->requestedOrigin();
        return loka::core::Frame(origin.x, origin.y - this->props.contentInsets_.top(), origin.width, origin.height);
      }

      /** Publishes one changed extent offset as a content origin through the
          owner's tracker, preserving size. An unbound seat has no fact. */
      void publishOrigin(int x, int extentY)
      {
        if (!this->props.viewport_.isValid())
          return;
        const loka::core::Frame current = this->props.viewport_.state()->get();
        this->writeViewport(x, extentY, current.width, current.height);
      }

      /** Publishes the layout's visible content size and extent offset as one
          content-coordinate fact. Insets do not shrink the visible size. */
      void publishViewport(int x, int extentY, int width, int height)
      {
        this->writeViewport(x, extentY, width, height);
      }

      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        if (this->props.scrollTo_.isValid())
          registrar.markDirtyOnChange(this->props.scrollTo_.state(), scene::NODE_DIRTY_LAYOUT);
        if (this->props.viewport_.isValid())
        {
          registrar.markDirtyOnChange(this->props.viewport_.state(), scene::NODE_DIRTY_LAYOUT);
        }
      }

      ScrollViewProps props;

    protected:
      virtual void onLifecycleFactChanged(scene::NodeLifecycleFact previous, scene::NodeLifecycleFact next)
      {
        if (previous == scene::NODE_FACT_ATTACHED && next != scene::NODE_FACT_ATTACHED)
          scene::PendingRequests::cancel(this->props.scrollTo_);
      }

    private:
      void writeViewport(int x, int extentY, int width, int height)
      {
        if (!this->props.viewport_.isValid())
          return;
        const int y = extentY - this->props.contentInsets_.top();
        const loka::core::Frame current = this->props.viewport_.state()->get();
        if (current.x != x || current.y != y || current.width != width || current.height != height)
          this->props.viewport_.set(loka::core::Frame(x, y, width, height));
      }
    };

    namespace scene
    {
      template <> struct NodePropsApplier<ScrollViewNode, ScrollViewProps>
      {
        static bool apply(ScrollViewNode *node, const ScrollViewProps &props)
        {
          if (!node->props.scrollTo_.same(props.scrollTo_))
            PendingRequests::cancel(node->props.scrollTo_);
          node->props = props;
          return true;
        }
      };
    } // namespace scene

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

      /** Grows the scrollable extent and places content below the top inset. */
      ScrollViewDefinition &contentInsets(int top, int bottom)
      {
        this->props.contentInsets(ScrollViewContentInsets::of(top, bottom));
        return *this;
      }

      ScrollViewDefinition &viewport(const scene::NodeState<loka::core::Frame> &value)
      {
        this->props.viewport(value);
        return *this;
      }

      ScrollViewDefinition &scrollTo(scene::Request<ScrollTarget> &value)
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
