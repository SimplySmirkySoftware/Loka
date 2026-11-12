#include "Win32InputDoor.hpp"
void pin(Win32AttributedTextContext &context)
{
  Win32InputDoor::attributedTextButton(context, WM_LBUTTONDOWN, 0);
}
