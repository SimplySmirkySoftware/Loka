#ifndef LOKA_TOOLBOX_MULTIVERSAL_SCRIPT_H
#define LOKA_TOOLBOX_MULTIVERSAL_SCRIPT_H

// Multiversal has no Script.h umbrella; its combined header declares the
// Script Manager (GetScriptManagerVariable, smSysScript, smRoman) that
// ToolboxPascalText.cpp reads.
#include <Multiverse.h>

// Multiversal declares CharacterByteType but omits its byte-type constants.
// Match Universal Script.h (CharByte byte types).
enum
{
  smFirstByte = -1
};

#endif
