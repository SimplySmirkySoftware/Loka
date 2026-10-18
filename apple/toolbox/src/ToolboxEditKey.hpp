#ifndef LOKA_TOOLBOX_EDIT_KEY_HPP
#define LOKA_TOOLBOX_EDIT_KEY_HPP

/** Ordinary single-line input policy; ClassifyTextEditorKey defines the separate TextEditor table. */
enum ToolboxEditKey
{
  EDIT_KEY_NAVIGATE,
  EDIT_KEY_DELETE_BACKWARD,
  EDIT_KEY_DELETE_FORWARD,
  EDIT_KEY_INSERT,
  EDIT_KEY_REFUSE_HIGH,
  EDIT_KEY_CONSUME
};

inline ToolboxEditKey ClassifyEditKey(unsigned char key, bool romanInput)
{
  if (key >= 28 && key <= 31) return EDIT_KEY_NAVIGATE;
  if (key == 8) return EDIT_KEY_DELETE_BACKWARD;
  if (key == 0x7F) return EDIT_KEY_DELETE_FORWARD;
  if (key >= 0x80) return romanInput ? EDIT_KEY_INSERT : EDIT_KEY_REFUSE_HIGH;
  if (key >= 0x20) return EDIT_KEY_INSERT;
  return EDIT_KEY_CONSUME;
}

/** TextEditor byte commands; high bytes enter the document refusal/restore path. */
enum ToolboxTextEditorKey
{
  TEXT_KEY_INSERT,
  TEXT_KEY_CARET,
  TEXT_KEY_DELETE_BACKWARD,
  TEXT_KEY_DELETE_FORWARD,
  TEXT_KEY_IGNORE
};

inline ToolboxTextEditorKey ClassifyTextEditorKey(unsigned char key)
{
  if (key >= 0x1C && key <= 0x1F)
    return TEXT_KEY_CARET;
  if (key == 0x08)
    return TEXT_KEY_DELETE_BACKWARD;
  if (key == 0x7F)
    return TEXT_KEY_DELETE_FORWARD;
  if (key == 0x09 || key == 0x0D || key >= 0x20)
    return TEXT_KEY_INSERT;
  return TEXT_KEY_IGNORE;
}

#endif
