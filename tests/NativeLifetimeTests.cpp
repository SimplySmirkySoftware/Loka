#include "NativeLifetimeTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>

#include "support/LifecycleFactTestAccess.hpp"

#include "app/nodes/nestable/Show.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/node/Conditional.hpp"
#include "app/scene/projection/NativeHandlePool.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "core/State.hpp"

// Pins for the W3-2 native table hint axis (#98): the one wish a Node may
// express about its native pair travels declare -> definition -> node ->
// native context as observable state, and the exact-match bucket earns its
// performance through counters, never adaptation.

namespace
{
  class HintProbeNode;
  struct HintProbeTypeTag
  {
  };

  struct HintProbeProps : public loka::app::scene::NodePropsBase<HintProbeProps>
  {
    typedef HintProbeTypeTag TypeTag;
    typedef HintProbeNode NodeType;
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
  };

  class HintProbeNode : public loka::app::scene::Node
  {
  public:
    typedef HintProbeTypeTag TypeTag;
    HintProbeProps props;
    HintProbeNode(const HintProbeProps &p)
        : props(p)
    {
    }
  };

  struct HintProbeDefinition : public loka::app::scene::NodeDefinition<HintProbeProps, HintProbeNode>
  {
  };

  int g_disposedHandleSum = 0;

  void recordDisposedHandle(int handle)
  {
    g_disposedHandleSum += handle;
  }
} // namespace

void testNodeDefaultsToDefaultNativeLifetimeHint()
{
  HintProbeDefinition definition;
  loka::app::scene::Node *node = definition.create();
  assert(node);
  assert(node->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DEFAULT);
  node->setNativeLifetimeHint(loka::app::scene::NATIVE_HINT_EAGER_RELEASE);
  assert(node->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE);
  loka::app::scene::DestroyHeapNode(node);
}

void testDefinitionCarriesNativeLifetimeHintToCreatedNode()
{
  HintProbeDefinition definition;
  definition.lifetimeHint(loka::app::scene::NATIVE_HINT_DESIRE_STAY);
  loka::app::scene::Node *node = definition.create();
  assert(node);
  assert(node->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);
  loka::app::scene::DestroyHeapNode(node);
}

void testDefinitionCloneAndApplyPreserveNativeLifetimeHint()
{
  HintProbeDefinition definition;
  definition.lifetimeHint(loka::app::scene::NATIVE_HINT_EAGER_RELEASE);

  loka::app::scene::NodeDefinitionBase *clone = definition.clone();
  assert(clone);
  assert(clone->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE);

  loka::app::scene::Node *node = clone->create();
  assert(node);
  assert(node->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE);

  // A props re-apply from a definition with a different hint refreshes the
  // node's hint alongside the tag, mirroring setNodeTag.
  HintProbeDefinition updated;
  updated.lifetimeHint(loka::app::scene::NATIVE_HINT_DESIRE_STAY);
  bool applied = updated.applyPropsToNode(node);
  (void)applied;
  assert(applied);
  assert(node->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);

  loka::app::scene::DestroyHeapNode(node);
  delete clone;
}

void testDefinitionAssignmentCarriesNativeLifetimeHint()
{
  HintProbeDefinition source;
  source.lifetimeHint(loka::app::scene::NATIVE_HINT_DESIRE_STAY);

  HintProbeDefinition copied(source);
  assert(copied.nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);

  HintProbeDefinition assigned;
  assigned = source;
  assert(assigned.nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);

  HintProbeDefinition *same = &assigned;
  assigned = *same;
  assert(assigned.nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);

  HintProbeDefinition policyTarget;
  policyTarget.copyTestIdPolicyFrom(source);
  assert(policyTarget.nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY);
}

void testConditionalAndShowDefinitionsCarryNativeLifetimeHint()
{
  loka::core::MutableState<bool> condition(true);
  loka::core::MutableState<bool> otherCondition(false);
  HintProbeDefinition branchContents;

  loka::app::scene::ConditionalDefinition conditional(loka::app::scene::ConditionalProps(&condition, 0, 0));
  loka::app::scene::ConditionalDefinition equivalentConditional(
      loka::app::scene::ConditionalProps(&condition, &branchContents, 0));
  loka::app::scene::ConditionalDefinition differentConditional(
      loka::app::scene::ConditionalProps(&otherCondition, 0, 0));
  LOKA_VERIFY(conditional.propsBase());
  LOKA_VERIFY(conditional.propsBase()->propsTypeId() == equivalentConditional.propsBase()->propsTypeId());
  LOKA_VERIFY(conditional.branchCondition() == equivalentConditional.branchCondition());
  LOKA_VERIFY(conditional.armPolicies(1).destroyOnDetach ==
              equivalentConditional.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(conditional.branchCondition() != differentConditional.branchCondition());
  conditional.setNativeLifetimeHint(loka::app::scene::NATIVE_HINT_EAGER_RELEASE);
  loka::app::scene::NodeDefinitionBase *conditionalCopy = conditional.clone();
  assert(conditionalCopy);
  assert(conditionalCopy->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE &&
         "definition metadata survives even though a conditional seat has no runtime node");
  delete conditionalCopy;

  loka::app::ShowDefinition show(&condition);
  loka::app::ShowDefinition equivalentShow(&condition);
  loka::app::ShowDefinition updatedShow(&otherCondition);
  show << branchContents;
  LOKA_VERIFY(show.propsBase());
  LOKA_VERIFY(show.propsBase()->propsTypeId() == equivalentShow.propsBase()->propsTypeId());
  LOKA_VERIFY(show.branchCondition() == equivalentShow.branchCondition() &&
         "Show branch contents do not change the condition source");
  LOKA_VERIFY(show.armPolicies(1).destroyOnDetach ==
              equivalentShow.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(show.branchCondition() != updatedShow.branchCondition());
  branchContents.setNativeLifetimeHint(loka::app::scene::NATIVE_HINT_DESIRE_STAY);
  loka::app::scene::Node *contentNode = branchContents.create();
  assert(contentNode);
  assert(contentNode->nativeLifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY &&
         "NativeLifetimeHint remains content-local when the seat has no runtime node");
  loka::app::scene::DestroyHeapNode(contentNode);
}

void testNativeContextObservesLifetimeHint()
{
  // The hint snapshot rides the fact observation points: the attach-time
  // read adopts the declare-time value, and every delivery-walk visit
  // refreshes it — no imperative observe call exists anymore.
  loka::app::scene::Node node;
  node.setNativeLifetimeHint(loka::app::scene::NATIVE_HINT_EAGER_RELEASE);
  node.setContext(new loka::app::scene::NativeNodeContext());
  LOKA_VERIFY(node.getContext()->lifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE &&
         "the attach-time read adopts the declare-time hint");

  node.setNativeLifetimeHint(loka::app::scene::NATIVE_HINT_DESIRE_STAY);
  LOKA_VERIFY(node.getContext()->lifetimeHint() == loka::app::scene::NATIVE_HINT_EAGER_RELEASE &&
         "a runtime change is invisible until the next walk visit");
  loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
  LOKA_VERIFY(node.getContext()->lifetimeHint() == loka::app::scene::NATIVE_HINT_DESIRE_STAY &&
         "the delivery walk refreshes the snapshot even without a fact transition");

  loka::app::scene::NativeNodeContext prioritized(loka::app::scene::NativeNodeContext::PRIORITY_HIGH);
  assert(prioritized.lifetimeHint() == loka::app::scene::NATIVE_HINT_DEFAULT);
}

void testExactMatchBucketCountsHitsMissesEvictsAndDepth()
{
  loka::app::scene::ExactMatchHandleBucket<int> bucket;
  int handle = 0;

  LOKA_VERIFY(!bucket.tryAcquire(handle));
  assert(bucket.missCount() == 1);
  assert(bucket.hitCount() == 0);
  assert(bucket.depth() == 0);

  bucket.offer(7);
  bucket.offer(9);
  assert(bucket.depth() == 2);

  LOKA_VERIFY(bucket.tryAcquire(handle));
  assert(handle == 9);
  assert(bucket.hitCount() == 1);
  assert(bucket.depth() == 1);

  g_disposedHandleSum = 0;
  bucket.drainWith(recordDisposedHandle);
  assert(g_disposedHandleSum == 7);
  assert(bucket.evictCount() == 1);
  assert(bucket.depth() == 0);

  LOKA_VERIFY(!bucket.tryAcquire(handle));
  assert(bucket.missCount() == 2);
}

void testExactMatchBucketDepthCapRefusesAndCountsEvicts()
{
  loka::app::scene::ExactMatchHandleBucket<int> bucket(2);

  LOKA_VERIFY(bucket.offer(1));
  LOKA_VERIFY(bucket.offer(2));
  LOKA_VERIFY(!bucket.offer(3));
  assert(bucket.depth() == 2);
  assert(bucket.evictCount() == 1);

  int handle = 0;
  LOKA_VERIFY(bucket.tryAcquire(handle));
  LOKA_VERIFY(bucket.offer(3));
  assert(bucket.depth() == 2);
  assert(bucket.evictCount() == 1);
}

void testExactMatchBucketInstancesStayIsolatedAndReusableAfterDrain()
{
  loka::app::scene::ExactMatchHandleBucket<int> bucketA;
  loka::app::scene::ExactMatchHandleBucket<int> bucketB;
  int handle = 0;

  bucketA.offer(5);
  LOKA_VERIFY(!bucketB.tryAcquire(handle));
  assert(bucketB.missCount() == 1);
  assert(bucketA.missCount() == 0);
  LOKA_VERIFY(bucketA.tryAcquire(handle));
  assert(handle == 5);

  // An empty drain touches nothing and counts nothing.
  g_disposedHandleSum = 0;
  bucketA.drainWith(recordDisposedHandle);
  assert(g_disposedHandleSum == 0);
  assert(bucketA.evictCount() == 0);

  // The bucket keeps working after a drain, with cumulative counters.
  bucketA.offer(11);
  bucketA.drainWith(recordDisposedHandle);
  assert(g_disposedHandleSum == 11);
  assert(bucketA.evictCount() == 1);
  LOKA_VERIFY(bucketA.tryAcquire(handle) == false);
  assert(bucketA.hitCount() == 1);
  assert(bucketA.missCount() == 1);

  // resetCounters brackets a measurement: counters go to zero, the bag
  // itself (depth gauge) is untouched.
  bucketA.offer(13);
  bucketA.resetCounters();
  assert(bucketA.hitCount() == 0);
  assert(bucketA.missCount() == 0);
  assert(bucketA.evictCount() == 0);
  assert(bucketA.depth() == 1);
  LOKA_VERIFY(bucketA.tryAcquire(handle));
  assert(handle == 13);
  assert(bucketA.hitCount() == 1);
}
