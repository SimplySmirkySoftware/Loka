#include "Win32InputDoor.hpp"
void pin(Win32AttributedTextContext &context)
{
  context.handleLinkButton(WM_LBUTTONDOWN, 0);
}
