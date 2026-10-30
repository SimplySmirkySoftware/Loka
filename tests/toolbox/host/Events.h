#ifndef LOKA_HOST_EVENTS_H
#define LOKA_HOST_EVENTS_H
#include "TextEdit.h"
/** Host event value carried unchanged through the editor seat input door. */
struct EventRecord
{
  unsigned short what;
  unsigned long message;
  unsigned long when;
  Point where;
  unsigned short modifiers;
};
#endif
