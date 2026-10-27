// SIZE partition override for the Retro68/Classic build.
// Tutorial is a small sample app.
// Measured need (MAME maciix, 2026-10-27, 0.0.8 Tutorial PR 1): 527.6K = peak live
// heap 498736 B + stack/A5/zone 41476 B, with Step4Node as the root (the README's
// "switch the typedef" path) after pressing Add 100 times to the declared
// capacity. The Keyed reservation for 100 rows alone raises the startup need to
// 461.6K. The default DoItYourself root needs less (338.8K, 2026-10-10).
// Rerun tests/toolbox/measure-example-heaps.sh after a size-relevant change.
// 68K: minimum = need x 1.1 rounded up to 32K, preferred = need x 1.5 rounded up to 64K.
// Rez resource ordering: Retro68APPL.r's SIZE (-1) is Rezzed first; this one
// is appended after and overrides it.
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
	/* Carbon partitions unmeasured: keep the RetroCarbonAPPL.r defaults so
	   a Carbon build is flag- and size-identical to the template. */
	1024 * 1024,
	1024 * 1024
#elif LOKA_CLASSIC_68K
	832 * 1024,	/* preferred */
	608 * 1024	/* minimum */
#else
	/* PPC, measured on MAME pmac6100 / Mac OS 8.1 (2026-10-10, #1109):
	   need 62K with virtual memory off, where the Process Manager adds
	   the code fragment to this partition; with it on, the heap peak
	   without the code section is 136K. Both fit, so the values stay. */
	512 * 1024,	/* preferred */
	384 * 1024	/* minimum */
#endif
};
