#include "Boundary.hpp"
#include "core/LokaAlloc.hpp"
#include "app/scene/Scene.hpp"
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
#include "platform/debug/DebugLog.hpp"
#endif

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace
      {
        const loka::core::LokaAllocationSite StagedDeclarationSite("BoundaryBranchSeat", "StagedDeclaration");
      }

      struct BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::Entry
      {
        explicit Entry(IBranchSeatDefinition *seatValue)
            : seat(seatValue), declaration(), next(0) {}
        IBranchSeatDefinition *seat;
        loka::core::OwnedDef<BranchSeatDeclaration> declaration;
        Entry *next;
      };

      BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::StagedDeclarationChain()
          : head_(0), tail_(0) {}

      BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::~StagedDeclarationChain()
      {
        this->clear();
      }

      bool BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::push(
          IBranchSeatDefinition *seat, loka::core::OwnedDef<BranchSeatDeclaration> &candidate)
      {
        Entry *entry = loka::core::LokaNew<Entry>(StagedDeclarationSite, seat);
        if (!entry) return false;
        entry->declaration.reset(candidate.take());
        if (this->tail_)
          this->tail_->next = entry;
        else
          this->head_ = entry;
        this->tail_ = entry;
        return true;
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::spliceTo(
          StagedDeclarationChain &target)
      {
        if (this == &target || !this->head_) return;
        if (target.tail_)
          target.tail_->next = this->head_;
        else
          target.head_ = this->head_;
        target.tail_ = this->tail_;
        this->head_ = 0;
        this->tail_ = 0;
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::clear()
      {
        while (this->head_)
        {
          Entry *entry = this->head_;
          this->head_ = entry->next;
          loka::core::LokaDelete(entry, StagedDeclarationSite);
        }
        this->tail_ = 0;
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::StagedDeclarationChain::commit()
      {
        for (Entry *entry = this->head_; entry; entry = entry->next)
          entry->seat->commitBranchDeclaration(entry->declaration.take());
        this->clear();
      }

      BoundaryBranchSeatRuntimeRegistrationPlan::BoundaryBranchSeatRuntimeRegistrationPlan()
          : declarations_(), entries_() {}

      BoundaryBranchSeatRuntimeRegistrationPlan::~BoundaryBranchSeatRuntimeRegistrationPlan() {}

      bool BoundaryBranchSeatRuntimeRegistrationPlan::stageDeclaration(
          IBranchSeatDefinition *seat, loka::core::OwnedDef<BranchSeatDeclaration> &candidate)
      {
        return this->declarations_.push(seat, candidate);
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::appendTo(
          BoundaryBranchSeatRuntimeRegistrationPlan &target)
      {
        if (this == &target) return;
        for (size_t i = 0; i < this->entries_.size(); ++i)
          target.entries_.push_back(this->entries_[i]);
        this->entries_.clear();
        this->declarations_.spliceTo(target.declarations_);
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::clear()
      {
        this->entries_.clear();
        this->declarations_.clear();
      }

      void BoundaryBranchSeatRuntimeRegistrationPlan::commitTo(BoundaryBranchSeatState &state)
      {
        // Check every provisional result before publishing any declaration or row.
#ifndef NDEBUG
        for (size_t i = 0; i < this->entries_.size(); ++i)
          assert(this->entries_[i].active.complete());
#endif
        this->declarations_.commit();
        for (size_t i = 0; i < this->entries_.size(); ++i)
        {
          Entry &entry = this->entries_[i];
          state.registerRuntime(entry.plan, entry.parent, entry.active.root, entry.stateOwner);
        }
        this->entries_.clear();
      }

      void BoundaryNode::JoinSceneFocus(Scene &scene, Node &node)
      {
        if (node.lifecycleFact() == NODE_FACT_RETIRED) return;
        FocusRow *row = node.asFocusParticipant();
        if (row) scene.focus().join(*row);
      }

      void BoundaryNode::WithdrawCandidateBindings(Node *node)
      {
        if (!node) return;
        ComposableNode *composable = node->asComposable();
        if (composable) composable->releaseCallbacks();
        INestable *nestable = node->asNestable();
        for (Node *child = nestable ? nestable->childrenHead() : 0;
             child; child = child->nextInComposition)
          WithdrawCandidateBindings(child);
      }

      void BoundaryNode::RetireUnattachedCandidate(Node *root, void *data)
      {
        ComponentContext &context = *static_cast<ComponentContext *>(data);
        // Declaration bindings may already be live, even before ATTACH.
        // Withdraw them without invoking attach-resource detachNode hooks.
        WithdrawCandidateBindings(root);
        DropRetainedHeldSlots(root);
        context.boundary()->retireDetachedNode(context, root);
      }

      bool BoundaryNode::materializeInitialChildren(ComponentContext &context)
      {
        // A retained declaration can refuse observation storage before replay.
        // Do not start or publish a factory candidate in that refused window.
        if (this->compositionState_.allocationFailedValue()) return false;
        // A refused mount owns its slots until the ordinary clock drains them.
        // UPDATE may revisit the white flag before that drain; do not readmit it.
        if (!this->seatReservations_.empty()
            && (this->retiredSubtreesHead_ || !this->retiredGenerations_.empty()))
        {
          this->noteComposeAllocationFailure();
          return false;
        }
        NodeComposition &composition = this->composition();
        composition.setContext(&context);
        context.setComposition(&composition);
        const NodeMaterializationResult result = composition.createNodeTreeCompleted();
        PendingSubtree candidate(&RetireUnattachedCandidate, &context);
        candidate.prepare(result.root);
        const bool rejectedMaterialization = !result.complete();
        if (rejectedMaterialization)
        {
          candidate.reclaim();
          this->resetRejectedInitialChildren(context);
        }
        else
        {
          if (candidate.root())
          {
            Node *child = candidate.root();
            if (this->seatReservations_.empty())
              this->addChild(candidate.take());
            else
              candidate.prepare(candidate.take(), &ReclaimPendingSeatRoot, &context);
            this->composeTree(child, context, COMPOSE_EVENT_ATTACH, this);
            if (candidate.root() && !this->compositionState_.allocationFailedValue())
              this->addChild(candidate.take());
          }
        }
        const bool rejectedAttach = !rejectedMaterialization && candidate.root()
                                    && this->compositionState_.allocationFailedValue();
        if (rejectedAttach)
        {
          this->retireSeatBranchRoot(context, candidate.take());
          this->resetRejectedInitialChildren(context);
        }
        composition.setContext(0);
        context.setComposition(0);
        return !rejectedMaterialization && !rejectedAttach;
      }

      void BoundaryNode::resetRejectedInitialChildren(ComponentContext &context)
      {
        this->retireDeclarationScope(context, this->branchSeats_);
        this->forgetBranchSeatDirtySources(this->branchSeats_);
        this->branchSeats_.clearRuntime();
        // Remove plans appended by runtime nodes before disposing declarations.
        // The retained mount definition and its cold reservations survive replay.
        this->branchSeats_.capture(this->composition().root());
        const std::vector<BoundaryBranchSeatPlanEntry> &plans = this->branchSeats_.plans();
        for (size_t i = 0; i < plans.size(); ++i)
          plans[i].seat()->commitBranchDeclaration(0);
        this->seatReservations_.resetInitialBuildRequests();
        this->captureBranchSeatPlan();
        this->noteComposeAllocationFailure();
      }

      void BoundaryNode::destroyUncommittedLocalRebuildCandidates(
          BoundaryLocalRebuildPlan &plan)
      {
        for (size_t i = 0; i < plan.entries.size(); ++i)
        {
          BoundaryLocalRebuildPlanEntry &entry = plan.entries[i];
          const bool materialized =
              entry.action == BoundaryLocalRebuildPlanEntry::ACTION_ATTACH ||
              entry.action == BoundaryLocalRebuildPlanEntry::ACTION_REPLACE;
          const bool exclusivelyPlanOwned = entry.definition != 0;
          if (materialized && exclusivelyPlanOwned && entry.node &&
              entry.node->arenaOwner() == 0 && !entry.node->isPartitionAllocated())
          {
            DestroyHeapNode(entry.node);
            entry.node = 0;
          }
        }
      }

      void BoundaryNode::retireOwnedNodeGeneration(ComponentContext &context)
      {
        detail::NodeArena::RetiredNodeGeneration gen;
        std::vector<Node *> parkedBranches;
        this->parkedBranches_.detachAll(parkedBranches);
        for (size_t i = 0; i < parkedBranches.size(); ++i)
        {
          Node *branch = parkedBranches[i];
          Node::MarkSubtreeLifecycleFact(branch, NODE_FACT_RETIRED);
          if (context.platformController())
          {
            context.platformController()->releaseNodeContexts(branch);
          }
          this->retireSubtree(branch);
        }
        this->branchSeats_.clearRuntime();

        Node *keptHead = 0;
        Node *keptTail = 0;
        Node *retired = this->retiredSubtreesHead_;
        while (retired)
        {
          Node *next = retired->nextInComposition;
          retired->nextInComposition = 0;
          if (this->seatReservations_.partitionFor(retired))
          {
            // Partition backing stays with the landlord, never with a snapshot.
            if (keptTail)
              keptTail->nextInComposition = retired;
            else
              keptHead = retired;
            keptTail = retired;
          }
          else if (retired->arenaOwner() == this->nodeArena())
          {
            // Subsumed: the generation ledger already owns this corpse.
          }
          else if (retired->arenaOwner() == 0)
          {
            // Heap corpses ride the generation so their arena descendants and
            // the ledger slots those descendants occupy drain as one unit.
            gen.heapRoots.push_back(retired);
          }
          else
          {
            assert(false && "a boundary must not hold retired subtrees of a foreign arena");
            if (keptTail)
            {
              keptTail->nextInComposition = retired;
            }
            else
            {
              keptHead = retired;
            }
            keptTail = retired;
          }
          retired = next;
        }
        this->retiredSubtreesHead_ = keptHead;
        this->retiredSubtreesTail_ = keptTail;

        const bool hasArenaGeneration = this->nodeArena_.detachRetiredGeneration(gen);
        if (hasArenaGeneration || !gen.heapRoots.empty())
        {
          this->retiredGenerations_.push_back(gen);
        }

        Scene *scene = this->getScene();
        if (scene)
        {
          scene->requestRetiredSubtreeDrainAfterRun();
        }
      }

      void BoundaryNode::retireDetachedNode(ComponentContext &context, Node *node)
      {
        if (!node)
        {
          return;
        }
        this->seatReservations_.recordRetiringRoot(node);
        Node::MarkSubtreeLifecycleFact(node, NODE_FACT_RETIRED);
        if (context.platformController())
        {
          context.platformController()->releaseNodeContexts(node);
        }
        this->retireSubtree(node);
      }

      void BoundaryNode::retireSubtree(Node *node)
      {
        if (!node)
        {
          return;
        }
        assert(node->nextInComposition == 0 &&
               "retired subtree root must be detached before reusing its sibling link");
        if (this->retiredSubtreesTail_)
        {
          this->retiredSubtreesTail_->nextInComposition = node;
        }
        else
        {
          this->retiredSubtreesHead_ = node;
        }
        this->retiredSubtreesTail_ = node;

        Scene *scene = this->getScene();
        if (scene)
        {
          scene->requestRetiredSubtreeDrainAfterRun();
        }
      }

      void BoundaryNode::retireHeldBlock(
          loka::core::detail::HeldBlockBase *block)
      {
        if (!block)
        {
          return;
        }
        assert(block->releaseQueued() && !block->released() &&
               "Boundary retire pool accepts only newly unowned Held blocks");
        block->setRetireNext(0);
        if (this->pendingHeldReleasesTail_)
        {
          this->pendingHeldReleasesTail_->setRetireNext(block);
        }
        else
        {
          this->pendingHeldReleasesHead_ = block;
        }
        this->pendingHeldReleasesTail_ = block;

        Scene *scene = this->getScene();
        if (scene)
        {
          scene->requestRetiredSubtreeDrainAfterRun();
        }
      }

      void BoundaryNode::ReclaimPartitionNode(Node *node, void *owner)
      {
        static_cast<BoundaryNode *>(owner)->destroyRetiredSubtree(node);
      }

      void BoundaryNode::ReclaimBoundedPartitionNode(Node *node, void *owner)
      {
        static_cast<BoundaryNode *>(owner)->destroyRetiredSubtree(node, true);
      }

      void BoundaryNode::ReclaimPlannedNode(Node *node, void *owner)
      {
        static_cast<BoundaryNode *>(owner)->destroyRetiredNode(node);
      }

      void BoundaryNode::destroyRetiredSubtree(Node *node, bool bounded)
      {
        if (!node)
        {
          return;
        }

        detail::NodePartition *partition = this->seatReservations_.partitionFor(node);
        if (bounded && partition && partition->reclaimScratch_.isReserved()
            && partition->reclaimTree(node, &ReclaimPlannedNode, this))
          return;
        BoundaryNode *nestedBoundary = node->asBoundary();
        if (!nestedBoundary || nestedBoundary == this)
        {
          INestable *nestable = node->asNestable();
          if (nestable)
          {
            std::vector<Node *> children;
            nestable->detachChildrenTo(children);
            for (size_t i = 0; i < children.size(); ++i)
            {
              this->destroyRetiredSubtree(children[i], bounded);
            }
          }
        }

        if (partition)
        {
          if (nestedBoundary && nestedBoundary != this)
            nestedBoundary->drainPendingHeldReleases();
          partition->reclaimAfterChildren(
              node, bounded ? &ReclaimBoundedPartitionNode : &ReclaimPartitionNode, this);
          // The partition fallback bypasses destroyRetiredNode. Its dependents
          // and destructor have completed, and its slot is now on the free list.
          this->seatReservations_.returnedNode(node);
        }
        else
          this->destroyRetiredNode(node);
      }

      void BoundaryNode::destroyRetiredNode(Node *node)
      {
        BoundaryNode *nestedBoundary = node->asBoundary();
        if (nestedBoundary && nestedBoundary != this)
        {
          // This drain runs at the retiring parent's tick boundary, which is
          // the clock the deferral was queued for. A retired Boundary is no
          // longer in the live tree, so nothing else will reach its queue —
          // and running the releaser from its destructor instead would put
          // observable app code on the reclaim path.
          nestedBoundary->drainPendingHeldReleases();
        }
        detail::NodePartition *partition = this->seatReservations_.partitionFor(node);
        if (partition)
          partition->destroyResident(node);
        else if (node->isArenaAllocated())
        {
          assert(node->arenaOwner() == this->nodeArena() &&
                 "retired arena node must belong to the retiring Boundary arena");
          const bool released = this->nodeArena_.releaseNode(node);
          assert(released && "retired arena node must belong to the retiring Boundary ledger");
          (void)released;
        }
        else
        {
          // Non-arena retired node: gate-created (composition heap fallback)
          // or plain-new; DestroyHeapNode routes by provenance.
          DestroyHeapNode(node);
        }
        // Complete destruction includes provider dependents and nested landlords.
        // The comparison uses identity only; no destroyed Node is inspected.
        this->seatReservations_.returnedNode(node);
      }

      void BoundaryNode::drainRetiredSubtreesAtNextTrackerRun()
      {
        this->drainRetiredSubtrees(true);
      }

      void BoundaryNode::drainRetiredSubtrees(bool bounded)
      {
        if (this->drainingRetiredSubtrees_ ||
            (!this->retiredSubtreesHead_ &&
             this->retiredGenerations_.empty() &&
             !this->pendingHeldReleasesHead_))
        {
          return;
        }

        detail::ReclaimScratch &scratch = this->nodeArena_.reclaimScratch();
        const bool planned = bounded && scratch.isReserved();
        Node *snapshot = this->retiredSubtreesHead_;
        this->retiredSubtreesHead_ = 0;
        this->retiredSubtreesTail_ = 0;
        std::vector<detail::NodeArena::RetiredNodeGeneration> generationSnapshot;
        generationSnapshot.swap(this->retiredGenerations_);
        loka::core::detail::HeldBlockBase *heldSnapshot =
            this->pendingHeldReleasesHead_;
        this->pendingHeldReleasesHead_ = 0;
        this->pendingHeldReleasesTail_ = 0;
        this->drainingRetiredSubtrees_ = true;
        while (snapshot)
        {
          Node *next = snapshot->nextInComposition;
          snapshot->nextInComposition = 0;
          if (bounded && this->seatReservations_.partitionFor(snapshot))
            this->destroyRetiredSubtree(snapshot, true);
          else if (planned)
          {
            detail::ReclaimScratch::Plan plan(scratch);
            if (plan.append(snapshot))
            {
              plan.severChildren();
              for (size_t i = 0; i < plan.count(); ++i)
                this->destroyRetiredSubtree(plan.node(i), bounded);
            }
            else
              this->destroyRetiredSubtree(snapshot, bounded);
          }
          else
            this->destroyRetiredSubtree(snapshot, bounded);
          snapshot = next;
        }
        for (size_t i = 0; i < generationSnapshot.size(); ++i)
        {
          this->seatReservations_.reclaimGeneration(generationSnapshot[i], planned ? &scratch : 0);
        }
        generationSnapshot.clear();
        while (heldSnapshot)
        {
          loka::core::detail::HeldBlockBase *next =
              heldSnapshot->retireNext();
          heldSnapshot->setRetireNext(0);
          heldSnapshot->runReleaser();
          heldSnapshot = next;
        }
        this->drainingRetiredSubtrees_ = false;
        if (this->seatReservations_.hasWaitingRequests())
          this->markViewDirty(static_cast<NodeDirtyFlags>(NODE_DIRTY_CHILD | NODE_DIRTY_LAYOUT));
      }

      void BoundaryNode::drainPendingHeldReleases()
      {
        loka::core::detail::HeldBlockBase *snapshot =
            this->pendingHeldReleasesHead_;
        this->pendingHeldReleasesHead_ = 0;
        this->pendingHeldReleasesTail_ = 0;
        while (snapshot)
        {
          loka::core::detail::HeldBlockBase *next = snapshot->retireNext();
          snapshot->setRetireNext(0);
          snapshot->runReleaser();
          snapshot = next;
        }
      }

      void BoundaryNode::drainAllRetiredSubtrees()
      {
        while (this->retiredSubtreesHead_ ||
               !this->retiredGenerations_.empty() ||
               this->pendingHeldReleasesHead_)
        {
          this->drainRetiredSubtrees(false);
        }
      }

      void BoundaryNode::releaseOwnedNodeStorage()
      {
        this->drainAllRetiredSubtrees();
        std::vector<Node *> parkedBranches;
        this->parkedBranches_.detachAll(parkedBranches);
        for (size_t i = 0; i < parkedBranches.size(); ++i)
        {
          this->destroyRetiredSubtree(parkedBranches[i]);
        }
        std::vector<Node *> children;
        this->detachChildrenTo(children);
        for (size_t i = 0; i < children.size(); ++i)
        {
          // Follow the live owner tree before sweeping orphan partition roots.
          // An arena child can enclose several nested partition generations.
          this->destroyRetiredSubtree(children[i]);
        }
        this->seatReservations_.reclaimPartitionRoots(&ReclaimPartitionNode, this);
        // Legacy arena residents retain their existing ledger destruction order.
        this->nodeArena_.clear();
      }

      void BoundaryNode::completeComposeResult()
      {
        if (this->compositionState_.allocationFailedValue() ||
            this->compositionState_.boundaryPlanRequiredValue())
        {
          // Refuse projection and record recovery for the next external update.
          this->compositionState_.failCompose();
          Scene *scene = this->getScene();
          if (scene)
          {
            scene->noteComposeAllocationFailure();
          }
          return;
        }
        this->compositionState_.completeCompose();
      }

      Scene *BoundaryNode::admitViewUpdate() const
      {
        return this->isFrozen() ? 0 : this->getScene();
      }

      void BoundaryNode::markViewDirty(NodeDirtyFlags flags)
      {
        Scene *scene = this->admitViewUpdate();
        if (scene)
          scene->requestBoundaryUpdate(this, flags, this->flushViewDirtyImmediately(flags));
      }

      void BoundaryNode::requestOwnInputUpdate(NodeDirtyFlags flags)
      {
        Scene *scene = this->admitViewUpdate();
        if (scene)
          scene->requestBoundaryUpdate(this, flags, false);
      }

      void BoundaryNode::InvalidateSceneThunk(void *userData)
      {
        BoundaryNode *self = static_cast<BoundaryNode *>(userData);
        if (!self)
        {
          return;
        }
        Scene *scene = self->getScene();
        if (scene)
        {
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
          loka::platform::DebugLogSceneUpdateTracked(static_cast<void *>(self), static_cast<void *>(scene));
#endif
          NodeDirtyFlags flags = NODE_DIRTY_NONE;
          if (!self->observedDirtyFlagsForCommittedStates(flags))
          {
            flags = self->observedDirtyFlags();
          }
          if (flags == NODE_DIRTY_NONE)
          {
            return;
          }
          self->markViewDirty(flags);
        }
      }

      void BoundaryNode::ObservedStateChangedThunk(void *userData)
      {
        BoundaryObservedStateBinding *binding = static_cast<BoundaryObservedStateBinding *>(userData);
        if (!binding || !binding->chain.boundary)
        {
          return;
        }
        for (ObservedUse *use = binding->chain.uses; use; use = use->nextSubscription)
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(use->node->lifecycleFact() != NODE_FACT_RETIRED);
#endif
          use->node->uses_.include(use->flags);
        }
        if (binding->state && binding->state->trackerOwner() == binding->chain.boundary->tracker())
        {
          return;
        }
        if (!binding->chain.boundary->getScene())
        {
          return;
        }
        NodeDirtyFlags flags = binding->flags;
        if (flags == NODE_DIRTY_NONE)
        {
          return;
        }
        loka::core::StateTracker *ownerTracker = binding->state ? binding->state->trackerOwner() : 0;
        // Same suppression as the own-tracker line above, one hop wider: an
        // inner owner's transaction commits into this Boundary, so its commit
        // is already the invalidation. Marking here too would apply twice.
        loka::core::PushStateTracker *pushOwner =
            ownerTracker ? ownerTracker->asPushTracker() : 0;
        if (pushOwner && pushOwner->invalidatesTarget(binding->chain.boundary))
        {
          return;
        }
        if (ownerTracker && ownerTracker->phase() != loka::core::TRACKER_IDLE)
        {
          binding->retain();
          ownerTracker->defer(loka::core::TrackerDeferKey(), &BoundaryNode::ObservedStateDeferredInvalidateThunk, binding);
          return;
        }
        binding->chain.boundary->markViewDirty(flags);
      }

      void BoundaryNode::ObservedStateDeferredInvalidateThunk(void *userData)
      {
        BoundaryObservedStateBinding *binding = static_cast<BoundaryObservedStateBinding *>(userData);
        if (!binding)
        {
          return;
        }
        if (!binding->chain.boundary)
        {
          if (binding->release())
          {
            delete binding;
          }
          return;
        }
        NodeDirtyFlags flags = binding->flags;
        if (flags == NODE_DIRTY_NONE)
        {
          if (binding->release())
          {
            delete binding;
          }
          return;
        }
        binding->chain.boundary->markViewDirty(flags);
        if (binding->release())
        {
          delete binding;
        }
      }
    } // namespace scene
  } // namespace app
} // namespace loka
