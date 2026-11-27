#ifndef LOKA_DOCUMENT_CONTAINER_TESTS_HPP
#define LOKA_DOCUMENT_CONTAINER_TESTS_HPP
void testDocumentNestedListMarkdown();
void testDocumentNestedListHtml();
void testDocumentNestedListGrammar();
void testDocumentNestedListOrdinal();
void testDocumentNestedListLostOrderedList();
void testDocumentNestedListSetextHeading();
void testDocumentNestedListLooseSeparator();
void testDocumentNestedListCaptureRefusal();
void testDocumentNestedListScreen();
void testDocumentNestedListDepth();
void testDocumentNestedListCap();
void testDocumentNumberedListMarkdown();
void testDocumentNumberedListHtml();
void testDocumentNumberedListCap();
void testDocumentNumberedListScreen();
void testDocumentNumberedListRetention();
void testDocumentImageMarkdown();
void testDocumentImageHtml();
void testDocumentImageRender();
void testDocumentImageRetainedSize();
void testDocumentMarkdownContainers();
void testDocumentHtmlContainers();
void testDocumentContainerNative();
int documentContainerCases(unsigned seed, unsigned count);
#endif
