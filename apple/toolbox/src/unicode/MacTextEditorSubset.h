#ifndef LOKA_TOOLBOX_MAC_TEXT_EDITOR_SUBSET_H
#define LOKA_TOOLBOX_MAC_TEXT_EDITOR_SUBSET_H

/**
 * Loka's own declarations for the part of the Multilingual Text Engine (MLTE)
 * that the Toolbox Unicode flavor calls.
 *
 * Provenance: written for this repository from Apple's public documentation
 * of the engine (the Multilingual Text Engine reference, Inside Macintosh:
 * Text, and Technote 1176 on Mac OS 9). It is not a copy of, or an excerpt
 * from, Apple's MacTextEditor.h or any other SDK header. The identifiers are
 * the engine's published API names; they are used so that this file names
 * the same exported routines, types and values as the engine (attribution),
 * which is not a claim of originality for the identifiers themselves. A
 * human provenance and originality review of this file is recorded in the
 * pull request that introduced it.
 *
 * Target: the Non-Carbon CFM PowerPC engine exported by libTextension.a on
 * Mac OS 9.0 or later (MLTE 1.1 or later). Only the routines this repository
 * calls are declared; a routine absent here is deliberately not part of the
 * flavor. Values and layouts are compared against the SDK by
 * tests/toolbox/unicode-abi/compare_subset.py on a machine that has the SDK,
 * and that comparison is the evidence that these declarations describe the
 * engine's binary interface.
 *
 * Layout: the engine's structures are two-byte packed. The packing is pushed
 * and restored so that nothing after this header inherits it.
 *
 * Interface sets: the base types come from the Toolbox headers that both
 * Universal Interfaces and Multiversal provide. The scalar typedefs below
 * that Multiversal lacks are declared identically to Universal Interfaces,
 * which C++ accepts as a redeclaration of the same type.
 */

#ifdef __MACTEXTEDITOR__
#error "MacTextEditorSubset.h replaces the SDK's MacTextEditor.h; a translation unit includes one of them, never both."
#endif

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Events.h>
#include <Windows.h>
#include <Files.h>
#include <QDOffscreen.h>

typedef SInt32 OSStatus;
typedef UInt32 FourCharCode;
typedef UInt32 ByteCount;
typedef UInt32 ItemCount;
typedef UInt32 TextEncoding;

#pragma pack(push, 2)

extern "C"
{

  /* ---- Handles and counters ------------------------------------------- */

  /** One text object: the engine's opaque handle for a frame and its text. */
  struct OpaqueTXNObject;
  typedef struct OpaqueTXNObject *TXNObject;

  /** Identifies one frame of a text object. */
  typedef UInt32 TXNFrameID;

  /** A character position inside a text object. */
  typedef UInt32 TXNOffset;

  /** Caller-owned word stored in the object at creation. */
  typedef void *TXNObjectRefcon;

  /** Bit set chosen at creation; see the kTXN*Mask values. */
  typedef UInt32 TXNFrameOptions;

  /** How a frame is laid out; only the TextEdit-style frame is used here. */
  typedef UInt32 TXNFrameType;

  /** File type the object reads and writes. */
  typedef OSType TXNFileType;

  /** How the object stores its text permanently. */
  typedef UInt32 TXNPermanentTextEncodingType;

  /** Encoding of a data buffer handed to or received from the object. */
  typedef OSType TXNDataType;

  /** Selects one per-object setting. */
  typedef FourCharCode TXNControlTag;

  /** Identifies the action an undo would reverse. */
  typedef UInt32 TXNActionKey;

  /** Engine-wide options chosen at initialization. */
  typedef UInt32 TXNInitOptions;

  /** Engine features reported by TXNVersionInformation. */
  typedef UInt32 TXNFeatureBits;

  /** Engine version reported by TXNVersionInformation. */
  typedef UInt32 TXNVersionValue;

  /** Kind of a tab stop. */
  typedef UInt8 TXNTabType;

  /** Selects one text attribute in TXNSetTypeAttributes. */
  typedef FourCharCode TXNTypeRunAttributes;

  /* ---- Values ----------------------------------------------------------- */

  /** Frame options (TXNFrameOptions bits). */
  enum
  {
    kTXNDrawGrowIconMask = 1L << 0,
    kTXNWantHScrollBarMask = 1L << 2,
    kTXNWantVScrollBarMask = 1L << 3,
    kTXNNoTSMEverMask = 1L << 4,
    kTXNReadOnlyMask = 1L << 5,
    kTXNDoNotInstallDragProcsMask = 1L << 10,
    kTXNAlwaysWrapAtViewEdgeMask = 1L << 11,
    kTXNDontDrawCaretWhenInactiveMask = 1L << 12,
    kTXNSingleLineOnlyMask = 1L << 14
  };

  /** Frame types (TXNFrameType). */
  enum
  {
    kTXNTextEditStyleFrameType = 1
  };

  /** File types (TXNFileType). */
  enum
  {
    kTXNTextFile = 'TEXT',
    kTXNUnicodeTextFile = 'utxt'
  };

  /** Permanent encodings (TXNPermanentTextEncodingType). */
  enum
  {
    kTXNSystemDefaultEncoding = 0,
    kTXNMacOSEncoding = 1,
    kTXNUnicodeEncoding = 2
  };

  /** Data buffer encodings (TXNDataType). */
  enum
  {
    kTXNTextData = 'TEXT',
    kTXNUnicodeTextData = 'utxt'
  };

  /** Offsets with a fixed meaning. */
  enum
  {
    kTXNStartOffset = 0,
    kTXNEndOffset = 0x7FFFFFFF,
    kTXNUseCurrentSelection = 0xFFFFFFFF
  };

  /** Per-object settings (TXNControlTag). */
  enum
  {
    kTXNIOPrivilegesTag = 'iopv',
    kTXNSelectionStateTag = 'slst',
    kTXNInlineStateTag = 'inst',
    kTXNWordWrapStateTag = 'wwrs',
    kTXNAutoIndentStateTag = 'auin',
    kTXNDoFontSubstitution = 'fSub',
    kTXNDisableDragAndDropTag = 'drag'
  };

  /** Values of the per-object settings above. */
  enum
  {
    kTXNReadWrite = 0,
    kTXNReadOnly = 1
  };
  enum
  {
    kTXNSelectionOff = 0,
    kTXNSelectionOn = 1
  };
  enum
  {
    kTXNUseInline = 0,
    kTXNUseBottomline = 1
  };
  enum
  {
    kTXNAutoWrap = 0,
    kTXNNoAutoWrap = 1
  };
  enum
  {
    kTXNAutoIndentOff = 0,
    kTXNAutoIndentOn = 1
  };

  /** Text attributes (TXNTypeRunAttributes) and the size of each value. */
  enum
  {
    kTXNQDFontFamilyIDAttribute = 'font',
    kTXNQDFontSizeAttribute = 'size',
    kTXNQDFontStyleAttribute = 'face',
    kTXNQDFontColorAttribute = 'klor'
  };
  enum
  {
    kTXNQDFontFamilyIDAttributeSize = 2,
    kTXNQDFontSizeAttributeSize = 2,
    kTXNQDFontStyleAttributeSize = 1,
    kTXNQDFontColorAttributeSize = 6
  };

  /* ---- Structures ------------------------------------------------------- */

  /** One tab stop. */
  struct TXNTab
  {
    SInt16 value;
    TXNTabType tabType;
    UInt8 filler;
  };
  typedef struct TXNTab TXNTab;

  /** Space kept between the frame edge and the text, in pixels. */
  struct TXNMargins
  {
    SInt16 topMargin;
    SInt16 leftMargin;
    SInt16 bottomMargin;
    SInt16 rightMargin;
  };
  typedef struct TXNMargins TXNMargins;

  /** The value of one per-object setting; the tag says which member holds it. */
  union TXNControlData
  {
    UInt32 uValue;
    SInt32 sValue;
    TXNTab tabValue;
    TXNMargins *marginsPtr;
  };
  typedef union TXNControlData TXNControlData;

  /** The value of one text attribute, by pointer or by value. */
  union TXNAttributeData
  {
    void *dataPtr;
    UInt32 dataValue;
  };
  typedef union TXNAttributeData TXNAttributeData;

  /** One text attribute to apply: which, how large, and its value. */
  struct TXNTypeAttributes
  {
    TXNTypeRunAttributes tag;
    ByteCount size;
    TXNAttributeData data;
  };
  typedef struct TXNTypeAttributes TXNTypeAttributes;

  /** A default font the engine should prefer for one encoding. */
  struct TXNMacOSPreferredFontDescription
  {
    UInt32 fontID;
    Fixed pointSize;
    TextEncoding encoding;
    Style fontStyle;
  };
  typedef struct TXNMacOSPreferredFontDescription TXNMacOSPreferredFontDescription;

  /* ---- Engine session --------------------------------------------------- */

  OSStatus TXNInitTextension(const TXNMacOSPreferredFontDescription *iDefaultFonts,
                             ItemCount iCountDefaultFonts,
                             TXNInitOptions iUsageFlags);
  void TXNTerminateTextension(void);
  TXNVersionValue TXNVersionInformation(TXNFeatureBits *oFeatureFlags);

  /* ---- Object lifetime and settings ------------------------------------- */

  OSStatus TXNNewObject(const FSSpec *iFileSpec,
                        WindowRef iWindow,
                        Rect *iFrame,
                        TXNFrameOptions iFrameOptions,
                        TXNFrameType iFrameType,
                        TXNFileType iFileType,
                        TXNPermanentTextEncodingType iPermanentEncoding,
                        TXNObject *oTXNObject,
                        TXNFrameID *oTXNFrameID,
                        TXNObjectRefcon iRefCon);
  void TXNDeleteObject(TXNObject iTXNObject);
  OSStatus TXNSetTXNObjectControls(TXNObject iTXNObject,
                                   Boolean iClearAll,
                                   ItemCount iControlCount,
                                   TXNControlTag *iControlTags,
                                   TXNControlData *iControlData);

  /* ---- Focus, drawing, idle, events ------------------------------------- */

  void TXNFocus(TXNObject iTXNObject, Boolean iBecomingFocused);
  void TXNDraw(TXNObject iTXNObject, GWorldPtr iDrawPort);
  void TXNIdle(TXNObject iTXNObject);
  Boolean TXNTSMCheck(TXNObject iTXNObject, EventRecord *iEvent);
  void TXNKeyDown(TXNObject iTXNObject, const EventRecord *iEvent);
  void TXNClick(TXNObject iTXNObject, const EventRecord *iEvent);
  void TXNAdjustCursor(TXNObject iTXNObject, RgnHandle ioCursorRgn);

  /* ---- Frame geometry --------------------------------------------------- */

  void TXNSetFrameBounds(
      TXNObject iTXNObject, SInt32 iTop, SInt32 iLeft, SInt32 iBottom, SInt32 iRight, TXNFrameID iTXNFrameID);
  void TXNResizeFrame(TXNObject iTXNObject, UInt32 iWidth, UInt32 iHeight, TXNFrameID iTXNFrameID);
  void TXNSetViewRect(TXNObject iTXNObject, const Rect *iViewRect);
  void TXNGetViewRect(TXNObject iTXNObject, Rect *oViewRect);

  /* ---- Text content ----------------------------------------------------- */

  OSStatus TXNSetData(TXNObject iTXNObject,
                      TXNDataType iDataType,
                      void *iDataPtr,
                      ByteCount iDataSize,
                      TXNOffset iStartOffset,
                      TXNOffset iEndOffset);
  OSStatus TXNGetData(TXNObject iTXNObject, TXNOffset iStartOffset, TXNOffset iEndOffset, Handle *oDataHandle);
  OSStatus TXNGetDataEncoded(
      TXNObject iTXNObject, TXNOffset iStartOffset, TXNOffset iEndOffset, Handle *oDataHandle, TXNDataType iEncoding);
  ByteCount TXNDataSize(TXNObject iTXNObject);
  OSStatus TXNGetLineCount(TXNObject iTXNObject, ItemCount *oLineTotal);
  OSStatus TXNGetLineMetrics(TXNObject iTXNObject, UInt32 iLineNumber, Fixed *oLineWidth, Fixed *oLineHeight);
  ItemCount TXNGetChangeCount(TXNObject iTXNObject);

  /* ---- Selection -------------------------------------------------------- */

  OSStatus TXNSetSelection(TXNObject iTXNObject, TXNOffset iStartOffset, TXNOffset iEndOffset);
  void TXNGetSelection(TXNObject iTXNObject, TXNOffset *oStartOffset, TXNOffset *oEndOffset);
  void TXNSelectAll(TXNObject iTXNObject);
  void TXNShowSelection(TXNObject iTXNObject, Boolean iShowEnd);
  OSStatus TXNOffsetToPoint(TXNObject iTXNObject, TXNOffset iOffset, Point *oPoint);
  OSStatus TXNPointToOffset(TXNObject iTXNObject, Point iPoint, TXNOffset *oOffset);

  /* ---- Editing and the scrap -------------------------------------------- */

  OSStatus TXNCut(TXNObject iTXNObject);
  OSStatus TXNCopy(TXNObject iTXNObject);
  OSStatus TXNPaste(TXNObject iTXNObject);
  OSStatus TXNClear(TXNObject iTXNObject);
  void TXNUndo(TXNObject iTXNObject);
  Boolean TXNCanUndo(TXNObject iTXNObject, TXNActionKey *oTXNActionKey);
  Boolean TXNIsScrapPastable(void);
  OSStatus TXNConvertToPublicScrap(void);
  OSStatus TXNConvertFromPublicScrap(void);

  /* ---- Attributes ------------------------------------------------------- */

  OSStatus TXNSetTypeAttributes(TXNObject iTXNObject,
                                ItemCount iAttrCount,
                                TXNTypeAttributes *iAttributes,
                                TXNOffset iStartOffset,
                                TXNOffset iEndOffset);

} /* extern "C" */

#pragma pack(pop)

#endif
