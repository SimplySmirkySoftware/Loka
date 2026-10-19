namespace toolbox_host
{
  void (*beforeHide)(ControlRef) = 0;
  unsigned refuseControls = 0;
} // namespace toolbox_host
// Included by ToolboxHost.cpp: all OS control calls share this one model.
namespace
{
  void RecordControl(ControlRef c, const char *operation, bool draws)
  {
    GrafPtr current;
    GetPort(&current);
    const Rect ownerClip = c->owner->native.clip;
    const Rect intersection = Intersection(c->rect, ownerClip);
    const bool painted = draws && !EmptyRect(&intersection);
    const toolbox_host::ControlCall call = {operation, current, current->native.clip, ownerClip, painted};
    c->calls.push_back(call);
  }
} // namespace
ControlRef NewControl(GrafPtr owner,
                      const Rect *rect,
                      const unsigned char *,
                      bool visible,
                      short value,
                      short minimum,
                      short maximum,
                      short proc,
                      long)
{
  if (toolbox_host::refuseControls)
  {
    --toolbox_host::refuseControls;
    return 0;
  }
  ControlRef c = new HostControl(value);
  if (proc == 0)
    c->tracking = toolbox_host::TrackScript(kControlButtonPart, kControlButtonPart);
  c->owner = owner;
  c->rect = *rect;
  c->minimum = minimum;
  c->maximum = maximum;
  c->visible = visible;
  owner->native.controls.insert(owner->native.controls.begin(), c);
  RecordControl(c, "new", visible);
  return c;
}
void DisposeControl(ControlRef c)
{
  std::vector<HostControl *> &list = c->owner->native.controls;
  list.erase(std::find(list.begin(), list.end(), c));
  delete c;
}
void MoveControl(ControlRef c, short x, short y)
{
  OffsetRect(&c->rect, x - c->rect.left, y - c->rect.top);
  RecordControl(c, "move", c->visible);
}
void SizeControl(ControlRef c, short width, short height)
{
  c->rect.right = c->rect.left + width;
  c->rect.bottom = c->rect.top + height;
  RecordControl(c, "size", c->visible);
}
void SetControlMinimum(ControlRef c, short value)
{
  c->minimum = value;
  c->value = std::max(c->value, value);
  RecordControl(c, "minimum", c->visible);
}
void SetControlMaximum(ControlRef c, short value)
{
  c->maximum = value;
  c->value = std::min(c->value, value);
  RecordControl(c, "maximum", c->visible);
}
void SetControlValue(ControlRef c, short value)
{
  ++toolbox_host::controlCalls.values;
  toolbox_host::controlCalls.valueClip = toolbox_host::currentClip();
  c->value = std::max(c->minimum, std::min(c->maximum, value));
  RecordControl(c, "value", c->visible);
}
void HiliteControl(ControlRef c, short hilite)
{
  ++toolbox_host::controlCalls.hilites;
  toolbox_host::controlCalls.hiliteClip = toolbox_host::currentClip();
  c->hilite = hilite;
  RecordControl(c, "hilite", c->visible);
}
void ShowControl(ControlRef c)
{
  ++toolbox_host::controlCalls.shows;
  toolbox_host::controlCalls.showClip = toolbox_host::currentClip();
  c->visible = true;
  RecordControl(c, "show", true);
}
void Draw1Control(ControlRef c)
{
  toolbox_host::paintOperations.push_back("bar");
  ++toolbox_host::controlCalls.draws;
  toolbox_host::controlCalls.drawClip = toolbox_host::currentClip();
  RecordControl(c, "draw", c->visible);
}
void HideControl(ControlRef c)
{
  if (toolbox_host::beforeHide)
    toolbox_host::beforeHide(c);
  ++toolbox_host::controlCalls.hides;
  toolbox_host::controlCalls.lastHidden = c;
  RecordControl(c, "hide", c->visible);
  if (c->visible && c->owner)
    c->owner->native.update.push_back(c->rect);
  c->visible = false;
}
short TestControl(ControlRef control, Point point)
{
  return control && control->visible && control->hilite != 255 && PtInRect(point, &control->rect)
             ? control->tracking.press
             : 0;
}
short FindControl(Point point, GrafPtr owner, ControlRef *out)
{
  *out = 0;
  for (std::size_t i = 0; i < owner->native.controls.size(); ++i)
  {
    ControlRef c = owner->native.controls[i];
    if (c->visible && c->hilite != 255 && PtInRect(point, &c->rect))
    {
      *out = c;
      return c->tracking.press;
    }
  }
  return 0;
}
short TrackControl(ControlRef c, Point, ControlActionUPP action)
{
  ++toolbox_host::tracks;
  RecordControl(c, action ? "track-action" : "track-no-action", false);
  const toolbox_host::TrackScript script = c->tracking;
  if (script.press == kControlIndicatorPart)
  {
    // A cancelled outline drag restores the starting value without callbacks.
    if (script.release != 0)
      SetControlValue(c, script.finalValue);
  }
  else if (action)
  {
    for (std::size_t i = 0; i < script.callbacks.size(); ++i)
      action(c, script.callbacks[i]);
  }
  return script.release;
}

#ifdef LOKA_HOST_CONTROL_WIDTH
void SetControlTitle(ControlRef control, const unsigned char *text)
{
  toolbox_host::controlTitles.push_back(std::string(reinterpret_cast<const char *>(text + 1), text[0]));
  if (control)
    RecordControl(control, "title", control->visible);
}
#endif
