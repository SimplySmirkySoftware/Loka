#ifndef LOKA_TESTS_MAC_MENU_PROJECTION_TESTS_HPP
#define LOKA_TESTS_MAC_MENU_PROJECTION_TESTS_HPP

void testMacMenuProjectionDetachesMainMenuBeforeReleasingTarget();

void testMacMenuAttachmentProjectsOnceForIdentity();
void testMacMenuDoorSkipsMergeForUnchangedSources();
void testMacMenuDoorSkipsProjectionDuringTracking();
void testMacMenuAttachmentDisconnectStripsTargetAndAction();
void testMacSceneDetachReleasesMenu();
void testMacMenuReleaseThenAbsentRemovesMainMenu();
void testMacNativeCloseReleasesMenuBeforeReclaim();
void testMacAppShutdownReleasesMenuBeforeAttachmentDestruction();
void testMacMenuAttachmentMergeRefusalPreservesProjection();
void testMacMenuAttachmentDispatchMayDisconnect();

void testMacMenuTitleFollowsState();
void testMacMenuSubmenuTitleFollowsState();
void testMacMenuTitleStateUnbindsOnRelease();

void testMacMenuAppFirstAndSections();
void testMacMenuQuitSuppressionAndSeparators();
void testMacMenuAppMiddlePreservesPayloads();
void testMacMenuEmptyActionTitleFollowsState();
void testMacMenuVisibilityReadOnceAndConstantFallback();
void testMacEditMenuDefaultsAndFallback();
void testMacMenuDeclaredEnabledProjection();
void testMacMenuEnabledNotificationAfterCommandGrowth();
void testMacEditMenuNativeValidation();
void testMacEditMenuValidationReentry();
void testMacEditMenuNoTargetValidationAndFallback();
void testMacEditMenuDispatchToTextView();
#endif // LOKA_TESTS_MAC_MENU_PROJECTION_TESTS_HPP
