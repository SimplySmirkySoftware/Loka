"""What MacTextEditorSubset.h claims about the engine's binary interface.

Data only: every type with its size, whether it is a class type, and each
field's offset and size; every constant with its value; every routine with
its return and parameter types. compare_subset.py turns these claims into
translation units for both headers and compares what the compiler produced.
Numbers are the Non-Carbon CFM PowerPC layout with two-byte packing.
"""


def fourcc(text):
    """The value of a four-character constant such as 'TEXT'."""
    if len(text) != 4:
        raise ValueError(f"not four characters: {text!r}")
    return int.from_bytes(text.encode("ascii"), "big")


# (name, size, is_class, ((field, offset, size), ...))
TYPES = (
    ("TXNObject", 4, False, ()),
    ("TXNFrameID", 4, False, ()),
    ("TXNOffset", 4, False, ()),
    ("TXNObjectRefcon", 4, False, ()),
    ("TXNFrameOptions", 4, False, ()),
    ("TXNFrameType", 4, False, ()),
    ("TXNFileType", 4, False, ()),
    ("TXNPermanentTextEncodingType", 4, False, ()),
    ("TXNDataType", 4, False, ()),
    ("TXNControlTag", 4, False, ()),
    ("TXNActionKey", 4, False, ()),
    ("TXNInitOptions", 4, False, ()),
    ("TXNFeatureBits", 4, False, ()),
    ("TXNVersionValue", 4, False, ()),
    ("TXNTabType", 1, False, ()),
    ("TXNTypeRunAttributes", 4, False, ()),
    ("TXNTab", 4, True, (("value", 0, 2), ("tabType", 2, 1), ("filler", 3, 1))),
    ("TXNMargins", 8, True,
     (("topMargin", 0, 2), ("leftMargin", 2, 2), ("bottomMargin", 4, 2), ("rightMargin", 6, 2))),
    ("TXNControlData", 4, True,
     (("uValue", 0, 4), ("sValue", 0, 4), ("tabValue", 0, 4), ("marginsPtr", 0, 4))),
    ("TXNAttributeData", 4, True, (("dataPtr", 0, 4), ("dataValue", 0, 4))),
    ("TXNTypeAttributes", 12, True, (("tag", 0, 4), ("size", 4, 4), ("data", 8, 4))),
    ("TXNMacOSPreferredFontDescription", 14, True,
     (("fontID", 0, 4), ("pointSize", 4, 4), ("encoding", 8, 4), ("fontStyle", 12, 1))),
)

# (name, value)
CONSTANTS = (
    ("kTXNDrawGrowIconMask", 1 << 0),
    ("kTXNWantHScrollBarMask", 1 << 2),
    ("kTXNWantVScrollBarMask", 1 << 3),
    ("kTXNNoTSMEverMask", 1 << 4),
    ("kTXNReadOnlyMask", 1 << 5),
    ("kTXNDoNotInstallDragProcsMask", 1 << 10),
    ("kTXNAlwaysWrapAtViewEdgeMask", 1 << 11),
    ("kTXNDontDrawCaretWhenInactiveMask", 1 << 12),
    ("kTXNSingleLineOnlyMask", 1 << 14),
    ("kTXNTextEditStyleFrameType", 1),
    ("kTXNTextFile", fourcc("TEXT")),
    ("kTXNUnicodeTextFile", fourcc("utxt")),
    ("kTXNSystemDefaultEncoding", 0),
    ("kTXNMacOSEncoding", 1),
    ("kTXNUnicodeEncoding", 2),
    ("kTXNTextData", fourcc("TEXT")),
    ("kTXNUnicodeTextData", fourcc("utxt")),
    ("kTXNStartOffset", 0),
    ("kTXNEndOffset", 0x7FFFFFFF),
    ("kTXNUseCurrentSelection", 0xFFFFFFFF),
    ("kTXNIOPrivilegesTag", fourcc("iopv")),
    ("kTXNSelectionStateTag", fourcc("slst")),
    ("kTXNInlineStateTag", fourcc("inst")),
    ("kTXNWordWrapStateTag", fourcc("wwrs")),
    ("kTXNAutoIndentStateTag", fourcc("auin")),
    ("kTXNDoFontSubstitution", fourcc("fSub")),
    ("kTXNDisableDragAndDropTag", fourcc("drag")),
    ("kTXNReadWrite", 0),
    ("kTXNReadOnly", 1),
    ("kTXNSelectionOff", 0),
    ("kTXNSelectionOn", 1),
    ("kTXNUseInline", 0),
    ("kTXNUseBottomline", 1),
    ("kTXNAutoWrap", 0),
    ("kTXNNoAutoWrap", 1),
    ("kTXNAutoIndentOff", 0),
    ("kTXNAutoIndentOn", 1),
    ("kTXNQDFontFamilyIDAttribute", fourcc("font")),
    ("kTXNQDFontSizeAttribute", fourcc("size")),
    ("kTXNQDFontStyleAttribute", fourcc("face")),
    ("kTXNQDFontColorAttribute", fourcc("klor")),
    ("kTXNQDFontFamilyIDAttributeSize", 2),
    ("kTXNQDFontSizeAttributeSize", 2),
    ("kTXNQDFontStyleAttributeSize", 1),
    ("kTXNQDFontColorAttributeSize", 6),
)

# (name, return type, (parameter types...)); an empty tuple is a routine with no parameters
FUNCTIONS = (
    ("TXNInitTextension", "OSStatus",
     ("const TXNMacOSPreferredFontDescription *", "ItemCount", "TXNInitOptions")),
    ("TXNTerminateTextension", "void", ()),
    ("TXNVersionInformation", "TXNVersionValue", ("TXNFeatureBits *",)),
    ("TXNNewObject", "OSStatus",
     ("const FSSpec *", "WindowRef", "Rect *", "TXNFrameOptions", "TXNFrameType", "TXNFileType",
      "TXNPermanentTextEncodingType", "TXNObject *", "TXNFrameID *", "TXNObjectRefcon")),
    ("TXNDeleteObject", "void", ("TXNObject",)),
    ("TXNSetTXNObjectControls", "OSStatus",
     ("TXNObject", "Boolean", "ItemCount", "TXNControlTag *", "TXNControlData *")),
    ("TXNFocus", "void", ("TXNObject", "Boolean")),
    ("TXNDraw", "void", ("TXNObject", "GWorldPtr")),
    ("TXNIdle", "void", ("TXNObject",)),
    ("TXNTSMCheck", "Boolean", ("TXNObject", "EventRecord *")),
    ("TXNKeyDown", "void", ("TXNObject", "const EventRecord *")),
    ("TXNClick", "void", ("TXNObject", "const EventRecord *")),
    ("TXNAdjustCursor", "void", ("TXNObject", "RgnHandle")),
    ("TXNSetFrameBounds", "void", ("TXNObject", "SInt32", "SInt32", "SInt32", "SInt32", "TXNFrameID")),
    ("TXNResizeFrame", "void", ("TXNObject", "UInt32", "UInt32", "TXNFrameID")),
    ("TXNSetViewRect", "void", ("TXNObject", "const Rect *")),
    ("TXNGetViewRect", "void", ("TXNObject", "Rect *")),
    ("TXNSetData", "OSStatus",
     ("TXNObject", "TXNDataType", "void *", "ByteCount", "TXNOffset", "TXNOffset")),
    ("TXNGetData", "OSStatus", ("TXNObject", "TXNOffset", "TXNOffset", "Handle *")),
    ("TXNGetDataEncoded", "OSStatus", ("TXNObject", "TXNOffset", "TXNOffset", "Handle *", "TXNDataType")),
    ("TXNDataSize", "ByteCount", ("TXNObject",)),
    ("TXNGetLineCount", "OSStatus", ("TXNObject", "ItemCount *")),
    ("TXNGetLineMetrics", "OSStatus", ("TXNObject", "UInt32", "Fixed *", "Fixed *")),
    ("TXNGetChangeCount", "ItemCount", ("TXNObject",)),
    ("TXNSetSelection", "OSStatus", ("TXNObject", "TXNOffset", "TXNOffset")),
    ("TXNGetSelection", "void", ("TXNObject", "TXNOffset *", "TXNOffset *")),
    ("TXNSelectAll", "void", ("TXNObject",)),
    ("TXNShowSelection", "void", ("TXNObject", "Boolean")),
    ("TXNOffsetToPoint", "OSStatus", ("TXNObject", "TXNOffset", "Point *")),
    ("TXNPointToOffset", "OSStatus", ("TXNObject", "Point", "TXNOffset *")),
    ("TXNCut", "OSStatus", ("TXNObject",)),
    ("TXNCopy", "OSStatus", ("TXNObject",)),
    ("TXNPaste", "OSStatus", ("TXNObject",)),
    ("TXNClear", "OSStatus", ("TXNObject",)),
    ("TXNUndo", "void", ("TXNObject",)),
    ("TXNCanUndo", "Boolean", ("TXNObject", "TXNActionKey *")),
    ("TXNIsScrapPastable", "Boolean", ()),
    ("TXNConvertToPublicScrap", "OSStatus", ()),
    ("TXNConvertFromPublicScrap", "OSStatus", ()),
    ("TXNSetTypeAttributes", "OSStatus",
     ("TXNObject", "ItemCount", "TXNTypeAttributes *", "TXNOffset", "TXNOffset")),
)
