#include "ToolboxEditKey.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "platform/ToolboxMacRoman.hpp"
#include "core/StringAccess.hpp"
#include "platform/String.hpp"
#include <Script.h>
#include <Sound.h>

namespace
{
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
    loka::platform::Utf8View utf8 = {0, 0};
    std::string scratch;
    const loka::core::Managed<loka::platform::String> &value = loka::core::StringAccess::handle(source);
    if (value.isValid() && !value->queryUtf8(utf8))
    {
      if (!loka::platform::CollectUtf8(source, scratch)) return false;
      utf8.bytes = scratch.data();
      utf8.length = scratch.size();
    }
    CharsHandle handle = TEGetText(te);
    if (!handle || !*handle || (**te).teLength < 0) return false;
    const unsigned char *native = reinterpret_cast<const unsigned char *>(*handle);
    std::size_t offset = 0;
    for (long i = 0; i < (**te).teLength; ++i)
    {
      unsigned long scalar = native[i];
      ToolboxMacRomanDecode(native[i], scalar);
      if (offset == utf8.length) return false;
      const ToolboxTextUnit unit = ToolboxNextTextUnit(utf8.bytes + offset, utf8.length - offset, false);
      // The strict reader recovers malformed input as '?'; only the literal
      // ASCII '?' can round trip to that scalar.
      if (unit.scalar != scalar || (unit.scalar == '?' && utf8.bytes[offset] != '?')) return false;
      offset += unit.consumed;
    }
    return offset == utf8.length;
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
  if (!owner || owner->getContext() != &ctx || owner->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return false;
  std::size_t index = 0;
  if (this->editControls_.find(&ctx, index))
  {
    const EditTextControlBinding &binding = this->editControls_[index];
    if (!binding.usedThisFrame || !binding.te || EmptyRect(&binding.rect))
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

bool ToolboxScenePlatformController::handleScrollKey(ToolboxScrollKey key)
{
  EditTextControlBinding *focusedEdit = this->editControls_.focused();
  if (focusedEdit && focusedEdit->te && focusedEdit->editor)
  {
    focusedEdit->editor->scrollKey(key);
    return true;
  }
  return false;
}

bool ToolboxScenePlatformController::handleKeyDown(char key)
{
  EditTextControlBinding *focusedEdit = this->editControls_.focused();
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
      case EDIT_KEY_CONSUME: return true;
      case EDIT_KEY_REFUSE_HIGH: SysBeep(1); return true;
      case EDIT_KEY_NAVIGATE:
        TEKey(key, focusedEdit->te);
        return true;
      case EDIT_KEY_DELETE_BACKWARD:
      case EDIT_KEY_DELETE_FORWARD:
      case EDIT_KEY_INSERT: break;
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
      if (kind == EDIT_KEY_DELETE_FORWARD && start == end)
        TESetSelect(static_cast<short>(start), static_cast<short>(start + 1), te);
      // No certificate is observable for a native mutation awaiting readback.
      focusedEdit->installed.revoke();
      TEKey(kind == EDIT_KEY_INSERT ? key : 8, te);
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

bool ToolboxScenePlatformController::handleMouseDown(const Point &point)
{
  if (this->handleControlClick(point))
  {
    return false;
  }
  if (this->handleEditClick(point))
    return true;
  for (size_t i = 0; i < this->hitLedger_.editHits_.size(); ++i)
  {
    EditHit &hit = this->hitLedger_.editHits_[i];
    if (hit.text && PtInRect(point, &hit.rect))
    {
      loka::app::FocusParticipant *row =
          hit.context && hit.context->owner()
              ? loka::app::FocusParticipant::from(hit.context->owner()->asFocusParticipant())
              : 0;
      if (!row)
        continue;
      row->connectSource(this->fallbackFocus_);
      return true;
    }
  }
  this->fallbackFocus_.cut();
  for (size_t i = 0; i < this->hitLedger_.popupHits_.size(); ++i)
  {
    PopupHit &hit = this->hitLedger_.popupHits_[i];
    if (hit.context && PtInRect(point, &hit.rect) && hit.context->handleMouseDown(point, this))
    {
      return false;
    }
  }
  for (size_t i = 0; i < this->hitLedger_.cellHits_.size(); ++i)
  {
    CellHit &hit = this->hitLedger_.cellHits_[i];
    if (hit.context && PtInRect(point, &hit.rect) && hit.context->handleMouseDown(point, this))
    {
      return false;
    }
  }
  for (size_t i = 0; i < this->hitLedger_.buttonHits_.size(); ++i)
  {
    ButtonHit &hit = this->hitLedger_.buttonHits_[i];
    if (hit.context && PtInRect(point, &hit.rect) && hit.context->handleMouseDown(point, this))
    {
      return false;
    }
  }
  return false;
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
  this->bindTextState(text);
}
