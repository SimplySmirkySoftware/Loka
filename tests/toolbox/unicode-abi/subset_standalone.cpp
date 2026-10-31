// Compile pin: MacTextEditorSubset.h is complete on its own, with nothing
// included before it, in both interface sets (Universal locally, Multiversal
// in the Toolbox PPC CI job). The three sizes below are the two-byte packing
// claims that the SDK comparison (compare_subset.py, manifest.py: TXNTab,
// TXNMacOSPreferredFontDescription, TXNControlData) proves against the SDK;
// they are repeated here so the interface set the comparison never sees
// still refuses a header that lost its packing.
#include "unicode/MacTextEditorSubset.h"

#ifndef LOKA_TOOLBOX_MAC_TEXT_EDITOR_SUBSET_H
#error "the subset header did not define its guard"
#endif

typedef char LokaTxnTabIsFourBytes[sizeof(TXNTab) == 4 ? 1 : -1];
typedef char LokaTxnFontDescriptionIsFourteenBytes[sizeof(TXNMacOSPreferredFontDescription) == 14 ? 1 : -1];
typedef char LokaTxnControlDataIsFourBytes[sizeof(TXNControlData) == 4 ? 1 : -1];
