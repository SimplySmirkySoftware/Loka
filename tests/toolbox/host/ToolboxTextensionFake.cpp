#include "ToolboxTextensionFake.hpp"
#include "support/TestVerify.hpp"
#include <cstring>
struct OpaqueTXNObject
{
  Rect frame;
  TXNFrameOptions options;
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
  unsigned objects, deleted, initialized, terminated, controlOrdinal, controlFailure;
  int depth;
  OSStatus record(const char *name, TXNObject object = 0, long argument = 0, const Rect &frame = Rect())
  {
    sequence.push_back(name);
    const TextensionFake::Call call = {name, object, argument, frame};
    eventRecords.push_back(call);
    if (failure == name)
    {
      failure.clear();
      return failureStatus;
    }
    return noErr;
  }
} // namespace
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
  }
  void failNext(const char *name, OSStatus status)
  {
    failure = name;
    failureStatus = status;
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
  return record("scrap-to");
}
OSStatus TXNConvertFromPublicScrap()
{
  return record("scrap-from");
}

void TXNIdle(TXNObject object)
{
  LOKA_VERIFY(object);
  record("idle", object);
}
void TXNKeyDown(TXNObject object, const EventRecord *event)
{
  record("key", object, event->what);
}
void TXNClick(TXNObject object, const EventRecord *event)
{
  record("click", object, event->what);
}
void TXNSetFrameBounds(TXNObject object, SInt32 top, SInt32 left, SInt32 bottom, SInt32 right, TXNFrameID frame)
{
  LOKA_VERIFY(object && frame == 1);
  const Rect rect = {
      static_cast<short>(top), static_cast<short>(left), static_cast<short>(bottom), static_cast<short>(right)};
  object->frame = rect;
  record("bounds", object, frame, rect);
}
