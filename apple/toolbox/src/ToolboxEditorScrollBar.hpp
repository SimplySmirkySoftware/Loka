#ifndef LOKA_TOOLBOX_EDITOR_SCROLL_BAR_HPP
#define LOKA_TOOLBOX_EDITOR_SCROLL_BAR_HPP
class ToolboxTextEditorContext;
/** Borrowed authority for one live editor presentation; revoked before either native row retires. */
class ToolboxEditorScrollBarAssociation
{
public:
  ToolboxEditorScrollBarAssociation()
      : editor_(0)
  {
  }
  void install(ToolboxTextEditorContext *editor)
  {
    this->editor_ = editor;
  }
  void revoke()
  {
    this->editor_ = 0;
  }
  ToolboxTextEditorContext *editor() const
  {
    return this->editor_;
  }

private:
  ToolboxTextEditorContext *editor_;
};
/** Completed, checked presentation. Hidden carries no usable range. */
class ToolboxEditorScrollBarPresentation
{
public:
  ToolboxEditorScrollBarPresentation()
      : shown(false),
        maximum(0),
        value(0),
        pageStep(0)
  {
  }
  ToolboxEditorScrollBarPresentation(short max, short current, short page)
      : shown(true),
        maximum(max),
        value(current),
        pageStep(page)
  {
  }
  const bool shown;
  const short maximum, value, pageStep;
};
#endif
