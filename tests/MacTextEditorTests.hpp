#ifndef LOKA_MAC_TEXT_EDITOR_TESTS_HPP
#define LOKA_MAC_TEXT_EDITOR_TESTS_HPP
void testMacTextEditorLineActions();
void testMacTextEditorRequests();
void testMacTextEditorRequestReverse();
void testMacTextEditorUndoLocation();
void testMacTextEditorStorageChanges();
void testMacTextEditorStorageAttributes();
void testMacTextEditorMultilinePasteRefusal();
void testMacTextEditorRefusals();
void testMacTextEditorCapacityRows();
void testMacTextEditorRowAdmissionRefusal();
void testMacTextEditorCompositionRefusal();
void testMacTextEditorNestedInput();
void testMacTextEditorReplacementFailure();
void testMacTextEditorHighlightAndLifecycle();
void testMacTextEditorQueuedRequests();

void testMacTextEditorPageCommands();
void testMacTextEditorPageBindingReplacement();
void testMacTextEditorPagePriority();
void testMacTextEditorPageEmptyLine();
void testMacTextEditorPageGeometryDeclinesWrappedLine();

void testMacTextEditorDetachHidesAndRetireRemoves();
void testMacTextEditorConversionHelpers();
void testMacTextEditorUtf8Boundary();
void testMacTextEditorUtf8StyleFallback();
void testMacTextEditorUtf8OpenCarets();
#endif
