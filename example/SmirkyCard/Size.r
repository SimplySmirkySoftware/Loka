#include "Processes.r"

/* Previous measurement (2026-09-16, MAME Macintosh II and Plus, MAIN.JS):
   about 1.1 MB used. Artwork partition: re-measure (delegator, MAME).
   Engine reload budget: about 0.8 MB + 2 x 512 KiB = 1.8 MB before pictures.
   13 color 320x200 8-bit pictures decoded = 13 x 64,000 B = 832,000 B
   (about 832 KB) on a color Classic screen; mono full pictures plus thumbs
   = 13 x (8,000 + 2,000) B = 130,000 B (about 130 KB).
   Prefer 3 MiB; minimum 2 MiB. A candidate that cannot get its heap is
   refused on the current card. These budgets need the measurement above. */
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground,
    doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
    ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
    onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    3 * 1024 * 1024, 2 * 1024 * 1024
};
