#ifndef LOKA_CORE2_SCENE_NODE_HPP
#define LOKA_CORE2_SCENE_NODE_HPP

#include "app/layout/FrameGeometry.hpp"

#include "app/Focus.hpp"

#include "core/diag/LifecycleAudit.hpp"
#include "app/scene/SceneFocus.hpp"

#include <cassert>

// static_assert-like macro. Modern compilers get real checks automatically;
// C++98 release builds keep optional checks lightweight unless explicitly gated.
#if __cplusplus >= 201103L
#define LOKA_STATIC_ASSERT(expr, msg) static_assert((expr), #msg)
#elif defined(NDEBUG)
#define LOKA_STATIC_ASSERT(expr, msg)                                                                                  \
  enum                                                                                                                 \
  {                                                                                                                    \
    static_assert_##msg = 1                                                                                            \
  }
#else
#define LOKA_STATIC_ASSERT(expr, msg)                                                                                  \
  enum                                                                                                                 \
  {                                                                                                                    \
    static_assert_##msg = sizeof(char[(expr) ? 1 : -1])                                                               \
  }
#endif

#include <cstddef>
#include <string>
#include <vector>
#include "dsl/composition/CompositionList.hpp"
#include "core/util/OwnedDef.hpp"

#include "core/State.hpp"
#include "core/LokaAlloc.hpp"
#include "core/Profiler.hpp"
#include "app/scene/ability/CapturableBitmap.hpp"
#include "app/scene/detail/ArenaMath.hpp"

#if defined(TEST_BUILD)
#define TEST_ID(value) testId(value)
#else
#define TEST_ID(value) noop()
#endif

namespace loka
{
  namespace app
  {
    template <class ItemT, class FactoryT, class KeyExprT>
    class ForBuilder;

    namespace testing { class NodeObservedUsesTestAccess; }
    namespace scene
    {
      template <typename T> class Reported;
      template <typename T> class Request;
      template <typename T> class RequestWithReply;
      template <typename T> class RequestQueueBase;
      template <typename T> class Reply;
      template <typename T> class WriteSeat;

      // NodeDirtyFlags: flags for node dirtiness (C++98-friendly)
      enum NodeDirtyFlags
      {
        NODE_DIRTY_NONE = 0x00,
        NODE_DIRTY_PROPS = 0x01,
        NODE_DIRTY_CHILD = 0x02,
        NODE_DIRTY_LAYOUT = 0x04,
        NODE_DIRTY_MYSELF = 0xFF, // All node-local dirty flags.
        NODE_DIRTY_INITIAL = 0x100
      };

      class Node;
      struct BoundaryObservedStateBinding;
      struct BoundaryObservedState;

      /** A node-owned observation edge. Withdrawal clears the subscription
          edge synchronously; storage stays with the node until reclaim. */
      struct ObservedUse
      {
        ObservedUse()
            : state(0), flags(NODE_DIRTY_NONE), generation(0), next(0),
              nextSubscription(0), node(0), subscription(0) {}
        loka::core::StateBase *state;
        NodeDirtyFlags flags;
        unsigned long generation;
        ObservedUse *next;
        ObservedUse *nextSubscription;
        Node *node;
        BoundaryObservedStateBinding *subscription;
      private:
        ObservedUse(const ObservedUse &);
        ObservedUse &operator=(const ObservedUse &);
      };

      /** Owns observation storage and the accumulated input fact. Inactive
          rows are reused; destruction reclaims storage without notifying. */
      class ObservedUses
      {
      public:
        ObservedUses() : head(0), mark(NODE_DIRTY_NONE) {}
        ~ObservedUses();
        void include(NodeDirtyFlags flags)
        { this->mark = static_cast<NodeDirtyFlags>(this->mark | flags); }
        NodeDirtyFlags takeMark()
        {
          const NodeDirtyFlags result = this->mark;
          this->mark = NODE_DIRTY_NONE;
          return result;
        }
        void withdraw();
        void discardPrepared();
        ObservedUse *reserve(loka::core::StateBase *state);
        ObservedUse *head;
        NodeDirtyFlags mark;
      private:
        ObservedUses(const ObservedUses &);
        ObservedUses &operator=(const ObservedUses &);
      };

      // ComposeEvent: describes why compose was invoked.
      enum ComposeEvent
      {
        COMPOSE_EVENT_ATTACH = 0,
        COMPOSE_EVENT_UPDATE = 1,
        COMPOSE_EVENT_DETACH = 2
      };

      enum ComposeAttachState
      {
        COMPOSE_ATTACH_STATE_NONE = 0,
        COMPOSE_ATTACH_STATE_PENDING_ATTACH = 1,
        COMPOSE_ATTACH_STATE_PRECOMPOSED = 2
      };

      struct ComposeAttachLifecycle
      {
        ComposeAttachLifecycle()
            : state(COMPOSE_ATTACH_STATE_NONE)
        {
        }

        void markPendingAttach()
        {
          state = COMPOSE_ATTACH_STATE_PENDING_ATTACH;
        }

        ComposeEvent resolveChildComposeEvent(ComposeEvent parentEvent)
        {
          if (state != COMPOSE_ATTACH_STATE_PENDING_ATTACH)
          {
            return parentEvent;
          }
          state = COMPOSE_ATTACH_STATE_NONE;
          return parentEvent == COMPOSE_EVENT_UPDATE ? COMPOSE_EVENT_ATTACH : parentEvent;
        }

        ComposeAttachState state;
      };

      enum NodeKind
      {
        NODE_KIND_UNKNOWN = 0,
        NODE_KIND_BOX = 1,
        NODE_KIND_ZSTACK = 2,
        NODE_KIND_GRID = 3,
        NODE_KIND_CELL = 4,
        NODE_KIND_STACK = 5,
        // Tracked scenario audits serialize these ordinals. Values are
        // wire-stable, and retired slot 6 must never be reassigned.
        NODE_KIND_TEXT = 7,
        NODE_KIND_BUTTON = 8,
        NODE_KIND_EDIT_TEXT = 9,
        NODE_KIND_POPUP_MENU = 10,
        NODE_KIND_OPEN_FILE_DIALOG = 11,
        NODE_KIND_IMAGE_VIEW = 12,
        NODE_KIND_RECT_SURFACE = 13,
        NODE_KIND_SCROLL_BAR = 14,
        NODE_KIND_SCROLL_VIEW = 15,
        NODE_KIND_CANVAS = 16,
        NODE_KIND_ATTRIBUTED_TEXT = 17,
        NODE_KIND_TEXT_EDITOR = 18,
        NODE_KIND_CONFIRM_DIALOG = 19,
        NODE_KIND_RIBBON_GROUP = 20,
        NODE_KIND_RIBBON_GROUP_EDGE = 21,
        NODE_KIND_CARD = 22,
        NODE_KIND_CARD_BAND = 23
      };

      typedef unsigned short NodeTag;
      enum
      {
        NODE_TAG_NONE = 0
      };

      /** The one wish axis a Node may express about its native pair.
          Crosses the wall as observable state alongside the lifecycle facts
          (attached / detached-retained / retired); the native side decides
          policy from facts + hint. No imperative release command exists. */
      enum NativeLifetimeHint
      {
        NATIVE_HINT_DEFAULT = 0,
        NATIVE_HINT_EAGER_RELEASE,
        NATIVE_HINT_DESIRE_STAY
      };

      /** The lifecycle fact: where a node stands relative to the attached
          path. Nodes are born ATTACHED; RETIRED is terminal. Every write
          passes the single door (Node::applyLifecycleFact) whose transition
          table asserts on revival — the lifecycle version of the ABA ban. */
      enum NodeLifecycleFact
      {
        NODE_FACT_ATTACHED = 0,
        NODE_FACT_DETACHED_RETAINED,
        NODE_FACT_RETIRED
      };

      struct INestable;
      class IPlatformController;
      class Node; // forward declaration for NodeContext owner
      struct NodeDefinitionBase;
      struct IProjectedLayoutNode;
      class ComposableNode;
      class BoundaryNode;
      class ComponentContext;
      class IStateOwner;
      struct DirtySourceRegistrar;
      namespace detail
      {
        class NodeArena;
        class NodePartition;
      }

      template <typename NodeT> struct NodeTypeTokenStorage
      {
        static const char value_;
      };

      template <typename NodeT> const char NodeTypeTokenStorage<NodeT>::value_ = 0;

      template <typename NodeT> const void *NodeTypeToken()
      {
        return &NodeTypeTokenStorage<NodeT>::value_;
      }
    } // namespace scene
  } // namespace app

  // Forward declarations for app nodes (for asXxx() methods)
  namespace app
  {
    class BoundarySectionNode;
    class BoxNode;
    class CardBandNode;
    class RibbonGroupNode;
    class RibbonGroupEdgeNode;
    class ZStackNode;
    class GridNode;
    class CellNode;
    class StackNode;
    class TextNode;
    class AttributedTextNode;
    class ButtonNode;
    class EditTextNode;
    class PopupMenuNode;
    class ScrollBarNode;
    class ScrollViewNode;
    class CanvasNode;
    class OpenFileDialogNode;
    class ConfirmDialogNode;
    class ImageViewNode;
    class RectSurfaceNode;
  } // namespace app

  namespace app
  {
    namespace scene
    {

      struct LayoutState
      {
        typedef short Coordinate;

        short x;
        short y;
        short width;
        short height;
        short lineHeight;
        short spacing;
        NodeDirtyFlags inputs;

        LayoutState()
            : x(0),
              y(0),
              width(0),
              height(0),
              lineHeight(0),
              spacing(0),
              inputs(NODE_DIRTY_NONE)
        {
        }
      };

      /** Framed layout capability. Implementing nodes also provide asNestable(). */
      struct IFramedNode
      {
        virtual ~IFramedNode() {}
        virtual layout::FrameGeometry frameGeometry() const = 0;
      };

      struct IProjectedLayoutNode
      {
        virtual ~IProjectedLayoutNode() {}
        virtual short layoutProjected(IPlatformController *controller, LayoutState &state) = 0;
      };

      bool PrepareProjectedLayout(IPlatformController *controller, Node *node, LayoutState &state);

      /** Opt-in seam for platforms that tag projected contexts with their
          owning boundary (the "which window/root am I in" routing tag).
          The platform wall applies the tag only through asBoundaryTagged, so
          a context that does not opt in can never be written through a
          mistyped downcast. */
      struct IBoundaryTaggedContext
      {
        virtual ~IBoundaryTaggedContext() {}
        virtual void setBoundary(BoundaryNode *boundary) = 0;
        virtual BoundaryNode *boundary() const = 0;
      };

      // Minimal NodeContext implementation
      struct NodeContext
      {
      public:
        NodeContext()
            : owner_(0),
              deliveredFact_(NODE_FACT_ATTACHED),
              observedLifetimeHint_(NATIVE_HINT_DEFAULT)
        {
        }
        explicit NodeContext(Node *owner)
            : owner_(owner),
              deliveredFact_(NODE_FACT_ATTACHED),
              observedLifetimeHint_(NATIVE_HINT_DEFAULT)
        {
        }
        virtual ~NodeContext() {}

        /** Whether this fresh context accepts installation in its creating Node.
            Default acceptance preserves contexts without a native resident. */
        virtual bool acceptsInstallation() const
        {
          return true;
        }

        void setOwner(Node *owner)
        {
          owner_ = owner;
        }
        Node *owner() const
        {
          return owner_;
        }
        /** The consumer-side baseline: the last lifecycle fact this context
            was told about (or read at attach time). Lives inside the wall —
            the one permanent field besides the Node's own fact. */
        NodeLifecycleFact deliveredFact() const
        {
          return deliveredFact_;
        }
        /** The native side's observed copy of the owner Node's lifetime
            hint — the one wish axis, a snapshot never a command. Rides the
            same observation points as the fact: the attach-time read and
            every delivery-walk visit. */
        NativeLifetimeHint lifetimeHint() const
        {
          return observedLifetimeHint_;
        }
        /** The context-side observation point — the one channel. Living
            transitions (A <-> D) arrive from the apply-phase diff walk;
            the terminal RETIRED arrives from the retire door, after
            severing and right before this context is destroyed. A fresh context
            refused by adoptContext also receives explicit RETIRED: this context's
            life ends even though its creating Node may still be ATTACHED. */
        virtual void onFactChanged(NodeLifecycleFact previous, NodeLifecycleFact next)
        {
          (void)previous;
          (void)next;
        }
        /** The kernel applied a new definition's props to this context's node while it
            stayed the same resident (a retained recompose). Contexts that derived a
            binding from props (a captured State pointer, a cached value) re-derive it
            here. Default: nothing. Never called at materialization (no context exists
            yet) and never for an equivalent-props repoint. */
        virtual void onPropsApplied() {}
        virtual ICapturableBitmap *asCapturableBitmap()
        {
          return 0;
        }
        virtual const ICapturableBitmap *asCapturableBitmap() const
        {
          return 0;
        }
        virtual IBoundaryTaggedContext *asBoundaryTagged()
        {
          return 0;
        }
        /** Returns the borrowed live text state represented by this projected
            context, when the platform needs to retire state-indexed records. */
        virtual loka::core::State<loka::core::String> *projectedTextState()
        {
          return 0;
        }
        virtual void render(IPlatformController *) {}
        virtual short layout(IPlatformController *, LayoutState &)
        {
          return 0;
        }

      private:
        NodeContext(const NodeContext &);
        NodeContext &operator=(const NodeContext &);

        /** Attach-time read (late-subscriber rule): adopt the current
            observable state (fact + hint) as the baseline without firing
            onFactChanged. Defined after Node. */
        void initializeDeliveredFact(Node *ownerNode);
        /** The hint snapshot rides every walk visit (before the same-value
            gate); the fact baseline moves before the hook fires, so
            reentrant reads see the delivered value. Same-value delivery is
            silent. Defined after Node. */
        void deliverFact(Node *ownerNode, NodeLifecycleFact next);

        Node *owner_;
        NodeLifecycleFact deliveredFact_;
        NativeLifetimeHint observedLifetimeHint_;

        friend class Node;
      };

      class Node LOKA_AUDITED(Node)
      {
      public:
        NodeContext *context;
        Node *nextInComposition;
        union
        {
          detail::NodeArena *arenaOwner_;
          detail::NodePartition *partitionOwner_;
        };
        // Compose-only signal used to upgrade a child update pass into an
        // attach pass when a parent swaps in a freshly created child.
        // Platform/native code should not depend on this compose-local state
        // for presentation or lifecycle decisions.
        ComposeAttachLifecycle composeAttachLifecycle_;
        std::string testId_;
        NodeTag nodeTag_;
        const void *propsTypeId_;
        NativeLifetimeHint nativeLifetimeHint_;

        Node()
            : context(0),
              nextInComposition(0),
              arenaOwner_(0),
              composeAttachLifecycle_(),
              testId_(),
              nodeTag_(NODE_TAG_NONE),
              propsTypeId_(0),
              nativeLifetimeHint_(NATIVE_HINT_DEFAULT),
              lifecycleFact_(NODE_FACT_ATTACHED),
              storageOrigin_(STORAGE_HEAP)
        {
        }

        virtual ~Node()
        {
          // Death is the last retire door: every deletion path funnels here,
          // so a node that skipped the explicit retire/teardown doors (dying
          // with its parent) still turns RETIRED before its context hears
          // the terminal. Already-retired nodes are a silent same-value set.
          //
          // Every door destroying non-heap storage lowers provenance first.
          // Provenance still raised here proves another door (plain delete or
          // a raw destructor call) is ending a node whose storage it does not own.
          // The base subobject is still alive in this body, so the read is
          // well-defined and cannot be optimised away.
          assert(this->storageOrigin_ == STORAGE_HEAP &&
                 "gate storage requires DestroyHeapNode; arena and partition storage requires its landlord through DestroyNodeInPlace");
          this->applyLifecycleFact(NODE_FACT_RETIRED);
          this->releaseContext();
        }

        /** Partition provenance is distinct from the bump arena landlord. */
        bool isPartitionAllocated() const { return this->storageOrigin_ == STORAGE_PARTITION; }
        void setPartitionOwner(detail::NodePartition *owner)
        { this->partitionOwner_ = owner; this->storageOrigin_ = static_cast<unsigned char>(STORAGE_PARTITION); }
        detail::NodePartition *partitionOwner() const
        { return this->isPartitionAllocated() ? this->partitionOwner_ : 0; }

        void setArenaOwner(detail::NodeArena *owner)
        {
          arenaOwner_ = owner;
          this->storageOrigin_ = static_cast<unsigned char>(owner ? STORAGE_ARENA : STORAGE_HEAP);
        }
        detail::NodeArena *arenaOwner() const
        {
          return this->storageOrigin_ == STORAGE_ARENA ? arenaOwner_ : 0;
        }
        bool isArenaAllocated() const
        {
          return this->storageOrigin_ == STORAGE_ARENA;
        }
        // Storage provenance for the allocation gate (LokaAlloc), mirroring
        // StateBase::setGateAllocated: set only by the creation path that
        // acquired this node's storage through LokaAllocRaw, so the destroy
        // path (DestroyHeapNode) can return the storage through the same door.
        // Never copied or assigned: provenance describes the storage, not the
        // value. Mutually exclusive with arena residency (arena nodes are
        // placement-constructed into slab storage the arena owns).
        void setGateAllocated(bool v)
        {
          this->storageOrigin_ = static_cast<unsigned char>(v ? STORAGE_GATE : STORAGE_HEAP);
        }
        bool isGateAllocated() const
        {
          return this->storageOrigin_ == STORAGE_GATE;
        }
        /** An admitted build already attached this subtree before publication. */
        void markPrecomposedAttach()
        { this->composeAttachLifecycle_.state = COMPOSE_ATTACH_STATE_PRECOMPOSED; }
        bool consumePrecomposedAttach(ComposeEvent event)
        {
          if (this->composeAttachLifecycle_.state != COMPOSE_ATTACH_STATE_PRECOMPOSED) return false;
          this->composeAttachLifecycle_.state = COMPOSE_ATTACH_STATE_NONE;
          return event != COMPOSE_EVENT_DETACH;
        }
        void markPendingAttachForCompose()
        {
          composeAttachLifecycle_.markPendingAttach();
        }
      protected:
        /** The second observation point (the first is the context's
            onFactChanged): nodes without contexts that own lifecycle
            reactions of their own hear their door writes here. Fires from
            the single door on real transitions only; note that the write in
            ~Node dispatches to the base once a derived destructor has run,
            so destructors must not rely on it. */
        virtual void onLifecycleFactChanged(NodeLifecycleFact previous, NodeLifecycleFact next)
        {
          (void)previous;
          (void)next;
        }

      public:
        /** Owners that park retained branches outside the live composition
            expose them here so every lifecycle walk — fact marking, delivery,
            and context release — descends into them.
            Returns the index-th parked branch root (compacted), 0 past the
            end. The retire door relies on this: a parked branch hands its
            native pair over at the door, never from the reclaim drain. */
        virtual Node *retainedLifecycleBranch(unsigned index)
        {
          (void)index;
          return 0;
        }
        ComposeEvent resolveChildComposeEvent(ComposeEvent parentEvent)
        {
          return composeAttachLifecycle_.resolveChildComposeEvent(parentEvent);
        }

        virtual void compose() {}
        virtual NodeKind kind() const
        {
          return NODE_KIND_UNKNOWN;
        }
        virtual INestable *asNestable()
        {
          return 0;
        }
        virtual ComposableNode *asComposable()
        {
          return 0;
        }
        virtual BoundaryNode *asBoundary()
        {
          return 0;
        }
        /** Optional leaf focus membership; the row belongs to the node. */
        virtual FocusRow *asFocusParticipant()
        {
          return 0;
        }
        virtual IStateOwner *asStateOwner()
        {
          return 0;
        }
        virtual ::loka::app::BoundarySectionNode *asBoundarySectionNode()
        {
          return 0;
        }
        virtual IProjectedLayoutNode *asProjectedLayoutNode()
        {
          return 0;
        }
        virtual const void *nodeTypeKey() const
        {
          return 0;
        }
        // App node type casts (avoid dynamic_cast for 68k performance)
        virtual ::loka::app::StackNode *asStackNode()
        {
          return 0;
        }
        virtual ::loka::app::CardBandNode *asCardBandNode()
        {
          return 0;
        }
        virtual ::loka::app::RibbonGroupEdgeNode *asRibbonGroupEdgeNode()
        {
          return 0;
        }
        virtual IFramedNode *asFramedNode()
        {
          return 0;
        }
        virtual ::loka::app::RibbonGroupNode *asRibbonGroupNode()
        {
          return 0;
        }
        virtual ::loka::app::BoxNode *asBoxNode()
        {
          return 0;
        }
        virtual ::loka::app::ZStackNode *asZStackNode()
        {
          return 0;
        }
        virtual ::loka::app::GridNode *asGridNode()
        {
          return 0;
        }
        virtual ::loka::app::CellNode *asCellNode()
        {
          return 0;
        }
        virtual ::loka::app::TextNode *asTextNode()
        {
          return 0;
        }
        virtual ::loka::app::AttributedTextNode *asAttributedTextNode()
        {
          return 0;
        }
        virtual ::loka::app::ButtonNode *asButtonNode()
        {
          return 0;
        }
        virtual ::loka::app::EditTextNode *asEditTextNode()
        {
          return 0;
        }
        virtual ::loka::app::PopupMenuNode *asPopupMenuNode()
        {
          return 0;
        }
        virtual ::loka::app::ScrollBarNode *asScrollBarNode()
        {
          return 0;
        }
        virtual ::loka::app::CanvasNode *asCanvasNode()
        {
          return 0;
        }
        virtual ::loka::app::ScrollViewNode *asScrollViewNode()
        {
          return 0;
        }
        virtual ::loka::app::ConfirmDialogNode *asConfirmDialogNode()
        {
          return 0;
        }
        virtual ::loka::app::OpenFileDialogNode *asOpenFileDialogNode()
        {
          return 0;
        }
        virtual ::loka::app::ImageViewNode *asImageViewNode()
        {
          return 0;
        }
        virtual ::loka::app::RectSurfaceNode *asRectSurfaceNode()
        {
          return 0;
        }
        virtual void declareDirtySources(DirtySourceRegistrar &) {}
        // Generic interface query (for findBoundary without RTTI)
        virtual void *queryInterface(const char *name)
        {
          (void)name;
          return 0;
        }
        virtual void render(IPlatformController *controller)
        {
          if (context)
          {
            context->render(controller);
          }
        }
        /** Reserve the declared observation rows before publishing a candidate. */
        bool prepareObservedUses();
        /** Restores a refused layout's inputs without losing newer marks. */
        void requeueLayoutInputs(NodeDirtyFlags inputs)
        { this->uses_.include(inputs); }

        /** Stack-local checkpoint for a projection whose restore can refuse.
            The node retains ownership; only refusal requeues the saved fact. */
        class LayoutInputsCheckpoint
        {
        public:
          explicit LayoutInputsCheckpoint(Node &node)
              : node_(node), inputs_(node.uses_.mark) {}
          void requeue() const { this->node_.requeueLayoutInputs(this->inputs_); }
        private:
          Node &node_;
          const NodeDirtyFlags inputs_;
          LayoutInputsCheckpoint(const LayoutInputsCheckpoint &);
          LayoutInputsCheckpoint &operator=(const LayoutInputsCheckpoint &);
        };

        /** Consume this node's projection inputs, including custom container
            layouts which bypass context layout. Refusal uses the existing
            LayoutInputsCheckpoint/requeueLayoutInputs contract. */
        NodeDirtyFlags takeLayoutInputs() { return this->uses_.takeMark(); }

        virtual short layout(IPlatformController *controller, LayoutState &state)
        {
          PROFILE_SECTION("layoutNode");
          if (context)
          {
            const NodeDirtyFlags callerInputs = state.inputs;
            state.inputs = this->takeLayoutInputs();
            const short result = context->layout(controller, state);
            state.inputs = callerInputs;
            return result;
          }
          return 0;
        }

        /** Consume a fresh, never-installed context into an empty slot, or end
            its life with explicit RETIRED and delete it. The creating Node and
            controller must be live. Refusal leaves the Node fact unchanged and
            schedules nothing; the next ensure visit may try again. */
        bool adoptContext(NodeContext *ctx)
        {
          assert(!this->context && ctx);
          assert(this->lifecycleFact() != NODE_FACT_RETIRED);
          assert(!ctx->owner() || ctx->owner() == this);
          assert(ctx->deliveredFact() == NODE_FACT_ATTACHED);
          if (!ctx->acceptsInstallation())
          {
            ctx->deliverFact(this, NODE_FACT_RETIRED);
            delete ctx;
            return false;
          }
          this->setContext(ctx);
          return true;
        }

        void setContext(NodeContext *ctx)
        {
          if (context == ctx)
          {
            return;
          }
          this->releaseContext();
          context = ctx;
          if (context)
          {
            // Attach-time read (late-subscriber rule): the context adopts
            // the current fact as its baseline; presentation from the
            // current fact is the installing handler's read, not a hook.
            context->setOwner(this);
            context->initializeDeliveredFact(this);
          }
        }

        NodeContext *getContext() const
        {
          return context;
        }
        void setTestId(const std::string &value)
        {
          testId_ = value;
        }
        const std::string &testId() const
        {
          return testId_;
        }
        void setNodeTag(NodeTag tag)
        {
          nodeTag_ = tag;
        }
        NodeTag nodeTag() const
        {
          return nodeTag_;
        }
        void setPropsTypeId(const void *propsTypeId)
        {
          this->propsTypeId_ = propsTypeId;
        }
        const void *propsTypeId() const
        {
          return this->propsTypeId_;
        }
        void setNativeLifetimeHint(NativeLifetimeHint hint)
        {
          nativeLifetimeHint_ = hint;
        }
        NativeLifetimeHint nativeLifetimeHint() const
        {
          return nativeLifetimeHint_;
        }
        NodeLifecycleFact lifecycleFact() const
        {
          return lifecycleFact_;
        }

      private:
        template <class PropsT, class NodeT> friend struct NodeDefinition;
        /** After a successful props apply, attached composables' bindings
            follow their new props without rebuilding their composition. */
        void bindingsFollowProps();

        /** The single door. Same-value writes are silent (including R->R);
            RETIRED is terminal, so R->A / R->D assert. The three writers are
            the compose door (composeTree ATTACH), the walk door
            (NotifySubtreeNode*), and the retire door (retire/teardown paths)
            — reclaim never writes lifecycle state. Detach work (focus source
            cuts, terminal membership removal and state withdrawal) precedes
            the living publication hook.
            In ~Node virtual dispatch is base-only: the derived row has already
            silently unlinked, so that final RETIRED write cannot touch it. */
        void applyLifecycleFact(NodeLifecycleFact next)
        {
          if (next == NODE_FACT_ATTACHED && lifecycleFact_ != NODE_FACT_RETIRED)
            this->uses_.include(static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
          if (lifecycleFact_ == next)
          {
            return;
          }
          if (lifecycleFact_ == NODE_FACT_RETIRED)
          {
            assert(false && "lifecycle fact: RETIRED is terminal; a retired node cannot re-enter the attached path");
            return;
          }
          const NodeLifecycleFact previous = lifecycleFact_;
          lifecycleFact_ = next;
          FocusRow *focusRow = this->asFocusParticipant();
          if (focusRow)
          {
            if (previous == NODE_FACT_ATTACHED)
            {
              // Any row may be a read source; only a Scene member can be
              // published, so the living hook skips rows that never joined
              // (e.g. a leaf whose owner attach was refused).
              focusRow->source_.cut();
              if (focusRow->owner_)
                focusRow->leaveAttached();
            }
            if (next == NODE_FACT_RETIRED) focusRow->unlink();
          }
          if (next == NODE_FACT_RETIRED)
            this->withdrawStateParticipation();
          this->onLifecycleFactChanged(previous, next);
        }
        /** After RETIRED, a write to a withdrawn State may change its value
            but propagates nothing, including inside the living publication
            hook. Withdraw the inner owner's participation before that hook;
            storage stays on the owning clock. In ~Node, base-only
            asStateOwner() returns null: derived owners are already gone. */
        void withdrawStateParticipation();

        static void MarkSubtreeLifecycleFact(Node *node, NodeLifecycleFact fact);
        static void DeliverLifecycleFactsSubtree(Node *node);

        ObservedUses uses_;
        friend struct BoundaryObservedState;
        friend class ::loka::app::testing::NodeObservedUsesTestAccess;
        NodeLifecycleFact lifecycleFact_;
        enum StorageOrigin { STORAGE_HEAP, STORAGE_GATE, STORAGE_PARTITION, STORAGE_ARENA };
        unsigned char storageOrigin_;

        friend void DestroyNodeInPlace(Node *node);
        friend class BoundaryNode;
        friend class Scene;
        friend void NotifySubtreeNodeDetached(Node *node);
        friend void NotifySubtreeNodeAttached(Node *node);
        // Test backdoor (SceneTestAccess precedent): unit pins drive the
        // delivery walk directly on scene-less node trees.
        friend struct LifecycleFactTestAccess;

        void releaseContext()
        {
          NodeContext *released = context;
          if (!released)
          {
            return;
          }
          context = 0;
          // Terminal delivery: the retire door has already written RETIRED
          // and the context is severed from the node (context == 0), so the
          // observer cannot route back into the tree. One onFactChanged(->R),
          // then the detach ritual and synchronous context-object deletion.
          // Native destruction belongs to the controller queue and its App
          // safe point, never to the context destructor. A release while the
          // fact is not RETIRED (context replacement on a live node) is silent;
          // native-owning platform contexts must use the retire door instead.
          released->deliverFact(this, lifecycleFact_);
          delete released;
        }
      };

      /** The one gate site shared by heap-node creation
          (NodeDefinition::create) and every heap-node free (DestroyHeapNode),
          so the audit ledger balances. Runtime nodes acquired here through
          LokaAllocRaw carry setGateAllocated(true). */
      inline const loka::core::LokaAllocationSite &NodeHeapAllocationSite()
      {
        static const loka::core::LokaAllocationSite site("NodeDefinition", "Node");
        return site;
      }

      /** Destroys one runtime node in storage owned by the caller.
          The caller returns the storage itself after this door lowers its
          provenance and invokes the virtual destructor. Null-safe.

          Classifying the storage is the caller's job, done before this call:
          once the door has run, every destructor in the chain sees heap
          provenance, so no destructor may decide anything from its own
          isArenaAllocated() / partitionOwner(). */
      inline void DestroyNodeInPlace(Node *node)
      {
        if (!node)
        {
          return;
        }
        node->storageOrigin_ = Node::STORAGE_HEAP;
        node->~Node();
      }

      /** Destroys one heap runtime node through the door its storage came from.
          Gate-resident nodes (NodeDefinition::create) return to the backend
          under NodeHeapAllocationSite(); plain-new nodes use global delete.
          Arena and partition nodes return through their landlords; one passed
          here is refused (asserts in debug, a no-op otherwise). Null-safe.

          Provenance is read before destruction, then lowered by
          DestroyNodeInPlace on the gate path. A raised provenance in ~Node
          proves some other door is destroying non-heap storage. The virtual
          destructor destroys the most-derived node and LokaFreeRaw returns
          gate storage. The creation path asserts the Node base subobject sits
          at the storage address, so freeing through Node* is address-correct. */
      inline void DestroyHeapNode(Node *node)
      {
        if (!node)
        {
          return;
        }
        if (node->isGateAllocated())
        {
          DestroyNodeInPlace(node);
          loka::core::LokaFreeRaw(node, NodeHeapAllocationSite());
          return;
        }
        if (node->isArenaAllocated() || node->isPartitionAllocated())
        {
          // Refused in every build: neither destroyed nor freed. The node
          // stays in its landlord's ledger, which ends it through its own door.
          assert(false && "arena and partition nodes return through their landlord");
          return;
        }
        delete node;
      }

      struct DirtySourceRegistrar
      {
        virtual ~DirtySourceRegistrar() {}
        // Register a live state as a source of the given dirty flags.
        virtual void markDirtyOnChange(loka::core::StateBase *state, NodeDirtyFlags flags) = 0;
      };

      // --- Generic Props base ---
      struct PropsBase
      {
        virtual ~PropsBase() {}
        typedef Node *(*NodeFactoryFunc)(const PropsBase &);
        virtual NodeFactoryFunc nodeFactory() const = 0;
        virtual bool operator<(const PropsBase &rhs) const = 0;

        // Type ID for RTTI-free type comparison (each Props type gets unique ID)
        virtual const void *propsTypeId() const = 0;
      };

      // --- NodePropsBase (templated common base) ---
      template <class PropsT> struct NodePropsBase : public PropsBase
      {
      protected:
        template <typename K> static WriteSeat<Focused<K> > reportSeat(Focus<K> &fact)
        {
          return fact.isValid() ? reportSeat(fact.fact_) : WriteSeat<Focused<K> >();
        }
        template <typename K> static WriteSeat< ::loka::app::detail::FocusTarget<K> > requestSeat(Focus<K> &fact)
        {
          return fact.isValid() ? requestSeat(fact.request_) : WriteSeat< ::loka::app::detail::FocusTarget<K> >();
        }

        /** Extract a reporting capability while constructing the borrowing Props. */
        template <typename T> static WriteSeat<T> reportSeat(Reported<T> &fact)
        {
          return fact.seat_.writeSeat();
        }

        /** Extract a request capability while constructing the borrowing Props. */
        template <typename T> static WriteSeat<T> requestSeat(Request<T> &request)
        {
          return request.seat_.writeSeat();
        }
        /** Extract the opt-in reply capability at the typed Props door. */
        template <typename T> static WriteSeat<Reply<T> > replySeat(RequestWithReply<T> &request)
        {
          return reportSeat(request.reply_);
        }

        template <typename T> static WriteSeat<T> requestSeat(RequestQueueBase<T> &request)
        {
          return request.seat_.writeSeat();
        }
        template <typename T> static WriteSeat<Reply<T> > replySeat(RequestQueueBase<T> &request)
        {
          return reportSeat(request.reply_);
        }

      public:
        static Node *createNode(const PropsBase &base)
        {
          const PropsT &props = static_cast<const PropsT &>(base);
          return new typename PropsT::NodeType(props);
        }
        static NodeFactoryFunc staticFactory()
        {
          return &NodePropsBase::createNode;
        }
        NodeFactoryFunc nodeFactory() const
        {
          return staticFactory();
        }

        // Auto-generate unique TypeID per Props type (address of static local)
        static const void *staticTypeId()
        {
          static char id;
          return &id;
        }
        const void *propsTypeId() const
        {
          return staticTypeId();
        }
      };

      // Forward declaration
      struct INestableDefinition;
      struct IBranchSeatDefinition;
      struct IBranchPolicyScopeDefinition;

      /** Seat-local lifecycle/diff policy values folded from definition-only
          branch annotations. */
      struct BranchPolicies
      {
        BranchPolicies(bool destroy = false, bool deliver = false)
            : destroyOnDetach(destroy),
              deliverWhileDetached(deliver)
        {
        }

        bool destroyOnDetach;
        bool deliverWhileDetached;
      };

      class BranchSeatDeclaration;
      class BoundaryBranchSeatState;
      class ComponentContext;

      namespace detail
      {
        class SeatReservation;
      }

      /** Definition-side description of an indexed branch seat. */
      struct IBranchSeatDefinition
      {
        virtual ~IBranchSeatDefinition() {}
        /** Keyed's persistent node reservation; fixed and unreserved seats decline. */
        virtual const detail::SeatReservation *seatReservation() const { return 0; }
        virtual bool prepareSeatReservation() { return true; }
        /** Dirty source whose changes require the Boundary to visit this seat. */
        virtual loka::core::StateBase *branchCondition() const = 0;
        /** Selects this visit's arm. False denotes the seat's empty state. */
        virtual bool selectArm(unsigned &armOut) const = 0;
        virtual unsigned armCount() const = 0;
        /** Definition-authored policy for this arm, combined with legacy root annotations. */
        virtual BranchPolicies armPolicies(unsigned) const
        {
          return BranchPolicies();
        }
        virtual NodeDefinitionBase *armDefinition(unsigned arm) const = 0;
        /** Compatibility door for callers written against the two-arm seat. */
        NodeDefinitionBase *branchDefinition(bool value) const
        {
          return this->armDefinition(value ? 1u : 0u);
        }
        virtual const void *branchSeatTypeId() const = 0;
        /** Fixed arms need no declaration. A renewing seat builds a candidate
            window without changing its committed declaration. */
        virtual bool needsBranchDeclaration() const
        {
          return false;
        }
        virtual BranchSeatDeclaration *declareBranchCandidate(ComponentContext &)
        {
          return 0;
        }
        virtual void commitBranchDeclaration(BranchSeatDeclaration *) {}
        virtual BoundaryBranchSeatState *declaredBranchSeats() const
        {
          return 0;
        }
      };

      /** Definition-only branch-root annotation. It is consumed while the
          boundary builds its seat plan and never enters the runtime tree. */
      struct IBranchPolicyScopeDefinition
      {
        virtual ~IBranchPolicyScopeDefinition() {}
        virtual BranchPolicies branchPolicies() const = 0;
        virtual NodeDefinitionBase *scopedBranchDefinition() const = 0;
      };

      /** Props-specific lifetime compatibility, including generic definitions and clones. */
      template <typename NodeT, typename PropsT> struct NodePropsCompatibility
      {
        static bool accepts(const NodeT *, const PropsT &) { return true; }
      };

      template <typename NodeT, typename PropsT> struct NodePropsApplier
      {
        static bool apply(NodeT *node, const PropsT &props)
        {
          node->props = props;
          return true;
        }
      };

      // Type-erased base for node definitions.
      struct NodeDefinitionBase LOKA_AUDITED(NodeDefinitionBase)
      {
      public:
        typedef void (*CleanupHook)(NodeDefinitionBase *, void *);

        NodeDefinitionBase()
            : nextInComposition(0),
              cleanupHook_(0),
              cleanupContext_(0),
              testId_(),
              hasTestId_(false),
              autoTestId_(false),
              nodeTag_(NODE_TAG_NONE),
              compositionSeatSlot_(-1),
              nativeLifetimeHint_(NATIVE_HINT_DEFAULT)
        {
        }
        NodeDefinitionBase(const NodeDefinitionBase &other)
            : LOKA_AUDITED_COPY(NodeDefinitionBase, other)
              nextInComposition(0),
              cleanupHook_(0),
              cleanupContext_(0),
              testId_(other.testId_),
              hasTestId_(other.hasTestId_),
              autoTestId_(other.autoTestId_),
              nodeTag_(other.nodeTag_),
              compositionSeatSlot_(other.compositionSeatSlot_),
              nativeLifetimeHint_(other.nativeLifetimeHint_)
        {
        }
        NodeDefinitionBase &operator=(const NodeDefinitionBase &other)
        {
          this->cleanupHook_ = 0;
          this->cleanupContext_ = 0;
          this->nextInComposition = 0;
          this->testId_ = other.testId_;
          this->hasTestId_ = other.hasTestId_;
          this->autoTestId_ = other.autoTestId_;
          this->nodeTag_ = other.nodeTag_;
          this->compositionSeatSlot_ = other.compositionSeatSlot_;
          this->nativeLifetimeHint_ = other.nativeLifetimeHint_;
          return *this;
        }
        virtual ~NodeDefinitionBase()
        {
          this->invokeCleanupHook();
        }
        /**
         * Allocation contract:
         * - `clone()` / pointer-returning materialization may return `0` only
         *   for allocation-style failure such as OOM.
         * - contract misuse should be rejected structurally or by debug assert,
         *   not normalized through arbitrary `0` returns.
         */
        virtual Node *create() const = 0;
        virtual Node *createInPlace(void *mem) const = 0;
        virtual size_t nodeSize() const = 0;
        virtual size_t nodeAlign() const = 0;
        /** The most-derived type every clone() in this class's hierarchy is guaranteed
         * to produce; NodeComposition returns stored clones as this type. */
        typedef NodeDefinitionBase CloneType;
        virtual NodeDefinitionBase *clone() const = 0;
        virtual NodeKind nodeKind() const = 0;
        virtual const PropsBase *propsBase() const = 0;
        virtual bool applyPropsToNode(Node *node) const = 0;
        virtual bool isCompatibleWithNode(const Node *node) const
        {
          return node && node->kind() == this->nodeKind();
        }
        virtual bool isBoundary() const
        {
          return false;
        }
        /** Definitions whose value tag is an owner-seat identity require
            sibling uniqueness at the seat-assignment seam. */
        virtual bool requiresUniqueSiblingTag() const
        {
          return false;
        }
        /** Definitions whose identity depends on tagged sibling enumeration
            require every member of their sibling list to carry a tag. */
        virtual bool requiresFullyTaggedSiblings() const
        {
          return false;
        }
        virtual INestableDefinition *asNestableDefinition()
        {
          return 0;
        }
        virtual IBranchSeatDefinition *asBranchSeatDefinition()
        {
          return 0;
        }
        virtual IBranchPolicyScopeDefinition *asBranchPolicyScopeDefinition()
        {
          return 0;
        }
        virtual NodeDefinitionBase *retainedDefinitionBranch(unsigned index)
        {
          (void)index;
          return 0;
        }
        NodeDefinitionBase *nextInComposition;

        void setCleanupHook(CleanupHook hook, void *context)
        {
          this->cleanupHook_ = hook;
          this->cleanupContext_ = context;
        }
        void clearCleanupHook()
        {
          this->cleanupHook_ = 0;
          this->cleanupContext_ = 0;
        }
        void setTestId(const char *value)
        {
          if (value && value[0] != '\0')
          {
            this->testId_ = value;
            this->hasTestId_ = true;
            this->autoTestId_ = false;
            return;
          }
          this->testId_.clear();
          this->hasTestId_ = false;
          this->autoTestId_ = false;
        }
        void setAutoTestId()
        {
          this->testId_.clear();
          this->hasTestId_ = false;
          this->autoTestId_ = true;
        }
        bool hasTestId() const
        {
          return this->hasTestId_;
        }
        bool wantsAutoTestId() const
        {
          return this->autoTestId_;
        }
        const std::string &testIdValue() const
        {
          return this->testId_;
        }
        void setNodeTag(NodeTag tag)
        {
          this->nodeTag_ = tag;
        }
        NodeTag nodeTag() const
        {
          return this->nodeTag_;
        }
        void setCompositionSeatSlot(int slot)
        {
          this->compositionSeatSlot_ = slot;
        }
        int compositionSeatSlot() const
        {
          return this->compositionSeatSlot_;
        }
        void setNativeLifetimeHint(NativeLifetimeHint hint)
        {
          this->nativeLifetimeHint_ = hint;
        }
        NativeLifetimeHint nativeLifetimeHint() const
        {
          return this->nativeLifetimeHint_;
        }
        void copyTestIdPolicyFrom(const NodeDefinitionBase &other)
        {
          this->testId_ = other.testId_;
          this->hasTestId_ = other.hasTestId_;
          this->autoTestId_ = other.autoTestId_;
          this->nodeTag_ = other.nodeTag_;
          this->compositionSeatSlot_ = other.compositionSeatSlot_;
          this->nativeLifetimeHint_ = other.nativeLifetimeHint_;
        }

      protected:
        void invokeCleanupHook()
        {
          if (!this->cleanupHook_)
          {
            return;
          }
          CleanupHook hook = this->cleanupHook_;
          void *context = this->cleanupContext_;
          this->cleanupHook_ = 0;
          this->cleanupContext_ = 0;
          hook(this, context);
        }

      private:
        CleanupHook cleanupHook_;
        void *cleanupContext_;
        std::string testId_;
        bool hasTestId_;
        bool autoTestId_;
        NodeTag nodeTag_;
        int compositionSeatSlot_;
        NativeLifetimeHint nativeLifetimeHint_;
      };

      // Shared DSL mixin for attaching test ids to node definitions.
      template <class DerivedT> struct TestIdDslMixin
      {
        DerivedT &noop()
        {
          return *static_cast<DerivedT *>(this);
        }
        DerivedT &testId(const char *value)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          self->setTestId(value);
          return *self;
        }
        DerivedT &testId()
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          self->setAutoTestId();
          return *self;
        }
      };

      template <class PropsT, class NodeT> struct NodeDefinition : public NodeDefinitionBase
      {
        typedef PropsT PropsType;
        typedef NodeT NodeType;

        // Optional static check when PropsT/NodeT have TypeTag (via SFINAE)
#if __cplusplus >= 201103L
        LOKA_STATIC_ASSERT((typename PropsT::TypeTag *)0 == (typename NodeT::TypeTag *)0, props_node_type_mismatch);
#elif defined(LOKA_NODEDEF_CHECK_TYPETAG)
        LOKA_STATIC_ASSERT((typename PropsT::TypeTag *)0 == (typename NodeT::TypeTag *)0, props_node_type_mismatch);
#endif
        PropsT props;
        NodeDefinition()
            : props()
        {
        }
        NodeDefinition(const PropsT &p)
            : props(p)
        {
        }
        NodeDefinition &set(const PropsT &p)
        {
          props = p;
          return *this;
        }
        NodeDefinition &tag(NodeTag value)
        {
          this->setNodeTag(value);
          return *this;
        }
        NodeDefinition &lifetimeHint(NativeLifetimeHint value)
        {
          this->setNativeLifetimeHint(value);
          return *this;
        }
        template <typename F> NodeDefinition &mutate(F f)
        {
          f(props);
          return *this;
        }
        Node *create() const
        {
          // Heap-node production crosses the allocation gate (#132 S2b). A 0
          // means the backend already gave up; callers treat it as
          // allocation-style failure under the #70 nullable contract.
          void *storage = loka::core::LokaAllocRaw(sizeof(NodeT), NodeHeapAllocationSite());
          if (!storage)
          {
            return 0;
          }
          NodeT *typed = new (storage) NodeT(props);
          Node *node = typed;
          // DestroyHeapNode frees through the Node*, so the Node base
          // subobject must sit at the storage address LokaAllocRaw returned.
          assert(static_cast<void *>(node) == storage &&
                 "Node base subobject must sit at the gate storage address");
          node->setGateAllocated(true);
          node->setPropsTypeId(PropsT::staticTypeId());
          node->setNodeTag(this->nodeTag());
          node->setNativeLifetimeHint(this->nativeLifetimeHint());
          return node;
        }
        Node *createInPlace(void *mem) const
        {
          Node *node = new (mem) NodeT(props);
          if (node)
          {
            node->setPropsTypeId(PropsT::staticTypeId());
            node->setNodeTag(this->nodeTag());
            node->setNativeLifetimeHint(this->nativeLifetimeHint());
          }
          return node;
        }
        size_t nodeSize() const
        {
          return sizeof(NodeT);
        }
        size_t nodeAlign() const
        {
          return detail::AlignOf<NodeT>::value;
        }
        typedef NodeDefinition CloneType;
        virtual NodeDefinitionBase *clone() const
        {
          return new NodeDefinition(*this);
        }
        virtual bool isCompatibleWithNode(const Node *node) const
        {
          return node && node->propsTypeId() == PropsT::staticTypeId()
              && NodePropsCompatibility<NodeT, PropsT>::accepts(static_cast<const NodeT *>(node), this->props);
        }
        virtual NodeKind nodeKind() const
        {
          NodeT probe(props);
          return probe.kind();
        }
        virtual const PropsBase *propsBase() const
        {
          return &props;
        }
        virtual bool applyPropsToNode(Node *node) const
        {
          if (!this->isCompatibleWithNode(node))
          {
            return false;
          }
          NodeT *typed = static_cast<NodeT *>(node);
          bool applied = NodePropsApplier<NodeT, PropsT>::apply(typed, this->props);
          if (applied)
          {
            typed->setNodeTag(this->nodeTag());
            typed->setNativeLifetimeHint(this->nativeLifetimeHint());
            node->uses_.include(static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
            node->bindingsFollowProps();
            if (node->context)
              node->context->onPropsApplied();
          }
          return applied;
        }
      };

      // --- Interface for nestable NodeDefinition ---
      struct INestableDefinition
      {
        virtual ~INestableDefinition() {}
        virtual void addChild(NodeDefinitionBase *child) = 0;
        virtual void addOwnedChild(NodeDefinitionBase *child)
        {
          addChild(child);
        }
        virtual NodeDefinitionBase *childrenHead() const = 0;
        virtual size_t childrenCount() const = 0;

        // Type cast (avoid dynamic_cast for 68k performance)
        virtual const NodeDefinitionBase *asNodeDefinitionBase() const = 0;

        // Overloads
        INestableDefinition &operator<<(NodeDefinitionBase &child);
        INestableDefinition &operator<<(const NodeDefinitionBase &child);
        INestableDefinition &operator<<(NodeDefinitionBase *ownedChild);

        // Explicit single element operator<< (no begin/end)
        // Type-safe: accept only NodeDefinitionBase

        // vector<NodeDefinitionBase*> explicit overload (C++98)
        INestableDefinition &operator<<(const std::vector<NodeDefinitionBase *> &container);

        // For future extensibility
      };

      template <class DerivedT> struct NestableDslMixin
      {
        DerivedT &operator<<(NodeDefinitionBase &child)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          static_cast<INestableDefinition &>(*self) << child;
          return *self;
        }

        DerivedT &operator<<(const NodeDefinitionBase &child)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          static_cast<INestableDefinition &>(*self) << child;
          return *self;
        }

        DerivedT &operator<<(NodeDefinitionBase *ownedChild)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          static_cast<INestableDefinition &>(*self) << ownedChild;
          return *self;
        }

        DerivedT &operator<<(const std::vector<NodeDefinitionBase *> &container)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          static_cast<INestableDefinition &>(*self) << container;
          return *self;
        }

        template <class ItemT, class FactoryT, class KeyExprT>
        DerivedT &operator<<(
            const ::loka::app::ForBuilder<ItemT, FactoryT, KeyExprT> &builder)
        {
          DerivedT *self = static_cast<DerivedT *>(this);
          builder.appendTo(static_cast<INestableDefinition &>(*self));
          return *self;
        }
      };

      // --- Helper base class for nestable definitions owning children ---
      /** Report a child clone refused inside the active declaration window.
          Implemented at the composition layer; a definition built outside a
          window has no Boundary result to report to. */
      void NoteDefinitionCaptureRefusal();

      class NestableDefinitionBase : public INestableDefinition
      {
      public:
        NestableDefinitionBase()
            : children_()
        {
        }
        virtual ~NestableDefinitionBase()
        {
          this->clearChildren();
        }

        virtual void addChild(NodeDefinitionBase *child)
        {
          if (!child)
          {
            return;
          }
          if (!this->children_.appendClone(*child))
            NoteDefinitionCaptureRefusal();
        }

        virtual void addOwnedChild(NodeDefinitionBase *child)
        {
          if (!child)
          {
            return;
          }
          children_.appendOwned(child);
        }

        virtual NodeDefinitionBase *childrenHead() const
        {
          return children_.head();
        }
        virtual size_t childrenCount() const
        {
          return children_.count();
        }

      protected:
        void clearChildren()
        {
          children_.clear();
        }

        template <typename BaseT>
        bool assignNestableBase(BaseT &baseSelf, const BaseT &otherBase, const NestableDefinitionBase &otherChildren)
        {
          if (!replaceChildrenFrom(otherChildren))
          {
            return false;
          }
          baseSelf = otherBase;
          return true;
        }

        bool replaceChildrenFrom(const NestableDefinitionBase &other)
        {
          loka::dsl::CompositionList<NodeDefinitionBase> newChildren;
          NodeDefinitionBase *cur = other.children_.head();
          while (cur)
          {
            loka::core::OwnedDef<NodeDefinitionBase> child(cur ? cur->clone() : 0);
            if (!child.isSet())
            {
              NoteDefinitionCaptureRefusal();
              return false;
            }
            newChildren.appendOwned(child.take());
            cur = cur->nextInComposition;
          }
          clearChildren();
          newChildren.detachTo(children_);
          return true;
        }

      private:
        NestableDefinitionBase(const NestableDefinitionBase &);
        NestableDefinitionBase &operator=(const NestableDefinitionBase &);

        loka::dsl::CompositionList<NodeDefinitionBase> children_;
      };

      template <class PropsT, class NodeT, class DerivedT>
      struct NestableNodeDefinition : public NodeDefinition<PropsT, NodeT>,
                                      public NestableDefinitionBase,
                                      public NestableDslMixin<DerivedT>
      {
        typedef NodeDefinition<PropsT, NodeT> BaseType;
        using NestableDslMixin<DerivedT>::operator<<;

        NestableNodeDefinition()
            : BaseType(),
              NestableDefinitionBase()
        {
        }

        explicit NestableNodeDefinition(const PropsT &p)
            : BaseType(p),
              NestableDefinitionBase()
        {
        }

        /** By-value copy cannot report refusal; children remain empty on failure.
            Refusal-capable callers must use clone(). */
        NestableNodeDefinition(const NestableNodeDefinition &other)
            : BaseType(),
              NestableDefinitionBase()
        {
          this->assignFromDefinition(other);
        }

        NestableNodeDefinition &operator=(const NestableNodeDefinition &other)
        {
          if (this != &other)
          {
            this->assignFromDefinition(other);
          }
          return *this;
        }

        virtual INestableDefinition *asNestableDefinition()
        {
          return static_cast<DerivedT *>(this);
        }

        virtual const NodeDefinitionBase *asNodeDefinitionBase() const
        {
          return static_cast<const DerivedT *>(this);
        }

        typedef DerivedT CloneType;
        virtual NodeDefinitionBase *clone() const
        {
          DerivedT *copy = new DerivedT();
          if (!copy)
          {
            return 0;
          }
          if (!copy->assignFromDefinition(*this))
          {
            delete copy;
            return 0;
          }
          return copy;
        }

      protected:
        bool assignFromDefinition(const NestableNodeDefinition &other)
        {
          return this->assignNestableBase(
              static_cast<BaseType &>(*this), static_cast<const BaseType &>(other), other);
        }
      };

      // --- Interface for nestable Node/Definition ---
      struct INestable
      {
        virtual ~INestable() {}
        virtual void addChild(Node *child) = 0;
        virtual bool replaceChild(Node *oldChild, Node *newChild) = 0;
        virtual void detachChildrenTo(std::vector<Node *> &out) = 0;
        /** Transfer child edges as an intact sibling chain, without allocation. */
        virtual Node *detachChildren() = 0;
        virtual Node *childrenHead() const = 0;
        virtual size_t childrenCount() const = 0;
      };

      // --- Helper node which owns children ---
      class NestableNode : public Node, public INestable
      {
      public:
        NestableNode()
            : Node(),
              children_()
        {
        }
        virtual ~NestableNode()
        {
          clearChildrenInternal();
        }

        virtual void addChild(Node *child)
        {
          if (child)
          {
            children_.appendOwned(child);
          }
        }

        virtual bool replaceChild(Node *oldChild, Node *newChild)
        {
          return children_.replace(oldChild, newChild);
        }

        virtual Node *detachChildren()
        {
          return this->children_.detach();
        }

        virtual void detachChildrenTo(std::vector<Node *> &out)
        {
          children_.detachTo(out);
        }

        virtual Node *childrenHead() const
        {
          return children_.head();
        }
        virtual size_t childrenCount() const
        {
          return children_.count();
        }
        virtual INestable *asNestable()
        {
          return this;
        }

      protected:
        void clearChildrenInternal()
        {
          if (children_.count() == 0)
          {
            return;
          }
          std::vector<Node *> nodes;
          children_.detachTo(nodes);
          for (size_t i = 0; i < nodes.size(); ++i)
          {
            Node *child = nodes[i];
            if (!child)
            {
              continue;
            }
            if (child->isPartitionAllocated() || child->isArenaAllocated())
            {
              continue;
            }
            // Arena and partition children belong to their landlords.
            // Remaining children return through their heap allocation door.
            DestroyHeapNode(child);
          }
        }
        loka::dsl::CompositionList<Node> children_;
      };

      inline void NodeContext::initializeDeliveredFact(Node *ownerNode)
      {
        if (!ownerNode)
        {
          return;
        }
        deliveredFact_ = ownerNode->lifecycleFact();
        observedLifetimeHint_ = ownerNode->nativeLifetimeHint();
      }

      inline void NodeContext::deliverFact(Node *ownerNode, NodeLifecycleFact next)
      {
        if (ownerNode)
        {
          observedLifetimeHint_ = ownerNode->nativeLifetimeHint();
        }
        if (deliveredFact_ == next)
        {
          return;
        }
        const NodeLifecycleFact previous = deliveredFact_;
        deliveredFact_ = next;
        this->onFactChanged(previous, next);
      }

      /** Marks a subtree's lifecycle fact through the single door, without
          delivering anything. Retained branches parked outside the live
          composition (Conditional slots) are not reached — they keep their
          own fact until their owner's door writes it. */
      inline void Node::MarkSubtreeLifecycleFact(Node *node, NodeLifecycleFact fact)
      {
        if (!node)
        {
          return;
        }
        node->applyLifecycleFact(fact);
        for (unsigned i = 0; Node *branch = node->retainedLifecycleBranch(i); ++i)
        {
          MarkSubtreeLifecycleFact(branch, fact);
        }
        INestable *nestable = node->asNestable();
        if (!nestable)
        {
          return;
        }
        for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        {
          MarkSubtreeLifecycleFact(child, fact);
        }
      }

      /** The apply-phase delivery walk: hands each context the diff between
          its baseline (deliveredFact_) and the node's fact — the only path
          that turns living transitions (A <-> D) into onFactChanged calls.
          Descends into parked retained branches through the owner's virtual,
          so a branch swapped out of the composition still hears its detach
          at the next apply. Reclaim never runs this walk. */
      inline void Node::DeliverLifecycleFactsSubtree(Node *node)
      {
        if (!node)
        {
          return;
        }
        if (node->context)
        {
          node->context->deliverFact(node, node->lifecycleFact_);
        }
        for (unsigned i = 0; Node *branch = node->retainedLifecycleBranch(i); ++i)
        {
          DeliverLifecycleFactsSubtree(branch);
        }
        INestable *nestable = node->asNestable();
        if (!nestable)
        {
          return;
        }
        for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        {
          DeliverLifecycleFactsSubtree(child);
        }
      }

      /** Walk door, detach direction: writes the retained-detach fact down a
          subtree. Contexts stay alive and hear the change from the
          apply-phase delivery walk — never synchronously from here. Nodes
          that own retained branches (Conditional) read their own fact to
          stop announcing swaps while an ancestor keeps them off the
          attached path. */
      inline void NotifySubtreeNodeDetached(Node *node)
      {
        if (!node)
        {
          return;
        }
        node->applyLifecycleFact(NODE_FACT_DETACHED_RETAINED);
        INestable *nestable = node->asNestable();
        if (!nestable)
        {
          return;
        }
        for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        {
          NotifySubtreeNodeDetached(child);
        }
      }

      /** Walk door, re-entry direction: first entry is announced by
          setContext()'s attach-time read. The walk follows live children
          only, so branches swapped out while hidden stay hidden and the
          current active path is shown at the next apply. */
      inline void NotifySubtreeNodeAttached(Node *node)
      {
        if (!node)
        {
          return;
        }
        node->applyLifecycleFact(NODE_FACT_ATTACHED);
        INestable *nestable = node->asNestable();
        if (!nestable)
        {
          return;
        }
        for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        {
          NotifySubtreeNodeAttached(child);
        }
      }

      inline INestableDefinition &INestableDefinition::operator<<(NodeDefinitionBase &child)
      {
        addChild(&child);
        return *this;
      }
      inline INestableDefinition &INestableDefinition::operator<<(const NodeDefinitionBase &child)
      {
        addChild(const_cast<NodeDefinitionBase *>(&child));
        return *this;
      }

      // operator<< implementation for generic containers (e.g., vector)
      inline INestableDefinition &INestableDefinition::operator<<(const std::vector<NodeDefinitionBase *> &container)
      {
        for (size_t i = 0; i < container.size(); ++i)
        {
          addChild(container[i]);
        }
        return *this;
      }

      inline INestableDefinition &INestableDefinition::operator<<(NodeDefinitionBase *ownedChild)
      {
        addOwnedChild(ownedChild);
        return *this;
      }
    } // namespace scene
  } // namespace app
} // namespace loka

// Conditional node inline implementations removed from this header to reduce coupling

#endif // LOKA_CORE2_SCENE_NODE_HPP
