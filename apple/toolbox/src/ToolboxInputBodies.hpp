/** Private operation bodies shared with the Linux controller neighbor.
    Included inside ToolboxScenePlatformController; ToolboxInputDoor is the
    supported invocation surface, parallel to the Win32 and macOS doors. */
private:
  friend class ToolboxInputDoor;
  void render();
  void renderDirty(const Rect &rect);
  bool handleMouseDown(const Point &point);
  bool handleKeyDown(char key);
  bool handleScrollKey(ToolboxScrollKey key);
  bool handleTabKey(ToolboxTabDirection direction);
  void idleTextEdits();
