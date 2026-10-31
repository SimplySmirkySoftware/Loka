#ifndef LOKA_TEST_PROCESSES_H
#define LOKA_TEST_PROCESSES_H
#include "Files.h"
struct ProcessSerialNumber { uint32_t highLongOfPSN; uint32_t lowLongOfPSN; };
struct ProcessInfoRec
{
  uint32_t processInfoLength;
  unsigned char *processName;
  FSSpec *processAppSpec;
};
OSErr GetCurrentProcess(ProcessSerialNumber *);
OSErr GetProcessInformation(const ProcessSerialNumber *, ProcessInfoRec *);
void ExitToShell();
#endif
