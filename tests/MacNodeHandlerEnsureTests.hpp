#ifndef LOKA_TESTS_MAC_NODE_HANDLER_ENSURE_TESTS_HPP
#define LOKA_TESTS_MAC_NODE_HANDLER_ENSURE_TESTS_HPP

void testMacNodeHandlerEnsureContract();
void testMacCaptureRefusalReleasesBitmapOnce();
void testMacTextItalicTraits();
void testMacTextUnderlineConstruction();
void testMacTextUnderlineApplyStyle();
void testMacTextUnderlineLayoutRecovery();
void testMacTextUnderlineClearMeasurement();
void testMacTextUnderlineTextState();
void testMacTextUnderlineRemoval();
void testMacTextUnderlineEmptyRecovery();
void testMacTextUnderlineColorRemovalAppearance();
void testMacTextUnderlineNeverAttributed();
void testMacAttributedTextUnderlineRuns();
void testMacTextUnderlineSizes();
void testMacAttributedTextUnderlineSizes();
void testMacTextUnderlineStyleState();
void testMacSmallLabelUsesTheSmallSystemFont();

void testMacAttributedTextWholeLineProjection();
void testMacAttributedTextRetainedLifecycle();
void testMacAttributedTextRefusalClearsProjection();

void testMacPlainTextMeasurementReuse();
void testMacAttributedTextMeasurementReuse();
void testMacTextMeasurementRefusalRecovery();
void testMacTextMeasurementInputPublications();

void testMacTextNaturalWidthMatchesLayout();
void testMacTextNaturalWidthDeclines();
void testMacRibbonGroupNaturalWidthFrames();
void testMacRibbonGroupEdgeEnsureAndRetirement();
void testMacButtonNaturalWidthMatchesCell();
void testMacIntrinsicImageNaturalWidthMatchesProjection();
void testMacRibbonNaturalWidthFrames();
void testMacButtonNativeHeightColumn();
void testMacButtonNativeHeightCenteredRow();
void testMacTextNaturalHeightAlignedRow();
void testMacTextNaturalHeightMatchesLeafAndLiveStyle();
void testMacTextNaturalHeightDeclines();
void testMacButtonDeclinedHeightKeepsRowAligned();
void testMacDirectRowNaturalWidthFrames();
void testMacToolbarEllipsisNarrowPixels();
void testMacOrdinaryRowStillSharesWidth();

void testMacContainerGapLeafExits();
void testMacContainerGapColumnAndPassThrough();
void testMacContainerGapNoSeats();
void testMacContainerGapBox();
void testMacContainerGapRow();

void testMacGapDoorColumnFragment();
void testMacGapDoorBoxSiblingIsolation();
void testMacGapDoorZStackReset();
void testMacGapDoorGridReset();
void testMacGapDoorScrollViewResetAndSiblingIsolation();
void testMacGapDoorUnspecifiedColumnReset();
void testMacGapDoorRegisteredColumnBridge();
void testMacGapDoorRowFragment();
void testMacGapDoorRootFragment();
void testMacGapDoorScrollViewContent();

#endif // LOKA_TESTS_MAC_NODE_HANDLER_ENSURE_TESTS_HPP
