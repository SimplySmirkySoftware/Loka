/** Event-local completed selection. Nothing here survives the input operation. */
struct ResolvedPress
{
  PressRow row;
  loka::app::scene::NodeContext *context;
  loka::app::ScrollViewNode *viewport;
  std::size_t viewportIndex;
  loka::app::ScrollBarOrientation axis;
  ControlRef control;
  ControlPartCode part;
  ToolboxEditorScrollBarAssociation editor;
  bool enabled;
  ResolvedPress()
      : row(),
        context(0),
        viewport(0),
        viewportIndex(0),
        axis(loka::app::SCROLL_BAR_VERTICAL),
        control(0),
        part(0),
        editor(),
        enabled(true)
  {
  }
};

/** One backward walk per press or hover sample over this controller's render order. */
bool ToolboxPressLedger::topmostAt(const Point &point,
                                   const ToolboxScenePlatformController &controller,
                                   ResolvedPress &out) const
{
  for (std::size_t i = this->live_.size(); i; --i)
    if (controller.resolvePress(this->live_[i - 1], point, out))
      return true;
  return false;
}

/** Resolve only rect-matching candidates; viewport identity is looked up once.
    No attachment test or allocation occurs while selecting the row. */
bool ToolboxScenePlatformController::resolvePress(const PressRow &row, const Point &point, ResolvedPress &out) const
{
  ResolvedPress value;
  value.row = row;
  switch (row.kind)
  {
  case PressRow::NATIVE_BUTTON:
  {
    const ButtonControlBinding &binding = this->buttonControls_[row.index];
    if (!binding.control || !(**binding.control).contrlVis || !PtInRect(point, &binding.rect))
      return false;
    value.context = binding.context;
    value.control = binding.control;
    value.enabled = !binding.enabled || binding.enabled->get();
    break;
  }
  case PressRow::NATIVE_BAR:
  {
    const ScrollBarControlBinding &binding = this->scrollBarLedger_.scrollBarControls_[row.index];
    if (!binding.control || !(**binding.control).contrlVis || !PtInRect(point, &binding.rect))
      return false;
    value.control = binding.control;
    value.editor = binding.editor;
    value.context = binding.editor.editor() ? binding.editor.editor() : binding.ownerContext;
    value.enabled = binding.active && (!binding.enabled || binding.enabled->get());
    if (!value.context)
    {
      for (std::size_t i = 0; i < this->scrollBarLedger_.viewportScrollBars_.size(); ++i)
      {
        const ViewportScrollBarBinding &viewport = this->scrollBarLedger_.viewportScrollBars_[i];
        if (viewport.barId[loka::app::SCROLL_BAR_VERTICAL] == binding.resourceId
            || viewport.barId[loka::app::SCROLL_BAR_HORIZONTAL] == binding.resourceId)
        {
          value.viewport = viewport.scrollView;
          value.viewportIndex = i;
          value.axis = viewport.barId[loka::app::SCROLL_BAR_VERTICAL] == binding.resourceId
                           ? loka::app::SCROLL_BAR_VERTICAL
                           : loka::app::SCROLL_BAR_HORIZONTAL;
          break;
        }
      }
    }
    break;
  }
  case PressRow::EDIT_CONTROL:
  {
    const EditTextControlBinding &binding = this->editControls_[row.index];
    if (!binding.te || !PtInRect(point, &binding.rect))
      return false;
    value.context = binding.ownerContext;
    break;
  }
  case PressRow::EDIT_HIT:
  {
    const EditHit &hit = this->hitLedger_.editHits_[row.index];
    if (!hit.context || !hit.text || !PtInRect(point, &hit.rect) || !PtInRect(point, &hit.context->chromeRect()))
      return false;
    value.context = hit.context;
    if (!value.context->owner() || !value.context->owner()->asFocusParticipant())
      return false;
    break;
  }
  case PressRow::POPUP_HIT:
  {
    const PopupHit &hit = this->hitLedger_.popupHits_[row.index];
    if (!hit.context || !PtInRect(point, &hit.rect) || !PtInRect(point, &hit.context->rect_) || !hit.context->items_
        || !hit.context->items_->size() || !hit.context->selectedIndex_)
      return false;
    value.context = hit.context;
    value.enabled = !hit.context->enabled_ || hit.context->enabled_->get();
    break;
  }
  case PressRow::CELL_HIT:
  {
    const CellHit &hit = this->hitLedger_.cellHits_[row.index];
    if (!hit.context || !PtInRect(point, &hit.rect) || !PtInRect(point, &hit.context->rect_) || !hit.context->node_
        || !hit.context->node_->props.onClick_)
      return false;
    value.context = hit.context;
    break;
  }
  case PressRow::BUTTON_HIT:
  {
    const ButtonHit &hit = this->hitLedger_.buttonHits_[row.index];
    if (!hit.context || !PtInRect(point, &hit.rect) || !PtInRect(point, &hit.context->rect_))
      return false;
    value.context = hit.context;
    value.enabled = !hit.context->enabled_ || hit.context->enabled_->get();
    break;
  }
  case PressRow::UNIT:
    if (!PtInRect(point, &this->unitLedger_.at(row.index).rect))
      return false;
    out = value;
    return true;
  case PressRow::PRESS_ROW_KIND_COUNT:
    return false;
  }
  if (!value.viewport && (!value.context || !value.context->owner()))
    return false;
  if (value.control)
    value.part = TestControl(value.control, point);
  out = value;
  return true;
}

/** One owner question for the selected row, with no repeated association search. */
PressOwnerFact ToolboxScenePlatformController::pressOwnerFact(const ResolvedPress &press) const
{
  if (press.row.kind == PressRow::UNIT)
    return PRESS_OWNER_SHIELD;
  if (press.viewport ? press.viewport->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED
                     : !loka::app::scene::IsAttachedCurrentContext(press.context))
    return PRESS_OWNER_PARKED;
  if (!press.enabled || (press.control && !press.part))
    return PRESS_OWNER_SHIELD;
  return PRESS_OWNER_ATTACHED;
}

bool ToolboxScenePlatformController::isPointInEdit(const Point &point) const
{
  ResolvedPress press;
  if (!this->pressLedger_.topmostAt(point, *this, press))
    return false;
  const PressOwnerFact owner = this->pressOwnerFact(press);
  return owner == PRESS_OWNER_ATTACHED
         && (press.row.kind == PressRow::EDIT_CONTROL || press.row.kind == PressRow::EDIT_HIT);
}

bool ToolboxScenePlatformController::pressOnEmptySpace()
{
  this->activateEditControl(this->editControls_.size());
  this->fallbackFocus_.cut();
  return false;
}

bool ToolboxScenePlatformController::handleMouseDown(const Point &point)
{
  ResolvedPress press;
  if (!this->pressLedger_.topmostAt(point, *this, press))
    return this->pressOnEmptySpace();
  switch (this->pressOwnerFact(press))
  {
  case PRESS_OWNER_PARKED:
    return false;
  case PRESS_OWNER_SHIELD:
    return false;
  case PRESS_OWNER_ATTACHED:
    break;
  }
  return this->dispatchPress(press, point);
}

/** One dispatch per accepted press. Copy all borrowed row inputs before callbacks. */
bool ToolboxScenePlatformController::dispatchPress(const ResolvedPress &press, const Point &point)
{
  const std::size_t index = press.row.index;
  switch (press.row.kind)
  {
  case PressRow::NATIVE_BUTTON:
  {
    const Rect rect = this->buttonControls_[index].rect;
    loka::core::EmitterState *emitter = this->buttonControls_[index].emitter;
    const ControlRef control = press.control;
    this->beginBatchUpdate();
    const ControlPartCode tracked = TrackControl(control, point, 0);
    this->invalidateUnitOverlap(rect);
    // A Button without onClick tracks like any button and emits nothing.
    if (tracked && emitter)
      emitter->emit();
    this->endBatchUpdate();
    return false;
  }
  case PressRow::NATIVE_BAR:
  {
    const ScrollBarControlBinding source = this->scrollBarLedger_.scrollBarControls_[index];
    const ToolboxEditorScrollBarAssociation editor = press.editor;
    const std::size_t viewportIndex = press.viewportIndex;
    loka::app::ScrollViewNode *viewport = press.viewport;
    const loka::app::ScrollBarOrientation axis = press.axis;
    const ControlRef control = press.control;
    gActiveScrollBarLineStep = source.lineStep;
    gActiveScrollBarPageStep = source.pageStep;
    TrackControl(control, point, press.part == kControlIndicatorPart ? 0 : ScrollBarActionUPP());
    this->invalidateUnitOverlap(source.rect);
    if (editor.editor())
    {
      const int settled = GetControlValue(control);
      if (settled != source.appliedValue)
        editor.editor()->scrollInput(settled);
    }
    else if (viewport)
      this->commitViewportScrollBarValue(this->scrollBarLedger_.viewportScrollBars_[viewportIndex],
                                         this->scrollBarLedger_.scrollBarControls_[index],
                                         axis);
    else
      this->commitScrollBarValueAt(index);
    return false;
  }
  case PressRow::EDIT_CONTROL:
  {
    const TEHandle te = this->editControls_[index].te;
    const Rect rect = this->editControls_[index].rect;
    ToolboxTextEditorContext *editor = this->editControls_[index].editor;
    this->activateEditControl(index);
    if (editor)
      editor->click(point);
    else
    {
      TEClick(point, false, te);
      this->invalidateUnitOverlap(rect);
    }
    return true;
  }
  case PressRow::EDIT_HIT:
  {
    loka::app::FocusParticipant *participant =
        loka::app::FocusParticipant::from(press.context->owner()->asFocusParticipant());
    this->activateEditControl(this->editControls_.size());
    participant->connectSource(this->fallbackFocus_);
    return true;
  }
  case PressRow::POPUP_HIT:
  {
    ToolboxPopupMenuContext *context = static_cast<ToolboxPopupMenuContext *>(press.context);
    this->pressOnEmptySpace();
    return context->handleMouseDown(point, this);
  }
  case PressRow::CELL_HIT:
  {
    ToolboxCellContext *context = static_cast<ToolboxCellContext *>(press.context);
    this->pressOnEmptySpace();
    return context->handleMouseDown(point, this);
  }
  case PressRow::BUTTON_HIT:
  {
    ToolboxButtonContext *context = static_cast<ToolboxButtonContext *>(press.context);
    this->pressOnEmptySpace();
    return context->handleMouseDown(point, this);
  }
  case PressRow::UNIT:
  case PressRow::PRESS_ROW_KIND_COUNT:
    return false;
  }
  return false;
}

/** Render callers append at reach; each lookup walks only this controller's rows. */
void ToolboxScenePlatformController::reachNativeButton(short id)
{
  for (std::size_t i = 0; i < this->buttonControls_.size(); ++i)
    if (this->buttonControls_[i].resourceId == id)
    {
      this->pressLedger_.append(PressRow::NATIVE_BUTTON, i);
      return;
    }
}
void ToolboxScenePlatformController::reachNativeBar(short id)
{
  for (std::size_t i = 0; i < this->scrollBarLedger_.scrollBarControls_.size(); ++i)
    if (this->scrollBarLedger_.scrollBarControls_[i].resourceId == id)
    {
      const ScrollBarControlBinding &row = this->scrollBarLedger_.scrollBarControls_[i];
      if (row.control && (**row.control).contrlVis)
        this->pressLedger_.append(PressRow::NATIVE_BAR, i);
      return;
    }
}
void ToolboxScenePlatformController::reachEditControl(loka::app::scene::NodeContext *context)
{
  std::size_t index = 0;
  if (this->editControls_.find(context, index))
    this->pressLedger_.append(PressRow::EDIT_CONTROL, index);
}
void ToolboxScenePlatformController::reachPopupHit(ToolboxPopupMenuContext *context)
{
  for (std::size_t i = 0; i < this->hitLedger_.popupHits_.size(); ++i)
    if (this->hitLedger_.popupHits_[i].context == context)
    {
      this->pressLedger_.append(PressRow::POPUP_HIT, i);
      return;
    }
}
