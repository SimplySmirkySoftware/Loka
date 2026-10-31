/** Ordinary TextEdit geometry and native interval pins, using the focus fixture. */
namespace edit_door
{
  void equal(const Rect &a, const Rect &b)
  {
    LOKA_VERIFY(EqualRect(&a, &b));
  }

  TEHandle ensure(Fixture &f, const Rect &full)
  {
    TEHandle te = f.controller.ensureEditTextControl(f.context(f.first), full, f.text.state(), NATIVE_HINT_DEFAULT).te;
    LOKA_VERIFY(te);
    return te;
  }

  void geometry(const char *mode)
  {
    Fixture f;
    Rect full = {20, 20, 60, 140};
    if (std::strcmp(mode, "full-edges") == 0)
    {
      const Rect clips[] = {{0, 40, 100, 200}, {30, 0, 100, 200}, {0, 0, 100, 100}, {0, 0, 40, 200}};
      for (unsigned i = 0; i < 4; ++i)
      {
        f.controller.projectionClip = clips[i];
        TEHandle te = ensure(f, full);
        equal((**te).destRect, full);
        equal((**te).viewRect, full);
        equal(f.controller.editControls_[0].placement.full(), full);
        Rect visible;
        LOKA_VERIFY(SectRect(&full, &clips[i], &visible));
        equal(f.controller.editControls_[0].placement.visible(), visible);
      }
      return;
    }
    if (std::strcmp(mode, "resize") == 0 || std::strcmp(mode, "overflow") == 0)
      SetRect(&f.controller.projectionClip, 20, 20, 100, 60);
    TEHandle te = ensure(f, full);
    TEScroll(-9, -13, te);
    const Rect displaced = (**te).destRect;
    const unsigned cal = (**te).calTextCalls;
    if (std::strcmp(mode, "translation") == 0)
    {
      OffsetRect(&full, 11, 17);
      ensure(f, full);
      Rect expected = displaced;
      OffsetRect(&expected, 11, 17);
      equal((**te).destRect, expected);
      equal((**te).viewRect, full);
      LOKA_VERIFY((**te).calTextCalls == cal + 1);
    }
    else if (std::strcmp(mode, "overflow") == 0)
    {
      (**te).destRect.left = 32700;
      (**te).destRect.right = 32760;
      OffsetRect(&full, 20, 0);
      ensure(f, full);
      equal((**te).destRect, full);
      equal((**te).viewRect, full);
      LOKA_VERIFY((**te).calTextCalls == cal + 1);
    }
    else if (std::strcmp(mode, "resize") == 0)
    {
      full.right += 5;
      ensure(f, full);
      equal((**te).destRect, full);
      equal((**te).viewRect, full);
      LOKA_VERIFY((**te).calTextCalls == cal + 1);
      TEScroll(0, -7, te);
      full.bottom += 9;
      OffsetRect(&full, 5, 8);
      ensure(f, full);
      equal((**te).destRect, full);
      equal((**te).viewRect, full);
      LOKA_VERIFY((**te).calTextCalls == cal + 2);
    }
    else if (std::strcmp(mode, "clip-only") == 0)
    {
      const Rect clip = {25, 30, 50, 100};
      f.controller.projectionClip = clip;
      ensure(f, full);
      equal((**te).destRect, displaced);
      equal((**te).viewRect, full);
      equal(f.controller.editControls_[0].placement.visible(), clip);
      LOKA_VERIFY((**te).calTextCalls == cal);
    }
    else if (std::strcmp(mode, "pool") == 0)
    {
      f.controller.retireEditTextControl(f.context(f.first), NATIVE_HINT_DEFAULT);
      f.controller.flushTE();
      OffsetRect(&full, 13, 19);
      f.controller.projectionClip = Rect();
      SetRect(&f.controller.projectionClip, full.left, full.top, full.right - 20, full.bottom);
      LOKA_VERIFY(ensure(f, full) == te);
      equal((**te).destRect, full);
      equal((**te).viewRect, full);
      LOKA_VERIFY((**te).autoView);
    }
    else
      LOKA_VERIFY(false);
  }

  void visibleReaders()
  {
    Fixture f;
    const Rect full = {0, 0, 20, 80};
    const Rect visible = {0, 40, 20, 80};
    f.controller.projectionClip = visible;
    ensure(f, full);
    equal(f.controller.editControls_[0].placement.full(), full);
    f.controller.publishFixturePressOrder();
    const Point outside = {5, 5};
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, outside));
    LOKA_VERIFY(!f.controller.editControls_.focused());
    f.controller.editControls_[0].placement = ToolboxNativePlacement(full, Rect());
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.first)));
  }

  void editorValue()
  {
    Fixture f;
    loka::testing::TextBufferFixture lines;
    Reported<LineCursor> cursor;
    StateBatchBase::CreateImmediateState(&f, cursor, LineCursor::None());
    LOKA_VERIFY(lines.attach(f.tracker()->asPushTracker(), 16) == ATTACH_OK);
    LOKA_VERIFY(lines.insert(0, String::Literal("abcdef")) == EDIT_OK);
    TextEditorNode editor(TextEditorProps(lines, cursor));
    f.attach(editor);
    LayoutState layout;
    layout.x = 10;
    layout.y = 20;
    layout.width = 200;
    layout.height = 80;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    IPlatformNodeHandler *handler = f.controller.nodeHandlerRegistry_.find(&editor);
    LOKA_VERIFY(handler);
    ToolboxTextEditorContext *context =
        static_cast<ToolboxTextEditorContext *>(handler->ensureContext(&editor, &f.controller, layout));
    LOKA_VERIFY(context);
    SetRect(&f.controller.projectionClip, 30, 30, 80, 60);
    context->layout(&f.controller, layout);
    context->render(&f.controller);
    std::size_t index = 0;
    LOKA_VERIFY(f.controller.editControls_.find(context, index));
    Rect port = Rect(), sizeBox = Rect();
    LOKA_VERIFY(f.nativeWindow.queryPortRect(port));
    f.nativeWindow.querySizeBoxRect(sizeBox);
    const Rect seat = {20, 10, 100, 210};
    const ToolboxTextEditorRects geometry = ToolboxTextEditorGeometry(seat, port, true, sizeBox);
    equal(f.controller.editControls_[index].placement.full(), geometry.hit);
    Rect visible;
    LOKA_VERIFY(SectRect(&geometry.hit, &f.controller.projectionClip, &visible));
    equal(f.controller.editControls_[index].placement.visible(), visible);
    const TEHandle te = f.controller.editControls_[index].te;
    LOKA_VERIFY(SectRect(&geometry.text, &f.controller.projectionClip, &visible));
    equal((**te).viewRect, visible);
    LifecycleFactTestAccess::MarkSubtreeRetired(&editor);
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    editor.setContext(0);
  }

  void failRepair(TEHandle)
  {
    toolbox_host::refuseReads = 1;
    toolbox_host::failSets = 1;
    toolbox_host::afterKey = 0;
  }

  void interval(const char *mode)
  {
    // The host region stores a bounding box only: complex regions are not simulated.
    for (unsigned shape = 0; shape < 6; ++shape)
    {
      const bool fast = shape >= 3 || std::strcmp(mode, "fast") == 0;
      const unsigned clipShape = shape % 3;
      Fixture f;
      const Rect full = {0, 0, 20, 80};
      const Rect visible = {0, 20, 20, 60};
      f.controller.projectionClip = fast ? full : visible;
      TEHandle te = ensure(f, full);
      f.controller.activateEditControl(0);
      TEHandle otherTE = 0;
      if (std::strcmp(mode, "deactivate") == 0 || std::strcmp(mode, "activate") == 0 || std::strcmp(mode, "tab") == 0)
      {
        const Rect otherFull = {40, 0, 60, 80};
        const Rect otherVisible = {40, 20, 60, 60};
        f.controller.projectionClip = fast ? otherFull : otherVisible;
        otherTE =
            f.controller.ensureEditTextControl(f.context(f.second), otherFull, f.text.state(), NATIVE_HINT_DEFAULT).te;
        LOKA_VERIFY(otherTE);
      }
      const Rect enclosing[] = {{0, 0, 0, 0}, {5, 30, 55, 50}, {-10, -10, 100, 100}};
      HostPortScope owner(f.nativeWindow.window());
      ClipRect(&enclosing[clipShape]);
      ToolboxWindow foreign;
      HostPortScope ambient(foreign.window());
      const Rect foreignClip = {200, 200, 220, 220};
      ClipRect(&foreignClip);
      (**te).calls.clear();
      if (otherTE)
        (**otherTE).calls.clear();
      const int regions = toolbox_host::regions;
      if (std::strcmp(mode, "click") == 0)
      {
        f.controller.publishFixturePressOrder();
        const Point point = {5, 25};
        LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, point));
      }
      else if (std::strcmp(mode, "navigate") == 0)
        LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 28));
      else if (std::strcmp(mode, "key") == 0 || std::strcmp(mode, "fast") == 0)
        LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
      else if (std::strcmp(mode, "delete") == 0)
      {
        // Seed selection directly, without adding a native select call to the log.
        (**te).selStart = (**te).selEnd = 0;
        LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x7F));
      }
      else if (std::strcmp(mode, "tab") == 0)
        LOKA_VERIFY(ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
      else if (std::strcmp(mode, "idle") == 0)
        ToolboxInputDoor::idleTextEdits(f.controller);
      else if (std::strcmp(mode, "deactivate") == 0 || std::strcmp(mode, "activate") == 0)
        f.controller.activateEditControl(1);
      else if (std::strcmp(mode, "sync") == 0)
      {
        f.controller.editControls_[0].installed.revoke();
        f.controller.syncEditTextFromState(f.controller.editControls_[0]);
      }
      else if (std::strcmp(mode, "rollback") == 0)
      {
        toolbox_host::afterKey = failRepair;
        LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
        LOKA_VERIFY((**te).text.empty());
      }
      else
        LOKA_VERIFY(false);
      GrafPtr current = 0;
      GetPort(&current);
      LOKA_VERIFY(current == foreign.window());
      equal(f.nativeWindow.port.native.clip, enclosing[clipShape]);
      equal(toolbox_host::currentClip(), foreignClip);
      if (fast)
        LOKA_VERIFY(toolbox_host::regions == regions);
      const bool target = std::strcmp(mode, "activate") == 0 || std::strcmp(mode, "tab") == 0;
      TEHandle observed = target ? otherTE : te;
      const Rect targetVisible = {40, 20, 60, 60};
      Rect expected = enclosing[clipShape];
      if (!fast)
        SectRect(&enclosing[clipShape], target ? &targetVisible : &visible, &expected);
      const char *operation =
          std::strcmp(mode, "fast") == 0 || std::strcmp(mode, "delete") == 0 || std::strcmp(mode, "navigate") == 0
              ? "key"
              : mode;
      if (std::strcmp(mode, "sync") == 0 || std::strcmp(mode, "rollback") == 0)
        operation = "set-text";
      if (std::strcmp(mode, "tab") == 0)
        operation = "select";
      unsigned count = 0, selects = 0;
      for (std::size_t i = 0; i < (**observed).calls.size(); ++i)
      {
        const HostTECall &call = (**observed).calls[i];
        if (call.operation == operation
            || ((std::strcmp(mode, "sync") == 0 || std::strcmp(mode, "delete") == 0) && call.operation == "select"))
        {
          LOKA_VERIFY(call.port == f.nativeWindow.window());
          equal(call.clip, expected);
          if (call.operation == "select")
            ++selects;
          if (call.operation == operation)
            ++count;
        }
      }
      LOKA_VERIFY(count == (std::strcmp(mode, "rollback") == 0 ? 2u : 1u));
      if (std::strcmp(mode, "sync") == 0 || std::strcmp(mode, "delete") == 0)
        LOKA_VERIFY(selects == 1);
    }
  }

  void refusal(const char *mode)
  {
    Fixture f;
    HostPortScope port(f.nativeWindow.window());
    const Rect full = {0, 0, 20, 80}, visible = {0, 20, 20, 60};
    f.controller.projectionClip = visible;
    ensure(f, full);
    f.controller.activateEditControl(0);
    const bool next =
        std::strcmp(mode, "activate") == 0 || std::strcmp(mode, "deactivate") == 0 || std::strcmp(mode, "tab") == 0;
    if (next)
    {
      const Rect otherFull = {40, 0, 60, 80}, otherVisible = {40, 20, 60, 60};
      f.controller.projectionClip = otherVisible;
      LOKA_VERIFY(
          f.controller.ensureEditTextControl(f.context(f.second), otherFull, f.text.state(), NATIVE_HINT_DEFAULT).te);
    }
    f.controller.observation_.reset();
    const char *expected = 0;
    RgnHandle scratch = f.controller.paintSuppressClipRgn_;
    if (std::strcmp(mode, "retire") == 0)
    {
      f.controller.paintSuppressClipRgn_ = 0;
      f.controller.retireEditTextControl(f.context(f.first), NATIVE_HINT_EAGER_RELEASE);
      expected = "ToolboxEditTextBinding.cpp:147";
    }
    else if (std::strcmp(mode, "verification") == 0)
    {
      f.controller.editControls_[0].installed.revoke();
      toolbox_host::failSets = 1;
      f.controller.syncEditTextFromState(f.controller.editControls_[0]);
      expected = "ToolboxEditTextBinding.cpp:234";
    }
    else if (std::strcmp(mode, "conversion") == 0)
    {
      RefusableText *source = new RefusableText("ab");
      write(f, String::FromPlatform(Managed<loka::platform::String>::Wrap(source)));
      source->refuse = true;
      f.controller.editControls_[0].installed.revoke();
      f.controller.observation_.reset();
      f.controller.syncEditTextFromState(f.controller.editControls_[0]);
      source->refuse = false;
      expected = "ToolboxEditTextBinding.cpp:207";
    }
    else
    {
      const int attempt = std::strcmp(mode, "tab") == 0 || std::strcmp(mode, "click") == 0 ? 3
                          : std::strcmp(mode, "activate") == 0                             ? 2
                                                                                           : 1;
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + attempt;
      if (std::strcmp(mode, "idle") == 0)
      {
        ToolboxInputDoor::idleTextEdits(f.controller);
        expected = "ToolboxTextEditorBinding.cpp:122";
      }
      else if (std::strcmp(mode, "sync") == 0)
      {
        f.controller.editControls_[0].installed.revoke();
        f.controller.syncEditTextFromState(f.controller.editControls_[0]);
        expected = "ToolboxEditTextBinding.cpp:214";
      }
      else if (std::strcmp(mode, "tab") == 0)
      {
        ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD);
        expected = "ToolboxFocus.cpp:172";
      }
      else if (next)
      {
        f.controller.activateEditControl(1);
        expected =
            std::strcmp(mode, "activate") == 0 ? "ToolboxTextEditorBinding.cpp:94" : "ToolboxTextEditorBinding.cpp:72";
      }
      else if (std::strcmp(mode, "click") == 0)
      {
        f.controller.publishFixturePressOrder();
        const Point point = {5, 25};
        ToolboxInputDoor::mouseDown(f.controller, point);
        expected = "ToolboxPress.cpp:279";
      }
      else
      {
        ToolboxInputDoor::keyDown(f.controller, std::strcmp(mode, "navigate") == 0 ? 28 : 'x');
        expected = std::strcmp(mode, "navigate") == 0 ? "ToolboxFocus.cpp:225" : "ToolboxFocus.cpp:257";
      }
      toolbox_host::refuseRegionAttempt = 0;
    }
    f.controller.paintSuppressClipRgn_ = scratch;
    const ToolboxPaintRefusalRecord &record = f.controller.observation_.outsideRefusal();
    std::fprintf(stderr, "edit expected=%s actual=%s\n", expected, record.site ? record.site : "-");
    LOKA_VERIFY(record.refused() && record.site);
    LOKA_VERIFY(std::strlen(record.site) >= std::strlen(expected));
    LOKA_VERIFY(std::strcmp(record.site + std::strlen(record.site) - std::strlen(expected), expected) == 0);
  }
  bool run(const char *mode)
  {
    if (std::strncmp(mode, "edit-refusal-", 13) == 0)
      refusal(mode + 13);
    else if (std::strncmp(mode, "edit-door-", 10) == 0)
      interval(mode + 10);
    else if (std::strcmp(mode, "edit-editor-value") == 0)
      editorValue();
    else if (std::strcmp(mode, "edit-visible-readers") == 0)
      visibleReaders();
    else if (std::strncmp(mode, "edit-", 5) == 0)
      geometry(mode + 5);
    else
      return false;
    std::printf("%s passed\n", mode);
    return true;
  }
} // namespace edit_door
