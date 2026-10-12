#include "Processes.r"

/* PowerPC partition, an estimate rather than a measurement (2026-10-12).
   LokaSmirkyCardPPC.bin carries 1,461,632 B of PEF code in its data fork;
   with virtual memory off the Code Fragment Manager loads it into the
   application heap, unlike the 68K CODE segments that Size.r budgets. Add
   the data (0.1 MB) and Size.r's engine budget (two QuickJS engines of at
   most 512 KiB during one reload admission): about 1.5 + 0.1 + 1.0 = 2.6 MB
   worst case, inside the 3 MiB preferred size. The 2.5 MiB minimum keeps
   startup (1.6 MB + one 512 KiB engine) with room; a reload that cannot get
   its second heap is refused on the current card, as on 68K. Re-measure on
   the owner's 1400c-class PowerPC ("About This Macintosh", VM off, both
   cards and a reload) before claiming anything smaller. */
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground,
    doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
    ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
    onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    3 * 1024 * 1024, 2560 * 1024
};
