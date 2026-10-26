#ifndef LOKA_TESTS_EDIT_MENU_HPP
#define LOKA_TESTS_EDIT_MENU_HPP
void testEditMenuMappingExhaustive();
void testEditMenuFocusedEditorSkipsBinding();
void testEditMenuNoFocusRunsBindingOnce();
void testEditMenuNoBindingIsNoOp();
void testEditMenuAllVerbsReachFocusedContext();
void testEditMenuRetiredFocusFallsBack();
void testEditMenuBindingCanDisconnectAttachment();
void testEditMenuFacadeCanDisconnectAttachment();
void testEditMenuTextEditorRecordsVerb();
#endif
