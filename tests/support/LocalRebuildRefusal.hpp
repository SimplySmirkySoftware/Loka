#ifndef LOKA_TESTS_SUPPORT_LOCAL_REBUILD_REFUSAL_HPP
#define LOKA_TESTS_SUPPORT_LOCAL_REBUILD_REFUSAL_HPP

#include <cstddef>
#include <new>
#include "app/nodes/nestable/Fragment.hpp"
#include "core/util/OwnedDef.hpp"

// Fail-count hook for retained props application during a local rebuild.
// The implementations live in tests/TestingHooks.cpp (TEST_BUILD only).
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Refuses the next `count` retained props applications; 0 disarms. */
      void failLocalRebuildProbeProps(unsigned count);
      /** True, and one fewer armed refusal, while any refusal is armed. */
      bool consumeLocalRebuildProbePropsFailure();
    } // namespace testing
  } // namespace app
} // namespace loka

namespace LocalRebuildRefusalSupport
{
  /** Records how many children the rebuild root links at the moment a
      retained member refuses its props. */
  struct RebuildRefusalObservation
  {
    loka::app::scene::INestable &root;
    size_t linkedChildren;
    explicit RebuildRefusalObservation(loka::app::scene::INestable &value)
        : root(value), linkedChildren(value.childrenCount()) {}
    void record() { this->linkedChildren = this->root.childrenCount(); }
  };

  /** Retained Fragment member whose props application refuses while the
      local-rebuild fail-count hook is armed. The optional observation
      records the rebuild root at the refusal. */
  struct RefusingRetainedFragment : loka::app::FragmentDefinition
  {
    RebuildRefusalObservation *observation;
    explicit RefusingRetainedFragment(RebuildRefusalObservation *value = 0)
        : observation(value) {}
    typedef RefusingRetainedFragment CloneType;
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    {
      loka::core::OwnedDef<RefusingRetainedFragment> copy(new (std::nothrow) RefusingRetainedFragment(*this));
      if (!copy.isSet() || copy->childrenCount() != this->childrenCount())
      {
        return 0;
      }
      return copy.take();
    }
    virtual bool applyPropsToNode(loka::app::scene::Node *node) const
    {
      if (loka::app::testing::consumeLocalRebuildProbePropsFailure())
      {
        if (this->observation)
          this->observation->record();
        return false;
      }
      return loka::app::FragmentDefinition::applyPropsToNode(node);
    }
  };
} // namespace LocalRebuildRefusalSupport

#endif // LOKA_TESTS_SUPPORT_LOCAL_REBUILD_REFUSAL_HPP
