/** One construction door shared with the host controller fixture. */
struct EditTextControlBinding
{
  EditTextControlBinding(loka::app::scene::NodeContext *owner,
                         ToolboxEditorSeat *seat,
                         loka::core::State<loka::core::String> *state)
      : ownerContext(owner),
        editor(seat),
        unicode(0),
        text(state),
        te(0),
        placement(),
        usedThisFrame(false),
        lifetimeHint(loka::app::scene::NATIVE_HINT_DEFAULT)
  {
  }
  loka::app::scene::NodeContext *ownerContext;
  ToolboxEditorSeat *editor;
  ToolboxUnicodeEditorResource *unicode;
  loka::core::State<loka::core::String> *text;
  loka::app::scene::WriteSeat<loka::core::String> textSeat;
  TEHandle te;
  ToolboxNativePlacement placement;
  bool usedThisFrame;
  ToolboxEditInstalled installed;
  loka::app::scene::NativeLifetimeHint lifetimeHint;
};
