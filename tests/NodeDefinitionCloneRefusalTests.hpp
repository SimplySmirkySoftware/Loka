#ifndef LOKA_TESTS_NODE_DEFINITION_CLONE_REFUSAL_TESTS_HPP
#define LOKA_TESTS_NODE_DEFINITION_CLONE_REFUSAL_TESTS_HPP

void testNodeDefinitionCloneRefusesGrandchild();
void testNodeDefinitionAssignmentPreservesDestination();
void testNodeDefinitionCopiesFullStructure();
void testNodeDefinitionByValueRefusalKeepsPropsDropsChildren();
void testNodeDefinitionDirectChildRefusalReleasesCandidate();
void testNodeDefinitionCloneBypassesDerivedCopy();

void testShowCloneRefusesSecondArm();
void testShowCloneRefusesFirstArm();
void testPolicyScopeCloneRefusesSecondChild();
void testShowCloneRefusalPreservesDeclaredRoot();
void testEmptyWrapperClones();
void testShowClonePreservesArmsAndPolicies();
void testPolicyScopeClonePreservesContentAndPolicies();
void testRefusedByValueFragmentCopyIsEmpty();
void testProbeArmSeatCloneRefusesOccupiedSlot();
void testRetainedFragmentCloneRefusesChild();

#endif
