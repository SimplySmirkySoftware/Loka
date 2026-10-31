#include "ToolboxUpdateEvidence.hpp"
#include "ToolboxHost.hpp"
#include "Script.h"
#include "Sound.h"
#include "platform/StringUTF8.hpp"
#include <algorithm>
#include <cstring>
namespace toolbox_host
{
  std::vector<std::string> paintOperations;
  std::vector<Rect> erasedRects;
  long systemScript = smRoman;
  long keyboardScript = smRoman;
  unsigned beeps = 0;
  unsigned scriptReads = 0;
  ControlCalls controlCalls;
  std::vector<std::string> controlTitles, menuTitles, menuAppends, menuInserts, menuSets, disposedMenuItems;
  std::vector<Fill> fills;
  std::vector<Draw> draws;
  std::vector<std::string> pascalDraws, windowTitles, widthPayloads, measurePayloads;
  std::vector<long> backgroundColors, foregroundColors;
  int erases = 0, widths = 0, measures = 0, fonts = 0, metrics = 0;
  unsigned invalidations = 0;
  std::vector<Rect> invalidatedRects;
  void (*afterIdle)(TEHandle) = 0;
  GrafPtr activationPort = 0, deactivationPort = 0;
  int failRegions = 0;
  int regions = 0;
  int refuseRegionAttempt = 0;
  int textHits = 0;
  void reset()
  {
    fills.clear();
    draws.clear();
    foregroundColors.clear();
    pascalDraws.clear();
    widthPayloads.clear();
    measurePayloads.clear();
    erases = widths = measures = fonts = metrics = textHits = regions = 0;
    failRegions = 0;
    beeps = 0;
  }
} // namespace toolbox_host
long GetScriptManagerVariable(short selector)
{
  if (selector == smSysScript)
  {
    ++toolbox_host::scriptReads;
    return toolbox_host::systemScript;
  }
  if (selector == smKeyScript)
    return toolbox_host::keyboardScript;
  assert(false);
  return 0;
}
namespace
{
  GrafPort initialPort = {{0, 0, {0, 0, 0, 0}},
                          7,
                          12,
                          0,
                          blackColor,
                          HostPortState(),
                          0
#ifdef LOKA_HOST_IMAGE_VIEW
                          ,
                          0
#endif
  };
  GrafPtr port = &initialPort;
  short penX, penY;
  Rect Intersection(const Rect &a, const Rect &b)
  {
    Rect r = {
        std::max(a.top, b.top), std::max(a.left, b.left), std::min(a.bottom, b.bottom), std::min(a.right, b.right)};
    return r;
  }
} // namespace
void ValidRect(const Rect *area)
{
  std::vector<Rect> remaining;
  for (size_t i = 0; i < port->native.update.size(); ++i)
  {
    const Rect old = port->native.update[i];
    const Rect cut = Intersection(old, *area);
    if (cut.left >= cut.right || cut.top >= cut.bottom)
    {
      remaining.push_back(old);
      continue;
    }
    const Rect pieces[] = {{old.top, old.left, cut.top, old.right},
                           {cut.bottom, old.left, old.bottom, old.right},
                           {cut.top, old.left, cut.bottom, cut.left},
                           {cut.top, cut.right, cut.bottom, old.right}};
    for (size_t j = 0; j < 4; ++j)
      if (pieces[j].left < pieces[j].right && pieces[j].top < pieces[j].bottom)
        remaining.push_back(pieces[j]);
  }
  port->native.update.swap(remaining);
}
namespace toolbox_host
{
  Rect currentClip()
  {
    return port->native.clip;
  }
} // namespace toolbox_host

bool ToolboxScenePlatformController::intersectWithProjectionClip(const Rect &rect, Rect &out) const
{
  out = Intersection(rect, projectionClip);
  if (this->projectionParentScopes_.activeDepth())
  {
    const loka::core::Frame &clip = this->projectionParentScopes_.current().clipRect;
    const Rect viewport = {static_cast<short>(clip.y),
                           static_cast<short>(clip.x),
                           static_cast<short>(clip.y + clip.height),
                           static_cast<short>(clip.x + clip.width)};
    out = Intersection(out, viewport);
  }
  return !EmptyRect(&out);
}
void GetPort(GrafPtr *out)
{
  port->clipRgn = &port->native.clipRegionPointer;
  *out = port;
}
void SetPort(GrafPtr value)
{
  // Like GetPort above, expose the native clip handle when selecting a port.
  value->clipRgn = &value->native.clipRegionPointer;
  port = value;
}
void TextFont(short value)
{
  port->txFont = value;
  ++toolbox_host::fonts;
}
void TextSize(short value)
{
  port->txSize = value;
}
void TextFace(Style value)
{
  port->txFace = value;
}
short GetDefFontSize()
{
  return 12;
}
short GetSysFont()
{
  return 0;
}
void GetFontInfo(FontInfo *out)
{
  ++toolbox_host::metrics;
  out->ascent = port->txSize;
  out->descent = 3;
  out->leading = 2;
  out->widMax = port->txSize / 3 + ((port->txFace & bold) ? 1 : 0);
}
void MeasureText(short count, const void *bytes, void *charLocs)
{
  toolbox_host::measurePayloads.push_back(std::string(static_cast<const char *>(bytes), count));
  ++toolbox_host::measures;
  short *positions = static_cast<short *>(charLocs);
  for (int i = 0; i <= count; ++i)
    positions[i] = static_cast<short>(i * (port->txSize / 3 + ((port->txFace & bold) ? 1 : 0)));
}
short TextWidth(const void *bytes, short offset, short length)
{
  assert(offset >= 0 && length >= 0);
  toolbox_host::widthPayloads.push_back(std::string(static_cast<const char *>(bytes) + offset, length));
  ++toolbox_host::widths;
  return static_cast<short>(length * (port->txSize / 3 + ((port->txFace & bold) ? 1 : 0)));
}
short StringWidth(const unsigned char *text)
{
  return TextWidth(text + 1, 0, text[0]);
}
void MoveTo(short x, short y)
{
  penX = x;
  penY = y;
}
void DrawText(const void *bytes, short offset, short length)
{
  const toolbox_host::Draw draw = {toolbox_host::currentClip(),
                                   penX,
                                   penY,
                                   port->txSize,
                                   port->txFace,
                                   length,
                                   std::string(static_cast<const char *>(bytes) + offset, length),
                                   port->fgColor};
  toolbox_host::draws.push_back(draw);
}
void SetRect(Rect *out, short l, short t, short r, short b)
{
  out->left = l;
  out->top = t;
  out->right = r;
  out->bottom = b;
}
bool EmptyRect(const Rect *r)
{
  return r->left >= r->right || r->top >= r->bottom;
}
bool SectRect(const Rect *a, const Rect *b, Rect *out)
{
  const Rect intersection = {std::max(a->top, b->top),
                             std::max(a->left, b->left),
                             std::min(a->bottom, b->bottom),
                             std::min(a->right, b->right)};
  *out = intersection;
  return !EmptyRect(out);
}
RgnHandle NewRgn()
{
  ++toolbox_host::regions;
  if (toolbox_host::regions == toolbox_host::refuseRegionAttempt)
    return 0;
  if (toolbox_host::failRegions > 0)
  {
    --toolbox_host::failRegions;
    return 0;
  }
  RgnHandle r = new Region *;
  *r = new Region;
  (*r)->rgnSize = sizeof(Region);
  return r;
}
void DisposeRgn(RgnHandle r)
{
  delete *r;
  delete r;
}
void GetClip(RgnHandle r)
{
  (*r)->rgnBBox = port->native.clip;
}
void RectRgn(RgnHandle r, const Rect *rect)
{
  (*r)->rgnBBox = *rect;
}
void SectRgn(RgnHandle a, RgnHandle b, RgnHandle out)
{
  (*out)->rgnBBox = Intersection((*a)->rgnBBox, (*b)->rgnBBox);
}
void SetClip(RgnHandle r)
{
  port->native.clip = (*r)->rgnBBox;
}
bool RectInRgn(const Rect *r, RgnHandle region)
{
  const Rect both = Intersection(*r, (*region)->rgnBBox);
  return !EmptyRect(&both);
}
void BackColor(long color)
{
  toolbox_host::backgroundColors.push_back(color);
}

void EraseRect(const Rect *rect)
{
  ++toolbox_host::erases;
  toolbox_host::paintOperations.push_back("erase");
  toolbox_host::erasedRects.push_back(*rect);
}

void ClipRect(const Rect *rect)
{
  port->native.clip = *rect;
}

#include "context/ToolboxTextEditorContext.hpp"
namespace toolbox_host
{
  void (*afterKey)(TEHandle) = 0;
  unsigned corruptSets = 0, refuseReads = 0;
  int copied = 0, sets = 0, disposals = 0, failSets = 0, failNew = 0, updates = 0, selections = 0;
}
namespace
{
  void RecordTECall(TEHandle te, const char *operation)
  {
    HostTECall call;
    call.operation = operation;
    GetPort(&call.port);
    call.clip = toolbox_host::currentClip();
    (**te).calls.push_back(call);
  }
} // namespace
TEHandle TENew(const Rect *dest, const Rect *view)
{
  if (toolbox_host::failNew)
  {
    --toolbox_host::failNew;
    return 0;
  }
  TEHandle te = new TERec *;
  *te = new TERec;
  (**te).txFont = port->txFont;
  (**te).txSize = port->txSize;
  FontInfo metrics;
  GetFontInfo(&metrics);
  (**te).lineHeight = metrics.ascent + metrics.descent + metrics.leading;
  (**te).fontAscent = metrics.ascent;
  (**te).destRect = *dest;
  (**te).viewRect = *view;
  (**te).teLength = (**te).selStart = (**te).selEnd = 0;
  (**te).data = 0;
  (**te).autoView = false;
  (**te).active = false;
  (**te).idleCalls = 0;
  (**te).calTextCalls = 0;
  RecordTECall(te, "new");
  (**te).caretState = 0;
  TECalText(te);
  return te;
}
namespace
{
  // Quarantine fake handle slots until exit so identity pins cannot confuse
  // allocator address reuse with pool payout; the TERec is destroyed at flush.
  struct DisposedHandles
  {
    std::vector<TEHandle> handles;
    ~DisposedHandles()
    {
      for (std::size_t i = 0; i < handles.size(); ++i) delete handles[i];
    }
  } disposedHandles;
}
void TEDispose(TEHandle te)
{
  RecordTECall(te, "dispose");
  ++toolbox_host::disposals;
  delete *te;
  *te = 0;
  disposedHandles.handles.push_back(te);
}
void TESetText(const void *bytes, long length, TEHandle te)
{
  RecordTECall(te, "set-text");
  ++toolbox_host::sets;
  if (toolbox_host::failSets)
  {
    --toolbox_host::failSets;
    length = 0;
  }
  (**te).text.assign(static_cast<const char *>(bytes), length);
  if (toolbox_host::corruptSets && length)
  { --toolbox_host::corruptSets; (**te).text[0] ^= 1; }
  (**te).teLength = static_cast<short>(length);
  TECalText(te);
}
Handle TEGetText(TEHandle te)
{
  RecordTECall(te, "get-text");
  if (toolbox_host::refuseReads) { --toolbox_host::refuseReads; return 0; }
  (**te).data = const_cast<char *>((**te).text.data());
  return &(**te).data;
}
void TESetSelect(short a, short b, TEHandle te)
{
  RecordTECall(te, "select");
  ++toolbox_host::selections;
  (**te).selStart = a;
  (**te).selEnd = b;
}
namespace
{
  void ApplyTECompletion(TEHandle te, std::vector<HostTECompletion> &script)
  {
    if (script.empty())
      return;
    (**te).destRect = script.front().destRect;
    (**te).selStart = script.front().selStart;
    (**te).selEnd = script.front().selEnd;
    script.erase(script.begin());
  }
} // namespace
void TEKey(char key, TEHandle te)
{
  RecordTECall(te, "key");
  TERec &t = **te;
  if (key >= 28 && key <= 31)
  {
    t.selStart = std::max(0, std::min(static_cast<int>(t.teLength), t.selStart + (key == 28 ? -1 : 1)));
    t.selEnd = t.selStart;
    ApplyTECompletion(te, t.keyCompletions);
    return;
  }
  if (t.selStart != t.selEnd)
    t.text.erase(t.selStart, t.selEnd - t.selStart);
  else if (key == '\b' && t.selStart)
  {
    --t.selStart;
    t.text.erase(t.selStart, 1);
  }
  if (key != '\b')
    t.text.insert(t.selStart++, 1, key);
  t.selEnd = t.selStart;
  t.teLength = static_cast<short>(t.text.size());
  TECalText(te);
  ApplyTECompletion(te, t.keyCompletions);
  if (toolbox_host::afterKey) toolbox_host::afterKey(te);
}
void TEClick(Point p, bool, TEHandle te)
{
  RecordTECall(te, "click");
  short offset = 0;
  int row = std::max(0, (p.v - (**te).viewRect.top) / 16);
  for (; offset < (**te).teLength && row; ++offset)
    if ((**te).text[offset] == '\r')
      --row;
  offset = std::min(static_cast<int>((**te).teLength), offset + std::max(0, (p.h - (**te).viewRect.left) / 6));
  TESetSelect(offset, offset, te);
  ApplyTECompletion(te, (**te).clickCompletions);
}
void TEAutoView(bool value, TEHandle te)
{
  RecordTECall(te, "auto-view");
  (**te).autoView = value;
}
void TECalText(TEHandle te)
{
  RecordTECall(te, "cal-text");
  ++(**te).calTextCalls;
  TERec &t = **te;
  t.nLines = 0;
  const int capacity = std::max(1, (t.destRect.right - t.destRect.left) / 6);
  int column = 0;
  t.lineStarts[0] = 0;
  for (short i = 0; i < t.teLength; ++i)
  {
    ++column;
    if (t.text[i] == '\r' || (column == capacity && i + 1 < t.teLength && t.text[i + 1] != '\r'))
    {
      t.lineStarts[++t.nLines] = i + 1;
      column = 0;
    }
  }
  if (column)
    t.lineStarts[++t.nLines] = t.teLength;
}
void TEScroll(short horizontal, short vertical, TEHandle te)
{
  RecordTECall(te, "scroll");
  OffsetRect(&(**te).destRect, horizontal, vertical);
}
void TEUpdate(const Rect *, TEHandle te)
{
  RecordTECall(te, "update");
  toolbox_host::paintOperations.push_back("text");
  ++toolbox_host::updates;
}
signed char HGetState(Handle) { return 0; }
void HSetState(Handle, signed char) {}
void HLock(Handle) {}
void HUnlock(Handle) {}
void BlockMoveData(const void *source, void *dest, long length)
{
  toolbox_host::copied += length;
  std::memmove(dest, source, length);
}
bool EqualRect(const Rect *a, const Rect *b)
{
  return std::memcmp(a, b, sizeof(Rect)) == 0;
}
void OffsetRect(Rect *r, short x, short y)
{
  r->left += x;
  r->right += x;
  r->top += y;
  r->bottom += y;
}
#ifdef LOKA_HOST_CONTROL_WIDTH
namespace toolbox_host { Rect controlRect; }
HostQD qd;
void FrameRect(const Rect *rect)
{
  toolbox_host::paintOperations.push_back("frame");
  toolbox_host::controlRect = *rect;
}
#else
void FrameRect(const Rect *)
{
  toolbox_host::paintOperations.push_back("frame");
}
#endif
#include "ToolboxTextEditorBinding.cpp"
#include "ToolboxNativeRetirement.cpp"
#include "ToolboxEditTextBinding.cpp"
void ToolboxScenePlatformController::retireTextEditorControl(loka::app::scene::NodeContext *context,
                                                             loka::app::scene::NativeLifetimeHint hint)
{
  this->retireEditTextControl(context, hint);
}
void ToolboxScenePlatformController::flushTE()
{
  this->flushRetiredEntriesInto(this->retiredTextEdits_, this->textEditBucket_);
}

void TEActivate(TEHandle te)
{
  RecordTECall(te, "activate");
  GetPort(&toolbox_host::activationPort);
  (**te).active = true;
}
void TEDeactivate(TEHandle te)
{
  RecordTECall(te, "deactivate");
  GetPort(&toolbox_host::deactivationPort);
  (**te).active = false;
}
void TEIdle(TEHandle te)
{
  RecordTECall(te, "idle");
  ++(**te).idleCalls;
  ApplyTECompletion(te, (**te).idleCompletions);
  if (toolbox_host::afterIdle)
    toolbox_host::afterIdle(te);
}
bool PtInRect(Point p, const Rect *r)
{
  return p.h >= r->left && p.h < r->right && p.v >= r->top && p.v < r->bottom;
}

namespace toolbox_host { GrafPtr frontWindow = 0; }
#include "ToolboxFocus.cpp"
#include "ToolboxEditPublication.cpp"

// Compile the real plain leaf alongside the real attributed leaf in this fixture.
#include "context/ToolboxTextContext.cpp"
#include "ToolboxTextHitPaint.cpp"
void DrawString(const unsigned char *text)
{
  toolbox_host::pascalDraws.push_back(HostPascalBytes(text));
  DrawText(text + 1, 0, text[0]);
}
void SetWTitle(GrafPtr, const unsigned char *text)
{ toolbox_host::windowTitles.push_back(HostPascalBytes(text)); }
short ToolboxScenePlatformController::measureTextWidth(
    const loka::core::String &value, const ToolboxTextFontDescriptor &descriptor) const
{
  return ToolboxTextMeasureScope(*this, descriptor).measure(value);
}

#include "core/util/StateTrackerGuard.hpp"
namespace toolbox_host
{
  short popupItem = 2;
  unsigned tracks = 0;
} // namespace toolbox_host
#include "ToolboxScrollBarLedger.cpp"
#include "ToolboxInputPublication.cpp"
#ifndef LOKA_HOST_CONTROL_WIDTH
#include "context/ToolboxButtonInput.cpp"
#endif
#ifndef LOKA_HOST_CELL_PAINT
#include "context/ToolboxCellInput.cpp"
#endif
#ifndef LOKA_HOST_CONTROL_WIDTH
#include "context/ToolboxPopupMenuInput.cpp"
#endif

#ifdef LOKA_HOST_GAP_DISPATCH
#include "ToolboxNodeDispatch.hpp"
#endif

// The host substitutes geometry traversal; the complete production render
// operation (including extent publication and its continuation) runs below.
namespace
{
  short MaxExplicitControlId(loka::app::scene::Node *) { return 0; }
#ifndef LOKA_HOST_GAP_DISPATCH
  short LayoutNode(loka::app::scene::Node *node,
                   loka::app::scene::LayoutState &state,
                   ToolboxScenePlatformController *controller,
                   loka::app::scene::BoundaryNode *)
  {
    if (!node)
      return 0;
#ifdef LOKA_HOST_CONTROL_ORDER
    if (node->asZStackNode())
      return controller->layoutZStack(node->asZStackNode(), state, 0);
    if (node->getContext())
      return node->getContext()->layout(controller, state);
#endif
    loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode();
    if (surface)
      controller->rectSurfaceExtentLedger_.record(surface, loka::core::Frame(0, 0, state.width, state.height));
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child; child = child->nextInComposition)
      LayoutNode(child, state, controller, 0);
    return state.width;
  }
  void RenderNode(loka::app::scene::Node *node, ToolboxScenePlatformController *controller)
  {
#ifdef LOKA_HOST_CONTROL_ORDER
    if (!node)
      return;
    if (node->asZStackNode())
    {
      controller->renderZStack(node->asZStackNode());
      return;
    }
    if (node->getContext())
      node->getContext()->render(controller);
    if (node->asRectSurfaceNode())
    {
      const Rect surface = {80, 0, 100, 100};
      EraseRect(&surface);
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child;
         child = child->nextInComposition)
      RenderNode(child, controller);
#else
    (void)node;
    if (controller->renderContext) controller->renderContext->render(controller);
#endif
  }
#endif
}
#include "core/Profiler.hpp"
#ifdef LOKA_HOST_GAP_DISPATCH
#include "ToolboxClipBoxRender.cpp"
#endif
#include "ToolboxZStack.cpp"
#include "ToolboxRender.cpp"
#ifdef LOKA_HOST_EDITOR_SCROLL
#include "ToolboxDirtyReplay.hpp"
namespace
{
  bool RectsIntersect(const Rect &a, const Rect &b)
  {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
  }
  bool HasImageViewNode(loka::app::scene::Node *)
  {
    return false;
  }
  bool HasRectSurfaceNode(loka::app::scene::Node *node)
  {
#ifdef LOKA_HOST_ZSTACK_UNITS
    if (!node)
      return false;
    if (node->asRectSurfaceNode())
      return true;
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child;
         child = child->nextInComposition)
      if (HasRectSurfaceNode(child))
        return true;
#else
    (void)node;
#endif
    return false;
  }
  void RenderDirtyRectSurfaces(loka::app::scene::Node *, ToolboxScenePlatformController *, const Rect &) {}
  void RenderDirtyImageViews(loka::app::scene::Node *, ToolboxScenePlatformController *, const Rect &) {}
} // namespace
#include "ToolboxDirtyPresentation.cpp"
#else
void ToolboxScenePlatformController::renderDirty(const Rect &)
{
  this->render(0);
}
#endif

short ToolboxScenePlatformController::measureTextWidth(const loka::core::String &value) const
{
  return this->measureTextWidth(value, ToolboxTextFontDescriptor());
}

void ForeColor(long ink)
{
  port->fgColor = ink;
  toolbox_host::foregroundColors.push_back(ink);
}

void FillRect(const Rect *rect, const Pattern *pattern)
{
  toolbox_host::Fill fill;
  fill.rect = Intersection(*rect, port->native.clip);
  fill.pattern = *pattern;
  fill.ink = port->fgColor;
  if (!EmptyRect(&fill.rect))
    toolbox_host::fills.push_back(fill);
}

// Call tracing only: the host shim deliberately does not rasterize arcs.
namespace toolbox_host
{
  std::vector<Rect> roundFrames;
  int shadowPaints = 0, regionDifferences = 0;
  short shadowMode = 0;
  Pattern shadowPattern = {{0}};
  PenState pen = {patCopy, 1, 1, {{255, 255, 255, 255, 255, 255, 255, 255}}};
} // namespace toolbox_host
namespace
{
  bool recordingCardRegion = false;
  Rect recordedCardRect = {0, 0, 0, 0};
} // namespace
void GetPenState(PenState *out)
{
  *out = toolbox_host::pen;
}
void SetPenState(const PenState *value)
{
  toolbox_host::pen = *value;
}
void PenNormal()
{
  toolbox_host::pen.mode = patCopy;
  toolbox_host::pen.width = toolbox_host::pen.height = 1;
  std::memset(&toolbox_host::pen.pattern, 255, sizeof(Pattern));
}
void PenPat(const Pattern *pattern)
{
  toolbox_host::pen.pattern = *pattern;
}
void PenMode(short mode)
{
  toolbox_host::pen.mode = mode;
}
void OpenRgn()
{
  recordingCardRegion = true;
}
void CloseRgn(RgnHandle region)
{
  (**region).rgnBBox = recordedCardRect;
  recordingCardRegion = false;
}
void FrameRoundRect(const Rect *rect, short, short)
{
  if (recordingCardRegion)
    recordedCardRect = *rect;
  else
  {
    toolbox_host::roundFrames.push_back(*rect);
    toolbox_host::cardOperations.push_back('B');
  }
}
void DiffRgn(RgnHandle a, RgnHandle, RgnHandle out)
{
  ++toolbox_host::regionDifferences;
  (**out).rgnBBox = (**a).rgnBBox;
}
void PaintRgn(RgnHandle)
{
  ++toolbox_host::shadowPaints;
  toolbox_host::shadowMode = toolbox_host::pen.mode;
  toolbox_host::shadowPattern = toolbox_host::pen.pattern;
}

namespace toolbox_host
{
  std::vector<char> cardOperations;
  std::vector<StencilTransfer> stencilTransfers;
  std::vector<Fill> stencilRuns;
} // namespace toolbox_host
void CopyBits(const BitMap *source, const BitMap *, const Rect *, const Rect *dest, short mode, RgnHandle)
{
  toolbox_host::cardOperations.push_back('T');
  toolbox_host::StencilTransfer transfer;
  transfer.rect = *dest;
  transfer.mode = mode;
  transfer.rowBytes = source->rowBytes;
  const unsigned char *bytes = reinterpret_cast<const unsigned char *>(source->baseAddr);
  transfer.bytes.assign(bytes, bytes + source->rowBytes * (source->bounds.bottom - source->bounds.top));
  toolbox_host::stencilTransfers.push_back(transfer);
}
#ifndef LOKA_HOST_RECT_SURFACE
void PaintRect(const Rect *rect)
{
  toolbox_host::cardOperations.push_back('R');
  toolbox_host::Fill run = {*rect, toolbox_host::pen.pattern, toolbox_host::pen.mode};
  toolbox_host::stencilRuns.push_back(run);
}
#endif

void ToolboxScenePlatformController::flushControls()
{
  // Same shared intake as the production flush; unrelated button pooling is
  // outside this fixture, so drain the temporary bucket before it dies.
  loka::app::scene::ExactMatchHandleBucket<ControlRef> buttons(0);
  this->flushRetiredEntriesInto(this->retiredControls_, buttons);
  buttons.drainWith(DisposeControl);
  this->flushRetiredEntriesInto(this->retiredScrollBarControls_, this->scrollBarLedger_.scrollBarBucket_);
}
#include "ToolboxControlFake.cpp"

#include "ToolboxWindowIdle.cpp"

#ifdef LOKA_HOST_CONTROL_ORDER
namespace
{
  enum
  {
    pushButProc = 0
  };
} // namespace
#include "ToolboxButtonControl.cpp"
#include "ToolboxEnabledControls.cpp"
#include "ToolboxContextProps.cpp"
#ifndef LOKA_HOST_GAP_DISPATCH
namespace
{
  void RenderChildren(loka::app::scene::INestable *node, ToolboxScenePlatformController *controller)
  {
    for (loka::app::scene::Node *child = node->childrenHead(); child; child = child->nextInComposition)
      RenderNode(child, controller);
  }
} // namespace
#endif
#include "ToolboxScrollViewRender.cpp"
#endif

#include "ToolboxPendingDialogs.cpp"

namespace toolbox_host
{
  unsigned staticCaptures = 0, staticCopies = 0, staticReleases = 0;
  unsigned bandCaptures = 0, bandCopies = 0, bandReleases = 0, bandHairlines = 0;
  bool staticEligible = true, staticCaptureSucceeds = true, staticCopySucceeds = true;
} // namespace toolbox_host
struct ToolboxBlurPixels::Image
{
  explicit Image(int value)
      : cell_(value)
  {
  }
  ~Image()
  {
    ++(this->cell_ == 8 ? toolbox_host::staticReleases : toolbox_host::bandReleases);
  }
  bool paint()
  {
    ++(this->cell_ == 8 ? toolbox_host::staticCopies : toolbox_host::bandCopies);
    return toolbox_host::staticCopySucceeds;
  }

private:
  const int cell_;
};
ToolboxBlurPixels::ToolboxBlurPixels()
    : image_(0)
{
}
ToolboxBlurPixels::~ToolboxBlurPixels()
{
  this->clear();
}
void ToolboxBlurPixels::clear()
{
  delete this->image_;
  this->image_ = 0;
}
bool ToolboxBlurPixels::eligible(GrafPtr, const Rect &)
{
  return toolbox_host::staticEligible;
}
bool ToolboxBlurPixels::capture(GrafPtr, const Rect &, int cell)
{
  ++(cell == 8 ? toolbox_host::staticCaptures : toolbox_host::bandCaptures);
  this->clear();
  if (!toolbox_host::staticCaptureSucceeds)
    return false;
  this->image_ = new Image(cell);
  return true;
}
bool ToolboxBlurPixels::paint(GrafPtr, const Rect &)
{
  return this->image_ && this->image_->paint();
}
#ifdef LOKA_HOST_ZSTACK_UNITS
#define requestInvalidate requestInvalidateImpl
#define requestInvalidateWithReason requestInvalidateWithReasonImpl
#define requestInvalidateRect requestInvalidateRectImpl
#include "ToolboxWindowInvalidate.cpp"
#undef requestInvalidateRect
#undef requestInvalidateWithReason
#undef requestInvalidate
void ToolboxWindow::requestInvalidate()
{
  ++toolbox_host::invalidations;
  this->requestInvalidateImpl();
}
void ToolboxWindow::requestInvalidateWithReason(ToolboxFullDrawReason reason)
{
  ++toolbox_host::invalidations;
  this->requestInvalidateWithReasonImpl(reason);
}
void ToolboxWindow::requestInvalidateRect(const Rect &rect)
{
  ++toolbox_host::invalidations;
  toolbox_host::invalidatedRects.push_back(rect);
  this->requestInvalidateRectImpl(rect);
}
// Deliberate production/host twin: the OS consumes evidence at BeginUpdate.
static void BeginUpdate(ToolboxWindow::WindowPort *window)
{
  if (window->updateRgn && *window->updateRgn)
    (**window->updateRgn).rgnBBox = Rect();
}
static void EndUpdate(ToolboxWindow::WindowPort *) {}
void ToolboxWindow::drawForUpdate()
{
  if (this->scenePlatformController_)
    assert(!this->scenePlatformController_->erasedPass().isOpen());
  const Rect evidence = ToolboxUpdateEvidence(this->port.updateRgn, this->port.portBits.bounds);
  if (this->scenePlatformController_)
    this->scenePlatformController_->notePresentDamage(
        ToolboxUpdateDamage(this->port.updateRgn, this->port.portBits.bounds));
  BeginUpdate(&this->port);
  this->drawWhole(evidence);
  EndUpdate(&this->port);
}
void ToolboxWindow::drawForFlush()
{
  if (this->scenePlatformController_)
    this->scenePlatformController_->notePresentDamage(
        ToolboxUpdateDamage(this->port.updateRgn, this->port.portBits.bounds));
  this->drawWhole(this->port.portRect);
}
static void DrawGrowIcon(ToolboxWindow::WindowPort *) {}
#include "ToolboxWindowPaint.cpp"

#endif
void ToolboxScenePlatformController::retireNodeContext(loka::app::scene::NodeContext *context,
                                                       loka::app::scene::NativeLifetimeHint lifetimeHint)
{
#include "ToolboxRetireContextRows.inc"
  this->retired.push_back(context);
}

#ifndef LOKA_HOST_ZSTACK_UNITS
void ToolboxWindow::requestInvalidateRect(const Rect &rect)
{
  ++toolbox_host::invalidations;
  toolbox_host::invalidatedRects.push_back(rect);
  if (this->scenePlatformController_)
    this->scenePlatformController_->noteWindowRectRequest();
}
void ToolboxWindow::requestInvalidateWithReason(ToolboxFullDrawReason reason)
{
  ++toolbox_host::invalidations;
  if (this->scenePlatformController_)
    this->scenePlatformController_->noteWindowFullRequest(ToolboxFullDrawReasonName(reason));
}
#endif
