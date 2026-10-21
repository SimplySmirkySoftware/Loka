#ifndef LOKA_TESTS_MENU_CLONE_REFUSAL_TESTS_HPP
#define LOKA_TESTS_MENU_CLONE_REFUSAL_TESTS_HPP

void testMenuCloneRefusesGrandchild();
void testMenuAssignmentPreservesDestination();
void testMenuMergeRefusesNestedClone();
void testMenuClonePreservesStructure();
void testMenuCloneFailCounts();
void testMenuItemAssignmentPreservesDestination();
void testMenuBarAssignmentPreservesDestination();
void testMenuCopyConstructorRefusalLeavesEmpty();

void testMenuListCopyCommitsOnlyCompletedChain();
void testMenuSelfAssignmentDoesNotClone();

void testMergeAppMenuRefusalIsAtomic();
#endif
