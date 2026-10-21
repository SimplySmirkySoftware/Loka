// SIZE partition for the paint-damage standalone pin bundle (#766).
//
// LokaPaintDamage68K composes eighteen windows (fourteen of them hidden, as
// of #770), each with its own scene, so it stopped fitting in the HelloWorld
// shipping partition it used to borrow (memFullErr, "type 25", before the
// first arm ran). This is a pin vehicle, not an example: it gets its own
// budget so adding an arm never measures the ceiling instead of the arm.
// At 768K the heap had 1,864 bytes free before the ZStack unit arms (main
// 28a95d85), and the band blur change (a second snapshot per controller) ran
// into the out-of-memory alert there; the Frost standalone shares this budget.
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
#if TARGET_API_MAC_CARBON
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
#else
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
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
	1024 * 1024,	/* preferred */
	896 * 1024	/* minimum */
#endif
};
