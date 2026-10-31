#ifndef LOKA_TESTS_ROW_WIDTH_CONSULTATION_TESTS_HPP
#define LOKA_TESTS_ROW_WIDTH_CONSULTATION_TESTS_HPP

void testRowFixedBoxWidthLeavesOnlyRemainingSeatForText();
void testRowWidthOnlyBoxLeavesOnlyRemainingSeatForText();
void testRowEmptyFragmentConsumesNeitherSeatNorGap();
void testRowMaterializedDialogConsumesNeitherSeatNorGap();
void testRowMaterializedDialogDoesNotRaiseAlignedRowHeight();
void testRowBranchArmSwitchRelayoutsWidthConsultation();
void testRowWidthOnlyBranchArmForwardsWidthClaim();
void testRowHeightOnlyBoxKeepsFlexSeat();
void testRowWidthOnlyBoxUsesRowHeight();
void testColumnWidthOnlyBoxUsesDeclaredWidth();
void testRowOversizedFixedBoxKeepsDeclaredSeatForSibling();
void testRowZeroWidthRowKeepsFixedSeatLiveAndGapped();
void testRowWidthHeuristicForwardsOnlySingleFragmentClaims();

void testRowNaturalWidthConsultation();
void testRowSharedWidthNeverQueriesSource();

void testNullNaturalWidthDeclinesUnrepresentableWidth();

void testStackNaturalWidthComposesGroup();
void testStackNaturalWidthSharedAndDeclaredSeats();
void testStackNaturalWidthDeclinesIncompleteAndOverflow();
void testStackNaturalWidthColumnDeclaredWidths();
void testStackNaturalWidthReadsLiveAxis();

void testContentOverflowIgnoresOfferBoundText();
void testContentOverflowOfferBoundAnswersZero();
void testContentOverflowNaturalDeclinePreservesOutput();
void testContentOverflowNestedMaximumAndOverflow();
void testContentOverflowBoxPaddingIsOfferBound();
void testContentOverflowPassThroughIsTransparent();
void testNaturalRowSeesThroughPassThroughWrapper();
void testContentOverflowRibbonUsesNaturalMeasurement();

void testBoxWidthDoorOrderAndSaturation();
void testBoxWidthColumnAlignment();
void testBoxWidthParentOffersAndHeight();
void testBoxWidthOverflowEstimates();

void testRowEllipsisShrinkIntegerRule();
void testRowEllipsisShrinkClassificationAndSaturation();
void testContentOverflowShrunkFramesAndNestedSeats();

void testMountedNullToolbarShrinksInsideFixedBoxes();

void testRowEllipsisCurrentArmsAndComponent();

void testRowEllipsisRefusalsAndLiveGaps();
void testContentOverflowFixedCarouselAndSharedSeat();

void testRowWrappedButtonHeightOffers();
void testRowShownButtonAndMultipleLeafHeightOffers();

void testRowSingleLiveLeafWidthShapes();

void testSingleLiveLeafStopsAtSecondSeat();

void testRowLabelsShareTables();
void testRowPolicyShapeWork();
void testRowLabelsShareWrapperFacts();
void testMountedNullLabelsShareStateAndFloor();
void testRowLabelsShareFloorsAndCandidates();
void testRowLabelsShareNestedAndRange();

#endif // LOKA_TESTS_ROW_WIDTH_CONSULTATION_TESTS_HPP
