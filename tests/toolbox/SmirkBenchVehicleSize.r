// The SmirkBench scenario vehicle carries the production SmirkBench code plus
// TEST_BUILD hooks, LOKA_DIAG and the scenario driver. It used to copy the
// shipping example's SIZE (576K preferred), which is sized for the smaller
// production binary (example/SmirkBench/Size.r), and ran with almost no
// headroom: about 3 KB of kernel code (#1776) made smirkbench/text-editor-scroll
// hit the out-of-memory alert during the drag auto-scroll on MAME maciix 8 MB,
// while the same build at 608K preferred passed with 0 differing pixels.
// Like the Scrapbook vehicles (ScrapbookVehicleSize.r, #1103), keep a vehicle
// partition with clear headroom instead of following the shipping budget.
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
	640 * 1024	/* minimum */
#endif
};
