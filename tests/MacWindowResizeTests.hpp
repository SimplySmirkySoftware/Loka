#ifndef LOKA_MAC_WINDOW_RESIZE_TESTS_HPP
#define LOKA_MAC_WINDOW_RESIZE_TESTS_HPP

void testMacWindowUserResizeStoresNativeFrameWithoutEchoingIntent();
void testMacWindowNativeFrameClampReentersAndRelayouts();

void testMacWindowContentMarginUnspecified();
void testMacWindowContentMarginZero();
void testMacWindowContentMarginExplicit();
void testMacWindowContentMarginStandard();
void testMacWindowContentMarginOversized();
void testMacWindowContentMarginWithoutWindow();

void testMacWindowCreationUsesMenuBarDisplay();
void testMacWindowSecondaryReadBackUsesMenuBarCoordinates();
void testMacWindowSecondarySizeWriteStaysOnSecondary();
void testMacWindowFrameWriteMovesAcrossDisplays();
void testMacWindowOffscreenWriteFallsBackToMenuBarDisplay();

#endif
