#ifndef LOKA_CORE2_SCENE_NODE_COMPOSABLENODE_HPP
#define LOKA_CORE2_SCENE_NODE_COMPOSABLENODE_HPP

#include <cassert>
#include <vector>
#include "app/scene/node/BindingToken.hpp"
#include "../Node.hpp"
#include "app/scene/context/ComponentContext.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "app/scene/state/StateBatchBase.hpp"
#include "app/scene/state/Reported.hpp"
#include "app/Focus.hpp"
#include "app/scene/state/StateOwner.hpp"
#include "core/Profiler.hpp"

class Window;

namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <typename FlowT> class FlowSlot;
      class ComposableNodeTestAccess;
      class BoundaryNode;
      class Scene;
      class ComponentNode;
      class RootBoundaryWrapper;
      template <class PropsT> class StdCompositionBoundaryNodeBase;
#ifdef TEST_BUILD
      template <class PropsT> class HeadlessNodeBase;
#endif

      namespace detail
      {
        template <class Seat> struct MutableNodeDependencyTraits;
        template <typename T> struct MutableNodeDependencyTraits<NodeState<T> >
        {
          static loka::core::StateBase *read(const void *seat, IStateOwner *&owner)
          {
            const NodeState<T> &value = *static_cast<const NodeState<T> *>(seat);
            owner = value.dangerouslyOwner();
            return value.state();
          }
        };

        /** Borrows a mutable seat address, resolved only at registration connect. */
        struct NodeStateDependency
        {
          NodeStateDependency() : seat_(0), read_(0) {}
          template <class Seat> explicit NodeStateDependency(Seat &seat)
              : seat_(&seat), read_(&MutableNodeDependencyTraits<Seat>::read) {}
          loka::core::StateBase *resolve(IStateOwner *&owner) const
          {
            owner = 0;
            return this->read_ ? this->read_(this->seat_, owner) : 0;
          }
          bool isPresent() const { return this->seat_ != 0; }
        private:
          const void *seat_;
          loka::core::StateBase *(*read_)(const void *, IStateOwner *&);
        };
      }

      enum ContextPlacement
      {
        CONTEXT_PLACEMENT_BOUNDARY = 0,
        CONTEXT_PLACEMENT_ROOT = 1
      };

      // Base class for nodes that build children via compose() when requested
      class ComposableNode : public NestableNode
      {
      public:
        using Node::compose;

        ComposableNode()
            : currentContext_(0),
              nodeStateOwner_(0),
              isAttached_(false),
              attached_(),
              participants_()
        {
        }
        virtual ~ComposableNode()
        {
          releaseCallbacks();
          clearChildren();
          reclaimParticipants();
        }

        virtual ComposableNode *asComposable()
        {
          return this;
        }

        virtual void compose(ComponentContext &context, ComposeEvent event)
        {
          PROFILE_FUNC();
          {
            ContextScope scope(this, &context);
            attached_.boundary_ = context.boundary();
            attached_.scene_ = context.scene();
            attached_.window_ = context.window();
            isAttached_ = attached_.boundary_ && attached_.scene_ && attached_.window_;
            nodeStateOwner_ = context.stateOwner();
            if (event != COMPOSE_EVENT_DETACH)
            {
              this->connectNodeStateRegistrations();
            }
            {
              PROFILE_SECTION("compWith");
              this->composeWithContext(context, event);
            }
            if (event == COMPOSE_EVENT_DETACH)
            {
              attached_.reset();
              isAttached_ = false;
              nodeStateOwner_ = 0;
            }
          }
        }

        virtual void compose(ComponentContext &context)
        {
          this->compose(context, COMPOSE_EVENT_ATTACH);
        }

      protected:
        virtual void composeWithContext(ComponentContext &context, ComposeEvent event) = 0;
        /** Called first in every declaring window. Immediate watches run here
            synchronously, before attach and child composition. */
        virtual void declareBindings(BindingToken &token)
        {
          (void)token;
        }
        virtual void attachNode(NodeComposition &c)
        {
          (void)c;
        }
        virtual void detachNode(NodeComposition &c)
        {
          (void)c;
          releaseCallbacks();
        }

        ComponentContext *componentContext() const
        {
          return currentContext_;
        }
        enum CallbackFamily { CALLBACK_ACTION, CALLBACK_STATE_WATCH, CALLBACK_COMMAND };

        struct CallbackEntryBase
        {
          virtual ~CallbackEntryBase() {}
          virtual CallbackFamily family() const = 0;
          virtual void unbind() = 0;
          virtual void invalidate() = 0;
          virtual bool matches(CallbackFamily requestedFamily, const void *source, void *node, const void *methodBytes, size_t methodSize) const = 0;
        };

        template <class NodeT> struct CallbackEntry : public CallbackEntryBase
        {
          typedef void (NodeT::*Method)();
          CallbackEntry(NodeT *node, loka::core::EmitterState *emitter, Method method)
              : node_(node),
                emitter_(emitter),
                method_(method),
                valid_(true)
          {
          }

          static void Invoke(void *userData)
          {
            CallbackEntry *self = static_cast<CallbackEntry *>(userData);
            if (!self || !self->valid_ || !self->node_)
            {
              return;
            }
            (self->node_->*(self->method_))();
          }

          CallbackFamily family() const { return CALLBACK_ACTION; }

          void unbind()
          {
            if (emitter_)
            {
              emitter_->deferUnbind(&Invoke, this);
            }
          }

          void invalidate()
          {
            valid_ = false;
          }

          bool matches(CallbackFamily requestedFamily, const void *source, void *node, const void *methodBytes, size_t methodSize) const
          {
            if (this->family() != requestedFamily || emitter_ != source || node_ != node || methodSize != sizeof(method_))
            {
              return false;
            }
            const Method *method = static_cast<const Method *>(methodBytes);
            return method && method_ == *method;
          }

          NodeT *node_;
          loka::core::EmitterState *emitter_;
          Method method_;
          bool valid_;
        };

        template <class StateT, class NodeT> struct StateCallbackEntry : public CallbackEntryBase
        {
          typedef void (NodeT::*Method)();
          StateCallbackEntry(StateT *state, NodeT *node, Method method)
              : state_(state),
                node_(node),
                method_(method),
                valid_(true)
          {
          }

          static void Invoke(void *userData)
          {
            StateCallbackEntry *self = static_cast<StateCallbackEntry *>(userData);
            if (!self || !self->valid_ || !self->node_)
            {
              return;
            }
            (self->node_->*(self->method_))();
          }

          CallbackFamily family() const { return CALLBACK_STATE_WATCH; }

          void unbind()
          {
            if (state_)
            {
              state_->unbind(&Invoke, this);
            }
          }

          void invalidate()
          {
            valid_ = false;
          }

          bool matches(CallbackFamily requestedFamily, const void *source, void *node, const void *methodBytes, size_t methodSize) const
          {
            if (this->family() != requestedFamily || state_ != source || node_ != node || methodSize != sizeof(method_))
            {
              return false;
            }
            const Method *method = static_cast<const Method *>(methodBytes);
            return method && method_ == *method;
          }

          StateT *state_;
          NodeT *node_;
          Method method_;
          bool valid_;
        };

        template <class NodeT, class E> struct CommandCallbackEntry : public CallbackEntryBase
        {
          typedef void (NodeT::*Method)(E);
          CommandCallbackEntry(NodeT *node, loka::core::EmitterState *emitter, Method method, E id)
              : node_(node),
                emitter_(emitter),
                method_(method),
                valid_(true),
                id_(id)
          {
          }

          static void Invoke(void *userData)
          {
            CommandCallbackEntry *self = static_cast<CommandCallbackEntry *>(userData);
            if (!self || !self->valid_ || !self->node_)
            {
              return;
            }
            (self->node_->*(self->method_))(self->id_);
          }

          CallbackFamily family() const { return CALLBACK_COMMAND; }

          void unbind()
          {
            if (emitter_)
            {
              emitter_->deferUnbind(&Invoke, this);
            }
          }

          void invalidate()
          {
            valid_ = false;
          }

          bool matches(CallbackFamily requestedFamily, const void *source, void *node, const void *methodBytes, size_t methodSize) const
          {
            if (this->family() != requestedFamily || emitter_ != source || node_ != node || methodSize != sizeof(method_))
            {
              return false;
            }
            const Method *method = static_cast<const Method *>(methodBytes);
            return method && method_ == *method;
          }

          NodeT *node_;
          loka::core::EmitterState *emitter_;
          Method method_;
          bool valid_;
          const E id_;
        };

      private:
        friend class BindingToken;

        template <class NodeT>
        void bindActionForUi(loka::core::EmitterState &emitter, NodeT *node, void (NodeT::*method)())
        {
          for (size_t i = 0; i < callbacks_.size(); ++i)
          {
            if (callbacks_[i] && callbacks_[i]->matches(CALLBACK_ACTION, &emitter, node, &method, sizeof(method)))
            {
              return;
            }
          }
          CallbackEntry<NodeT> *entry = new CallbackEntry<NodeT>(node, &emitter, method);
          callbacks_.push_back(entry);
          emitter.deferBind(&CallbackEntry<NodeT>::Invoke, entry);
        }

        template <class NodeT, class E>
        void bindCommandForUi(loka::core::EmitterState &emitter, NodeT *node,
                              void (NodeT::*method)(E), E id)
        {
          for (size_t i = 0; i < this->callbacks_.size(); ++i)
          {
            if (this->callbacks_[i] && this->callbacks_[i]->matches(CALLBACK_COMMAND, &emitter, node, &method, sizeof(method)))
            {
              return;
            }
          }
          CommandCallbackEntry<NodeT, E> *entry = new CommandCallbackEntry<NodeT, E>(node, &emitter, method, id);
          this->callbacks_.push_back(entry);
          emitter.deferBind(&CommandCallbackEntry<NodeT, E>::Invoke, entry);
        }

        template <class NodeT> void bindActionForUi(loka::core::EmitterState &emitter, void (NodeT::*method)())
        {
          NodeT *self = static_cast<NodeT *>(this);
          this->bindActionForUi(emitter, self, method);
        }

        template <class NodeT> void bindForUi(loka::core::EmitterState &emitter, NodeT *node, void (NodeT::*method)())
        {
          this->bindActionForUi(emitter, node, method);
        }

        template <class NodeT> void bindForUi(loka::core::EmitterState &emitter, void (NodeT::*method)())
        {
          this->bindActionForUi(emitter, method);
        }

        template <class StateT, class NodeT>
        void watchStateForUi(StateT &state, NodeT *node, void (NodeT::*method)(), bool callImmediately = false)
        {
          for (size_t i = 0; i < callbacks_.size(); ++i)
          {
            if (callbacks_[i] && callbacks_[i]->matches(CALLBACK_STATE_WATCH, &state, node, &method, sizeof(method)))
            {
              return;
            }
          }
          StateCallbackEntry<StateT, NodeT> *entry = new StateCallbackEntry<StateT, NodeT>(&state, node, method);
          callbacks_.push_back(entry);
          state.bind(&StateCallbackEntry<StateT, NodeT>::Invoke, entry, callImmediately);
        }

        template <class StateT, class NodeT>
        void watchStateForUi(StateT &state, void (NodeT::*method)(), bool callImmediately = false)
        {
          NodeT *self = static_cast<NodeT *>(this);
          this->watchStateForUi(state, self, method, callImmediately);
        }

        template <class StateT, class NodeT>
        void bindStateForUi(StateT &state, NodeT *node, void (NodeT::*method)(), bool callImmediately = false)
        {
          this->watchStateForUi(state, node, method, callImmediately);
        }

        template <class StateT, class NodeT>
        void bindStateForUi(StateT &state, void (NodeT::*method)(), bool callImmediately = false)
        {
          this->watchStateForUi(state, method, callImmediately);
        }

      protected:
        // Registers Node-local state with the current composition owner.
        // This does not make ComposableNode a state owner; nodes that need a
        // shorter ownership scope than Boundary should expose an explicit
        // IStateOwner through asStateOwner().
        template <typename K> void state(Focus<K> &out)
        {
          this->state(out.fact_, Focused<K>::none());
          this->state(out.request_, ::loka::app::detail::FocusTarget<K>::None());
        }

        /** Initial focus is a request initializer, never an initial reported fact. */
        template <typename K> void state(Focus<K> &out, K initial)
        {
          this->state(out.fact_, Focused<K>::none());
          this->state(out.request_, ::loka::app::detail::FocusTarget<K>(initial));
        }

        template <typename T> void state(Request<T> &out, const T &initial)
        {
          (void)sizeof(typename RequestDeclarationWall<T>::Wall);
          this->state(out.seat_, initial);
        }
        template <typename T> void state(RequestWithReply<T> &out, const T &initial)
        {
          this->state(static_cast<Request<T> &>(out), initial);
          this->state(out.reply_, Reply<T>());
        }

        template <typename T> void state(RequestQueueBase<T> &out, const T &initial)
        {
          this->state(out.seat_, initial);
          this->state(out.reply_, Reply<T>());
        }

        template <typename T> void state(Reported<T> &out, const T &initial)
        {
          this->state(out.seat_, initial);
        }

        template <typename T> void state(NodeState<T> &out, const T &initial)
        {
          for (size_t i = 0; i < participants_.size(); ++i)
          {
            if (participants_[i]->asStateRegistration() && participants_[i]->asStateRegistration()->matches(&out))
            {
              assert(false && "ComposableNode::state registered the same NodeState twice");
              return;
            }
          }
          NodeStateRegistration<T> *entry = new NodeStateRegistration<T>(&out, initial);
          participants_.push_back(entry);
          this->connectNodeStateRegistration(entry);
        }

        /** Seats a derived value from one or two mutable NodeState inputs.
            Completed registration order is dependency order: declareStates()
            registers when the batch's last copy dies; end it before derived().
            Takes ownership of eval at entry, including refusal. EvalFn must be
            pure (no State writes or lifecycle calls); immutable node reads are
            allowed. Its destructor is cleanup-only and must never read node
            members or dependencies. Called once at declaration and connect,
            never during updates; scans only this node's registration rows. */
        template <class T, class D1>
        void derived(DerivedNodeState<T> &out, D1 &dep,
                     typename loka::core::DerivedState<T>::EvalFn *eval)
        {
          this->registerDerived(out, detail::NodeStateDependency(dep),
                                detail::NodeStateDependency(), eval);
        }

        template <class T, class D1, class D2>
        void derived(DerivedNodeState<T> &out, D1 &dep1, D2 &dep2,
                     typename loka::core::DerivedState<T>::EvalFn *eval)
        {
          this->registerDerived(out, detail::NodeStateDependency(dep1),
                                detail::NodeStateDependency(dep2), eval);
        }

        class NodeStateBatch : private StateBatchBase
        {
        public:
          enum
          {
            kStorageBytes = StateBatchBase::kStorageBytes
          };

          NodeStateBatch(ComposableNode *node, size_t capacity)
              : block_(new Block(node, capacity))
          {
          }

          NodeStateBatch(const NodeStateBatch &other)
              : block_(other.block_)
          {
            if (block_)
            {
              ++block_->refs;
            }
          }

          ~NodeStateBatch()
          {
            this->releaseBlock();
          }

          /** A focus bundle reserves two physical state rows. */
          template <typename K> NodeStateBatch &state(Focus<K> &out)
          {
            this->state(out.fact_, Focused<K>::none());
            return this->state(out.request_, ::loka::app::detail::FocusTarget<K>::None());
          }

          template <typename K> NodeStateBatch &state(Focus<K> &out, K initial)
          {
            this->state(out.fact_, Focused<K>::none());
            return this->state(out.request_, ::loka::app::detail::FocusTarget<K>(initial));
          }

          /** RequestWithReply uses two physical entries in declareStates capacity. */
          template <typename T> NodeStateBatch &state(Request<T> &out, const T &initial)
          {
            (void)sizeof(typename RequestDeclarationWall<T>::Wall);
            return this->state(out.seat_, initial);
          }
          template <typename T> NodeStateBatch &state(RequestWithReply<T> &out, const T &initial)
          {
            this->state(static_cast<Request<T> &>(out), initial);
            return this->state(out.reply_, Reply<T>());
          }

          /** A queue uses two physical entries, like RequestWithReply. */
          template <typename T> NodeStateBatch &state(RequestQueueBase<T> &out, const T &initial)
          {
            this->state(out.seat_, initial);
            return this->state(out.reply_, Reply<T>());
          }

          template <typename T> NodeStateBatch &state(Reported<T> &out, const T &initial)
          {
            return this->state(out.seat_, initial);
          }

          template <typename T> NodeStateBatch &state(NodeState<T> &out, const T &initial)
          {
            typedef char LokaNodeStateBatchInitializerTooLarge[(sizeof(T) <= kStorageBytes) ? 1 : -1];
            (void)sizeof(LokaNodeStateBatchInitializerTooLarge);
            assert(block_ && "NodeStateBatch::state requires a live batch");
            if (!block_)
            {
              return *this;
            }
            for (size_t i = 0; i < block_->count; ++i)
            {
              if (block_->entries[i].matches && block_->entries[i].matches(block_->entries[i], &out))
              {
                assert(false && "NodeStateBatch::state registered the same NodeState twice");
                return *this;
              }
            }
            assert(block_->count < block_->capacity && "NodeStateBatch capacity exceeded");
            if (block_->count >= block_->capacity)
            {
              if (block_->node)
              {
                block_->node->state(out, initial);
              }
              return *this;
            }
            Entry &entry = block_->entries[block_->count++];
            entry.out = &out;
            entry.connect = &ConnectState<T>;
            entry.pendingArenaBytes = &PendingArenaBytes<T>;
            entry.disconnect = &DisconnectState<T>;
            entry.matches = &MatchesState<T>;
            entry.destroyInitial = &DestroyInitial<T>;
            CopyInitial<T>(entry.storage.bytes, initial);
            return *this;
          }

        private:
          typedef StateBatchBase::Storage Storage;

        public:
          struct Entry
          {
            Entry()
                : out(0),
                  owner(0),
                  state(0),
                  connect(0),
                  disconnect(0),
                  matches(0),
                  destroyInitial(0),
                  pendingArenaBytes(0),
                  storage()
            {
            }
            void *out;
            IStateOwner *owner;
            loka::core::StateBase *state;
            void (*connect)(Entry &, IStateOwner *);
            void (*disconnect)(Entry &);
            bool (*matches)(const Entry &, const void *);
            void (*destroyInitial)(Entry &);
            size_t (*pendingArenaBytes)(const Entry &);
            Storage storage;
          };

        private:
          struct Block
          {
            Block(ComposableNode *ownerNode, size_t entryCapacity)
                : node(ownerNode),
                  entries(0),
                  count(0),
                  capacity(entryCapacity),
                  refs(1)
            {
              if (capacity > 0)
              {
                entries = new Entry[capacity];
              }
            }

            ~Block()
            {
              if (entries)
              {
                for (size_t i = 0; i < count; ++i)
                {
                  if (entries[i].destroyInitial)
                  {
                    entries[i].destroyInitial(entries[i]);
                  }
                }
                delete[] entries;
              }
            }

            ComposableNode *node;
            Entry *entries;
            size_t count;
            size_t capacity;
            size_t refs;
          };

          void releaseBlock()
          {
            if (!block_)
            {
              return;
            }
            --block_->refs;
            if (block_->refs == 0)
            {
              if (block_->node && block_->entries && block_->count > 0)
              {
                block_->node->addNodeStateBatchRegistration(block_->entries, block_->count);
                block_->entries = 0;
                block_->count = 0;
              }
              delete block_;
            }
            block_ = 0;
          }

          template <typename T> static bool MatchesState(const Entry &entry, const void *out)
          {
            return entry.out == out;
          }

          /** Bytes this entry will take from the arena when it materializes;
              zero once it is connected, so a recompose reserves nothing. */
          template <typename T> static size_t PendingArenaBytes(const Entry &entry)
          {
            const NodeState<T> *out = static_cast<const NodeState<T> *>(entry.out);
            return (out && !out->isValid()) ? ArenaBytesForState<T>() : 0;
          }

          template <typename T> static void ConnectState(Entry &entry, IStateOwner *owner)
          {
            NodeState<T> *out = static_cast<NodeState<T> *>(entry.out);
            if (!out || !owner)
            {
              return;
            }
            if (out->isValid())
            {
              assert(out->dangerouslyOwner() == owner && "Node-local state reattached to a different owner");
              entry.owner = out->dangerouslyOwner();
              entry.state = out->dangerouslyMutableState();
              return;
            }
            T *initial = reinterpret_cast<T *>(entry.storage.bytes);
            CreateStateFromInitial<T>(owner, *out, *initial);
            entry.owner = out->dangerouslyOwner();
            entry.state = out->dangerouslyMutableState();
          }

          template <typename T> static void DisconnectState(Entry &entry)
          {
            if (entry.owner && entry.state)
            {
              entry.owner->releaseState(entry.state);
            }
            entry.owner = 0;
            entry.state = 0;
            entry.out = 0;
          }

          template <typename T> static void DestroyInitial(Entry &entry)
          {
            DestroyInitialObject<T>(entry.storage.bytes);
          }

          NodeStateBatch &operator=(const NodeStateBatch &);

          mutable Block *block_;
        };

        NodeStateBatch declareStates()
        {
          return NodeStateBatch(this, 16);
        }

        NodeStateBatch declareStates(size_t count)
        {
          return NodeStateBatch(this, count);
        }

        struct AttachedContext
        {
          AttachedContext()
              : boundary_(0),
                scene_(0),
                window_(0)
          {
          }
          BoundaryNode *boundary() const
          {
            return boundary_;
          }
          Scene *scene() const
          {
            return scene_;
          }
          ::Window *window() const
          {
            return window_;
          }
          void reset()
          {
            boundary_ = 0;
            scene_ = 0;
            window_ = 0;
          }

          BoundaryNode *boundary_;
          Scene *scene_;
          ::Window *window_;
        };

        const AttachedContext *attachedContext() const
        {
          return isAttached_ ? &attached_ : 0;
        }

        const AttachedContext &ensureAttached() const
        {
          assert(isAttached_ && "ComposableNode requires attached context");
          return attached_;
        }

        BoundaryNode *boundary() const
        {
          return ensureAttached().boundary();
        }
        Scene *scene() const
        {
          return ensureAttached().scene();
        }
        ::Window *window() const
        {
          return ensureAttached().window();
        }

      private:
        friend class Node;
        friend class ComponentNode;
        friend class LazyScopeNode;
        friend class GenerationRoot;
        friend class BoundaryNode;
        friend class RootBoundaryWrapper;
        template <class PropsT> friend class StdCompositionBoundaryNodeBase;
#ifdef TEST_BUILD
        template <class PropsT> friend class HeadlessNodeBase;
#endif

        /** Kernel declaring window: reset the ledger, declare bindings, then
            disarm the capability before any attach or composition callback. */
        NodeComposition &beginDeclaringWindow(ComponentContext &context)
        {
          NodeComposition &composition = this->beginComposition(context);
          this->declareBindingsWithToken();
          return composition;
        }

        /** Shares the synchronous arm/call/disarm sequence with props refresh. */
        void declareBindingsWithToken()
        {
          BindingToken::DeclarationScope scope(this->bindingToken_, *this);
          this->declareBindings(this->bindingToken_);
        }

      protected:
        NodeComposition &beginComposition(ComponentContext &context)
        {
          releaseCallbacks();
          composition_.reset();
          composition_.setContext(&context);
          return composition_;
        }

        NodeComposition &composition()
        {
          return composition_;
        }
        const NodeComposition &composition() const
        {
          return composition_;
        }

        void clearChildren()
        {
          clearChildrenInternal();
        }

      protected:
        template <typename T> friend struct NodeDerivedStateRegistration;

      private:
        template <typename T>
        void registerDerived(DerivedNodeState<T> &out,
                             const detail::NodeStateDependency &dep1,
                             const detail::NodeStateDependency &dep2,
                             typename loka::core::DerivedState<T>::EvalFn *eval)
        {
          for (size_t i = 0; i < this->participants_.size(); ++i)
          {
            if (this->participants_[i]->asStateRegistration() && this->participants_[i]->asStateRegistration()->matches(&out))
            {
              delete eval;
              assert(false && "ComposableNode::derived registered the same seat twice");
              return;
            }
          }
          NodeDerivedStateRegistration<T> *entry =
              new NodeDerivedStateRegistration<T>(&out, dep1, dep2, eval);
          this->participants_.push_back(entry);
          this->connectNodeStateRegistration(entry);
        }

      protected:
        struct NodeStateRegistrationBase;
        /** One lifetime axis; only State rows expose reservation/connect operations. */
        struct Participant
        {
          virtual ~Participant() {}
          virtual void withdraw() = 0;
          virtual void reclaim() = 0;
          virtual NodeStateRegistrationBase *asStateRegistration() { return 0; }
        };

        struct NodeStateRegistrationBase : Participant
        {
          virtual ~NodeStateRegistrationBase() {}
          virtual NodeStateRegistrationBase *asStateRegistration() { return this; }
          // State withdrawal belongs to the existing IStateOwner ledger sweep.
          virtual void withdraw() {}
          virtual void reclaim() { this->disconnect(); delete this; }
          virtual bool matches(const void *out) const = 0;
          virtual void connect(IStateOwner *owner) = 0;
          virtual void disconnect() = 0;
          /** Arena bytes the next connect() will consume; zero once
              materialized, so recomposes reserve nothing. */
          virtual size_t pendingArenaBytes() const = 0;
        };

        template <typename T> struct NodeStateRegistration : public NodeStateRegistrationBase
        {
          NodeStateRegistration(NodeState<T> *out, const T &initial)
              : out_(out),
                initial_(initial),
                owner_(0),
                state_(0)
          {
          }

          bool matches(const void *out) const
          {
            return out_ == out;
          }

          void connect(IStateOwner *owner)
          {
            if (!out_ || !owner)
            {
              return;
            }
            if (out_->isValid())
            {
              assert(out_->dangerouslyOwner() == owner && "Node-local state reattached to a different owner");
              owner_ = out_->dangerouslyOwner();
              state_ = out_->dangerouslyMutableState();
              return;
            }
            StateBatchBase::CreateStateFromInitial<T>(owner, *out_, initial_);
            owner_ = out_->dangerouslyOwner();
            state_ = out_->dangerouslyMutableState();
          }

          size_t pendingArenaBytes() const
          {
            return (out_ && !out_->isValid())
                       ? StateBatchBase::ArenaBytesForState<T>()
                       : 0;
          }

          void disconnect()
          {
            if (owner_ && state_)
            {
              owner_->releaseState(state_);
            }
            owner_ = 0;
            state_ = 0;
            out_ = 0;
          }

          NodeState<T> *out_;
          T initial_;
          IStateOwner *owner_;
          loka::core::MutableState<T> *state_;
        };

        struct NodeStateBatchRegistration : public NodeStateRegistrationBase
        {
          typedef NodeStateBatch::Entry Entry;

          NodeStateBatchRegistration(Entry *entries, size_t count)
              : entries_(entries),
                count_(count)
          {
          }

          ~NodeStateBatchRegistration()
          {
            if (entries_)
            {
              for (size_t i = 0; i < count_; ++i)
              {
                if (entries_[i].destroyInitial)
                {
                  entries_[i].destroyInitial(entries_[i]);
                }
              }
              delete[] entries_;
            }
          }

          bool matches(const void *out) const
          {
            for (size_t i = 0; i < count_; ++i)
            {
              if (entries_[i].matches && entries_[i].matches(entries_[i], out))
              {
                return true;
              }
            }
            return false;
          }

          void connect(IStateOwner *owner)
          {
            for (size_t i = 0; i < count_; ++i)
            {
              if (entries_[i].connect)
              {
                entries_[i].connect(entries_[i], owner);
              }
            }
          }

          size_t pendingArenaBytes() const
          {
            size_t total = 0;
            for (size_t i = 0; i < count_; ++i)
            {
              if (entries_[i].pendingArenaBytes)
              {
                total += entries_[i].pendingArenaBytes(entries_[i]);
              }
            }
            return total;
          }

          void disconnect()
          {
            for (size_t i = 0; i < count_; ++i)
            {
              if (entries_[i].disconnect)
              {
                entries_[i].disconnect(entries_[i]);
              }
            }
          }

          Entry *entries_;
          size_t count_;
        };

        void addNodeStateBatchRegistration(NodeStateBatch::Entry *entries, size_t count)
        {
          if (!entries || count == 0)
          {
            return;
          }
          NodeStateBatchRegistration *entry = new NodeStateBatchRegistration(entries, count);
          participants_.push_back(entry);
          this->connectNodeStateRegistration(entry);
        }

        void connectNodeStateRegistration(NodeStateRegistrationBase *entry)
        {
          if (!entry || !nodeStateOwner_)
          {
            return;
          }
          // Reserve is the arena's only growth door; allocate never grows.
          // Without this, every node-local state silently took the gate heap
          // fallback (#267). pendingArenaBytes() is zero once materialized,
          // so recomposes reserve nothing.
          const size_t arenaBytes = entry->pendingArenaBytes();
          if (arenaBytes != 0)
          {
            nodeStateOwner_->reserveStateArena(arenaBytes);
          }
          entry->connect(nodeStateOwner_);
        }

        void connectNodeStateRegistrations()
        {
          if (!nodeStateOwner_)
          {
            return;
          }
          // One reservation for the whole attach batch: per-entry reserves
          // would cut one tiny slab per state, which is the same economy as
          // the heap fallback with extra bookkeeping.
          size_t arenaBytes = 0;
          for (size_t i = 0; i < participants_.size(); ++i)
          {
            if (NodeStateRegistrationBase *entry = participants_[i]->asStateRegistration())
            {
              arenaBytes += entry->pendingArenaBytes();
            }
          }
          if (arenaBytes != 0)
          {
            nodeStateOwner_->reserveStateArena(arenaBytes);
          }
          for (size_t i = 0; i < participants_.size(); ++i)
          {
            if (NodeStateRegistrationBase *entry = participants_[i]->asStateRegistration())
            {
              entry->connect(nodeStateOwner_);
            }
          }
        }

        void withdrawParticipants()
        {
          for (size_t i = 0; i < this->participants_.size(); ++i)
            this->participants_[i]->withdraw();
        }

        void reclaimParticipants()
        {
          for (size_t i = 0; i < this->participants_.size(); ++i)
            this->participants_[i]->reclaim();
          this->participants_.clear();
        }

        void enrollParticipant(Participant &entry) { this->participants_.push_back(&entry); }
        void unlinkParticipant(Participant &entry)
        {
          for (std::vector<Participant *>::iterator it = this->participants_.begin();
               it != this->participants_.end(); ++it)
            if (*it == &entry) { this->participants_.erase(it); return; }
          assert(false && "Participant was not enrolled");
        }
        template <typename FlowT> friend class FlowSlot;

        void releaseCallbacks()
        {
          for (size_t i = 0; i < callbacks_.size(); ++i)
          {
            callbacks_[i]->unbind();
            callbacks_[i]->invalidate();
            delete callbacks_[i];
          }
          callbacks_.clear();
        }

        struct ContextScope
        {
          ComposableNode *node;
          ComponentContext *previous;
          Node *previousOwner;
          IStateOwner *previousStateOwner;
          ContextScope(ComposableNode *n, ComponentContext *ctx)
              : node(n),
                previous(n->currentContext_),
                previousOwner(0),
                previousStateOwner(0)
          {
            node->currentContext_ = ctx;
            if (node->currentContext_)
            {
              previousOwner = node->currentContext_->owner();
              node->currentContext_->setOwner(n);
              previousStateOwner = node->currentContext_->stateOwner();
              IStateOwner *currentOwner = n->asStateOwner();
              node->currentContext_->setStateOwner(currentOwner ? currentOwner : previousStateOwner);
            }
          }
          ~ContextScope()
          {
            node->currentContext_ = previous;
            if (previous && previousOwner)
            {
              previous->setOwner(previousOwner);
            }
            if (previous)
            {
              previous->setStateOwner(previousStateOwner);
            }
          }
        };

        ComponentContext *currentContext_;
        IStateOwner *nodeStateOwner_;
        NodeComposition composition_;
      private:
        BindingToken bindingToken_;
      protected:
        bool isAttached_;
        AttachedContext attached_;
        friend class ComposableNodeTestAccess;
        std::vector<CallbackEntryBase *> callbacks_;
        std::vector<Participant *> participants_;
      };

      /** Derived registration, sharing the ordered connect/release protocol
          with NodeStateRegistration and completed state batches. */
      template <typename T>
      struct NodeDerivedStateRegistration : public ComposableNode::NodeStateRegistrationBase
      {
        NodeDerivedStateRegistration(DerivedNodeState<T> *out,
                                     const detail::NodeStateDependency &dep1,
                                     const detail::NodeStateDependency &dep2,
                                     typename loka::core::DerivedState<T>::EvalFn *eval)
            : out_(out), dep1_(dep1), dep2_(dep2), eval_(eval), owner_(0), state_(0) {}
        virtual ~NodeDerivedStateRegistration() { delete this->eval_; }
        bool matches(const void *out) const { return this->out_ == out; }
        size_t pendingArenaBytes() const
        {
          return (this->out_ && !this->out_->isValid())
                     ? StateBatchBase::ArenaBytesForDerivedState<T>()
                     : 0;
        }

        void connect(IStateOwner *owner)
        {
          if (this->out_->isValid())
          {
            assert(this->out_->owner_ == owner && "Node-local state reattached to a different owner");
            this->owner_ = this->out_->owner_;
            this->state_ = this->out_->state_;
            return;
          }
          IStateOwner *owner1 = 0;
          IStateOwner *owner2 = 0;
          loka::core::StateBase *dep1 = this->dep1_.resolve(owner1);
          loka::core::StateBase *dep2 = this->dep2_.resolve(owner2);
          if (!owner || !owner->stateStorageOwner() || !this->eval_ ||
              !dep1 || owner1 != owner ||
              (this->dep2_.isPresent() && (!dep2 || owner2 != owner)))
          {
            if (owner)
              owner->noteStateAllocationFailure();
            return;
          }
          loka::core::DerivedState<T> *state = 0;
          const size_t align = detail::AlignOf<loka::core::DerivedState<T> >::value;
          void *mem = owner->allocateStateMemory(sizeof(loka::core::DerivedState<T>), align);
          if (mem)
          {
            state = this->dep2_.isPresent()
                ? new (mem) loka::core::DerivedState<T>(dep1, dep2, this->eval_)
                : new (mem) loka::core::DerivedState<T>(dep1, this->eval_);
            state->setArenaAllocated(true);
            owner->registerStateMemory(state, &StateBatchBase::DestroyDerivedState<T>);
          }
          else
          {
            state = this->dep2_.isPresent()
                ? loka::core::LokaNew<loka::core::DerivedState<T> >(
                      HeapStateAllocationSite(), dep1, dep2, this->eval_)
                : loka::core::LokaNew<loka::core::DerivedState<T> >(
                      HeapStateAllocationSite(), dep1, this->eval_);
          }
          if (!state)
          {
            owner->noteStateAllocationFailure();
            return;
          }
          if (!state->isArenaAllocated())
          {
            assert(static_cast<void *>(static_cast<loka::core::StateBase *>(state)) ==
                       static_cast<void *>(state) &&
                   "gate frees through StateBase; its subobject must sit at the storage address");
            state->setGateAllocated(true);
          }
          this->eval_ = 0;
          owner->adoptStateUnchecked(state);
          this->owner_ = owner;
          this->state_ = state;
          *this->out_ = DerivedNodeState<T>(state, owner);
        }

        void disconnect()
        {
          if (this->owner_ && this->state_)
            this->owner_->releaseState(this->state_);
          this->owner_ = 0;
          this->state_ = 0;
          this->out_ = 0;
        }

      private:
        DerivedNodeState<T> *out_;
        detail::NodeStateDependency dep1_;
        detail::NodeStateDependency dep2_;
        typename loka::core::DerivedState<T>::EvalFn *eval_;
        IStateOwner *owner_;
        loka::core::State<T> *state_;
      };

      template <class NodeT>
      inline void BindingToken::action(loka::core::EmitterState &emitter,
                                       NodeT *node, void (NodeT::*method)())
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        assert(this->owner_ && "bindings are declared only inside declareBindings");
#endif
        if (!this->owner_)
        {
          return;
        }
        this->owner_->bindActionForUi(emitter, node, method);
      }

      template <class E, int N, class NodeT>
      inline void BindingToken::actions(CommandSet<E, N> &set, NodeT *node,
                                        void (NodeT::*method)(E))
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        assert(this->owner_ && "bindings are declared only inside declareBindings");
#endif
        if (!this->owner_)
        {
          return;
        }
        for (unsigned i = 0; i < set.count(); ++i)
          this->owner_->bindCommandForUi(*set.emitter(E(i)), node, method, E(i));
      }

      template <class StateT, class NodeT>
      inline void BindingToken::watch(StateT &state, NodeT *node,
                                      void (NodeT::*method)(), bool callImmediately)
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        assert(this->owner_ && "bindings are declared only inside declareBindings");
#endif
        if (!this->owner_)
        {
          return;
        }
        this->owner_->watchStateForUi(state, node, method, callImmediately);
      }

    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_NODE_COMPOSABLENODE_HPP
