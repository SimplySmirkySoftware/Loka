// SIZE partition override for the Retro68/Classic build.
// TE SimpleText clamps its document profile to 256 rows and 8 KiB.
// Measured need (after #1107, MAME maciix, 2026-10-10): 429.0K = peak live heap
// 404884 B + stack/A5/zone 34460 B, under the workload in
// tests/toolbox/measure-example-heaps.sh (rerun it after a size-relevant change):
// open a document at both caps through the open dialog, edit it, and save it.
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
	1024 * 1024,
	1024 * 1024
#elif LOKA_TOOLBOX_UNICODE
	/* Provisional MLTE partition; replace with measured peaks in PR 2b-2.
	   The 4 MiB document profile reserves its committed and candidate
	   snapshots (8 MiB) at attach, before the row arrays, the MLTE object
	   and the code; the minimum must leave room for all of them. */
	24 * 1024 * 1024,	/* preferred */
	16 * 1024 * 1024	/* minimum */
#elif LOKA_CLASSIC_68K
	704 * 1024,	/* preferred */
	480 * 1024	/* minimum */
#else
	/* PPC, measured on MAME pmac6100 / Mac OS 8.1 (2026-10-10, #1109):
	   need 122K with virtual memory off, where the Process Manager adds
	   the code fragment to this partition; with it on, the heap peak
	   without the code section is 211K. Both fit, so the values stay. */
	1024 * 1024,	/* preferred */
	512 * 1024	/* minimum */
#endif
};
