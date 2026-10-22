// Provisional spike partition, not measured on Classic hardware or MAME.
// The 64 preview seats add allocation beyond SimpleText's editor-only workload.
// Native heap sizing is follow-up work; no Classic runtime claim is made.
#include "Processes.r"
#include "LokaClassicTarget.r"

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
	2 * 1024 * 1024,
	2 * 1024 * 1024
#elif LOKA_CLASSIC_68K
	2 * 1024 * 1024,	/* preferred */
	2 * 1024 * 1024	/* minimum */
#else
	2 * 1024 * 1024,	/* preferred */
	2 * 1024 * 1024	/* minimum */
#endif
};
