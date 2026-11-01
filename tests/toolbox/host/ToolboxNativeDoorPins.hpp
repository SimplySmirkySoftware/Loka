/** Control Manager door pins reuse the real press paths and existing OS ledger. */
namespace native_door
{
  void run(const char *mode)
  {
    const bool button = std::strstr(mode, "Button") != 0;
    const bool replay = std::strstr(mode, "Replay") != 0;
    const bool draw = std::strstr(mode, "Draw") != 0;
    const bool empty = std::strstr(mode, "Empty") != 0;
    const bool refused = std::strstr(mode, "Refused") != 0;
    parked_press::Fixture f;
    f.install(button ? parked_press::NATIVE_BUTTON : parked_press::DSL_BAR);
    f.controller.publishFixturePressOrder();
    ControlRef control = button ? f.controller.buttonControls_[0].control : f.controller.scrollControl(0).control;
    Rect visible;
    LOKA_VERIFY(button ? f.controller.buttonControls_[0].placement.queryVisible(visible)
                       : f.controller.scrollControl(0).placement.queryVisible(visible));
    // The clip crosses the row's bottom/right edges, but cuts its top/left.
    const Rect clip = {2, 2, 150, 160};
    ClipRect(&clip);
    {
      ToolboxPaintSuppressScope suppress(empty ? f.controller.paintSuppressClipRgn_ : 0);
      const Rect before = toolbox_host::currentClip();
      Rect expected = before;
      if (!refused)
        SectRect(&before, &visible, &expected);
      ToolboxWindow other;
      HostPortScope otherPort(other.window());
      // Opposite hit decisions detect testing the foreign clip before selection.
      const Rect disjoint = {200, 200, 220, 220};
      const Rect foreign = draw && empty ? clip : disjoint;
      ClipRect(&foreign);
      control->calls.clear();
      f.controller.observation_.reset();
      if (refused)
        toolbox_host::failRegions = 1;
      if (replay)
        f.controller.drawControlsInRect(clip);
      else if (draw)
        f.controller.drawScrollBarControl(f.controller.scrollControl(0).resourceId);
      else
      {
        if (!button)
        {
          control->tracking = toolbox_host::TrackScript(21, 21);
          control->tracking.callbacks.push_back(21);
        }
        LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, f.point));
      }
      LOKA_VERIFY(toolbox_host::failRegions == 0);
      LOKA_VERIFY(f.controller.observation_.outsideRefusal().reason
                  == (refused ? TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION : TOOLBOX_PAINT_REFUSAL_NONE));
      GrafPtr current;
      GetPort(&current);
      LOKA_VERIFY(current == other.window());
      LOKA_VERIFY(std::memcmp(&f.window.port.native.clip, &before, sizeof(Rect)) == 0);
      LOKA_VERIFY(std::memcmp(&other.port.native.clip, &foreign, sizeof(Rect)) == 0);
      unsigned doors = 0, actions = 0;
      for (std::size_t i = 0; i < control->calls.size(); ++i)
      {
        const toolbox_host::ControlCall &call = control->calls[i];
        if (call.operation == "draw" || call.operation == "track-action" || call.operation == "track-no-action"
            || call.operation == "value")
        {
          if (call.operation == "value")
            ++actions;
          else
            ++doors;
          LOKA_VERIFY(call.currentPort == f.window.window());
          LOKA_VERIFY(EqualRect(&call.currentClip, &expected));
          LOKA_VERIFY(EqualRect(&call.ownerClip, &expected));
        }
      }
      // The standalone draw rejects an empty owner clip before opening its door.
      LOKA_VERIFY(doors == (draw && empty ? 0u : 1u));
      if (!button && !draw && !replay)
        LOKA_VERIFY(actions == 1);
    }
    const Rect restored = toolbox_host::currentClip();
    LOKA_VERIFY(std::memcmp(&restored, &clip, sizeof(Rect)) == 0);
  }
} // namespace native_door
