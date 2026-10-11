#ifndef LOKA_FOR_TESTS_HPP
#define LOKA_FOR_TESTS_HPP

void testForIndexBuildsHandWrittenSectionTree();
void testForRejectsDuplicateKeysWithinBatch();
void testForRejectsInvalidTagsBeforeInsertion();
void testForFactoryCloneFailureLeavesParentUnchanged();
void testForFactoryCloneRefusalReportsToDeclarationWindow();
void testForSuccessfulDeclarationDoesNotReportRefusal();
void testUniqueTaggedSiblingListRejectsAnonymousSibling();
void testForVectorBuilderReadsCurrentContentsAtAppend();
void testForWindowBuildsHandWrittenSubrange();
void testForWindowClampsToLastValidStart();
void testForWindowSurvivesKeyChainInBothOrders();
void testForWindowRejectsDuplicateKeysOutsideWindow();

#endif // LOKA_FOR_TESTS_HPP
