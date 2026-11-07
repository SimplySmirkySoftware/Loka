#ifndef LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_OBSERVED_STATE_HPP
#define LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_OBSERVED_STATE_HPP

#include <vector>
#include "app/scene/boundary/PaintBaselineStats.hpp"
#include "app/scene/Node.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "platform/debug/DebugLog.hpp"

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class BoundaryObservedStateTestAccess;
    }
  } // namespace dsl

  namespace app
  {
    namespace scene
    {
      class BoundaryNode;

      /** Noncopyable owner route shared by State subscriptions and own input.
          Node storage outlives unlink; clearing a route never reclaims nodes. */
      struct ObservedUseChain
      {
        ObservedUseChain()
            : boundary(0),
              uses(0)
        {
        }

        void link(ObservedUse &use, DirtySourceKey source, Node *node)
        {
          use.source = source;
          use.node = node;
          use.subscription = this;
          use.nextSubscription = this->uses;
          this->uses = &use;
#ifdef TEST_BUILD
          ++testing::paintBaselineStats().observedUses;
#endif
        }

        void unlink(ObservedUse &use)
        {
          ObservedUse **link = &this->uses;
          while (*link && *link != &use)
            link = &(*link)->nextSubscription;
          assert(*link == &use);
          *link = use.nextSubscription;
          clearUse(use);
        }

        void prune(unsigned long generation)
        {
          ObservedUse **link = &this->uses;
          while (*link)
          {
            ObservedUse *use = *link;
            if (use->generation == generation)
              link = &use->nextSubscription;
            else
            {
              *link = use->nextSubscription;
              clearUse(*use);
            }
          }
        }

        void clear()
        {
          while (this->uses)
          {
            ObservedUse *use = this->uses;
            this->uses = use->nextSubscription;
            clearUse(*use);
          }
          this->boundary = 0;
        }

        BoundaryNode *boundary;
        ObservedUse *uses;

      private:
        ObservedUseChain(const ObservedUseChain &);
        ObservedUseChain &operator=(const ObservedUseChain &);
        static void clearUse(ObservedUse &use)
        {
#ifdef TEST_BUILD
          assert(testing::paintBaselineStats().observedUses != 0);
          --testing::paintBaselineStats().observedUses;
#endif
          use.source = DirtySourceKey::none();
          use.flags = NODE_DIRTY_NONE;
          use.generation = 0;
          use.nextSubscription = 0;
          use.subscription = 0;
        }
      };

      struct BoundaryObservedStateBinding
      {
        BoundaryObservedStateBinding()
            : chain(),
              state(0),
              flags(NODE_DIRTY_NONE),
              refs(1),
              stateLifetimeToken(0),
              changedThunk(0)
        {
        }

        ~BoundaryObservedStateBinding()
        {
          if (stateLifetimeToken)
          {
            loka::core::StateBase::releaseExternalLifetimeToken(stateLifetimeToken);
            stateLifetimeToken = 0;
          }
        }

        void retain()
        {
          ++refs;
        }

        bool release()
        {
          --refs;
          return refs == 0;
        }

        ObservedUseChain chain;
        loka::core::StateBase *state;
        NodeDirtyFlags flags;
        int refs;
        void *stateLifetimeToken;
        void (*changedThunk)(void *);
      };

      struct BoundaryObservedStateEntry
      {
        BoundaryObservedStateEntry()
            : state(0),
              flags(NODE_DIRTY_NONE),
              binding(0)
        {
        }
        loka::core::StateBase *state;
        NodeDirtyFlags flags;
        BoundaryObservedStateBinding *binding;
      };

      struct BoundaryObservedState
      {
        struct ObservedPassState
        {
          ObservedPassState()
              : generation(0)
          {
          }

          void begin()
          {
            ++generation;
            if (generation == 0)
            {
              generation = 1;
            }
          }

          unsigned long generation;
        };

        struct ObservedDirtyState
        {
          ObservedDirtyState()
              : dirtyFlags(NODE_DIRTY_NONE)
          {
          }

          void clear()
          {
            dirtyFlags = NODE_DIRTY_NONE;
          }

          void include(NodeDirtyFlags flagsToAdd)
          {
            if (flagsToAdd == NODE_DIRTY_NONE)
            {
              return;
            }
            dirtyFlags = static_cast<NodeDirtyFlags>(dirtyFlags | flagsToAdd);
          }

          NodeDirtyFlags value() const
          {
            return dirtyFlags;
          }

          NodeDirtyFlags dirtyFlags;
        };

        BoundaryObservedState()
            : pass(),
              dirty(),
              entries()
        {
        }

        void clearDirtyFlags()
        {
          dirty.clear();
        }

        NodeDirtyFlags currentDirtyFlags() const
        {
          return dirty.value();
        }

        void clearEntries(void (*changedThunk)(void *))
        {
          for (size_t i = 0; i < entries.size(); ++i)
          {
            releaseEntry(entries[i], changedThunk);
          }
          entries.clear();
          this->ownInputRoute.clear();
        }

        void forgetState(loka::core::StateBase *state,
                         void (*changedThunk)(void *))
        {
          if (!state)
          {
            return;
          }
          for (size_t i = 0; i < entries.size(); ++i)
          {
            if (entries[i].state != state)
            {
              continue;
            }
            releaseEntry(entries[i], changedThunk);
            entries.erase(entries.begin() + i);
            return;
          }
        }

        void beginPass()
        {
          pass.begin();
          for (size_t i = 0; i < entries.size(); ++i)
          {
            entries[i].flags = NODE_DIRTY_NONE;
            if (entries[i].binding)
            {
              entries[i].binding->flags = NODE_DIRTY_NONE;
            }
          }
        }

        /** Only a completed declaration can retire unvisited uses. A refused
            walk keeps live subscriptions available to request the next retry. */
        void finishPass(void (*changedThunk)(void *), bool refused)
        {
          if (refused)
          {
            // beginPass cleared scheduling contributions, but the incomplete
            // walk cannot replace the union of the still-live use set.
            for (size_t i = 0; i < entries.size(); ++i)
            {
              BoundaryObservedStateEntry &entry = entries[i];
              entry.flags = NODE_DIRTY_NONE;
              for (ObservedUse *use = entry.binding ? entry.binding->chain.uses : 0; use; use = use->nextSubscription)
                entry.flags = static_cast<NodeDirtyFlags>(entry.flags | use->flags);
              if (entry.binding) entry.binding->flags = entry.flags;
              dirty.include(entry.flags);
            }
            return;
          }
          this->ownInputRoute.prune(this->pass.generation);
          for (size_t i = 0; i < entries.size();)
          {
            BoundaryObservedStateBinding *binding = entries[i].binding;
            if (binding)
              binding->chain.prune(this->pass.generation);
            if (binding && binding->chain.uses)
            {
              ++i;
              continue;
            }
            releaseEntry(entries[i], changedThunk);
            entries.erase(entries.begin() + i);
          }
        }

        /** Called by the node's synchronous withdrawal door. Identity chooses
            the embedded route or the State subscription; only the latter can
            release an entry. */
        void withdraw(ObservedUse &use)
        {
          ObservedUseChain *chain = use.subscription;
          if (!chain)
            return;
          chain->unlink(use);
          if (chain == &this->ownInputRoute || chain->uses)
            return;
          for (size_t i = 0; i < this->entries.size(); ++i)
          {
            BoundaryObservedStateBinding *binding = this->entries[i].binding;
            if (!binding || &binding->chain != chain)
              continue;
            releaseEntry(this->entries[i], binding->changedThunk);
            this->entries.erase(this->entries.begin() + i);
            return;
          }
        }

        bool registerOwnInput(BoundaryNode *boundary, Node *node, NodeDirtyFlags flags)
        {
          if (!boundary || !node || flags == NODE_DIRTY_NONE || !DirtySourceRegistrar::acceptsOwnInput(flags))
            return true;
          ObservedUse *use = node->uses_.reserve(DirtySourceKey::ownInput());
          if (!use)
            return false;
          this->ownInputRoute.boundary = boundary;
          this->registerUse(*use, this->ownInputRoute, DirtySourceKey::ownInput(), node, flags);
          return true;
        }

        void addDirtyFlags(NodeDirtyFlags flagsToAdd)
        {
          dirty.include(flagsToAdd);
        }

        bool registerState(BoundaryNode *boundary,
                           loka::core::StateBase *state,
                           NodeDirtyFlags flagsToAdd,
                           void (*changedThunk)(void *),
                           Node *node)
        {
          if (!boundary || !node || !state || flagsToAdd == NODE_DIRTY_NONE)
            return true;
          ObservedUse *use = node->uses_.reserve(DirtySourceKey::ofState(state));
          if (!use) return false;
          BoundaryObservedStateEntry *entry = 0;
          for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].state == state) { entry = &entries[i]; break; }
          if (!entry)
          {
            BoundaryObservedStateEntry added;
            added.state = state;
            added.binding = new BoundaryObservedStateBinding();
            added.binding->chain.boundary = boundary;
            added.binding->state = state;
            added.binding->changedThunk = changedThunk;
            added.binding->stateLifetimeToken = state->retainExternalLifetimeToken();
            state->bind(changedThunk, added.binding, false, false, 0);
            entries.push_back(added);
            entry = &entries.back();
          }
          this->registerUse(*use, entry->binding->chain, DirtySourceKey::ofState(state), node, flagsToAdd);
          addDirtyFlags(flagsToAdd);
          entry->flags = static_cast<NodeDirtyFlags>(entry->flags | flagsToAdd);
          entry->binding->flags = entry->flags;
          return true;
        }

        /** Returns false when commit identities are unavailable. A known set
            may yield NONE: none of its states has a current local observation. */
        bool dirtyFlagsForCommittedStates(const loka::core::PushStateTracker *pushTracker,
                                          NodeDirtyFlags &flags) const
        {
          flags = NODE_DIRTY_NONE;
          if (!pushTracker)
          {
            return false;
          }
          const loka::core::PushStateTracker::StateList *knownIdentities =
              pushTracker->committedSet().knownIdentities();
          if (!knownIdentities)
          {
            return false;
          }
          const loka::core::PushStateTracker::StateList &dirtyStates = *knownIdentities;
          for (size_t stateIndex = 0; stateIndex < dirtyStates.size(); ++stateIndex)
          {
            loka::core::StateBase *dirtyState = dirtyStates[stateIndex];
            NodeDirtyFlags stateFlags = NODE_DIRTY_NONE;
            for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
            {
              if (entries[entryIndex].state == dirtyState)
              {
                stateFlags = static_cast<NodeDirtyFlags>(stateFlags | entries[entryIndex].flags);
              }
            }
            if (stateFlags == NODE_DIRTY_NONE)
            {
              loka::platform::DebugLogUnobservedState(dirtyState);
            }
            flags = static_cast<NodeDirtyFlags>(flags | stateFlags);
          }
          return true;
        }

      private:
        void
        registerUse(ObservedUse &use, ObservedUseChain &chain, DirtySourceKey source, Node *node, NodeDirtyFlags flags);

        static void releaseEntry(BoundaryObservedStateEntry &entry,
                                 void (*changedThunk)(void *))
        {
          if (entry.binding)
            entry.binding->chain.clear();
          if (entry.state && entry.binding)
          {
            if (entry.binding->stateLifetimeToken &&
                loka::core::StateBase::isExternalLifetimeTokenAlive(
                    entry.binding->stateLifetimeToken))
            {
              entry.state->unbind(changedThunk, entry.binding);
            }
          }
          if (entry.binding)
          {
            entry.binding->chain.boundary = 0;
            entry.binding->state = 0;
            entry.binding->flags = NODE_DIRTY_NONE;
            if (entry.binding->release())
            {
              delete entry.binding;
            }
          }
          entry.state = 0;
          entry.flags = NODE_DIRTY_NONE;
          entry.binding = 0;
        }

        ObservedUseChain ownInputRoute;
        ObservedPassState pass;
        ObservedDirtyState dirty;
        std::vector<BoundaryObservedStateEntry> entries;

        friend class ::loka::dsl::testing::BoundaryObservedStateTestAccess;
      };

    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_OBSERVED_STATE_HPP
