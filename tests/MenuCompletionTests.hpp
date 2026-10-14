#ifndef LOKA_MENU_COMPLETION_TESTS_HPP
#define LOKA_MENU_COMPLETION_TESTS_HPP
void testMenuProjectedAtCompletionNotOnActivation();
void testLastWindowCloseProjectsDefaultInSameTurn();
void testMenuSourceOffersDefaultAndSceneIdentity();
void testMergeMenuBarsReplacesInPlaceAndAppendsInOrder();
void testMenuOffersDoNotConsumeCloneRefusal();
void testInactiveWindowCloseKeepsActiveBar();
void testTwoAdmissionsReofferSameSource();
void testBootstrapProjectsOnce();
void testDefaultBarComposedOnceAtBootstrap();
void testDefaultBarBootstrapCloneRefusalLeavesNoDefault();
void testMenuProjectionRecordIdentityAndRelease();
#endif
