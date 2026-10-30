#ifndef LOKA_STARTUP_REDRAW_TESTS_HPP
#define LOKA_STARTUP_REDRAW_TESTS_HPP

void testStartupRedrawCount_Before();
void testStartupRedrawCount_After();
void testToolboxChildDirtyInvalidationPrefersFullRedraw();
void testToolboxManualInvalidateDoesNotSkipFollowupUpdateDraw();

#endif // LOKA_STARTUP_REDRAW_TESTS_HPP
void testToolboxAutoControlIdsNeverReissueLiveIds();
void testToolboxAutoControlIdsSaturateAtShortMax();
void testToolboxFocusedEditSurvivesLowerBindingErase();
void testToolboxEditControlDetachRetiresExactContext();
void testToolboxEditControlSharedStateChangeFansOut();
void testToolboxEditControlPreparedStorage();
void testToolboxEditControlCapacityReuse();
