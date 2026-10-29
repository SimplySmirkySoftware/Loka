/** Full native geometry and per-rectangle move discharge at the host rail. */
namespace full_control
{
  void placement(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    const bool bar = std::strstr(mode, "Bar") != 0;
    const bool edge = std::strstr(mode, "Edge") != 0;
    const bool sizeOnly = std::strstr(mode, "Scroll") != 0;
    const Rect full = {240, 10, 280, 110};
    const Rect moved = {230, 10, 270, 110};
    const Rect clip = {0, 0, 260, 360};
    controller.projectionClip = clip;
    ClipRect(&clip);
    ButtonNode button(ButtonProps().text("Full"));
    NotifySubtreeNodeAttached(&button);
    ToolboxButtonContext &context = *new ToolboxButtonContext(&button, &controller);
    button.setContext(&context);
    context.updateRect(full);
    const short barId = controller.allocateControlId();
    if (bar)
      LOKA_VERIFY(
          controller.ensureScrollBarControl(barId, full, ScrollBarProps().range(0, 100), 0, NATIVE_HINT_DEFAULT));
    else
      context.render(&controller);
    ControlRef control = bar ? controller.scrollControl(0).control : controller.buttonControls_[0].control;
    if (!sizeOnly && !edge)
      LOKA_VERIFY(EqualRect(&control->rect, &full));
    control->calls.clear();
    window.port.native.update.clear();
    {
      ToolboxErasedPass::Guard pass(controller.erasedPass(), clip);
      if (bar)
        LOKA_VERIFY(
            controller.ensureScrollBarControl(barId, moved, ScrollBarProps().range(0, 100), 0, NATIVE_HINT_DEFAULT));
      else
      {
        context.updateRect(moved);
        context.render(&controller);
      }
    }
    unsigned sizes = 0, moves = 0, draws = 0;
    for (std::size_t i = 0; i < control->calls.size(); ++i)
    {
      sizes += control->calls[i].operation == "size";
      moves += control->calls[i].operation == "move";
      draws += control->calls[i].painted;
    }
    if (edge)
    {
      LOKA_VERIFY(draws == 1);
      LOKA_VERIFY(window.port.native.update.empty());
      LOKA_VERIFY(EqualRect(&control->rect, &moved));
    }
    else if (sizeOnly)
      LOKA_VERIFY(sizes == 0 && moves == 1);
    else
    {
      const ToolboxNativePlacement &placed =
          bar ? controller.scrollControl(0).placement : controller.buttonControls_[0].placement;
      Rect visible;
      const Rect expected = {230, 10, 260, 110};
      LOKA_VERIFY(EqualRect(&control->rect, &moved) && EqualRect(&placed.full(), &moved));
      LOKA_VERIFY(placed.queryVisible(visible) && EqualRect(&visible, &expected));
      LOKA_VERIFY(EqualRect(&control->calls.back().ownerClip, &expected));
      controller.publishFixturePressOrder();
      const Point point = {245, 30};
      LOKA_VERIFY(!ToolboxInputDoor::mouseDown(controller, point));
      LOKA_VERIFY(control->calls.back().operation == "track-no-action");
      LOKA_VERIFY(EqualRect(&control->calls.back().ownerClip, &expected));
      // A clip-only change must still refresh the last applied visible side.
      controller.projectionClip.top = 240;
      context.render(&controller);
      Rect narrower;
      LOKA_VERIFY(controller.buttonControls_[0].placement.queryVisible(narrower));
      LOKA_VERIFY(narrower.top == 240 && narrower.bottom == 260);
      control->calls.clear();
      // The frozen empty-visible early return never writes an empty row.
      controller.projectionClip.top = 290;
      ToolboxScenePlatformController::ButtonControlBinding *untouched = 0;
      LOKA_VERIFY(controller.ensureButtonControl(controller.buttonControls_[0].resourceId,
                                                 moved,
                                                 String::Literal("Full"),
                                                 0,
                                                 0,
                                                 NATIVE_HINT_DEFAULT,
                                                 &context,
                                                 untouched));
      LOKA_VERIFY(!untouched && control->calls.empty());
      Rect retained;
      LOKA_VERIFY(controller.buttonControls_[0].placement.queryVisible(retained));
      LOKA_VERIFY(EqualRect(&retained, &narrower));
    }
    controller.clearControls();
  }
  void discharge(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    // QuickDraw literals are top, left, bottom, right.
    const Rect before = {0, 0, 16, 100};
    const Rect after = {40, 120, 48, 170};
    const Rect intermediate = {40, 120, 56, 220};
    const Rect corner = {20, 10, 30, 30};
    const Rect outside = {100, 240, 110, 250};
    const bool partial = std::strstr(mode, "Partial") != 0;
    window.port.native.update.clear();
    window.port.native.update.push_back(before);
    window.port.native.update.push_back(after);
    window.port.native.update.push_back(intermediate);
    window.port.native.update.push_back(corner);
    window.port.native.update.push_back(outside);
    {
      ToolboxErasedPass::Guard pass(controller.erasedPass(), partial ? before : window.port.portRect);
      controller.validateOwnControlMove(before, after);
    }
    LOKA_VERIFY(window.port.native.update.size() == (partial ? 4u : 2u));
    const std::size_t cornerIndex = partial ? 2 : 0;
    LOKA_VERIFY(EqualRect(&window.port.native.update[cornerIndex], &corner));
    LOKA_VERIFY(EqualRect(&window.port.native.update[cornerIndex + 1], &outside));
    if (partial)
    {
      LOKA_VERIFY(EqualRect(&window.port.native.update[0], &after));
      LOKA_VERIFY(EqualRect(&window.port.native.update[1], &intermediate));
    }
  }
  void run(const char *mode)
  {
    if (std::strstr(mode, "Discharge"))
      discharge(mode);
    else
      placement(mode);
  }
} // namespace full_control
