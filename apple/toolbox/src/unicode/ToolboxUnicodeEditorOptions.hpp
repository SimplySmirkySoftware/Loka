#ifndef LOKA_TOOLBOX_UNICODE_EDITOR_OPTIONS_HPP
#define LOKA_TOOLBOX_UNICODE_EDITOR_OPTIONS_HPP
#include "MacTextEditorSubset.h"
/** Editable bottom-line options shared by the production seat and lifecycle probe. */
inline unsigned long ToolboxUnicodeEditorOptions()
{
  return kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask | kTXNDontDrawCaretWhenInactiveMask
         | kTXNDoNotInstallDragProcsMask;
}
#endif
