#ifndef LOKA_WIN32_MENU_ATTACHMENT_TESTS_HPP
#define LOKA_WIN32_MENU_ATTACHMENT_TESTS_HPP

void testWin32MenuAttachmentProjectsOncePerIdentity();
void testWin32MenuAttachmentRetriesRefusedOffer();
void testWin32MenuAttachmentReleaseThenAbsentRemovesMenu();
void testWin32MenuAttachmentAbsentClearsEmptyProjection();
void testWin32MenuAttachmentDisconnectClearsIdentity();
void testWin32MenuAttachmentAppDetachClearsIdentity();
void testWin32AppMenuReofferSkipsMerge();
void testWin32MenuAttachmentReleaseFromSourceDisconnects();
void testWin32SceneDetachReleasesMenu();
void testWin32TwoWindowsOwnTheirMenus();
void testWin32AppShutdownReleasesMenuBeforeAttachmentDestruction();

void testWin32MenuAcceleratorTranslatesDeclaredShortcut();
void testWin32QuitHasNoDefaultAccelerator();
void testWin32MenuAcceleratorInertAfterRelease();
void testWin32MenuAcceleratorSwapsWithProjection();
void testWin32TwoWindowsOwnTheirAccelerators();
void testWin32MenuAcceleratorPrecedesDialogAndDispatch();
void testWin32MenuTitleFollowsState();
void testWin32MenuPopupTitleFollowsState();
void testWin32MenuTitleStateUnbindsOnRelease();
void testWin32AppQuitPlacement();
void testWin32AppQuitSuppressesOrdinaryLeaves();
void testWin32AppQuitTitleFallbackAndPayload();
void testWin32InvisibleAppQuitKeepsOrdinaryQuit();
void testWin32AppQuitRefusalPreservesProjection();
void testWin32EditMenuDefaultsAndFallback();
void testWin32EditMenuTextEditor();
void testWin32EditMenuEditText();
void testWin32EditMenuTargetWalls();
void testWin32EditMenuSelectAllInput();
void testWin32EditMenuPasteShortcut();
void testWin32EditMenuSelectAllReentry();
#endif
