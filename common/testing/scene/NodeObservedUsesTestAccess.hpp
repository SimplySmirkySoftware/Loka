#ifndef LOKA_TESTING_SCENE_NODE_OBSERVED_USES_TEST_ACCESS_HPP
#define LOKA_TESTING_SCENE_NODE_OBSERVED_USES_TEST_ACCESS_HPP

#include "app/scene/Node.hpp"

namespace loka { namespace app { namespace testing {
/** Read-only observation facts for contract pins and snapshots. */
class NodeObservedUsesTestAccess
{
public:
  static scene::NodeDirtyFlags mark(const scene::Node &node)
  { return node.uses_.mark; }
  static unsigned useCount(const scene::Node &node)
  {
    unsigned count = 0;
    for (const scene::ObservedUse *row = node.uses_.head; row; row = row->next)
      if (row->subscription) ++count;
    return count;
  }
  static unsigned reservedCount(const scene::Node &node)
  {
    unsigned count = 0;
    for (const scene::ObservedUse *row = node.uses_.head; row; row = row->next)
      if (!row->source.isFree())
        ++count;
    return count;
  }
  static unsigned storageCount(const scene::Node &node)
  {
    unsigned count = 0;
    for (const scene::ObservedUse *row = node.uses_.head; row; row = row->next) ++count;
    return count;
  }
};
} } }
#endif
