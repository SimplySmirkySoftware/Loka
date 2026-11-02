#include "ToolboxTextensionFake.hpp"
#include "support/TestVerify.hpp"
#include <cstring>
#include <algorithm>
#include <map>
struct OpaqueTXNObject
{
  Rect frame;
  TXNFrameOptions options;
  std::vector<unsigned short> units, history;
  TXNOffset start, end;
  ItemCount changes;
  OpaqueTXNObject()
      : frame(),
        options(0),
        start(0),
        end(0),
        changes(0)
  {
  }
};
namespace
{
  std::vector<std::string> sequence;
  std::vector<TextensionFake::Call> eventRecords;
  bool consumedEvent = false, rewriteEvent = false;
  std::vector<TextensionFake::Draw> drawings;
  std::vector<TextensionFake::Creation> creationRecords;
  std::vector<TextensionFake::Control> controlRecords;
  std::string failure;
  OSStatus failureStatus;
  unsigned failureOrdinal = 1;
  unsigned objects, deleted, initialized, terminated, controlOrdinal, controlFailure;
  int depth;
  OSStatus record(const char *name, TXNObject object = 0, long argument = 0, const Rect &frame = Rect())
  {
    sequence.push_back(name);
    const TextensionFake::Call call = {name, object, argument, frame};
    eventRecords.push_back(call);
    if (failure == name && --failureOrdinal == 0)
    {
      failure.clear();
      return failureStatus;
    }
    return noErr;
  }
} // namespace
namespace
{
  std::vector<unsigned short> privateScrap, publicScrap, keyUnits;
  bool keyPending = false, replaceKeySelection = true, clickPending = false;
  TXNOffset clickStart, clickEnd;
  bool caretPending = false;
  TXNOffset caretStart, caretEnd;
  TXNObject latestObject;
  long sizeOverride = -1, handleOverride = -1;
  std::map<Handle, long> handleSizes;
  void replaceUnits(TXNObject object, TXNOffset start, TXNOffset end, const std::vector<unsigned short> &units)
  {
    start = std::min<TXNOffset>(start, object->units.size());
    end = std::max(start, std::min<TXNOffset>(end, object->units.size()));
    object->history = object->units;
    object->units.erase(object->units.begin() + start, object->units.begin() + end);
    object->units.insert(object->units.begin() + start, units.begin(), units.end());
    object->start = object->end = start + units.size();
    ++object->changes;
  }
} // namespace
Handle NewHandle(long size)
{
  Handle handle = new char *;
  *handle = new char[size ? size : 1];
  handleSizes[handle] = size;
  return handle;
}
long GetHandleSize(Handle handle)
{
  return handleSizes[handle];
}
void DisposeHandle(Handle handle)
{
  if (!handle)
    return;
  handleSizes.erase(handle);
  delete[] *handle;
  delete handle;
}
namespace TextensionFake
{
  void (*afterKey)() = 0;
  void (*afterService)() = 0;
  void (*afterIdle)() = 0;
  TXNObject latest()
  {
    return latestObject;
  }
  const std::vector<unsigned short> &body(TXNObject object)
  {
    return object->units;
  }
  void nextKey(const std::vector<unsigned short> &units, bool replaceSelection)
  {
    keyUnits = units;
    keyPending = true;
    replaceKeySelection = replaceSelection;
  }
  void nextClick(TXNOffset start, TXNOffset end)
  {
    clickPending = true;
    clickStart = start;
    clickEnd = end;
  }
  void nextCaret(TXNOffset start, TXNOffset end)
  {
    caretPending = true;
    caretStart = start;
    caretEnd = end;
  }
  void injectChange(const std::vector<unsigned short> &units)
  {
    latestObject->units = units;
    ++latestObject->changes;
  }
  void publicText(const std::vector<unsigned short> &units)
  {
    publicScrap = units;
  }
  void lieSize(long size)
  {
    sizeOverride = size;
  }
  void lieHandle(long size)
  {
    handleOverride = size;
  }
} // namespace TextensionFake
OSStatus TXNSetData(TXNObject object, TXNDataType type, void *data, ByteCount size, TXNOffset start, TXNOffset end)
{
  const OSStatus result = record("set-data", object, size);
  if (result != noErr)
    return result;
  if (size & 1 || type != kTXNUnicodeTextData)
    return -50;
  const unsigned short *units = static_cast<const unsigned short *>(data);
  std::vector<unsigned short> replacement;
  if (size)
    replacement.assign(units, units + size / 2);
  replaceUnits(object, start, end, replacement);
  return noErr;
}
OSStatus TXNGetData(TXNObject object, TXNOffset start, TXNOffset end, Handle *out)
{
  const OSStatus result = record("get-data", object);
  if (result != noErr)
    return result;
  start = std::min<TXNOffset>(start, object->units.size());
  end = std::max(start, std::min<TXNOffset>(end, object->units.size()));
  const long size = (end - start) * 2;
  *out = size ? NewHandle(size) : 0;
  if (size)
    std::memcpy(**out, &object->units[start], size);
  if (handleOverride >= 0 && *out)
  {
    handleSizes[*out] = handleOverride;
    handleOverride = -1;
  }
  return noErr;
}
ByteCount TXNDataSize(TXNObject object)
{
  const OSStatus result = record("data-size", object);
  if (result != noErr)
    return static_cast<ByteCount>(result);
  if (sizeOverride >= 0)
  {
    const long size = sizeOverride;
    sizeOverride = -1;
    return size;
  }
  return object->units.size() * 2;
}
ItemCount TXNGetChangeCount(TXNObject object)
{
  record("changes", object);
  return object->changes;
}
void TXNGetSelection(TXNObject object, TXNOffset *start, TXNOffset *end)
{
  record("get-selection", object);
  *start = object->start;
  *end = object->end;
}
OSStatus TXNSetSelection(TXNObject object, TXNOffset start, TXNOffset end)
{
  const OSStatus result = record("set-selection", object, start);
  if (result != noErr)
    return result;
  object->start = std::min<TXNOffset>(start, object->units.size());
  object->end = std::max(object->start, std::min<TXNOffset>(end, object->units.size()));
  return noErr;
}
void TXNSelectAll(TXNObject object)
{
  record("select-all", object);
  object->start = 0;
  object->end = object->units.size();
}
void TXNShowSelection(TXNObject object, Boolean end)
{
  record("show-selection", object, end);
}
OSStatus TXNGetLineCount(TXNObject object, ItemCount *count)
{
  const OSStatus result = record("line-count", object);
  *count = 1;
  for (std::size_t i = 0; i < object->units.size(); ++i)
    if (object->units[i] == '\r' || object->units[i] == '\n')
      ++*count;
  return result;
}
OSStatus TXNCopy(TXNObject object)
{
  const OSStatus result = record("copy", object);
  if (result == noErr)
    privateScrap.assign(object->units.begin() + object->start, object->units.begin() + object->end);
  return result;
}
OSStatus TXNCut(TXNObject object)
{
  const OSStatus result = record("cut", object);
  if (result == noErr)
  {
    privateScrap.assign(object->units.begin() + object->start, object->units.begin() + object->end);
    replaceUnits(object, object->start, object->end, std::vector<unsigned short>());
  }
  return result;
}
OSStatus TXNPaste(TXNObject object)
{
  const OSStatus result = record("paste", object);
  if (result == noErr)
    replaceUnits(object, object->start, object->end, privateScrap);
  return result;
}
void TXNUndo(TXNObject object)
{
  if (record("undo", object) != noErr)
    return;
  object->units.swap(object->history);
  object->start = object->end = 0;
  ++object->changes;
}
Boolean TXNIsScrapPastable()
{
  return record("pastable") == noErr && !privateScrap.empty();
}

namespace TextensionFake
{
  const std::vector<Call> &events()
  {
    return eventRecords;
  }
  void clearCalls()
  {
    sequence.clear();
    eventRecords.clear();
    drawings.clear();
  }
  void service(bool consumed, bool rewrite)
  {
    consumedEvent = consumed;
    rewriteEvent = rewrite;
  }
  const std::vector<Creation> &creations()
  {
    return creationRecords;
  }
  const std::vector<Control> &controls()
  {
    return controlRecords;
  }
  void reset()
  {
    LOKA_VERIFY(objects == 0);
    eventRecords.clear();
    consumedEvent = rewriteEvent = false;
    sequence.clear();
    drawings.clear();
    creationRecords.clear();
    controlRecords.clear();
    failure.clear();
    deleted = initialized = terminated = controlOrdinal = controlFailure = 0;
    depth = 0;
    keyPending = clickPending = caretPending = false;
    afterKey = afterService = afterIdle = 0;
    sizeOverride = handleOverride = -1;
    latestObject = 0;
    privateScrap.clear();
    publicScrap.clear();
    keyUnits.clear();
    LOKA_VERIFY(handleSizes.empty());
  }
  void failNext(const char *name, OSStatus status)
  {
    failure = name;
    failureOrdinal = 1;
    failureStatus = status;
  }
  void failNth(const char *name, unsigned ordinal, OSStatus status)
  {
    failNext(name, status);
    failureOrdinal = ordinal;
  }
  void failControl(unsigned ordinal, OSStatus status)
  {
    controlFailure = ordinal;
    failureStatus = status;
  }
  const std::vector<std::string> &calls()
  {
    return sequence;
  }
  const std::vector<Draw> &draws()
  {
    return drawings;
  }
  unsigned live()
  {
    return objects;
  }
  unsigned deletes()
  {
    return deleted;
  }
  unsigned inits()
  {
    return initialized;
  }
  unsigned terminates()
  {
    return terminated;
  }
  void beginUpdate()
  {
    ++depth;
  }
  void endUpdate()
  {
    LOKA_VERIFY(depth == 1);
    --depth;
  }
} // namespace TextensionFake
OSStatus TXNInitTextension(const TXNMacOSPreferredFontDescription *fonts, ItemCount count, TXNInitOptions options)
{
  LOKA_VERIFY(!fonts && !count && !options);
  ++initialized;
  return record("init");
}
void TXNTerminateTextension()
{
  ++terminated;
  record("terminate");
}
TXNVersionValue TXNVersionInformation(TXNFeatureBits *bits)
{
  *bits = 0;
  record("version");
  return 0x01100000;
}
OSStatus TXNNewObject(const FSSpec *file,
                      WindowRef window,
                      Rect *frame,
                      TXNFrameOptions options,
                      TXNFrameType type,
                      TXNFileType fileType,
                      TXNPermanentTextEncodingType encoding,
                      TXNObject *object,
                      TXNFrameID *frameID,
                      TXNObjectRefcon refcon)
{
  LOKA_VERIFY(!file && window && frame && !refcon);
  LOKA_VERIFY(type == kTXNTextEditStyleFrameType && fileType == kTXNTextFile && encoding == kTXNUnicodeEncoding);
  const TextensionFake::Creation creation = {window, *frame, options};
  creationRecords.push_back(creation);
  const OSStatus status = record("new");
  if (status != noErr)
    return status;
  *object = new OpaqueTXNObject;
  (*object)->frame = *frame;
  (*object)->options = options;
  *frameID = 1;
  latestObject = *object;
  ++objects;
  return noErr;
}
void TXNDeleteObject(TXNObject object)
{
  LOKA_VERIFY(object && objects);
  record("delete", object);
  ++deleted;
  --objects;
  delete object;
}
OSStatus
TXNSetTXNObjectControls(TXNObject object, Boolean clear, ItemCount count, TXNControlTag *tag, TXNControlData *data)
{
  LOKA_VERIFY(object && !clear && count == 1);
  LOKA_VERIFY((*tag == kTXNInlineStateTag && data->uValue == kTXNUseBottomline)
              || (*tag == kTXNDisableDragAndDropTag && data->uValue == 1));
  const TextensionFake::Control control = {*tag, data->uValue};
  controlRecords.push_back(control);
  ++controlOrdinal;
  const OSStatus status = record("controls");
  return controlOrdinal == controlFailure ? failureStatus : status;
}
void TXNDraw(TXNObject object, GWorldPtr port)
{
  LOKA_VERIFY(object && !port);
  record("draw", object, depth);
  TextensionFake::Draw draw = {object, depth};
  drawings.push_back(draw);
}
Boolean TXNTSMCheck(TXNObject object, EventRecord *event)
{
  LOKA_VERIFY(!object);
  record("tsm", object, event->what);
  if (TextensionFake::afterService)
    TextensionFake::afterService();
  if (consumedEvent && rewriteEvent)
    event->what = 0;
  return consumedEvent;
}
void MaxApplZone()
{
  record("maxapplzone");
}

void TXNFocus(TXNObject object, Boolean focused)
{
  LOKA_VERIFY(object);
  record(focused ? "focus-on" : "focus-off", object, focused);
}
OSStatus TXNConvertToPublicScrap()
{
  const OSStatus status = record("scrap-to");
  if (status == noErr)
    publicScrap = privateScrap;
  return status;
}
OSStatus TXNConvertFromPublicScrap()
{
  const OSStatus status = record("scrap-from");
  if (status == noErr)
    privateScrap = publicScrap;
  return status;
}

void TXNIdle(TXNObject object)
{
  LOKA_VERIFY(object);
  record("idle", object);
  if (TextensionFake::afterIdle)
    TextensionFake::afterIdle();
}
void TXNKeyDown(TXNObject object, const EventRecord *event)
{
  if (record("key", object, event->what) != noErr)
    return;
  if (caretPending)
  {
    caretPending = false;
    object->start = std::min<TXNOffset>(caretStart, object->units.size());
    object->end = std::max(object->start, std::min<TXNOffset>(caretEnd, object->units.size()));
  }
  else if (keyPending)
  {
    keyPending = false;
    replaceUnits(object, object->start, replaceKeySelection ? object->end : object->start, keyUnits);
  }
  else if ((event->message & 255) == 8)
    replaceUnits(object, object->start ? object->start - 1 : 0, object->end, std::vector<unsigned short>());
  else if ((event->message & 255) >= 32 || (event->message & 255) == 13)
    replaceUnits(object, object->start, object->end, std::vector<unsigned short>(1, event->message & 255));
  if (TextensionFake::afterKey)
    TextensionFake::afterKey();
}
void TXNClick(TXNObject object, const EventRecord *event)
{
  if (record("click", object, event->what) != noErr)
    return;
  if (clickPending)
  {
    clickPending = false;
    object->start = std::min<TXNOffset>(clickStart, object->units.size());
    object->end = std::max(object->start, std::min<TXNOffset>(clickEnd, object->units.size()));
  }
}
void TXNSetFrameBounds(TXNObject object, SInt32 top, SInt32 left, SInt32 bottom, SInt32 right, TXNFrameID frame)
{
  LOKA_VERIFY(object && frame == 1);
  const Rect rect = {
      static_cast<short>(top), static_cast<short>(left), static_cast<short>(bottom), static_cast<short>(right)};
  object->frame = rect;
  record("bounds", object, frame, rect);
}
