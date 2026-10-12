#ifndef LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_ARENA_HPP
#define LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_ARENA_HPP

#include <cassert>
#include <new>
#include <vector>
#include "app/scene/Node.hpp"
#include "app/scene/boundary/detail/ReclaimScratch.hpp"
#include "app/scene/boundary/detail/NodePartition.hpp"
#include "app/scene/detail/ArenaMath.hpp"
#include "core/Held.hpp"
#include "core/LokaAlloc.hpp"
#include "core/State.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace detail
      {
        // NodeArena: pre-allocated memory block for batch node creation
        class NodeArena
        {
        public:
          struct RetiredNodeGeneration
          {
            RetiredNodeGeneration()
                : buffer(0),
                  raw(0)
            {
            }

            char *buffer;
            char *raw;
            std::vector<Node *> nodes;
            /** Heap subtree roots retired alongside this generation; deleted
                first, while the ledger they may reference is still alive. */
            std::vector<Node *> heapRoots;
          };

          NodeArena()
              : buffer_(0),
                raw_(0),
                size_(0),
                offset_(0)
          {
          }
          ~NodeArena()
          {
            clear();
          }

          /** An arena reservation refusal is storage-strategy degradation,
              not a logical materialization failure. Only a refusal to
              materialize at BOTH doors — the arena and the final heap door —
              becomes a compose failure (#132 ruling 3). */
          void reserve(size_t totalSize)
          {
            clear();
            if (totalSize > 0)
            {
              // Allocate with extra padding and align the base pointer.
              const size_t kArenaAlign = 16;
              size_t rawSize = totalSize + kArenaAlign;
              raw_ = static_cast<char *>(loka::core::LokaAllocRaw(rawSize, slabSite()));
              if (!raw_)
              {
                return;
              }
              size_t rawAddr = reinterpret_cast<size_t>(raw_);
              size_t alignedAddr = (rawAddr + (kArenaAlign - 1)) & ~(kArenaAlign - 1);
              buffer_ = reinterpret_cast<char *>(alignedAddr);
              size_ = rawSize - (alignedAddr - rawAddr);
              offset_ = 0;
            }
          }

          void *allocate(size_t size, size_t align)
          {
            if (!buffer_ || size == 0)
            {
              return 0;
            }
            align = detail::NormalizeArenaAlign(align);
            // Align the offset
            size_t mask = align - 1;
            size_t aligned = (offset_ + mask) & ~mask;
            if (aligned + size > size_)
            {
              return 0; // Arena full
            }
            void *ptr = buffer_ + aligned;
            offset_ = aligned + size;
            return ptr;
          }

          void registerNode(Node *node)
          {
            if (node)
            {
              node->setArenaOwner(this);
              nodes_.push_back(node);
            }
          }

          /** Tombstones and destroys one arena node without moving the ledger. */
          bool releaseNode(Node *node)
          {
            if (!node)
            {
              return false;
            }
            assert(node->arenaOwner() == this && "node released to a foreign arena ledger");
            for (size_t i = 0; i < nodes_.size(); ++i)
            {
              if (nodes_[i] == node)
              {
                nodes_[i] = 0;
                DestroyNodeInPlace(node);
                return true;
              }
            }
            return false;
          }

          void clear()
          {
            RetiredNodeGeneration gen;
            if (detachRetiredGeneration(gen))
            {
              destroyRetiredGeneration(gen);
            }
          }

          bool detachRetiredGeneration(RetiredNodeGeneration &out)
          {
            if (!buffer_ && !raw_ && nodes_.empty())
            {
              return false;
            }

            out.buffer = buffer_;
            out.raw = raw_;
            out.nodes.swap(nodes_);
            buffer_ = 0;
            raw_ = 0;
            size_ = 0;
            offset_ = 0;
            nodes_.clear();
            return true;
          }

          /** Cold provisioning for explicit drains only, including Boundary drains. */
          bool reserveReclaimScratch(size_t nodes)
          {
            return this->reclaimScratch_.reserve(nodes);
          }
          ReclaimScratch &reclaimScratch()
          {
            return this->reclaimScratch_;
          }

          /** Read-only preflight over this generation's ledger and heap edges.
              Heap destructors precede reverse-creation-order arena destructors,
              preserving the existing provider/borrower lifetime contract. */
          static bool planRetiredGeneration(RetiredNodeGeneration &gen, ReclaimScratch::Plan &plan)
          {
            for (size_t i = 0; i < gen.heapRoots.size(); ++i)
              if (!plan.append(gen.heapRoots[i]))
                return false;
            for (size_t i = 0; i < gen.nodes.size(); ++i)
            {
              Node *node = gen.nodes[i];
              INestable *nestable = node && !node->asBoundary() ? node->asNestable() : 0;
              for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
                if (!child->isArenaAllocated() && !plan.append(child))
                  return false;
            }
            for (size_t i = gen.nodes.size(); i > 0; --i)
              if (!plan.appendLeaf(gen.nodes[i - 1]))
                return false;
            return true;
          }

          /** Explicit reclaim: O(ledger + heap edges), bounded before mutation.
              On overflow the caller retains the complete generation for retry.
              Partition leaves keep their edges for their own landlord's plan. */
          static bool destroyRetiredGeneration(RetiredNodeGeneration &gen, ReclaimScratch &scratch,
                                               void (*returned)(Node *, void *) = 0, void *owner = 0)
          {
            ReclaimScratch::Plan plan(scratch, ReclaimScratch::Plan::HEAP_CHILDREN);
            if (!planRetiredGeneration(gen, plan))
              return false;
            plan.severChildren();
            for (size_t i = 0; i < plan.count(); ++i)
            {
              Node *node = plan.node(i);
              if (node->isArenaAllocated())
                DestroyNodeInPlace(node);
              else
                NodePartition::reclaimDetached(node, returned, owner);
            }
            gen.heapRoots.clear();
            freeGeneration(gen);
            // Arena identities complete only after their backing is returned.
            if (returned)
              for (size_t i = 0; i < gen.nodes.size(); ++i)
                if (gen.nodes[i]) returned(gen.nodes[i], owner);
            gen.nodes.clear();
            return true;
          }

          static void destroyRetiredGeneration(RetiredNodeGeneration &gen,
                                               void (*returned)(Node *, void *) = 0, void *owner = 0)
          {
            // Heap roots go first, while every arena node they may still
            // reference is alive: their destructors skip arena children.
            for (size_t i = 0; i < gen.heapRoots.size(); ++i)
            {
              // Non-arena roots retain their heap or partition provenance.
              NodePartition::reclaimDetached(gen.heapRoots[i], returned, owner);
            }
            gen.heapRoots.clear();
            // Sever this landlord's child edges while the whole ledger is alive.
            std::vector<Node *> detachedChildren;
            std::vector<Node *> detachedHeapRoots;
            for (size_t i = 0; i < gen.nodes.size(); ++i)
            {
              Node *node = gen.nodes[i];
              // A nested landlord owns its child storage. Leave those edges for
              // its virtual destructor below: non-arena does not imply heap
              // inside a Boundary (its children may occupy a NodePartition).
              if (node && node->asBoundary())
              {
                continue;
              }
              INestable *nestable = node ? node->asNestable() : 0;
              if (nestable)
              {
                nestable->detachChildrenTo(detachedChildren);
                for (size_t childIndex = 0; childIndex < detachedChildren.size(); ++childIndex)
                {
                  Node *child = detachedChildren[childIndex];
                  if (child && !child->isArenaAllocated())
                  {
                    detachedHeapRoots.push_back(child);
                  }
                }
              }
            }
            for (size_t i = 0; i < detachedHeapRoots.size(); ++i)
            {
              // Detached non-arena roots include partition-backed generations.
              NodePartition::reclaimDetached(detachedHeapRoots[i], returned, owner);
            }
            // Creation is parent-first, so reverse order destroys children first.
            for (size_t i = gen.nodes.size(); i > 0; --i)
            {
              Node *node = gen.nodes[i - 1];
              if (node)
              {
                DestroyNodeInPlace(node);
              }
            }
            freeGeneration(gen);
            if (returned)
              for (size_t i = 0; i < gen.nodes.size(); ++i)
                if (gen.nodes[i]) returned(gen.nodes[i], owner);
            gen.nodes.clear();
          }

          bool hasCapacity() const
          {
            return buffer_ != 0;
          }

        private:
          static void freeGeneration(RetiredNodeGeneration &gen)
          {
            if (gen.raw)
            {
              loka::core::LokaFreeRaw(gen.raw, slabSite());
            }
            else
            {
              delete[] gen.buffer;
            }
            gen.buffer = 0;
            gen.raw = 0;
          }
          ReclaimScratch reclaimScratch_;
          /** One static gate site per slab kind: reserve() and
              destroyRetiredGeneration() must free through the tags the
              acquisition used so the audit ledger balances. */
          static const loka::core::LokaAllocationSite &slabSite()
          {
            static const loka::core::LokaAllocationSite site("NodeArena", "slab");
            return site;
          }

          char *buffer_;
          char *raw_;
          size_t size_;
          size_t offset_;
          std::vector<Node *> nodes_;
        };
      } // namespace detail

      typedef detail::NodeArena NodeArena;

      class StateArena
      {
      public:
        StateArena()
            : first_(0),
              tail_(0),
              states_(),
              heldBlocks_(0)
        {
        }
        ~StateArena()
        {
          clear();
        }

        /** Ensures one owner-lifetime allocation batch has enough arena
            capacity. allocate() consumes existing capacity only; it never
            grows implicitly for unreserved node-local state. An arena
            reservation refusal is storage-strategy degradation, not a logical
            materialization failure. Only a refusal to materialize at BOTH
            doors — the arena and the final heap door — becomes a compose
            failure (#132 ruling 3). */
        void reserve(size_t totalSize)
        {
          if (totalSize == 0)
          {
            return;
          }
          if (!tail_)
          {
            appendBlock(totalSize);
            return;
          }
          if (remainingCapacity(*tail_) < totalSize)
          {
            appendBlock(totalSize);
            return;
          }
        }

        /** Serves allocations from the tail block only. Leftover capacity in
            earlier blocks is intentionally stranded, not tracked in a free
            list, for locality and simplicity. */
        void *allocate(size_t size, size_t align)
        {
          if (size == 0)
          {
            return 0;
          }
          if (!tail_)
          {
            return 0;
          }
          void *ptr = allocateFromBlock(*tail_, size, align);
          if (ptr)
          {
            return ptr;
          }
          return 0;
        }

        void registerState(loka::core::StateBase *state, void (*destroy)(loka::core::StateBase *))
        {
          if (state && destroy)
          {
            StateEntry entry;
            entry.state = state;
            entry.destroy = destroy;
            states_.push_back(entry);
          }
        }

        void releaseState(loka::core::StateBase *state)
        {
          if (!state)
          {
            return;
          }
          for (size_t i = 0; i < states_.size();)
          {
            if (states_[i].state == state)
            {
              if (states_[i].destroy)
              {
                states_[i].destroy(states_[i].state);
              }
              states_.erase(states_.begin() + i);
            }
            else
            {
              ++i;
            }
          }
        }

        /** Registers a Held block without allocating a second ledger row.
            The block itself carries the intrusive arena link, so all control
            metadata remains inside the creating owner's resident bytes. */
        void registerHeld(loka::core::detail::HeldBlockBase *block)
        {
          if (!block)
          {
            return;
          }
          assert(!block->arenaNext() &&
                 "a Held block may have only one arena landlord");
          block->setArenaNext(this->heldBlocks_);
          this->heldBlocks_ = block;
        }

        void clear()
        {
          for (size_t i = 0; i < states_.size(); ++i)
          {
            if (states_[i].destroy)
            {
              states_[i].destroy(states_[i].state);
            }
          }
          states_.clear();
          while (this->heldBlocks_)
          {
            loka::core::detail::HeldBlockBase *block = this->heldBlocks_;
            this->heldBlocks_ = block->arenaNext();
            block->setArenaNext(0);
            // Block audit precedes slab release: live slots or a missing
            // retire-pool releaser fail while the bytes are still readable.
            block->destroyStorage();
          }
          clearBlocks();
        }

        bool hasBlocks() const
        {
          return tail_ != 0;
        }

      private:
        struct Block
        {
          Block()
              : buffer(0),
                raw(0),
                size(0),
                offset(0),
                next(0)
          {
          }

          char *buffer;
          char *raw;
          size_t size;
          size_t offset;
          Block *next;
        };

        struct StateEntry
        {
          StateEntry()
              : state(0),
                destroy(0)
          {
          }
          loka::core::StateBase *state;
          void (*destroy)(loka::core::StateBase *);
        };

        static size_t remainingCapacity(const Block &block)
        {
          return block.offset < block.size ? (block.size - block.offset) : 0;
        }

        static void *allocateFromBlock(Block &block, size_t size, size_t align)
        {
          if (!block.buffer)
          {
            return 0;
          }
          align = detail::NormalizeArenaAlign(align);
          size_t mask = align - 1;
          size_t aligned = (block.offset + mask) & ~mask;
          if (aligned + size > block.size)
          {
            return 0;
          }
          void *ptr = block.buffer + aligned;
          block.offset = aligned + size;
          return ptr;
        }

        void appendBlock(size_t totalSize)
        {
          const size_t kArenaAlign = 16;
          Block *block = loka::core::LokaNew<Block>(blockSite());
          if (!block)
          {
            return;
          }
          size_t rawSize = totalSize + kArenaAlign;
          block->raw = static_cast<char *>(loka::core::LokaAllocRaw(rawSize, slabSite()));
          if (!block->raw)
          {
            loka::core::LokaDelete(block, blockSite());
            return;
          }
          size_t rawAddr = reinterpret_cast<size_t>(block->raw);
          size_t alignedAddr = (rawAddr + (kArenaAlign - 1)) & ~(kArenaAlign - 1);
          block->buffer = reinterpret_cast<char *>(alignedAddr);
          block->size = rawSize - (alignedAddr - rawAddr);
          if (!first_)
          {
            first_ = block;
          }
          else
          {
            tail_->next = block;
          }
          tail_ = block;
        }

        void clearBlocks()
        {
          Block *block = first_;
          while (block)
          {
            Block *next = block->next;
            loka::core::LokaFreeRaw(block->raw, slabSite());
            loka::core::LokaDelete(block, blockSite());
            block = next;
          }
          first_ = 0;
          tail_ = 0;
        }

        /** One static gate site per acquisition kind: appendBlock() and
            clearBlocks() must free through the tags the acquisition used so
            the audit ledger balances. */
        static const loka::core::LokaAllocationSite &blockSite()
        {
          static const loka::core::LokaAllocationSite site("StateArena", "Block");
          return site;
        }

        static const loka::core::LokaAllocationSite &slabSite()
        {
          static const loka::core::LokaAllocationSite site("StateArena", "slab");
          return site;
        }

        Block *first_;
        Block *tail_;
        std::vector<StateEntry> states_;
        loka::core::detail::HeldBlockBase *heldBlocks_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARY_ARENA_HPP
