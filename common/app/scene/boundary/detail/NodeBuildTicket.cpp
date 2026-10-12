#include "app/scene/boundary/detail/NodeBuildTicket.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace detail
      {

        void *NodeBuildTicket::consumeAndAllocate(const NodeSlotLayout &layout)
        {
          void *storage = this->partition_.consumeBuildSlot(layout);
          if (!storage) this->result_ = LAYOUT_REFUSED;
          return storage;
        }

        void *NodePartition::consumeBuildSlot(const NodeSlotLayout &layout)
        {
          Class *entry = this->findClass(layout);
          if (!entry || !entry->buildQuota.consume())
          {
            assert(false && "seat node declaration incomplete: include every descendant and ATTACH child");
            return 0;
          }
          void *storage = this->allocate(layout);
          assert(storage && "admitted node class has no free slot");
          return storage;
        }

        Node *NodeBuildTicket::create(NodeDefinitionBase &definition, Node *owner)
        {
          const NodeSlotLayout layout(definition.nodeSize(), definition.nodeAlign(), 1);
          void *storage = this->consumeAndAllocate(layout);
          if (!storage)
            return 0;
          Node *node = definition.createInPlace(storage);
          if (!node)
          {
            this->partition_.cancel(storage, layout);
            return 0;
          }
          if (owner && !this->partition_.resident(owner))
            owner = 0;
          const bool registered = this->partition_.registerPlaced(node, storage, layout, owner);
          assert(registered && "strict node construction must register its owner edge");
          if (!registered)
          {
            DestroyNodeInPlace(node);
            this->partition_.cancel(storage, layout);
            return 0;
          }
          return node;
        }

        bool NodePartition::build(const NodeSlotLayout *demand, size_t count, NodeBuildOperation &operation)
        {
          if (!demand || !count || count > SeatLayoutTable::capacity)
            return false;
          SeatLayoutTable table;
          for (size_t i = 0; i < count; ++i)
            if (!table.append(demand[i]))
              return false;
          if (!table.normalize())
            return false;
          for (size_t i = 0; i < table.count(); ++i)
          {
            Class *c = this->findClass(table.layouts()[i]);
            if (!c || table.layouts()[i].count() > c->count)
            {
              assert(false && "seat demand exceeds the installed node reservation");
              return false;
            }
          }
          for (size_t i = 0; i < this->classCount_; ++i) this->classes_[i].buildQuota = BuildQuota();
          for (size_t i = 0; i < table.count(); ++i)
            this->findClass(table.layouts()[i])->buildQuota = BuildQuota(table.layouts()[i].count());
          NodeBuildTicket ticket(*this);
          return operation.buildAndAttach(ticket);
        }

      } // namespace detail
    } // namespace scene
  } // namespace app
} // namespace loka
