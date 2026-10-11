#ifndef LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARYCOMPOSITIONSTATE_HPP
#define LOKA_CORE2_SCENE_BOUNDARY_DETAIL_BOUNDARYCOMPOSITIONSTATE_HPP

#include <vector>
#include "app/scene/Node.hpp"
#include "app/scene/boundary/BoundaryStateTypes.hpp"
#include "app/scene/boundary/detail/BoundaryBranchSeatState.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      struct BoundaryLocalRebuildPlanEntry
      {
        enum Action
        {
          ACTION_RETAIN = 0,
          ACTION_RECONCILE = 1,
          ACTION_ATTACH = 2,
          ACTION_REPLACE = 3,
          ACTION_RETIRE = 4
        };

        BoundaryLocalRebuildPlanEntry()
            : node(0),
              previousNode(0),
              definition(0),
              action(ACTION_RETAIN),
              tag(NODE_TAG_NONE)
        {
        }

        static BoundaryLocalRebuildPlanEntry retain(Node *nodeValue,
                                                    NodeDefinitionBase *definitionValue,
                                                    NodeTag tagValue)
        {
          BoundaryLocalRebuildPlanEntry entry;
          entry.node = nodeValue;
          entry.definition = definitionValue;
          entry.tag = tagValue;
          return entry;
        }

        static BoundaryLocalRebuildPlanEntry reconcile(Node *nodeValue,
                                                       NodeDefinitionBase *definitionValue,
                                                       NodeTag tagValue)
        {
          BoundaryLocalRebuildPlanEntry entry =
              retain(nodeValue, definitionValue, tagValue);
          entry.action = ACTION_RECONCILE;
          return entry;
        }

        static BoundaryLocalRebuildPlanEntry attach(Node *nodeValue,
                                                    NodeDefinitionBase *definitionValue,
                                                    NodeTag tagValue)
        {
          BoundaryLocalRebuildPlanEntry entry;
          entry.node = nodeValue;
          entry.definition = definitionValue;
          entry.action = ACTION_ATTACH;
          entry.tag = tagValue;
          return entry;
        }

        static BoundaryLocalRebuildPlanEntry attach(Node *nodeValue, NodeTag tagValue)
        {
          return attach(nodeValue, 0, tagValue);
        }

        static BoundaryLocalRebuildPlanEntry replace(Node *nodeValue,
                                                     Node *previousNodeValue,
                                                     NodeDefinitionBase *definitionValue,
                                                     NodeTag tagValue)
        {
          BoundaryLocalRebuildPlanEntry entry;
          entry.node = nodeValue;
          entry.previousNode = previousNodeValue;
          entry.definition = definitionValue;
          entry.action = ACTION_REPLACE;
          entry.tag = tagValue;
          return entry;
        }

        static BoundaryLocalRebuildPlanEntry replace(Node *nodeValue,
                                                     Node *previousNodeValue,
                                                     NodeTag tagValue)
        {
          return replace(nodeValue, previousNodeValue, 0, tagValue);
        }

        static BoundaryLocalRebuildPlanEntry retire(Node *nodeValue, NodeTag tagValue)
        {
          BoundaryLocalRebuildPlanEntry entry;
          entry.node = nodeValue;
          entry.action = ACTION_RETIRE;
          entry.tag = tagValue;
          return entry;
        }

        bool keepsLiveNode() const
        {
          return this->action != ACTION_RETIRE;
        }

        bool requiresAttachCompose() const
        {
          return this->action == ACTION_ATTACH || this->action == ACTION_REPLACE;
        }

        Node *detachedNode() const
        {
          if (this->action == ACTION_REPLACE)
          {
            return this->previousNode;
          }
          if (this->action == ACTION_RETIRE)
          {
            return this->node;
          }
          return 0;
        }

        Node *node;
        Node *previousNode;
        /** Borrowed from the current compose arena for this apply pass. */
        NodeDefinitionBase *definition;
        Action action;
        NodeTag tag;
      };

      struct BoundaryLocalRebuildPlan
      {
        void reserve(size_t count)
        {
          entries.reserve(count);
        }

        void clear()
        {
          entries.clear();
          branchSeatRegistrations.clear();
        }

        std::vector<BoundaryLocalRebuildPlanEntry> entries;
        BoundaryBranchSeatRuntimeRegistrationPlan branchSeatRegistrations;
      };

      struct BoundaryCompositionState
      {
        BoundaryCompositionState()
            : result()
        {
        }

        void clearResult()
        {
          result.clear();
        }

        void beginCompose(ComposeEvent event, NodeDirtyFlags dirtyFlags)
        {
          result.event = event;
          result.dirtyFlagsSeen = dirtyFlags;
          result.composed = false;
          result.allocationFailed = false;
          result.boundaryPlanRequired = false;
        }

        void completeCompose()
        {
          result.composed = true;
        }

        void noteAllocationFailure()
        {
          result.allocationFailed = true;
        }

        bool allocationFailedValue() const
        {
          return result.allocationFailed;
        }

        void noteBoundaryPlanRequired()
        {
          result.boundaryPlanRequired = true;
        }

        bool boundaryPlanRequiredValue() const
        {
          return result.boundaryPlanRequired;
        }

        /** Refuse projection while preserving the recorded failure reason. */
        void failCompose()
        {
          result.composed = false;
        }

        BoundaryComposeResult &composeResult()
        {
          return result;
        }

        const BoundaryComposeResult &composeResult() const
        {
          return result;
        }

        BoundaryComposeResult result;
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif
