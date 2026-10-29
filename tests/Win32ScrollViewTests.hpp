#ifndef LOKA_TESTS_WIN32_SCROLL_VIEW_TESTS_HPP
#define LOKA_TESTS_WIN32_SCROLL_VIEW_TESTS_HPP

void testWin32ToolbarEllipsisSeatPixels();
void testWin32ToolbarShrunkScrollRequestClamp();
void testWin32ScrollViewRebasedClipSeat();
void testWin32ScrollViewViewportSizeNoOverflow();
void testWin32ScrollViewViewportSizeVerticalOverflow();
void testWin32ScrollViewViewportSizeBothOverflows();
void testWin32ScrollViewParentsAndClipsProjectedChildren();
void testWin32ScrollViewOffsetIsRelayoutInput();
void testWin32ScrollViewInsetsPlaceContentBelowTheTopInset();
void testWin32ScrollViewInsetsGrowTheHeightlessExtent();
void testWin32ScrollViewInsetsRefuseAnExtentAboveShort();
void testWin32ScrollViewMessagePublishesOffsetFact();
void testWin32ScrollViewWheelStepsOffsetThroughLinePath();
void testWin32ScrollViewResizeReclampsOffsetOnce();
void testWin32NestedScrollViewRefusesWithoutDisturbingOuterScope();

void testWin32ScrollViewContainerGapsAndNoSeats();
void testWin32ScrollViewGappedOriginRefuses();

void testWin32ScrollViewHorizontalRangeAndBar();
void testWin32ScrollViewHorizontalBarShortensChildHeight();
void testWin32ScrollViewRefusedLayoutKeepsHorizontalMetrics();

void testWin32ScrollViewHorizontalMessagePublishesOffsetFact();

void testWin32ScrollViewHorizontalWheelStepsOffset();

void testWin32ScrollViewInsetBarGeometry();
void testWin32ScrollViewInsetBarRefusalAndDpi();
void testWin32ScrollViewSubordinateRetirement();
void testWin32ScrollViewFrameTabAndArrowFocus();

void testWin32ScrollViewInsetBarTinyAndFractionalSeat();

void testWin32ScrollRequestBothAxesAndNoEcho();
void testWin32ScrollRequestClampReadBack();
void testWin32ScrollRequestReadFailures();
void testWin32ScrollRequestAbsentAxesAndHeightless();
void testWin32ScrollRequestHeightlessKeepsUnmovedX();
void testWin32ScrollRequestZeroRange();
void testWin32ScrollRequestRefusedVisits();
void testWin32ScrollViewSettledInputSequences();
void testWin32ScrollRequestSceneBorrow();
void testWin32ScrollRequestDeferredMount();
void testWin32ScrollRequestNativeSizeOuterBorrow();
void testWin32ScrollRequestFirstVisit();

#endif // LOKA_TESTS_WIN32_SCROLL_VIEW_TESTS_HPP
