#ifndef LOKA_TESTS_CARD_TESTS_HPP
#define LOKA_TESTS_CARD_TESTS_HPP
#ifdef LOKA_CARD_CLONE_FAULT_PINS
#include <cstddef>
// Match the leaf clone independently of other allocations made during composition.
void refuseNextDefinitionAllocation(std::size_t size);
#endif
void testCardStandardStyle();
void testCardDefinitionCloneRefusal();
void testCardInsertionFormsReachContent();
void testCardGeometryAndLimits();
void testCardNaturalWidthAndSeats();
void testCardSingleStyleRetainedAndRefusal();
void testCardBodyReconstruction();
void testCardShadowHarnessRetainedValues();
void testCardShadowValueIdentity();
void testCardShadowSixGeometries();
void testCardShadowSeatLookAndReentry();
void testCardShadowStateWidthRetainsBands();
#endif
