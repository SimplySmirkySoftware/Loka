#ifndef LOKA_TESTS_NULL_LAYOUT_TESTS_HPP
#define LOKA_TESTS_NULL_LAYOUT_TESTS_HPP

void testFallbackControlMetricsContract();
void testNullLayoutRowProducesFixedChildGeometry();
void testNullLayoutColumnProducesFixedChildGeometry();
void testNullLayoutFragmentAdvancesAcrossChildren();
void testNullLayoutBoxProducesFixedChildGeometry();
void testNullLayoutFixedBoxOwnsDeclaredExtent();
void testNullLayoutGridProducesFixedChildGeometry();
void testNullLayoutGridReturnedHeight();
void testNullLayoutZStackProducesFixedChildGeometry();
void testNullLayoutLayerSharesZStackSeat();
void testNullLayoutLayerStacksChildren();
void testNullLayerIdentityAndPaintSkip();
void testNullLayoutNestedBoxAndRowProduceFixedChildGeometry();
void testNullLayoutOnChangeUsesRegisteredTraversal();

#endif // LOKA_TESTS_NULL_LAYOUT_TESTS_HPP
