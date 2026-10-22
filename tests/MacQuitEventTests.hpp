#ifndef LOKA_MAC_QUIT_EVENT_TESTS_HPP
#define LOKA_MAC_QUIT_EVENT_TESTS_HPP

void testMacQuitEventHoldsReplyAndReservesQuit();
void testMacQuitEventSuccessBeforeQuitReturns();
void testMacQuitEventDeclinesWhenAttemptEnds();
void testMacQuitEventDestructionDeclines();
void testMacQuitEventSecondEventRefused();
void testMacQuitEventWithoutTimerRefused();
void testMacQuitEventWithoutAppRefused();
void testMacQuitEventSequentialAppsAttach();
void testMacQuitEventTerminateFallbackCancelsAndReserves();
void testMacQuitEventQuitTwiceRepliesOnce();

#endif
