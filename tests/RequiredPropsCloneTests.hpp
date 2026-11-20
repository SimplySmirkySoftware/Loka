#ifndef LOKA_TESTS_REQUIRED_PROPS_CLONE_TESTS_HPP
#define LOKA_TESTS_REQUIRED_PROPS_CLONE_TESTS_HPP

#include "app/nodes/nestable/BoundarySection.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "support/Headless.hpp"
#include "support/LimitedCloneProbe.hpp"
#include "support/TestVerify.hpp"

inline void testKeyedSectionCopyPreservesIdentity()
{
  using namespace loka::app;
  using namespace DefinitionCloneTestSupport;
  Section source(37);
  source << LimitedCloneProbeDefinition();
  Section copy(source);
  LOKA_VERIFY(copy.props.key() == 37 && copy.nodeTag() == 37);
  LOKA_VERIFY(copy.childrenCount() == 1);
  loka::core::OwnedDef<scene::NodeDefinitionBase> clone(source.clone());
  LOKA_VERIFY(clone.isSet());
  LOKA_VERIFY(static_cast<const SectionProps *>(clone->propsBase())->key() == 37 && clone->nodeTag() == 37);
  LOKA_VERIFY(clone->asNestableDefinition()->childrenCount() == 1);
  g_limitedCloneBudget = 0;
  Section refused(source);
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(refused.childrenCount() == 0);
  LOKA_VERIFY(refused.props.key() == 37 && refused.nodeTag() == 37);
}

inline void testRefusedScrollViewCopyKeepsBindings()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace DefinitionCloneTestSupport;
  HeadlessStateOwner owner;
  NodeState<loka::core::Frame> viewport;
  Request<ScrollTarget> request;
  StateBatchBase::CreateImmediateState(&owner, viewport, loka::core::Frame(0, 0, 80, 30));
  StateBatchBase::CreateImmediateState(&owner, request, ScrollTarget::None());
  ScrollView source(viewport);
  source.scrollTo(request) << LimitedCloneProbeDefinition();
  LOKA_VERIFY(source.childrenCount() == 1 && source.props.scrollTo_.isValid());
  g_limitedCloneBudget = 0;
  ScrollView refused(source);
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(refused.childrenCount() == 0);
  LOKA_VERIFY(viewport.state() && refused.props.viewport_.state() == viewport.state());
  LOKA_VERIFY(refused.props.scrollTo_.same(source.props.scrollTo_));
}

#endif
