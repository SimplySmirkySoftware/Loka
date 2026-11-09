#ifndef LOKA_TESTS_ATTRIBUTED_TEXT_TESTS_HPP
#define LOKA_TESTS_ATTRIBUTED_TEXT_TESTS_HPP

void testTextBreakerCharacterization();
void testAttributedTextMetrics();
void testAttributedTextPropsIdentity();
void testAttributedTextDirtySeatsAndEditorPresentation();
void testAttributedTextInvalidProjectionRefusesAndRecovers();
void testAttributedTextHandlerCannotBeReplaced();

void testAttributedTextSegmentationIndependentLayout();
void testAttributedTextWrapUsesJoinedWordsAndRunMetrics();

void testTextBreakerRangesAndRefusal();
void testNullTextShapingDispatch();

void testTextSpanTable();

void testTextBreakerAllocationCensus();

void testAttributedStringEstimateExtent();
void testAttributedStringEstimateRefusal();

void testTextBreakerCountingCapacityRefusal();

void testTextSpanUnlinkedCharacterization();

void testTextSpanLinkBoundaries();
void testTextSpanPreviousNonempty();
void testTextSpanDescriptorInlineGrowth();
void testTextSpanDescriptorHeapGrowth();
void testTextSpanDescriptorUnderfill();
void testTextSpanCharacterHeapGrowth();
void testTextSpanCharacterUnderfill();
void testTextSpanEmptyBecomesNonempty();
void testTextSpanNonemptyBecomesEmpty();

#endif
