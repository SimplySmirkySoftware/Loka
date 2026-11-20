#ifndef LOKA_DOCUMENT_HTML_TESTS_HPP
#define LOKA_DOCUMENT_HTML_TESTS_HPP
/** Cost: one mounted export per block golden per test invocation. */
void testDocumentHtmlBlocks();
/** Cost: one mounted paragraph per inline golden per test invocation. */
void testDocumentHtmlInline();
/** Cost: one export per refusal with a prefilled destination per test invocation. */
void testDocumentHtmlFailure();
/** Cost: one pruning fixture and two exports per test invocation. */
void testDocumentHtmlWalk();
/** Cost: one subtree export per selected seat observation per test invocation. */
void testDocumentHtmlSeats();
/** Cost: one tool render of one heading per test invocation. */
void testDocumentHtmlRender();
#endif
