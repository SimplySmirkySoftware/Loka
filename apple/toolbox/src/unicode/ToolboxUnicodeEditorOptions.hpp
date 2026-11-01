#ifndef LOKA_TOOLBOX_UNICODE_EDITOR_OPTIONS_HPP
#define LOKA_TOOLBOX_UNICODE_EDITOR_OPTIONS_HPP
#include "MacTextEditorSubset.h"
/** Read-only staging options shared by the production seat and lifecycle probe. */
inline unsigned long ToolboxUnicodeEditorOptions()
{
  return kTXNReadOnlyMask | kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask | kTXNDontDrawCaretWhenInactiveMask
         | kTXNDoNotInstallDragProcsMask;
}
#endif
