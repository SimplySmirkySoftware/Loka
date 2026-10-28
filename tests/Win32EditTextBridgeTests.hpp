#ifndef LOKA_WIN32_EDIT_TEXT_BRIDGE_TESTS_HPP
#define LOKA_WIN32_EDIT_TEXT_BRIDGE_TESTS_HPP

void testWin32EditTextBridgeRoundTripsUtf16();

void testWin32TextEditorConversion();
void testWin32TextEditorCapacityRows();
void testWin32TextEditorCapacityDefaultRows();
void testWin32TextEditorActionsUseLineQueries();
void testWin32TextEditorRefusalRestoresAndClearsUndo();
void testWin32TextEditorNestedInput();
void testWin32TextEditorFailedReplacementRetries();
void testWin32TextEditorLayoutDpiAndRetirement();

void testWin32TextEditorQueuedRequests();

void testWin32TextEditorUtf8BoundaryHelpers();
void testWin32TextEditorHintEvidence();
void testWin32TextEditorUtf8ClosedPolicy();
void testWin32TextEditorEvidenceLifetime();
void testWin32TextEditorDeferredUtf8Cases();
void testWin32TextEditorEvidenceCaptureFailure();
void testWin32TextEditorMonospaceFont();

#endif // LOKA_WIN32_EDIT_TEXT_BRIDGE_TESTS_HPP
