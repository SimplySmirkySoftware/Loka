#ifndef LOKA_TEST_SCRIPT_H
#define LOKA_TEST_SCRIPT_H

enum { smSysScript = 18, smKeyScript = 22, smRoman = 0, smJapanese = 1, smFontForce = 8, smSystemScript = -1 };
namespace toolbox_host
{
  extern bool fractionalWidths;
  extern long fontForce;
  extern short fontScript, lastScriptFont;
  extern unsigned fontScriptReads;
  extern int positionMode;
  extern long systemScript;
  extern long keyboardScript;
  extern unsigned scriptReads;
}
short FontToScript(short font);
long GetScriptManagerVariable(short selector);

#endif
