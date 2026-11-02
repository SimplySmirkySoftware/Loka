#pragma once
// PROBE (branch probe/unicode-arrival; never for main). Page 2e of the Toolbox
// Unicode flavor rally asks at which stage of one ToolboxApp iteration a
// Kotoeri confirmation reaches the focused MLTE object. The probe samples
// TXNGetChangeCount of the focused object at every stage and appends one line
// to ARRIVAL.TXT beside the application whenever the count moves or an event
// of interest arrives. A file named ARRIVAL-AE beside the application turns
// on AEProcessAppleEvent for kHighLevelEvent (variant B).
struct EventRecord;
struct OpaqueTXNObject;
namespace loka
{
  namespace toolbox
  {
    namespace probe
    {
#if LOKA_TOOLBOX_UNICODE
      void ArrivalFocus(OpaqueTXNObject *object, bool focused);
      void ArrivalForget(OpaqueTXNObject *object);
      void ArrivalSample(const char *stage, const EventRecord *record);
      void ArrivalNote(const char *text, long value);
      bool ArrivalProcessesAppleEvents();
#else
      inline void ArrivalFocus(OpaqueTXNObject *, bool) {}
      inline void ArrivalForget(OpaqueTXNObject *) {}
      inline void ArrivalSample(const char *, const EventRecord *) {}
      inline void ArrivalNote(const char *, long) {}
      inline bool ArrivalProcessesAppleEvents() { return false; }
#endif
    }
  }
}
