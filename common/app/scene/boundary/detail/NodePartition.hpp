#ifndef LOKA_NODE_PARTITION_HPP
#define LOKA_NODE_PARTITION_HPP

#include "app/scene/Node.hpp"
#include <cstring>
#include "app/scene/boundary/detail/ReclaimScratch.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class BoundaryNode;
      namespace detail
      {

        /** Target-sized storage facts. Equal size/alignment pairs form one class;
            count is a capacity, not a runtime growth hint. */
        class NodeSlotLayout
        {
        public:
          /** Empty workspace slot; normalization refuses it until populated. */
          NodeSlotLayout()
              : size_(0),
                alignment_(0),
                count_(0)
          {
          }
          NodeSlotLayout(size_t size, size_t alignment, size_t count)
              : size_(size),
                alignment_(alignment),
                count_(count)
          {
          }
          template <class T> static NodeSlotLayout of(size_t count)
          {
            return NodeSlotLayout(sizeof(T), AlignOf<T>::value, count);
          }
          size_t size() const
          {
            return this->size_;
          }
          size_t alignment() const
          {
            return this->alignment_;
          }
          size_t count() const
          {
            return this->count_;
          }
          bool stride(size_t &out) const
          {
            const size_t a =
                this->alignment_ > AlignOf<void *>::value ? this->alignment_ : size_t(AlignOf<void *>::value);
            return this->size_ && this->alignment_ && !(this->alignment_ & (this->alignment_ - 1))
                   && round(this->size_ > sizeof(void *) ? this->size_ : sizeof(void *), a, out);
          }
          static bool add(size_t a, size_t b, size_t &out)
          {
            if (b > size_t(-1) - a)
              return false;
            out = a + b;
            return true;
          }
          static bool multiply(size_t a, size_t b, size_t &out)
          {
            if (a && b > size_t(-1) / a)
              return false;
            out = a * b;
            return true;
          }
          static bool round(size_t n, size_t a, size_t &out)
          {
            if (!a || (a & (a - 1)))
              return false;
            if (!add(n, a - 1, out))
              return false;
            out &= ~(a - 1);
            return true;
          }

        private:
          size_t size_, alignment_, count_;
        };

        class NodeBuildOperation;
        class SeatBuildRequest;
        class SeatLayoutTable;

        /** Isolated reusable node storage. Boot once, then pop/register/destroy without
            growing its class, occupancy or resident metadata. It is not a NodeArena
            route and conveys no permission to reclaim before the owning clock.

            registerNode declares the nearest resident owner/provider, including for
            unpublished candidates. That owner must already be registered, making the
            dependency graph acyclic. Children owned by a Node must use that same owner
            chain. Heap children are traversed through actual child edges; unconnected
            heap candidates use the explicitly reserved heap rows.

            Callers transfer detached, reclaim-eligible roots. Existing Boundary parked
            ledgers must release their branches through their ordinary retirement door
            first; this primitive cannot remove private Boundary ledger references.
            Destructors may not reenter this storage. Explicit destroy uses a bounded
            preflight when reserveReclaimScratch was called; whole-store destructor
            teardown retains legacy scratch and is outside that certification. */
        class NodePartition
        {
          struct Resident
          {
            Node *node;
            Node *owner;
            bool ownedBy(Node *provider) const
            {
              return this->node && this->owner == provider;
            }
          };
          /** Remaining entitlement for one admitted class, including cold ATTACH.
              Returning a failed factory's slot never refunds construction quota. */
          class BuildQuota
          {
          public:
            explicit BuildQuota(size_t count = 0) : remaining_(count) {}
            bool consume()
            {
              if (!this->remaining_) return false;
              --this->remaining_;
              return true;
            }
          private:
            size_t remaining_;
          };
          struct Class
          {
            size_t size, alignment, count, stride;
            BuildQuota buildQuota;
            char *begin;
            void *head;
            unsigned char *occupied;
            Resident *residents;
          };

        public:
          NodePartition()
              : raw_(0),
                classes_(0),
                classCount_(0),
                heap_(0),
                heapCount_(0)
          {
          }
          ~NodePartition()
          {
            this->clear();
          }

          /** Checked boot footprint, shared verbatim with boot's packing. Includes
              resident dependencies separately from the occupancy bitmap. */
          static bool reservationBytes(const NodeSlotLayout *layouts, size_t count, size_t heapCount, size_t &out)
          {
            if (!layouts || !count)
              return false;
            size_t n = 0;
            if (!part(n, count, sizeof(Class), AlignOf<Class>::value)
                || !part(n, heapCount, sizeof(Resident), AlignOf<Resident>::value))
              return false;
            for (size_t i = 0; i < count; ++i)
            {
              size_t stride = 0;
              if (!layouts[i].count() || !layouts[i].stride(stride))
                return false;
              for (size_t j = 0; j < i; ++j)
                if (layouts[i].size() == layouts[j].size() && layouts[i].alignment() == layouts[j].alignment())
                  return false;
              size_t bits = layouts[i].count() / 8 + (layouts[i].count() % 8 != 0);
              if (!part(n, layouts[i].count(), sizeof(Resident), AlignOf<Resident>::value) || !part(n, bits, 1, 1)
                  || !part(n, layouts[i].count(), stride, effectiveAlign(layouts[i])))
                return false;
            }
            out = n;
            return true;
          }

          /** The only acquisition door. A successful boot is immutable until teardown;
              a failed boot leaves an empty object and may be retried. */
          bool boot(const NodeSlotLayout *layouts, size_t count, size_t heapCount = 0)
          {
            size_t total = 0;
            if (this->raw_ || !reservationBytes(layouts, count, heapCount, total))
              return false;
            char *raw = static_cast<char *>(core::LokaAllocRaw(total, site()));
            if (!raw)
              return false;
            char *cursor = raw;
            this->classes_ = static_cast<Class *>(take(cursor, count, sizeof(Class), AlignOf<Class>::value));
            this->heap_ = static_cast<Resident *>(take(cursor, heapCount, sizeof(Resident), AlignOf<Resident>::value));
            for (size_t h = 0; h < heapCount; ++h)
              new (this->heap_ + h) Resident();
            for (size_t i = 0; i < count; ++i)
            {
              Class &c = *new (this->classes_ + i) Class();
              c.size = layouts[i].size();
              c.alignment = layouts[i].alignment();
              c.count = layouts[i].count();
              layouts[i].stride(c.stride);
              c.residents = static_cast<Resident *>(take(cursor, c.count, sizeof(Resident), AlignOf<Resident>::value));
              for (size_t s = 0; s < c.count; ++s)
                new (c.residents + s) Resident();
              const size_t bits = c.count / 8 + (c.count % 8 != 0);
              c.occupied = static_cast<unsigned char *>(take(cursor, bits, 1, 1));
              std::memset(c.occupied, 0, bits);
              c.begin = static_cast<char *>(take(cursor, c.count, c.stride, effectiveAlign(layouts[i])));
              c.head = 0;
              for (size_t s = c.count; s > 0; --s)
                push(c, c.begin + (s - 1) * c.stride);
            }
            this->classCount_ = count;
            this->heapCount_ = heapCount;
            this->raw_ = raw;
            return true;
          }

          /** Synchronous admitted build: the operation owns one ticket through root,
              recursive construction, attach, and cleanup. The caller exclusively
              lends this partition for the entire call; callbacks must not reenter
              the partition or retain the ticket. */
          bool build(const NodeSlotLayout *demand, size_t count, NodeBuildOperation &operation);
#ifdef TEST_BUILD
          bool buildFixture(const NodeSlotLayout *demand, size_t count, NodeBuildOperation &operation)
          { return this->build(demand, count, operation); }
#endif

          /** Current class counts from this bank's own free lists. Unsupported
              demand is a capacity contract error, distinct from pending returns. */
          enum BuildCapacity { BUILD_AVAILABLE, BUILD_WAIT, BUILD_OVERFLOW };
          BuildCapacity buildCapacity(const NodeSlotLayout *demand, size_t count) const;

          /** Exact layout match; no larger-class borrowing and no upstream fallback. */
          void *allocate(const NodeSlotLayout &layout)
          {
            Class *c = this->findClass(layout);
            if (!c || !c->head)
              return 0;
            void *p = c->head;
            std::memcpy(&c->head, p, sizeof(void *));
            const size_t s = (static_cast<char *>(p) - c->begin) / c->stride;
            c->occupied[s / 8] |= static_cast<unsigned char>(1u << (s % 8));
            return p;
          }

          /** Registers the actual base subobject, permitting adjusted Node pointers.
              Storage must contain a completed placement construction of T. */
          template <class T> bool registerNode(T *node, Node *owner)
          {
            return this->registerPlaced(node, node, NodeSlotLayout::of<T>(1), owner);
          }

          bool registerPlaced(Node *base, void *storage, const NodeSlotLayout &layout, Node *owner)
          {
            Class *c = this->findClass(layout);
            size_t s = 0;
            if (!c || !locate(*c, storage, s) || !occupied(*c, s) || c->residents[s].node
                || (owner && !this->resident(owner)))
              return false;
            base->setPartitionOwner(this);
            c->residents[s].node = base;
            c->residents[s].owner = owner;
            return true;
          }

          /** Owns an unpublished heap candidate; capacity was fixed at boot.
              An unpublished heap candidate is allocation-gate storage; partition
              storage from any partition and plain new are refused. */
          bool registerHeap(Node *node, Node *owner)
          {
            if (!node || !node->isGateAllocated() || node->isArenaAllocated() || this->resident(node)
                || (owner && !this->resident(owner)))
              return false;
            for (size_t i = 0; i < this->classCount_; ++i)
            {
              const Class &c = this->classes_[i];
              const size_t address = reinterpret_cast<size_t>(node);
              const size_t begin = reinterpret_cast<size_t>(c.begin);
              if (address >= begin && address - begin < c.count * c.stride)
                return false;
            }
            for (size_t i = 0; i < this->heapCount_; ++i)
              if (!this->heap_[i].node)
              {
                this->heap_[i].node = node;
                this->heap_[i].owner = owner;
                return true;
              }
            return false;
          }

          /** Returns a popped slot in which no object lifetime began. */
          bool cancel(void *p, const NodeSlotLayout &layout)
          {
            Class *c = this->findClass(layout);
            size_t s = 0;
            if (!c || !locate(*c, p, s) || !occupied(*c, s) || c->residents[s].node)
              return false;
            put(*c, s);
            return true;
          }

          /** Destroys a transferred root and its dependents, then returns its slot.
              A still-owned child must be reclaimed through its owner. A registered
              Boundary is not an ownership wall for this partition's dependents:
              attached residents registered with it as owner are planned through
              nextDependent and their child edges are severed before destruction.
              Children in that Boundary's own landlord storage remain attached for
              its destructor; this partition never walks the nested landlord ledger. */
          bool destroy(void *p, const NodeSlotLayout &layout)
          {
            Class *c = this->findClass(layout);
            size_t s = 0;
            if (!c || !locate(*c, p, s) || !occupied(*c, s) || !c->residents[s].node || c->residents[s].owner)
              return false;
            if (!this->reclaimScratch_.isReserved())
            {
              this->destroyTree(c->residents[s].node);
              return true;
            }
            return this->reclaimTree(c->residents[s].node, &DestroyResident, this);
          }

          /** Returns a detached subtree through its storage landlords, then
              reports completed identities. Uses this partition's bounded plan
              when reserved; legacy traversal remains the overflow fallback. */
          static void reclaimDetached(Node *node, void (*returned)(Node *, void *), void *owner)
          {
            if (!node) return;
            NodePartition *partition = node->partitionOwner();
            DetachedReclaim completion(returned, owner, partition);
            if (partition && partition->reclaimScratch_.isReserved()
                && partition->reclaimTree(node, &ReclaimDetachedRow, &completion))
              return;
            ReclaimDetachedTree(node, &completion);
          }

          /** Cold explicit-door provisioning; destructor teardown stays legacy. */
          bool reserveReclaimScratch(size_t nodes)
          {
            return this->reclaimScratch_.reserve(nodes);
          }

        private:
          struct DetachedReclaim
          {
            DetachedReclaim(void (*callback)(Node *, void *), void *context, NodePartition *landlord)
                : returned(callback), owner(context), partition(landlord) {}
            void (*returned)(Node *, void *);
            void *owner;
            NodePartition *partition;
          };
          static void ReclaimDetachedTree(Node *node, void *context)
          {
            INestable *nestable = node->asBoundary() ? 0 : node->asNestable();
            Node *child = nestable ? nestable->detachChildren() : 0;
            while (child)
            {
              Node *next = child->nextInComposition;
              child->nextInComposition = 0;
              if (!child->isArenaAllocated())
              {
                if (child->partitionOwner() && child->partitionOwner() != node->partitionOwner())
                {
                  DetachedReclaim &completion = *static_cast<DetachedReclaim *>(context);
                  reclaimDetached(child, completion.returned, completion.owner);
                }
                else
                  ReclaimDetachedTree(child, context);
              }
              child = next;
            }
            ReclaimDetachedRow(node, context);
          }
          static void ReclaimDetachedRow(Node *node, void *context)
          {
            if (node->isArenaAllocated())
              return;
            DetachedReclaim &completion = *static_cast<DetachedReclaim *>(context);
            NodePartition *partition = node->partitionOwner();
            // PR2 fixture banks may also register tagged heap dependents. Their
            // origin remains heap, but the borrowing landlord must clear its row.
            if (!partition && completion.partition && completion.partition->resident(node))
              partition = completion.partition;
            if (partition)
              partition->reclaimAfterChildren(node, &ReclaimDetachedTree, context);
            else
              DestroyHeapNode(node);
            if (completion.returned)
              completion.returned(node, completion.owner);
          }
          friend class ::loka::app::scene::BoundaryNode;
          friend class SeatReservations;
          friend class NodeBuildTicket;
          void *consumeBuildSlot(const NodeSlotLayout &layout);
          friend class SeatBuildRequest;
          bool admitReturned(SeatBuildRequest &request, const SeatLayoutTable &demand, NodeBuildOperation &operation);
          typedef void (*ReclaimNode)(Node *, void *);

          /** Bounded explicit reclaim shared with the Boundary clock. The callback
              releases a completed row through its storage owner, without walking
              children again. Refusal leaves all edges intact for legacy fallback. */
          bool reclaimTree(Node *node, ReclaimNode reclaim, void *context)
          {
            ReclaimScratch::Plan plan(
                this->reclaimScratch_, ReclaimScratch::Plan::ALL_CHILDREN, &NodePartition::nextDependent, this);
            if (!plan.append(node))
              return false;
            this->severChildren(plan);
            for (size_t i = 0; i < plan.count(); ++i)
              reclaim(plan.node(i), context);
            return true;
          }
          static void DestroyResident(Node *node, void *context)
          {
            static_cast<NodePartition *>(context)->destroyResident(node);
          }

          /** Existing clock callback owns traversal and nested-landlord ordering.
              Only owner-edge candidates absent from child lists remain here. */
          void reclaimAfterChildren(Node *node, ReclaimNode reclaim, void *context)
          {
            this->destroyDependents(node, reclaim, context);
            this->destroyResident(node);
          }
          void reclaimRoots(ReclaimNode reclaim, void *context)
          {
            for (size_t i = 0; i < this->classCount_; ++i)
              for (size_t s = 0; s < this->classes_[i].count; ++s)
              {
                Resident &r = this->classes_[i].residents[s];
                if (r.node && !r.owner)
                  reclaim(r.node, context);
              }
            for (size_t h = 0; h < this->heapCount_; ++h)
              if (this->heap_[h].node && !this->heap_[h].owner)
                reclaim(this->heap_[h].node, context);
          }
          NodePartition(const NodePartition &);
          NodePartition &operator=(const NodePartition &);
          static const core::LokaAllocationSite &site()
          {
            static const core::LokaAllocationSite s("NodePartition", "backing");
            return s;
          }
          static size_t effectiveAlign(const NodeSlotLayout &l)
          {
            return l.alignment() > AlignOf<void *>::value ? l.alignment() : size_t(AlignOf<void *>::value);
          }
          static bool part(size_t &n, size_t count, size_t size, size_t alignment)
          {
            size_t payload = 0;
            return NodeSlotLayout::multiply(count, size, payload) && NodeSlotLayout::add(n, alignment - 1, n)
                   && NodeSlotLayout::add(n, payload, n);
          }
          static void *take(char *&cursor, size_t count, size_t size, size_t alignment)
          {
            const size_t address = reinterpret_cast<size_t>(cursor);
            const size_t padding = (alignment - (address & (alignment - 1))) & (alignment - 1);
            char *out = cursor + padding;
            cursor = out + count * size;
            return out;
          }
          Class *findClass(const NodeSlotLayout &l)
          {
            for (size_t i = 0; i < this->classCount_; ++i)
              if (this->classes_[i].size == l.size() && this->classes_[i].alignment == l.alignment())
                return this->classes_ + i;
            return 0;
          }
          static bool locate(const Class &c, const void *p, size_t &s)
          {
            const size_t address = reinterpret_cast<size_t>(p), begin = reinterpret_cast<size_t>(c.begin);
            if (address < begin || address - begin >= c.count * c.stride || (address - begin) % c.stride)
              return false;
            s = (address - begin) / c.stride;
            return true;
          }
          static bool occupied(const Class &c, size_t s)
          {
            return (c.occupied[s / 8] & (1u << (s % 8))) != 0;
          }
          static void push(Class &c, void *p)
          {
            std::memcpy(p, &c.head, sizeof(void *));
            c.head = p;
          }
          static void put(Class &c, size_t s)
          {
            c.occupied[s / 8] &= static_cast<unsigned char>(~(1u << (s % 8)));
            push(c, c.begin + s * c.stride);
          }
          Resident *resident(Node *node)
          {
            for (size_t i = 0; i < this->classCount_; ++i)
            {
              Class &c = this->classes_[i];
              const size_t address = reinterpret_cast<size_t>(node);
              const size_t begin = reinterpret_cast<size_t>(c.begin);
              if (address >= begin && address - begin < c.count * c.stride)
              {
                Resident &r = c.residents[(address - begin) / c.stride];
                return r.node == node ? &r : 0;
              }
            }
            for (size_t h = 0; h < this->heapCount_; ++h)
              if (this->heap_[h].node == node)
                return this->heap_ + h;
            return 0;
          }
          /** Partition commit, once per successful plan. Normal edges use the
              shared walk. At registered Boundaries, retain foreign child edges
              in order and remove only dependencies selected by nextDependent.
              O(plan rows + Boundary child edges * (classes + heap rows)), over
              this partition's resident index; no nested landlord ledger scan. */
          void severChildren(const ReclaimScratch::Plan &plan)
          {
            plan.severChildren();
            for (size_t i = 0; i < plan.count(); ++i)
            {
              Node *node = plan.node(i);
              if (!node->asBoundary() || !this->resident(node))
                continue;
              INestable *boundary = node->asNestable();
              Node *child = boundary->detachChildren();
              while (child)
              {
                Node *next = child->nextInComposition;
                child->nextInComposition = 0;
                Resident *resident = this->resident(child);
                if (!resident || !resident->ownedBy(node))
                  boundary->addChild(child);
                child = next;
              }
            }
          }

          /** Planner callback: advances through this partition's own rows once
              per node. Class lookup costs O(K) per returned dependency. */
          static Node *nextDependent(void *context, Node *owner, size_t &cursor)
          {
            NodePartition &self = *static_cast<NodePartition *>(context);
            size_t base = 0;
            for (size_t i = 0; i < self.classCount_; ++i)
            {
              Class &c = self.classes_[i];
              for (size_t s = cursor > base ? cursor - base : 0; s < c.count; ++s)
              {
                cursor = base + s + 1;
                if (c.residents[s].ownedBy(owner))
                  return c.residents[s].node;
              }
              base += c.count;
            }
            for (size_t h = cursor > base ? cursor - base : 0; h < self.heapCount_; ++h)
            {
              cursor = base + h + 1;
              if (self.heap_[h].ownedBy(owner))
                return self.heap_[h].node;
            }
            return 0;
          }
          void destroyDependents(Node *owner, ReclaimNode reclaim, void *context)
          {
            for (size_t i = 0; i < this->classCount_; ++i)
              for (size_t s = 0; s < this->classes_[i].count; ++s)
                if (this->classes_[i].residents[s].ownedBy(owner))
                  reclaim(this->classes_[i].residents[s].node, context);
            for (size_t h = 0; h < this->heapCount_; ++h)
              if (this->heap_[h].ownedBy(owner))
                reclaim(this->heap_[h].node, context);
          }
          static void DestroyTree(Node *node, void *context)
          {
            static_cast<NodePartition *>(context)->destroyTree(node);
          }
          void destroyTree(Node *node)
          {
            std::vector<Node *> children;
            INestable *nestable = node->asNestable();
            if (nestable)
              nestable->detachChildrenTo(children);
            for (size_t i = 0; i < children.size(); ++i)
              this->destroyTree(children[i]);
            this->reclaimAfterChildren(node, &DestroyTree, this);
          }
          void destroyResident(Node *node)
          {
            Resident *r = this->resident(node);
            for (size_t i = 0; r && i < this->classCount_; ++i)
            {
              Class &c = this->classes_[i];
              const size_t address = reinterpret_cast<size_t>(node);
              const size_t begin = reinterpret_cast<size_t>(c.begin);
              if (address < begin || address - begin >= c.count * c.stride)
                continue;
              const size_t s = (address - begin) / c.stride;
              assert(occupied(c, s) && "reclaim requires this partition's occupied slot");
              if (!occupied(c, s))
                return;
              DestroyNodeInPlace(node);
              r->node = 0;
              r->owner = 0;
              put(c, s);
              return;
            }
            DestroyHeapNode(node);
            if (r)
            {
              r->node = 0;
              r->owner = 0;
            }
          }
          void clear()
          {
            this->reclaimRoots(&DestroyTree, this);
            core::LokaFreeRaw(this->raw_, site());
          }
          ReclaimScratch reclaimScratch_;
          char *raw_;
          Class *classes_;
          size_t classCount_;
          Resident *heap_;
          size_t heapCount_;
        };

      } // namespace detail
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
