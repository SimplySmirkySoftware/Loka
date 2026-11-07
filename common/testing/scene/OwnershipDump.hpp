#ifndef LOKA_DSL_TESTING_SCENE_OWNERSHIP_DUMP_HPP
#define LOKA_DSL_TESTING_SCENE_OWNERSHIP_DUMP_HPP

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "app/core/App.hpp"
#include "app/core/Window.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/BoundarySection.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/boundary/LazyScopeNode.hpp"
#include "core/Held.hpp"
#include "testing/core/HeldTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class Scene;
    }
  } // namespace app


  namespace dsl
  {
    namespace testing
    {
      /** Testing-only deterministic view of the live ownership tree. */
      class OwnershipDump
      {
      public:
        static std::string dump(const ::App &app)
        {
          OwnershipDump walker;
          walker.indexApp(app);
          walker.renderApp(app);
          return walker.output_.str();
        }

        static std::string dump(
            const ::loka::app::scene::Scene &scene)
        {
          OwnershipDump walker;
          walker.indexScene(scene);
          walker.renderScene(scene, 0);
          return walker.output_.str();
        }

        /** Runtime rows for the boundary's declared seats, including retired links. */
        static std::string dumpSeatRuntime(const ::loka::app::scene::BoundaryNode &boundary)
        {
          using namespace ::loka::app::scene;
          std::vector<const BoundaryBranchSeatRuntimeEntry *> rows;
          collectSeatRuntime(boundary, boundary.branchSeats_, rows);
          std::ostringstream output;
          for (size_t i = 0; i < rows.size(); ++i)
          {
            const bool parentAttached = rows[i]->parent && rows[i]->parent->lifecycleFact() == NODE_FACT_ATTACHED;
            const bool activeAttached = rows[i]->active && rows[i]->active->lifecycleFact() == NODE_FACT_ATTACHED;
            output << "seat parent=" << (parentAttached ? "attached" : "retired")
                   << " active=" << (activeAttached ? "attached" : "retired") << "\n";
          }
          return output.str();
        }

        /** Testing-only append for composed fixtures. The appender's composition
            must outlive the appended plan: Headless DETACH drops its composition
            while the Boundary still holds the plan. */
        static void appendNestedBranchSeatPlan(
            ::loka::app::scene::BoundaryNode &boundary,
            ::loka::app::scene::NodeComposition &composition)
        {
          composition.assignCompositionSeatSlots();
          boundary.branchSeats_.append(composition.root());
          boundary.registerBranchSeatDirtySources();
        }

        /** Test snapshot access; callers must not retain rows across mutation. */
        static const ::loka::app::scene::BoundaryBranchSeatState &seatState(
            const ::loka::app::scene::BoundaryNode &boundary)
        {
          return boundary.branchSeats_;
        }

        /** Storage addresses of the rows dumpSeatRuntime() prints, in the same
            order. Identity only: a pin compares them across one ledger
            operation to prove that a row moved, and never dereferences one. */
        static std::vector<const void *> seatRuntimeRowAddresses(
            const ::loka::app::scene::BoundaryNode &boundary)
        {
          std::vector<const ::loka::app::scene::BoundaryBranchSeatRuntimeEntry *> rows;
          collectSeatRuntime(boundary, boundary.branchSeats_, rows);
          return std::vector<const void *>(rows.begin(), rows.end());
        }

      private:
        /** Declaration order: each plan's row, then its declared scope's rows. */
        static void collectSeatRuntime(
            const ::loka::app::scene::BoundaryNode &boundary,
            const ::loka::app::scene::BoundaryBranchSeatState &scope,
            std::vector<const ::loka::app::scene::BoundaryBranchSeatRuntimeEntry *> &rows)
        {
          using namespace ::loka::app::scene;
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            const BoundaryBranchSeatRuntimeEntry *row = boundary.branchSeats_.findRuntime(plans[i].key);
            if (row)
              rows.push_back(row);
            const BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested) collectSeatRuntime(boundary, *nested, rows);
          }
        }

        struct OwnerLabel
        {
          OwnerLabel(::loka::app::scene::IStateOwner *ownerValue,
                     const std::string &labelValue)
              : owner(ownerValue),
                label(labelValue)
          {
          }

          ::loka::app::scene::IStateOwner *owner;
          std::string label;
        };

        OwnershipDump()
            : output_(),
              owners_(),
              heldBlocks_()
        {
        }

        static std::string boundaryLabel(
            const ::loka::app::scene::BoundaryNode &boundary)
        {
          if (boundary.testId().empty())
          {
            return "boundary";
          }
          return std::string("boundary \"") + boundary.testId() + "\"";
        }

        static std::string sectionLabel(
            const ::loka::app::BoundarySectionNode &section)
        {
          std::ostringstream label;
          label << "section(" << section.props.key() << ")";
          return label.str();
        }

        void indexApp(const ::App &app)
        {
          if (!app.group_)
          {
            return;
          }
          const AppComponentGroup::Components components =
              app.group_->getComponents();
          for (size_t i = 0; i < components.size(); ++i)
          {
            Window *window = components[i] ? components[i]->asWindow() : 0;
            if (window && window->scene())
            {
              this->indexScene(*window->scene());
            }
          }
        }

        void indexScene(const ::loka::app::scene::Scene &scene)
        {
          this->indexNode(SceneTestAccess::rootNode(scene));
        }

        void indexNode(::loka::app::scene::Node *node)
        {
          if (!node)
          {
            return;
          }
          ::loka::app::scene::BoundaryNode *boundary = node->asBoundary();
          ::loka::app::BoundarySectionNode *section =
              node->asBoundarySectionNode();
          if (boundary)
          {
            this->owners_.push_back(
                OwnerLabel(boundary, boundaryLabel(*boundary)));
          }
          else if (section)
          {
            this->owners_.push_back(
                OwnerLabel(section, sectionLabel(*section)));
          }
          else if (node->nodeTypeKey() == ::loka::app::scene::NodeTypeToken< ::loka::app::scene::LazyScopeNode>())
          {
            this->owners_.push_back(OwnerLabel(node->asStateOwner(), "lazy-scope"));
          }
          ::loka::app::scene::INestable *nestable = node->asNestable();
          for (::loka::app::scene::Node *child =
                   nestable ? nestable->childrenHead() : 0;
               child;
               child = child->nextInComposition)
          {
            this->indexNode(child);
          }
          // Parked branches are alive and can own residents and holds, but
          // they are not children. A dump that skipped them would render a
          // parked branch's resources as unowned -- the exact lie this tool
          // exists to remove.
          for (unsigned i = 0;
               ::loka::app::scene::Node *branch =
                   node->retainedLifecycleBranch(i);
               ++i)
          {
            this->indexNode(branch);
          }
        }

        void renderApp(const ::App &app)
        {
          this->line(0, "app");
          if (!app.group_)
          {
            return;
          }
          const AppComponentGroup::Components components =
              app.group_->getComponents();
          size_t windowIndex = 0;
          for (size_t i = 0; i < components.size(); ++i)
          {
            Window *window = components[i] ? components[i]->asWindow() : 0;
            if (!window)
            {
              continue;
            }
            this->renderWindow(*window, windowIndex, 1);
            ++windowIndex;
          }
        }

        void renderWindow(const ::Window &window,
                          size_t index,
                          unsigned depth)
        {
          std::ostringstream label;
          label << "window[" << index << "]";
          this->line(depth, label.str());
          if (window.scene())
          {
            this->renderScene(*window.scene(), depth + 1);
          }
        }

        void renderScene(const ::loka::app::scene::Scene &scene,
                         unsigned depth)
        {
          this->line(depth, "scene");
          this->renderNode(SceneTestAccess::rootNode(scene), depth + 1);
        }

        void renderNode(::loka::app::scene::Node *node, unsigned depth)
        {
          if (!node)
          {
            return;
          }
          ::loka::app::scene::BoundaryNode *boundary = node->asBoundary();
          ::loka::app::BoundarySectionNode *section =
              node->asBoundarySectionNode();
          unsigned childDepth = depth;
          if (boundary)
          {
            this->renderBoundary(*boundary, depth);
            childDepth = depth + 1;
          }
          else if (section)
          {
            this->renderSection(*section, depth);
            childDepth = depth + 1;
          }
          else if (node->nodeTypeKey() == ::loka::app::scene::NodeTypeToken< ::loka::app::scene::LazyScopeNode>())
          {
            ::loka::app::scene::LazyScopeNode &scope =
                *static_cast< ::loka::app::scene::LazyScopeNode *>(node);
            this->line(depth, "lazy-scope");
            this->renderOwner(scope.stateOwner_.ownedStates_, scope.stateOwner_.holdLedger_, depth + 1);
            childDepth = depth + 1;
          }
          ::loka::app::scene::INestable *nestable = node->asNestable();
          for (::loka::app::scene::Node *child =
                   nestable ? nestable->childrenHead() : 0;
               child;
               child = child->nextInComposition)
          {
            this->renderNode(child, childDepth);
          }
          for (unsigned i = 0;
               ::loka::app::scene::Node *branch =
                   node->retainedLifecycleBranch(i);
               ++i)
          {
            this->line(childDepth, "parked");
            this->renderNode(branch, childDepth + 1);
          }
        }

        void renderBoundary(::loka::app::scene::BoundaryNode &boundary,
                            unsigned depth)
        {
          this->line(depth, boundaryLabel(boundary));
          this->renderOwner(boundary.ownedStates_,
                            boundary.holdLedger_,
                            depth + 1);
          size_t pendingCount = 0;
          for (::loka::core::detail::HeldBlockBase *pending =
                   boundary.pendingHeldReleasesHead_;
               pending;
               pending = pending->retireNext())
          {
            ++pendingCount;
          }
          if (pendingCount != 0)
          {
            std::ostringstream row;
            row << "pending-releases: " << pendingCount;
            this->line(depth + 1, row.str());
          }
          const size_t observedCount =
              BoundaryObservedStateTestAccess::entryCount(
                  boundary.observedState_);
          if (observedCount != 0)
          {
            std::ostringstream row;
            row << "observed: " << observedCount;
            this->line(depth + 1, row.str());
          }
          const size_t ownInputCount = BoundaryObservedStateTestAccess::ownInputUseCount(boundary.observedState_);
          if (ownInputCount != 0)
          {
            std::ostringstream row;
            row << "own-input: " << ownInputCount;
            this->line(depth + 1, row.str());
          }
        }

        void renderSection(::loka::app::BoundarySectionNode &section,
                           unsigned depth)
        {
          this->line(depth, sectionLabel(section));
          ::loka::app::scene::BoundaryInnerStateOwner &owner = section;
          this->renderOwner(owner.ownedStates_,
                            owner.holdLedger_,
                            depth + 1);
        }

        void renderOwner(
            const std::vector< ::loka::core::StateBase *> &states,
            const ::loka::core::HoldLedger &ledger,
            unsigned depth)
        {
          if (!states.empty())
          {
            size_t arenaCount = 0;
            for (size_t i = 0; i < states.size(); ++i)
            {
              if (states[i] && states[i]->isArenaAllocated())
              {
                ++arenaCount;
              }
            }
            std::ostringstream row;
            row << "states: " << states.size()
                << " (arena " << arenaCount
                << ", heap " << (states.size() - arenaCount) << ")";
            this->line(depth, row.str());
          }
          const ::loka::core::detail::HoldSlot *slot =
              ::loka::core::testing::HeldTestAccess::firstSlot(ledger);
          for (; slot;
               slot = ::loka::core::testing::HeldTestAccess::nextSlot(slot))
          {
            const ::loka::core::detail::HeldBlockBase *block =
                ::loka::core::testing::HeldTestAccess::block(slot);
            if (block)
            {
              this->renderHeld(*block, depth);
            }
          }
        }

        void renderHeld(
            const ::loka::core::detail::HeldBlockBase &block,
            unsigned depth)
        {
          unsigned totalCount = 0;
          std::ostringstream holders;
          bool wroteHolder = false;
          for (unsigned i = 0;
               i < ::loka::core::detail::HeldBlockBase::kSlotCapacity;
               ++i)
          {
            ::loka::app::scene::IStateOwner *owner = block.slotOwner(i);
            const unsigned count = block.slotHoldCount(i);
            if (!owner || count == 0)
            {
              continue;
            }
            if (wroteHolder)
            {
              holders << ", ";
            }
            holders << this->labelForOwner(owner) << " x" << count;
            wroteHolder = true;
            totalCount += count;
          }
          std::ostringstream row;
          row << "held#" << this->heldId(&block)
              << " count=" << totalCount
              << " held-by [" << holders.str() << "]";
          this->line(depth, row.str());
        }

        const std::string &labelForOwner(
            const ::loka::app::scene::IStateOwner *owner) const
        {
          for (size_t i = 0; i < this->owners_.size(); ++i)
          {
            if (this->owners_[i].owner == owner)
            {
              return this->owners_[i].label;
            }
          }
          static const std::string unknown("unknown-owner");
          return unknown;
        }

        size_t heldId(
            const ::loka::core::detail::HeldBlockBase *block)
        {
          for (size_t i = 0; i < this->heldBlocks_.size(); ++i)
          {
            if (this->heldBlocks_[i] == block)
            {
              return i + 1;
            }
          }
          this->heldBlocks_.push_back(block);
          return this->heldBlocks_.size();
        }

        void line(unsigned depth, const std::string &text)
        {
          for (unsigned i = 0; i < depth; ++i)
          {
            this->output_ << "  ";
          }
          this->output_ << text << "\n";
        }

        std::ostringstream output_;
        std::vector<OwnerLabel> owners_;
        std::vector<const ::loka::core::detail::HeldBlockBase *> heldBlocks_;
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka

#endif // LOKA_DSL_TESTING_SCENE_OWNERSHIP_DUMP_HPP
