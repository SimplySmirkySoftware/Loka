#ifndef LOKA_TESTS_INCOMING_URL_TESTS_HPP
#define LOKA_TESTS_INCOMING_URL_TESTS_HPP

void testURLSchemeParsing();
void testIncomingURLAcceptedAndPreserved();
void testIncomingURLRefusalPreservesOutput();
void testIncomingURLLexicalAgreement();
void testIncomingURLEmbeddedNul();
void testIncomingURLLengthBoundary();
void testIncomingURLNoneAndEquality();
void testIncomingURLRestStartsWith();

#endif // LOKA_TESTS_INCOMING_URL_TESTS_HPP
