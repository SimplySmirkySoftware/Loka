/** Render-order press pins. Native contexts, render transaction and input door
    are production code; the host supplies fixed leaf geometry only. */
namespace press_door
{
  class PlacedButton : public ToolboxButtonContext
  {
    Rect placement_;

  public:
    PlacedButton(ButtonNode *node, ToolboxScenePlatformController *controller, const Rect &rect)
        : ToolboxButtonContext(node, controller),
          placement_(rect)
    {
      this->updateData(String("press"), node->props.onClick_, node->props.enabled_, 0, 0);
      this->updateRect(rect);
    }
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      this->updateRect(this->placement_);
      state.y = this->placement_.bottom;
      return this->placement_.right - this->placement_.left;
    }
  };
  class EmptyUnit : public NativeNodeContext
  {
  public:
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      state.y = 80;
      return state.width;
    }
  };
  struct Fixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    ZStackNode stack;
    EditTextNode field;
    MutableState<String> text;
    EmitterState groundAction, upperAction, lastAction;
    unsigned groundCalls, upperCalls, lastCalls;
    static void count(void *p)
    {
      ++*static_cast<unsigned *>(p);
    }
    Fixture()
        : port(window.window()),
          controller(&window),
          stack(ZStackProps()),
          field(EditTextProps()),
          text(String("focus")),
          groundCalls(0),
          upperCalls(0),
          lastCalls(0)
    {
      this->groundAction.bind(&count, &this->groundCalls, false);
      this->upperAction.bind(&count, &this->upperCalls, false);
      this->lastAction.bind(&count, &this->lastCalls, false);
      NotifySubtreeNodeAttached(&this->stack);
      NotifySubtreeNodeAttached(&this->field);
      this->field.setContext(new ToolboxEditTextContext(&this->field, &this->controller));
      this->controller.rootNode_ = &this->stack;
    }
    ~Fixture()
    {
      this->controller.clearControls();
      this->controller.rootNode_ = 0;
      this->groundAction.unbind(&count, &this->groundCalls);
      this->upperAction.unbind(&count, &this->upperCalls);
      this->lastAction.unbind(&count, &this->lastCalls);
    }
    ButtonNode *button(EmitterState &action, bool small = false, State<bool> *enabled = 0)
    {
      ButtonNode *node = new ButtonNode(ButtonProps().onClick(&action).enabled(enabled));
      const Rect rect = {10, 10, static_cast<short>(small ? 30 : 80), 110};
      node->setContext(new PlacedButton(node, &this->controller, rect));
      this->stack.addChild(node);
      NotifySubtreeNodeAttached(node);
      return node;
    }
    void empty()
    {
      RectSurfaceNode *node = new RectSurfaceNode(RectSurfaceProps());
      node->setContext(new EmptyUnit());
      this->stack.addChild(node);
      NotifySubtreeNodeAttached(node);
    }
    void render()
    {
      ToolboxInputDoor::render(this->controller);
    }
    void focus()
    {
      const Rect rect = {150, 0, 170, 100};
      ToolboxEditTextContext *context = static_cast<ToolboxEditTextContext *>(this->field.getContext());
      LOKA_VERIFY(this->controller.ensureEditTextControl(context, rect, &this->text, NATIVE_HINT_DEFAULT).te);
      LOKA_VERIFY(this->controller.applyNativeFocus(*context));
    }
    void expectFocus(NodeContext *expected)
    {
      NodeContext *actual = 0;
      LOKA_VERIFY(this->controller.readNativeFocus(actual) && actual == expected);
    }
    void press(short y = 20)
    {
      const Point point = {y, 20};
      ToolboxInputDoor::mouseDown(this->controller, point);
    }
  };
  void nativeAboveParked()
  {
    Fixture f;
    ButtonNode *ground = f.button(f.groundAction);
    ButtonNode *upper = f.button(f.upperAction);
    // Deliberately create in reverse paint order: FindControl picks ground.
    upper->getContext()->render(&f.controller);
    ground->getContext()->render(&f.controller);
    f.render();
    NotifySubtreeNodeDetached(ground);
    f.focus();
    f.press();
    LOKA_VERIFY(f.groundCalls == 0 && f.upperCalls == 1);
    f.expectFocus(f.field.getContext());
    // A clipped replay still reaches and republishes every row.
    const Rect miss = {200, 200, 210, 210};
    ToolboxRenderDirtyInCompositionOrder(f.controller, miss);
    f.press();
    LOKA_VERIFY(f.upperCalls == 2);
    std::puts("[pin] N1 live native above parked, reverse creation and replay passed");
  }
  void unitShield()
  {
    Fixture f;
    f.button(f.groundAction);
    f.empty();
    f.render();
    f.focus();
    f.press(60);
    LOKA_VERIFY(f.groundCalls == 0);
    f.expectFocus(f.field.getContext());
    f.window.window()->portRect.left = -20000;
    f.window.window()->portRect.right = 20000;
    f.render();
    f.press(60);
    LOKA_VERIFY(f.groundCalls == 0);
    f.expectFocus(f.field.getContext());
    std::puts("[pin] N2 empty unit shields ground, including refused render, passed");
  }
  void insideAndLater(bool laterOnly = false)
  {
    Fixture f;
    f.button(f.groundAction);
    f.button(f.upperAction);
    if (!laterOnly)
    {
      f.render();
      f.press();
      LOKA_VERIFY(f.upperCalls == 1 && f.groundCalls == 0);
      std::puts("[pin] N3 button inside unit acts passed");
    }
    f.button(f.lastAction);
    f.render();
    f.press();
    LOKA_VERIFY(f.upperCalls == (laterOnly ? 0u : 1u) && f.lastCalls == 1 && f.groundCalls == 0);
    std::puts("[pin] N4 later unit wins passed");
  }
  void refusedRender()
  {
    for (unsigned parked = 0; parked != 2; ++parked)
    {
      Fixture f;
      ButtonNode *node = f.button(f.groundAction);
      f.render();
      f.focus();
      if (parked)
        NotifySubtreeNodeDetached(node);
      f.window.window()->portRect.left = -20000;
      f.window.window()->portRect.right = 20000;
      f.render();
      f.press();
      LOKA_VERIFY(f.groundCalls == (parked ? 0u : 1u));
      f.expectFocus(f.field.getContext());
      LOKA_VERIFY(!f.controller.buttonControls_[0].usedThisFrame);
    }
    std::puts("[pin] N5 refused render preserves live and parked native rows passed");
  }
  void disabledShield()
  {
    Fixture f;
    State<bool> disabled(false);
    f.button(f.groundAction);
    f.button(f.upperAction, false, &disabled);
    f.render();
    f.focus();
    const unsigned tracks = toolbox_host::tracks;
    f.press();
    LOKA_VERIFY(f.groundCalls == 0 && f.upperCalls == 0 && toolbox_host::tracks == tracks);
    f.expectFocus(f.field.getContext());
    std::puts("[pin] N7 disabled native shields ground and preserves focus passed");
  }
  void erasedNative()
  {
    Fixture f;
    f.button(f.groundAction);
    f.button(f.upperAction);
    f.button(f.lastAction, true);
    f.render();
    LOKA_VERIFY(f.controller.buttonControls_.size() == 3);
    // Stage a replay too: retirement must repair both lists before commit.
    f.controller.renderZStack(&f.stack);
    const short id = f.controller.buttonControls_[0].resourceId;
    f.controller.destroyButtonControl(id, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.buttonControls_.size() == 2);
    f.press(60);
    LOKA_VERIFY(f.groundCalls == 0 && f.upperCalls == 1);
    f.controller.pressLedger_.commit();
    f.press(60);
    LOKA_VERIFY(f.upperCalls == 2 && f.groundCalls == 0);
    std::puts("[pin] N8 native erase shifts live and staging indices passed");
  }
  void emptyFocus()
  {
    Fixture f;
    f.button(f.groundAction);
    f.render();
    f.focus();
    const Point empty = {250, 250};
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, empty));
    f.expectFocus(0);
    // The fallback source is a separate cut, not an effect of native deactivation.
    FocusParticipant *participant = FocusParticipant::from(f.field.asFocusParticipant());
    participant->connectSource(f.controller.fallbackFocus_);
    LOKA_VERIFY(f.controller.fallbackFocusContext() == f.field.getContext());
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, empty));
    LOKA_VERIFY(!f.controller.fallbackFocusContext());
    std::puts("[pin] N9 empty space cuts native and fallback focus passed");
  }
  void run(const char *name)
  {
    const bool all = std::strcmp(name, "PressDoor") == 0;
    if (all || std::strcmp(name, "PressDoor-N1") == 0)
      nativeAboveParked();
    if (all || std::strcmp(name, "PressDoor-N2") == 0)
      unitShield();
    if (all || std::strcmp(name, "PressDoor-N4") == 0)
      insideAndLater(!all);
    if (all || std::strcmp(name, "PressDoor-N5") == 0)
      refusedRender();
    if (all || std::strcmp(name, "PressDoor-N7") == 0)
      disabledShield();
    if (all || std::strcmp(name, "PressDoor-N8") == 0)
      erasedNative();
    if (all || std::strcmp(name, "PressDoor-N9") == 0)
      emptyFocus();
  }
} // namespace press_door
