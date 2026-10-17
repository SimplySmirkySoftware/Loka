#ifndef LOKA_WIN32_ZSTACK_UNIT_TESTS_HPP
#define LOKA_WIN32_ZSTACK_UNIT_TESTS_HPP

void testWin32ZStackLaterChildGetsUnitWindow();
void testWin32ZStackUnitPaintsOpaqueGround();
void testWin32ZStackUnitsStackInCompositionOrder();
void testWin32ZStackUnitTakesTabFirst();
void testWin32ZStackUnitInsideViewportPropagatesRefusal();
void testWin32ZStackUnitCreationRefusalLaysOutNothing();
void testWin32ScrollViewRefusedInsideUnit();
void testWin32ZStackUnitForwardsWheelToViewport();

#endif
