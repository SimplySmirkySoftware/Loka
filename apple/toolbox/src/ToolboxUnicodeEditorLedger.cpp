#include "ToolboxUnicodeEditorLedger.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "unicode/MacTextEditorSubset.h"
#include "ToolboxWindow.hpp"
#include "ToolboxEditorFrame.hpp"
#include "core/Frame.hpp"
#include "core/LokaAlloc.hpp"
#include <cassert>

/** One ledger-owned native object and its borrowed context identity. */
struct ToolboxUnicodeEditorResource
{
  TXNObject object;
  TXNFrameID frame;
  loka::app::scene::NodeContext *context;
  ToolboxUnicodeEditorResource *next;
};
namespace
{
  loka::core::LokaAllocationSite ResourceSite()
  {
    return loka::core::LokaAllocationSite("ToolboxUnicodeEditorLedger", "Resource");
  }
  void DisposeWrapper(ToolboxUnicodeEditorResource *resource)
  {
    if (resource)
      resource->~ToolboxUnicodeEditorResource();
    loka::core::LokaFreeRaw(resource, ResourceSite());
  }
} // namespace
ToolboxUnicodeEditorLedger::Prepared::Prepared(loka::app::scene::NodeContext *context)
    : resource_(static_cast<ToolboxUnicodeEditorResource *>(
          loka::core::LokaAllocRaw(sizeof(ToolboxUnicodeEditorResource), ResourceSite())))
{
  if (this->resource_)
  {
    new (this->resource_) ToolboxUnicodeEditorResource();
    this->resource_->context = context;
    this->resource_->next = 0;
  }
}
ToolboxUnicodeEditorLedger::Prepared::~Prepared()
{
  assert(!this->resource_ || !this->resource_->object);
  DisposeWrapper(this->resource_);
}
ToolboxUnicodeEditorLedger::ToolboxUnicodeEditorLedger()
    : live_(0),
      retired_(0)
{
}
ToolboxUnicodeEditorLedger::~ToolboxUnicodeEditorLedger()
{
  assert(!this->live_ && !this->retired_);
}
bool ToolboxUnicodeEditorLedger::install(ToolboxUnicodeEditorResource *prepared,
                                         ToolboxWindow *window,
                                         const loka::core::Frame &frame,
                                         unsigned long options)
{
  assert(prepared && !prepared->object);
  if (!prepared || !window || !window->window())
    return false;
  if (frame.x < SHRT_MIN || frame.x > SHRT_MAX || frame.y < SHRT_MIN || frame.y > SHRT_MAX)
    return false;
  Rect rect = ToolboxEditorFrame(static_cast<short>(frame.x), static_cast<short>(frame.y), frame.width, frame.height);
  TXNObject object = 0;
  TXNFrameID frameID = 0;
  if (TXNNewObject(0,
                   window->window(),
                   &rect,
                   options,
                   kTXNTextEditStyleFrameType,
                   kTXNTextFile,
                   kTXNUnicodeEncoding,
                   &object,
                   &frameID,
                   0)
      != noErr)
    return false;
  TXNControlTag tag = kTXNInlineStateTag;
  TXNControlData data;
  data.uValue = kTXNUseBottomline;
  if (TXNSetTXNObjectControls(object, false, 1, &tag, &data) != noErr)
  {
    TXNDeleteObject(object);
    return false;
  }
  tag = kTXNDisableDragAndDropTag;
  data.uValue = true;
  if (TXNSetTXNObjectControls(object, false, 1, &tag, &data) != noErr)
  {
    TXNDeleteObject(object);
    return false;
  }
  prepared->object = object;
  prepared->frame = frameID;
  return true;
}
OpaqueTXNObject *ToolboxUnicodeEditorLedger::object(ToolboxUnicodeEditorResource *resource) const
{
  return resource ? resource->object : 0;
}
bool ToolboxUnicodeEditorLedger::replace(ToolboxUnicodeEditorResource *resource,
                                         Prepared &fresh,
                                         ToolboxWindow *window,
                                         const loka::core::Frame &frame,
                                         unsigned long options)
{
  if (!resource || !fresh.resource() || !this->install(fresh.resource(), window, frame, options))
    return false;
  const TXNObject oldObject = resource->object;
  const TXNFrameID oldFrame = resource->frame;
  resource->object = fresh.resource()->object;
  resource->frame = fresh.resource()->frame;
  fresh.resource()->object = oldObject;
  fresh.resource()->frame = oldFrame;
  this->retire(fresh.release());
  return true;
}
void ToolboxUnicodeEditorLedger::link(ToolboxUnicodeEditorResource *resource)
{
  assert(resource && resource->object && !this->find(resource->context));
  resource->next = this->live_;
  this->live_ = resource;
}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::find(loka::app::scene::NodeContext *context)
{
  for (ToolboxUnicodeEditorResource *r = this->live_; r; r = r->next)
    if (r->context == context)
      return r;
  return 0;
}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::take(loka::app::scene::NodeContext *context)
{
  for (ToolboxUnicodeEditorResource **p = &this->live_; *p; p = &(*p)->next)
    if ((*p)->context == context)
    {
      ToolboxUnicodeEditorResource *result = *p;
      *p = result->next;
      result->next = 0;
      return result;
    }
  return 0;
}
void ToolboxUnicodeEditorLedger::retire(ToolboxUnicodeEditorResource *resource)
{
  if (!resource)
    return;
  this->focus(resource, false);
  resource->next = this->retired_;
  this->retired_ = resource;
}
bool ToolboxUnicodeEditorLedger::hasPending() const
{
  return this->retired_ != 0;
}
void ToolboxUnicodeEditorLedger::drain()
{
  while (this->retired_)
  {
    ToolboxUnicodeEditorResource *resource = this->retired_;
    this->retired_ = resource->next;
    TXNDeleteObject(resource->object);
    DisposeWrapper(resource);
  }
  assert(!this->retired_);
}
void ToolboxUnicodeEditorLedger::idle(ToolboxUnicodeEditorResource *resource)
{
  if (resource)
    TXNIdle(resource->object);
}
void ToolboxUnicodeEditorLedger::place(ToolboxUnicodeEditorResource *resource, const loka::core::Frame &frame)
{
  assert(resource && resource->object);
  TXNSetFrameBounds(resource->object, frame.y, frame.x, frame.y + frame.height, frame.x + frame.width, resource->frame);
}
void ToolboxUnicodeEditorLedger::focus(ToolboxUnicodeEditorResource *resource, bool focused)
{
  if (resource)
    TXNFocus(resource->object, focused);
}
void ToolboxUnicodeEditorLedger::draw(ToolboxUnicodeEditorResource *resource)
{
  assert(resource && resource->object);
  TXNDraw(resource->object, 0);
}
#else

ToolboxUnicodeEditorLedger::ToolboxUnicodeEditorLedger()
    : live_(0),
      retired_(0)
{
}
ToolboxUnicodeEditorLedger::~ToolboxUnicodeEditorLedger() {}
bool ToolboxUnicodeEditorLedger::install(ToolboxUnicodeEditorResource *,
                                         ToolboxWindow *,
                                         const loka::core::Frame &,
                                         unsigned long)
{
  return false;
}
OpaqueTXNObject *ToolboxUnicodeEditorLedger::object(ToolboxUnicodeEditorResource *) const
{
  return 0;
}
bool ToolboxUnicodeEditorLedger::replace(
    ToolboxUnicodeEditorResource *, Prepared &, ToolboxWindow *, const loka::core::Frame &, unsigned long)
{
  return false;
}
void ToolboxUnicodeEditorLedger::link(ToolboxUnicodeEditorResource *) {}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::find(loka::app::scene::NodeContext *)
{
  return 0;
}
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::take(loka::app::scene::NodeContext *)
{
  return 0;
}
void ToolboxUnicodeEditorLedger::retire(ToolboxUnicodeEditorResource *) {}
bool ToolboxUnicodeEditorLedger::hasPending() const
{
  return false;
}
void ToolboxUnicodeEditorLedger::drain() {}
ToolboxUnicodeEditorLedger::Prepared::Prepared(loka::app::scene::NodeContext *)
    : resource_(0)
{
}
ToolboxUnicodeEditorLedger::Prepared::~Prepared() {}
void ToolboxUnicodeEditorLedger::idle(ToolboxUnicodeEditorResource *) {}
void ToolboxUnicodeEditorLedger::place(ToolboxUnicodeEditorResource *, const loka::core::Frame &) {}
void ToolboxUnicodeEditorLedger::focus(ToolboxUnicodeEditorResource *, bool) {}
void ToolboxUnicodeEditorLedger::draw(ToolboxUnicodeEditorResource *) {}
#endif
ToolboxUnicodeEditorResource *ToolboxUnicodeEditorLedger::Prepared::release()
{
  ToolboxUnicodeEditorResource *result = this->resource_;
  this->resource_ = 0;
  return result;
}
