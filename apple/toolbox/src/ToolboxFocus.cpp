#include "ToolboxEditDoor.hpp"
#include "ToolboxEditKey.hpp"
#include "ToolboxPortScope.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "platform/ToolboxMacRoman.hpp"
#include "platform/ToolboxUtf8Bytes.hpp"
#include <Script.h>
#include <Sound.h>

namespace
{
  bool ParkedContext(const loka::app::scene::NodeContext *context)
  {
    return context && context->owner() && !loka::app::scene::IsAttachedCurrentContext(context);
  }

  template <class Row> bool AcceptsNativeFocus(const Row &row)
  {
    return row.usedThisFrame && (row.te || (row.unicode && row.editor->hasNativeHandle()))
           && !EmptyRect(&row.placement.visible());
  }

  bool TabKeyBefore(const Rect &a, std::size_t aIndex, const Rect &b, std::size_t bIndex)
  {
    if (a.top != b.top)
      return a.top < b.top;
    if (a.left != b.left)
      return a.left < b.left;
    return aIndex < bIndex;
  }

  ToolboxEditKey OrdinaryEditKey(char key)
  {
    const unsigned char byte = static_cast<unsigned char>(key);
    const bool roman = byte < 0x80 || (GetScriptManagerVariable(smSysScript) == smRoman
        && GetScriptManagerVariable(smKeyScript) == smRoman);
    return ClassifyEditKey(byte, roman);
  }

  /** Compare canonical decoded native units without constructing a decoded String.
      Collect before borrowing the movable handle; the walk itself cannot allocate. */
  bool EditRoundTrips(TEHandle te, const loka::core::String &source)
  {
    const ToolboxUtf8Bytes utf8(source);
    if (!utf8.valid())
      return false;
    CharsHandle handle = TEGetText(te);
    if (!handle || !*handle || (**te).teLength < 0) return false;
    const unsigned char *native = reinterpret_cast<const unsigned char *>(*handle);
    std::size_t offset = 0;
    for (long i = 0; i < (**te).teLength; ++i)
    {
      unsigned long scalar = native[i];
      ToolboxMacRomanDecode(native[i], scalar);
      if (offset == utf8.length())
        return false;
      const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.bytes() + offset, utf8.length() - offset, false);
      // The strict reader recovers malformed input as '?'; only the literal
      // ASCII '?' can round trip to that scalar.
      if (unit.scalar != scalar || (unit.scalar == '?' && utf8.bytes()[offset] != '?'))
        return false;
      offset += unit.consumed;
    }
    return offset == utf8.length();
  }
}

/** Controller focus reads and key delivery, shared with the host fixture. */
ToolboxEditTextContext *ToolboxScenePlatformController::fallbackFocusContext() const
{
  loka::app::FocusParticipant *row = loka::app::FocusParticipant::from(this->fallbackFocus_.peerRow());
  // Only fallback EditText hits connect this source slot.
  return row ? static_cast<ToolboxEditTextContext *>(row->context()) : 0;
}

bool ToolboxScenePlatformController::readNativeFocus(loka::app::scene::NodeContext *&out)
{
  out = 0;
  if (!this->window_ || !this->window_->window() || FrontWindow() != this->window_->window())
    return false;
  EditTextControlBinding *native = this->editControls_.focused();
  out = native ? native->ownerContext : this->fallbackFocusContext();
  return true;
}

bool ToolboxScenePlatformController::applyNativeFocus(loka::app::scene::NodeContext &ctx)
{
  if (!this->window_ || !this->window_->window() || FrontWindow() != this->window_->window())
    return false;
  loka::app::scene::Node *owner = ctx.owner();
  if (!loka::app::scene::IsAttachedCurrentContext(&ctx))
    return false;
  std::size_t index = 0;
  if (this->editControls_.find(&ctx, index))
  {
    const EditTextControlBinding &binding = this->editControls_[index];
    if (!AcceptsNativeFocus(binding))
      return false;
    // Activation copies native handles before calling TextEdit; no ctx access follows.
    this->activateEditControl(index);
    return true;
  }
  for (std::size_t i = 0; i < this->hitLedger_.editHits_.size(); ++i)
  {
    const EditHit &hit = this->hitLedger_.editHits_[i];
    if (hit.context != &ctx || !hit.text || EmptyRect(&hit.rect))
      continue;
    loka::app::FocusParticipant *row = loka::app::FocusParticipant::from(owner->asFocusParticipant());
    if (!row)
      return false;
    // Plain TE activation has no Loka callbacks. Copy the source before the call.
    this->activateEditControl(this->editControls_.size());
    row->connectSource(this->fallbackFocus_);
    return true;
  }
  return false;
}

bool ToolboxScenePlatformController::handleTabKey(ToolboxTabDirection direction)
{
  if (!this->window_ || !this->window_->window())
    return false;
  const EditTextControlBinding *focused = this->editControls_.focused();
  if (focused && focused->editor)
    return false;
  Rect portRect;
  if (!this->window_->queryPortRect(portRect))
    return false;
  const std::size_t count = this->editControls_.size();
  std::size_t origin = count;
  const bool hasOrigin = this->editControls_.queryFocusedIndex(origin);
  std::size_t edge = count;
  std::size_t next = count;
  const bool forward = direction == TAB_FORWARD;
  for (std::size_t i = 0; i < count; ++i)
  {
    const EditTextControlBinding &row = this->editControls_[i];
    if (!AcceptsNativeFocus(row) || row.editor != 0)
      continue;
    if (!loka::app::scene::IsAttachedCurrentContext(row.ownerContext))
      continue;
    Rect visible;
    if (!SectRect(&row.placement.visible(), &portRect, &visible))
      continue;
    if (edge == count
        || (forward ? TabKeyBefore(row.placement.visible(), i, this->editControls_[edge].placement.visible(), edge)
                    : TabKeyBefore(this->editControls_[edge].placement.visible(), edge, row.placement.visible(), i)))
      edge = i;
    if (hasOrigin
        && (forward ? TabKeyBefore(this->editControls_[origin].placement.visible(), origin, row.placement.visible(), i)
                    : TabKeyBefore(row.placement.visible(), i, this->editControls_[origin].placement.visible(), origin))
        && (next == count
            || (forward
                    ? TabKeyBefore(row.placement.visible(), i, this->editControls_[next].placement.visible(), next)
                    : TabKeyBefore(this->editControls_[next].placement.visible(), next, row.placement.visible(), i))))
      next = i;
  }
  const std::size_t target = next != count ? next : edge;
  if (target == count)
    return false;
  TEHandle te = this->editControls_[target].te;
  const Rect rect = this->editControls_[target].placement.visible();
  this->activateEditControl(target);
  {
    ToolboxEditDoor door(this->window_->window(), this->editControls_[target].placement);
    if (door.clipAllocationRefused(this->editControls_[target].placement))
      LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    TESetSelect(0, (**te).teLength, te);
  }
  this->invalidateUnitOverlap(rect);
  return true;
}

/** B1/B1'/B2/B4: one focused row, no search and no copied App phase. */
bool ToolboxScenePlatformController::syncNativeFocus(NativeFocusMode mode)
{
  EditTextControlBinding *row = this->editControls_.focused();
  if (!row || !row->unicode)
    return false;
  const bool eligible = mode == NATIVE_FOCUS_NORMAL && this->window_ && IsWindowHilited(this->window_->window())
                        && row->editor->hasNativeHandle() && !ParkedContext(row->ownerContext);
  this->unicodeEditors_.focus(row->unicode, eligible);
  return eligible;
}
void ToolboxScenePlatformController::releaseNativeFocus(loka::app::scene::NodeContext *context)
{
  EditTextControlBinding *row = this->editControls_.focused();
  if (row && row->ownerContext == context)
    this->unicodeEditors_.focus(row->unicode, false);
}

bool ToolboxScenePlatformController::offerKeyEvent(const EventRecord &event)
{
  EditTextControlBinding *row = this->editControls_.focused();
  if (!row || !row->unicode || ParkedContext(row->ownerContext))
    return false;
  ToolboxTabDirection direction;
  // Tab belongs to the window's traversal path; direction is decoded there.
  if (QueryToolboxTabKey(static_cast<unsigned char>(event.message), false, direction))
    return false;
  return this->syncNativeFocus(NATIVE_FOCUS_NORMAL) && row->editor->offerKeyEvent(event);
}

bool ToolboxScenePlatformController::handleScrollKey(ToolboxScrollKey key)
{
  EditTextControlBinding *focusedEdit = this->editControls_.focused();
  // A parked owner keeps its focused row until the sweep; its keys do nothing.
  if (focusedEdit && ParkedContext(focusedEdit->ownerContext))
    return true;
  if (focusedEdit && (focusedEdit->te || (focusedEdit->unicode && focusedEdit->editor->hasNativeHandle()))
      && focusedEdit->editor)
  {
    focusedEdit->editor->scrollKey(key);
    return true;
  }
  return false;
}

bool ToolboxScenePlatformController::handleKeyDown(char key)
{
  EditTextControlBinding *focusedEdit = this->editControls_.focused();
  // Same refusal as handleScrollKey; presses use the resolved press door.
  if (focusedEdit && ParkedContext(focusedEdit->ownerContext))
    return true;
  if (focusedEdit && focusedEdit->te)
  {
    if (focusedEdit->editor)
    {
      focusedEdit->editor->key(key);
      return true;
    }
    const ToolboxEditKey kind = OrdinaryEditKey(key);
    switch (kind)
    {
    case EDIT_KEY_CONSUME:
      return true;
    case EDIT_KEY_REFUSE_HIGH:
      SysBeep(1);
      return true;
    case EDIT_KEY_NAVIGATE:
    {
      {
        ToolboxEditDoor door(this->window_->window(), focusedEdit->placement);
        if (door.clipAllocationRefused(focusedEdit->placement))
          LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
        TEKey(key, focusedEdit->te);
      }
      this->invalidateUnitOverlap(focusedEdit->placement.visible());
      return true;
    }
    case EDIT_KEY_DELETE_BACKWARD:
    case EDIT_KEY_DELETE_FORWARD:
    case EDIT_KEY_INSERT:
      break;
    }
    if (!focusedEdit->text) return true;
    this->beginBatchUpdate();
    const loka::core::String before = focusedEdit->text->get();
    if (!focusedEdit->installed.holds(before))
    {
      this->syncEditTextFromState(*focusedEdit);
      this->addPendingDirty(static_cast<ToolboxEditTextContext *>(focusedEdit->ownerContext)->chromeRect());
    }
    TEHandle te = focusedEdit->te;
    const long length = (**te).teLength;
    const long start = (**te).selStart, end = (**te).selEnd;
    if (!focusedEdit->installed.holds(before) || !EditRoundTrips(te, before)
        || start < 0 || end < start || length < end
        || (kind == EDIT_KEY_INSERT && length - (end - start) + 1 > 32767))
      SysBeep(1);
    else if (kind != EDIT_KEY_DELETE_FORWARD || start != end || end != length)
    {
      // No certificate is observable for a native mutation awaiting readback.
      focusedEdit->installed.revoke();
      {
        ToolboxEditDoor door(this->window_->window(), focusedEdit->placement);
        if (door.clipAllocationRefused(focusedEdit->placement))
          LOKA_PAINT_REFUSE(this->observation_, TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
        if (kind == EDIT_KEY_DELETE_FORWARD && start == end)
          TESetSelect(static_cast<short>(start), static_cast<short>(start + 1), te);
        TEKey(kind == EDIT_KEY_INSERT ? key : 8, te);
      }
      this->invalidateUnitOverlap(focusedEdit->placement.visible());
      this->updateStateFromEdit(*focusedEdit, before);
      // Publication may retire or relocate the row. Do not borrow it again.
    }
    this->endBatchUpdate();
    return true;
  }
  this->beginBatchUpdate();
  if (!this->handleTextKey(key))
  {
    this->endBatchUpdate();
    return false;
  }
  this->endBatchUpdate();
  return true;
}

bool ToolboxScenePlatformController::handleTextKey(char key)
{
  ToolboxEditTextContext *context = this->fallbackFocusContext();
  loka::core::State<loka::core::String> *text = context ? context->projectedTextState() : 0;
  if (!text)
  {
    return false;
  }
  const ToolboxEditKey kind = OrdinaryEditKey(key);
  switch (kind)
  {
    case EDIT_KEY_NAVIGATE:
    case EDIT_KEY_DELETE_FORWARD:
    case EDIT_KEY_CONSUME: return true;
    case EDIT_KEY_REFUSE_HIGH: SysBeep(1); return true;
    case EDIT_KEY_DELETE_BACKWARD:
    case EDIT_KEY_INSERT: break;
  }
  const loka::core::String before = text->get();
  std::string utf8;
  if (!loka::platform::CollectUtf8(before, utf8))
  { SysBeep(1); return true; }
  if (kind == EDIT_KEY_DELETE_BACKWARD)
  {
    if (utf8.empty()) return true;
    std::size_t start = utf8.size() - 1;
    while (start && (static_cast<unsigned char>(utf8[start]) & 0xC0) == 0x80) --start;
    const ToolboxTextUnit tail = ToolboxNextTextUnit(utf8.data() + start, utf8.size() - start, true);
    // A malformed byte recovers as '?'; only a literal '?' may have that shape.
    if (tail.consumed != utf8.size() - start
        || (tail.scalar == '?' && utf8[start] != '?'))
    { SysBeep(1); return true; }
    utf8.erase(start);
  }
  else
  {
    const unsigned char byte = static_cast<unsigned char>(key);
    loka::core::String scalar;
    std::string encoded;
    if (!ToolboxDecodeNative(&byte, 1, scalar) || !loka::platform::CollectUtf8(scalar, encoded))
    { SysBeep(1); return true; }
    utf8 += encoded;
  }
  const loka::app::scene::WriteSeat<loka::core::String> seat = context->projectedWriteSeat();
  if (!seat.isValid()) return false;
  seat.set(loka::core::String(utf8));
  return true;
}

void ToolboxScenePlatformController::recordEditHit(const Rect &rect,
                                                   loka::core::State<loka::core::String> *text,
                                                   loka::app::scene::BoundaryNode *boundary,
                                                   ToolboxEditTextContext *context)
{
  Rect clipped;
  if (!this->intersectWithProjectionClip(rect, clipped))
  {
    if (this->fallbackFocusContext() == context)
      this->fallbackFocus_.cut();
    return;
  }
  EditHit hit;
  hit.context = context;
  hit.rect = clipped;
  hit.text = text;
  hit.boundary = boundary;
  this->hitLedger_.editHits_.push_back(hit);
  this->pressLedger_.append(PressRow::EDIT_HIT, this->hitLedger_.editHits_.size() - 1);
  this->bindTextState(text);
}

bool ToolboxScenePlatformController::handleEditCommand(loka::app::EditVerb verb)
{
  EditTextControlBinding *row = this->editControls_.focused();
  return row && row->unicode && !ParkedContext(row->ownerContext) && row->editor->editVerb(verb);
}
