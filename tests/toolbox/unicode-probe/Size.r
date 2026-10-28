// Probe partition: 16 MiB preferred so H1 (2 MiB UTF-16 + capture Handle) fits
// with room; the minimum that still passes is found on the second run.
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	16 * 1024 * 1024,	/* preferred */
	8 * 1024 * 1024	/* minimum */
};
