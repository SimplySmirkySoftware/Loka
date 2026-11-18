#ifndef LOKA_DOCUMENT_MARKDOWN_TESTS_HPP
#define LOKA_DOCUMENT_MARKDOWN_TESTS_HPP
void testDocumentMarkdownBlocks();
void testDocumentMarkdownInline();
void testDocumentMarkdownPhysicalLines();
void testDocumentMarkdownFailure();
void testDocumentMarkdownWalk();
void testDocumentMarkdownSeats();
void testDocumentMarkdownLazyWindow();
int documentMarkdownCases(unsigned seed, unsigned count);
#endif
