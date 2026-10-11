#ifndef LOKA_SIMPLE_TEXT_TESTS_HPP
#define LOKA_SIMPLE_TEXT_TESTS_HPP
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(LOKA_RETRO68)
void testSimpleTextCloseCommandClosesItsOwnWindow();
void testSimpleTextCloseTwiceBeforeAdmissionClosesOnce();
void testSimpleTextCloseOfTheLastWindowQuits();
void testSimpleTextCloseDuringAChooserDoesNothing();
void testSimpleTextNewOpensASecondWindow();
void testSimpleTextNewAtCapacitySetsTheError();
void testSimpleTextClosingAWindowDropsItsDocument();
void testSimpleTextLaunchWindowTakesTheSeat();
void testSimpleTextEditorReceivesRemainingWindow();
void testSimpleTextOpenSaveAndSaveAs();
void testSimpleTextNewAndSaveWithoutDestination();
void testSimpleTextPendingCommandsAndTerminalResults();
void testSimpleTextReadAndWriteFailuresPreserveDestination();
void testSimpleTextCommitExhaustionPreservesDestination();
void testSimpleTextRepeatedOpenAndCaretReplacement();
void testSimpleTextMenuAndDialogProps();
void testSimpleTextRibbonFiresTheMenuEmitters();
void testSimpleTextMenuDisconnectsOnUnmount();
#endif
#endif
