/** Private operation bodies shared with the Linux controller neighbor.
    Included inside ToolboxScenePlatformController; ToolboxInputDoor is the
    supported invocation surface, parallel to the Win32 and macOS doors. */
private:
  friend class ToolboxInputDoor;
  void render(ToolboxFullDrawPresenter *presenter = 0);
  void renderDirty(const Rect &rect);
  friend class ToolboxPressLedger;
  bool resolvePress(const PressRow &, const Point &, ResolvedPress &) const;
  PressOwnerFact pressOwnerFact(const ResolvedPress &) const;
  bool dispatchPress(const ResolvedPress &, const Point &, const EventRecord &);
  bool pressOnEmptySpace();
  bool handleMouseDown(const Point &point, const EventRecord &event);
  bool offerKeyEvent(const EventRecord &event);
  bool handleKeyDown(char key);
  bool handleScrollKey(ToolboxScrollKey key);
  bool handleTabKey(ToolboxTabDirection direction);
  void idleTextEdits();
