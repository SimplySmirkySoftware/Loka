#ifndef LOKA_WIN32_CLIP_BOX_TESTS_HPP
#define LOKA_WIN32_CLIP_BOX_TESTS_HPP

void testWin32ClipBoxGeometryAndFlatTwins();
void testWin32ClipBoxCardBandsAndFlatTwin();
void testWin32ClipBoxMixing();
void testWin32ClipBoxZeroWidthDoesNotCoverSibling();
void testWin32ClipBoxCreationRefusalsRetry();
void testWin32ClipBoxDepthRefusalStaysFlat();
void testWin32ClipBoxEntryRefusalKeepsLogicalHeight();
void testWin32ClipBoxChannelRefusalKeepsPlacement();
void testWin32ClipBoxLifecycleAndRetryWhileDetached();
void testWin32ClipBoxContainerMessagesAndGround();

void testWin32ClipBoxReservedContextWall();

void testWin32ClipBoxNaturalHeightAndFirstChannelRefusal();

#endif
