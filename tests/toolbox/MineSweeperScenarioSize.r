// The scenario vehicle carries the production board plus audit and actuation
// vocabulary. Keep its partition separate from the shipping example's measured
// budget (#1103): a second registered tour crossed the old 384K/512K partition
// during the existing two-New-Game recomposition sequence. The seeded-reveal
// capture then ran out of memory when the test core grew by 1 KB (#1043
// button follow), with main passing in the same build tree: 704K/576K.
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
#if TARGET_API_MAC_CARBON
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
#else
	ignoreSuspendResumeEvents,
	reserved,
	cannotBackground,
	needsActivateOnFGSwitch,
#endif
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
#if TARGET_API_MAC_CARBON
	isHighLevelEventAware,
#else
	notHighLevelEventAware,
#endif
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
#if TARGET_API_MAC_CARBON
	1024 * 1024,
	1024 * 1024
#else
	704 * 1024,	/* preferred */
	576 * 1024	/* minimum */
#endif
};
