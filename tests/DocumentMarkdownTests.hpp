#ifndef LOKA_DOCUMENT_MARKDOWN_TESTS_HPP
#define LOKA_DOCUMENT_MARKDOWN_TESTS_HPP
void testDocumentMarkdownBlocks();
void testDocumentMarkdownInline();
void testDocumentMarkdownPhysicalLines();
void testDocumentMarkdownFailure();
void testDocumentMarkdownWalk();
void testDocumentMarkdownSeats();
void testDocumentMarkdownLazyWindow();
/** Cost: one Markdown export and optionally one HTML export per generated block. */
int documentExportCases(unsigned seed, unsigned count, bool html);
#endif
