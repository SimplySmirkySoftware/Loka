#include "NodeDefinitionCloneRefusalTests.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "support/LimitedCloneProbe.hpp"
#include "support/TestVerify.hpp"

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
using namespace DefinitionCloneTestSupport;

namespace
{
  template <class T> void fillSource(T &source)
  {
    source << (Box() << LimitedCloneProbeDefinition()
                     << LimitedCloneProbeDefinition()) << ZStack();
    source.setNodeTag(42);
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
  LOKA_VERIFY(destination.childrenHead() != source.childrenHead());
  LOKA_VERIFY(copy->asNestableDefinition()->childrenHead() != source.childrenHead());
  LOKA_VERIFY(destination.childrenHead()->asNestableDefinition()->childrenHead() !=
              source.childrenHead()->asNestableDefinition()->childrenHead());
}

void testNodeDefinitionByValueRefusalLeavesEmpty()
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
  LOKA_VERIFY(copy.props.padding == loka::app::layout::LayoutGap::unspecified() && copy.nodeTag() == 0);
  LOKA_VERIFY(g_probePropsAlive == alive);
  verifyStructure(source);
}
