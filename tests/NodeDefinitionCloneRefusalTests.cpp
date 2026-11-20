#include "NodeDefinitionCloneRefusalTests.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "testing/scene/ProbeArmSeatDefinition.hpp"
#include "support/LocalRebuildRefusal.hpp"
#include "support/LimitedCloneProbe.hpp"
#include "support/TestVerify.hpp"

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
using namespace DefinitionCloneTestSupport;

namespace
{
  struct AppendingCopy : NestableNodeDefinition<CloneProbeProps, CloneProbeNode, AppendingCopy>
  {
    typedef NestableNodeDefinition<CloneProbeProps, CloneProbeNode, AppendingCopy> BaseType;
    AppendingCopy()
        : BaseType()
    {
    }
    explicit AppendingCopy(const CloneShell<AppendingCopy> &shell)
        : BaseType(shell)
    {
    }
    AppendingCopy(const AppendingCopy &other)
        : BaseType(other)
    {
      *this << ZStack();
    }
  };

  template <class T> void fillSource(T &source)
  {
    source << (Box() << LimitedCloneProbeDefinition()
                     << LimitedCloneProbeDefinition()) << ZStack();
    source.setNodeTag(42);
    source.setCompositionSeatSlot(17);
  }

  void verifyStructure(INestableDefinition &definition)
  {
    LOKA_VERIFY(definition.childrenCount() == 2);
    NodeDefinitionBase *parent = definition.childrenHead();
    LOKA_VERIFY(parent && parent->nodeKind() == NODE_KIND_BOX);
    LOKA_VERIFY(parent->asNestableDefinition()->childrenCount() == 2);
    LOKA_VERIFY(parent->nextInComposition->nodeKind() == NODE_KIND_ZSTACK);
    LOKA_VERIFY(parent->nextInComposition->nextInComposition == 0);
  }
}

void testNodeDefinitionCloneRefusesGrandchild()
{
  ZStack source;
  fillSource(source);
  const int alive = g_probePropsAlive;
  g_limitedCloneCalls = 0;
  g_limitedCloneBudget = 1;
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(!copy.isSet());
  LOKA_VERIFY(g_probePropsAlive == alive);
  verifyStructure(source);
}

void testNodeDefinitionAssignmentPreservesDestination()
{
  Box source, destination;
  fillSource(source);
  source.props.padding = loka::app::layout::LayoutGap::of(23);
  destination.props.padding = loka::app::layout::LayoutGap::of(7);
  destination.setNodeTag(9);
  destination << ZStack();
  NodeDefinitionBase *oldHead = destination.childrenHead();
  const int alive = g_probePropsAlive;
  g_limitedCloneCalls = 0;
  g_limitedCloneBudget = 1;
  destination = source;
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(destination.childrenCount() == 1);
  LOKA_VERIFY(destination.childrenHead() == oldHead);
  LOKA_VERIFY(destination.childrenHead()->nodeKind() == NODE_KIND_ZSTACK);
  LOKA_VERIFY(destination.props.padding == loka::app::layout::LayoutGap::of(7));
  LOKA_VERIFY(destination.nodeTag() == 9);
  LOKA_VERIFY(g_probePropsAlive == alive);
  verifyStructure(source);
}

void testNodeDefinitionCopiesFullStructure()
{
  Box source, destination;
  fillSource(source);
  source.props.padding = loka::app::layout::LayoutGap::of(23);
  destination << ZStack();
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  LOKA_VERIFY(copy.isSet());
  verifyStructure(*copy->asNestableDefinition());
  destination = source;
  Box byValue(source);
  verifyStructure(destination);
  verifyStructure(byValue);
  LOKA_VERIFY(destination.props.padding == loka::app::layout::LayoutGap::of(23) && destination.nodeTag() == 42);
  LOKA_VERIFY(byValue.props.padding == loka::app::layout::LayoutGap::of(23) && byValue.nodeTag() == 42);
  LOKA_VERIFY(static_cast<const BoxProps *>(copy->propsBase())->padding == loka::app::layout::LayoutGap::of(23));
  LOKA_VERIFY(copy->nodeTag() == 42);
  LOKA_VERIFY(copy->compositionSeatSlot() == 17);
  LOKA_VERIFY(destination.childrenHead() != source.childrenHead());
  LOKA_VERIFY(copy->asNestableDefinition()->childrenHead() != source.childrenHead());
  LOKA_VERIFY(destination.childrenHead()->asNestableDefinition()->childrenHead() !=
              source.childrenHead()->asNestableDefinition()->childrenHead());
}

void testNodeDefinitionByValueRefusalKeepsPropsDropsChildren()
{
  Box source;
  fillSource(source);
  source.props.padding = loka::app::layout::LayoutGap::of(23);
  const int alive = g_probePropsAlive;
  g_limitedCloneCalls = 0;
  g_limitedCloneBudget = 1;
  Box copy(source);
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(copy.childrenCount() == 0);
  LOKA_VERIFY(copy.props.padding == source.props.padding);
  LOKA_VERIFY(copy.nodeTag() == source.nodeTag());
  LOKA_VERIFY(copy.compositionSeatSlot() == source.compositionSeatSlot());
  LOKA_VERIFY(g_probePropsAlive == alive);
  verifyStructure(source);
}

void testNodeDefinitionDirectChildRefusalReleasesCandidate()
{
  AppendingCopy source;
  source << LimitedCloneProbeDefinition() << LimitedCloneProbeDefinition();
  const int alive = g_probePropsAlive;
  g_limitedCloneBudget = 1;
  g_limitedCloneCalls = 0;
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(!copy.isSet());
  LOKA_VERIFY(g_probePropsAlive == alive);
  LOKA_VERIFY(source.childrenCount() == 2);
}

void testNodeDefinitionCloneBypassesDerivedCopy()
{
  AppendingCopy source;
  source << Box();
  AppendingCopy byValue(source);
  LOKA_VERIFY(byValue.childrenCount() == 2);
  OwnedDef<NodeDefinitionBase> clone(source.clone());
  LOKA_VERIFY(clone.isSet());
  INestableDefinition *children = clone->asNestableDefinition();
  LOKA_VERIFY(children->childrenCount() == 1);
  LOKA_VERIFY(children->childrenHead()->nodeKind() == NODE_KIND_BOX);
  LOKA_VERIFY(children->childrenHead() != source.childrenHead());
}

namespace
{
  FragmentDefinition &storedFragment(NodeDefinitionBase &wrapper, unsigned index)
  {
    NodeDefinitionBase *branch = wrapper.retainedDefinitionBranch(index);
    LOKA_VERIFY(branch && branch->propsBase() && branch->propsBase()->propsTypeId() == FragmentProps::staticTypeId());
    return *static_cast<FragmentDefinition *>(branch);
  }

  void armCloneBudget(int budget)
  {
    g_limitedCloneCalls = 0;
    g_limitedCloneBudget = budget;
  }

  void verifyShowRefusal(int budget)
  {
    g_limitedCloneBudget = -1;
    ShowDefinition source(0);
    source << LimitedCloneProbeDefinition();
    FragmentDefinition &yes = storedFragment(source, 0);
    FragmentDefinition &no = storedFragment(source, 1);
    no << LimitedCloneProbeDefinition();
    LOKA_VERIFY(yes.childrenCount() == 1 && no.childrenCount() == 1);
    NodeDefinitionBase *trueChild = yes.childrenHead();
    NodeDefinitionBase *falseChild = no.childrenHead();
    const int alive = g_probePropsAlive;
    armCloneBudget(budget);
    OwnedDef<NodeDefinitionBase> copy(source.clone());
    g_limitedCloneBudget = -1;
    LOKA_VERIFY(g_limitedCloneCalls == 2);
    LOKA_VERIFY(!copy.isSet());
    LOKA_VERIFY(yes.childrenHead() == trueChild && no.childrenHead() == falseChild);
    LOKA_VERIFY(g_probePropsAlive == alive);
  }

  void setCloneMetadata(NodeDefinitionBase &definition, int tag, const char *id)
  {
    definition.setNodeTag(tag);
    definition.setCompositionSeatSlot(tag + 10);
    definition.setNativeLifetimeHint(NATIVE_HINT_EAGER_RELEASE);
    if (id)
      definition.setTestId(id);
    else
      definition.setAutoTestId();
  }

  void verifyCloneMetadata(NodeDefinitionBase &source, NodeDefinitionBase &copy)
  {
    LOKA_VERIFY(&source != &copy);
    LOKA_VERIFY(copy.nodeTag() == source.nodeTag());
    LOKA_VERIFY(copy.compositionSeatSlot() == source.compositionSeatSlot());
    LOKA_VERIFY(copy.nativeLifetimeHint() == source.nativeLifetimeHint());
    LOKA_VERIFY(copy.hasTestId() == source.hasTestId());
    LOKA_VERIFY(copy.testIdValue() == source.testIdValue());
    LOKA_VERIFY(copy.wantsAutoTestId() == source.wantsAutoTestId());
  }

  void verifyClonedBranch(NodeDefinitionBase &source, NodeDefinitionBase &copy, unsigned index)
  {
    FragmentDefinition &original = storedFragment(source, index);
    FragmentDefinition &cloned = storedFragment(copy, index);
    verifyCloneMetadata(original, cloned);
    LOKA_VERIFY(original.childrenCount() == 1 && cloned.childrenCount() == 1);
    verifyCloneMetadata(*original.childrenHead(), *cloned.childrenHead());
  }
} // namespace

void testShowCloneRefusesSecondArm()
{
  verifyShowRefusal(1);
}

void testShowCloneRefusesFirstArm()
{
  verifyShowRefusal(0);
}

void testPolicyScopeCloneRefusesSecondChild()
{
  g_limitedCloneBudget = -1;
  PolicyScopeDefinition source;
  source << LimitedCloneProbeDefinition() << LimitedCloneProbeDefinition();
  FragmentDefinition &content = storedFragment(source, 0);
  LOKA_VERIFY(content.childrenCount() == 2);
  NodeDefinitionBase *first = content.childrenHead();
  NodeDefinitionBase *second = first->nextInComposition;
  LOKA_VERIFY(second);
  const int alive = g_probePropsAlive;
  armCloneBudget(1);
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(!copy.isSet());
  LOKA_VERIFY(content.childrenHead() == first && first->nextInComposition == second);
  LOKA_VERIFY(g_probePropsAlive == alive);
}

void testShowCloneRefusalPreservesDeclaredRoot()
{
  g_limitedCloneBudget = -1;
  NodeComposition composition;
  composition.declare(Box());
  NodeDefinitionBase *old = composition.root();
  LOKA_VERIFY(old && old->nodeKind() == NODE_KIND_BOX);
  ShowDefinition show(0);
  show << LimitedCloneProbeDefinition();
  FragmentDefinition source;
  source << show;
  LOKA_VERIFY(source.childrenCount() == 1);
  LOKA_VERIFY(storedFragment(*source.childrenHead(), 0).childrenCount() == 1);
  LOKA_VERIFY(storedFragment(*source.childrenHead(), 1).childrenCount() == 0);
  armCloneBudget(0);
  composition.declare(source);
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 1);
  LOKA_VERIFY(composition.root() == old);
}

void testEmptyWrapperClones()
{
  ShowDefinition show(0);
  PolicyScopeDefinition scope;
  OwnedDef<NodeDefinitionBase> showCopy(show.clone());
  OwnedDef<NodeDefinitionBase> scopeCopy(scope.clone());
  LOKA_VERIFY(showCopy.isSet() && scopeCopy.isSet());
  LOKA_VERIFY(storedFragment(*showCopy, 0).childrenCount() == 0);
  LOKA_VERIFY(storedFragment(*showCopy, 1).childrenCount() == 0);
  LOKA_VERIFY(storedFragment(*scopeCopy, 0).childrenCount() == 0);
}

void testShowClonePreservesArmsAndPolicies()
{
  g_limitedCloneBudget = -1;
  MutableState<bool> condition(true);
  ShowDefinition source(&condition);
  Box sibling;
  source.nextInComposition = &sibling;
  source.destroyOnDetach();
  setCloneMetadata(source, 10, "show");
  for (unsigned index = 0; index < 2; ++index)
  {
    FragmentDefinition &branch = storedFragment(source, index);
    branch << LimitedCloneProbeDefinition();
    LOKA_VERIFY(branch.childrenCount() == 1);
    setCloneMetadata(branch, 20 + index, "branch");
    setCloneMetadata(*branch.childrenHead(), 30 + index, index ? 0 : "trueChild");
  }
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  LOKA_VERIFY(copy.isSet());
  LOKA_VERIFY(copy->nextInComposition == 0 && source.nextInComposition == &sibling);
  verifyCloneMetadata(source, *copy);
  verifyClonedBranch(source, *copy, 0);
  verifyClonedBranch(source, *copy, 1);
  const ShowDefinition &cloned = *static_cast<ShowDefinition *>(copy.get());
  LOKA_VERIFY(cloned.props().condition == &condition);
  LOKA_VERIFY(cloned.props().trueDef == copy->retainedDefinitionBranch(0));
  LOKA_VERIFY(cloned.props().falseDef == copy->retainedDefinitionBranch(1));
  LOKA_VERIFY(cloned.armPolicies(1).destroyOnDetach && !cloned.armPolicies(0).destroyOnDetach);
}

void testPolicyScopeClonePreservesContentAndPolicies()
{
  g_limitedCloneBudget = -1;
  Box sibling;
  for (unsigned policy = 0; policy < 2; ++policy)
  {
    PolicyScopeDefinition source;
    source.nextInComposition = &sibling;
    if (policy == 0)
      source.destroyOnDetach();
    else
      source.deliverWhileDetached();
    source << LimitedCloneProbeDefinition();
    FragmentDefinition &content = storedFragment(source, 0);
    LOKA_VERIFY(content.childrenCount() == 1);
    setCloneMetadata(source, 40, "scope");
    setCloneMetadata(content, 41, "content");
    setCloneMetadata(*content.childrenHead(), 42, "child");
    OwnedDef<NodeDefinitionBase> copy(source.clone());
    LOKA_VERIFY(copy.isSet());
    LOKA_VERIFY(copy->nextInComposition == 0 && source.nextInComposition == &sibling);
    verifyCloneMetadata(source, *copy);
    verifyClonedBranch(source, *copy, 0);
    BranchPolicies policies = copy->asBranchPolicyScopeDefinition()->branchPolicies();
    LOKA_VERIFY(policies.destroyOnDetach == (policy == 0));
    LOKA_VERIFY(policies.deliverWhileDetached == (policy == 1));
  }
}

void testRefusedByValueFragmentCopyIsEmpty()
{
  g_limitedCloneBudget = -1;
  FragmentDefinition source;
  source << LimitedCloneProbeDefinition() << LimitedCloneProbeDefinition();
  LOKA_VERIFY(source.childrenCount() == 2);
  const int alive = g_probePropsAlive;
  armCloneBudget(1);
  FragmentDefinition copy(source);
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 2);
  LOKA_VERIFY(copy.childrenCount() == 0 && source.childrenCount() == 2);
  LOKA_VERIFY(g_probePropsAlive == alive);
}

void testProbeArmSeatCloneRefusesOccupiedSlot()
{
  g_limitedCloneBudget = -1;
  LimitedCloneProbeDefinition probe;
  NodeDefinitionBase *arms[] = {0, &probe};
  scene::testing::ProbeArmSeatDefinition source(0, arms, 2);
  LOKA_VERIFY(source.armDefinition(0) == 0 && source.armDefinition(1));
  OwnedDef<NodeDefinitionBase> complete(source.clone());
  LOKA_VERIFY(complete.isSet());
  LOKA_VERIFY(complete->retainedDefinitionBranch(0) == 0 && complete->retainedDefinitionBranch(1));
  armCloneBudget(0);
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 1);
  LOKA_VERIFY(!copy.isSet());
}

void testRetainedFragmentCloneRefusesChild()
{
  g_limitedCloneBudget = -1;
  LocalRebuildRefusalSupport::RefusingRetainedFragment source;
  source << LimitedCloneProbeDefinition();
  LOKA_VERIFY(source.childrenCount() == 1);
  armCloneBudget(0);
  OwnedDef<NodeDefinitionBase> copy(source.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 1);
  LOKA_VERIFY(!copy.isSet());
}
