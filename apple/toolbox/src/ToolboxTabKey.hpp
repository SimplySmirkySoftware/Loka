#ifndef LOKA_TOOLBOX_TAB_KEY_HPP
#define LOKA_TOOLBOX_TAB_KEY_HPP

/** Direction of EditText keyboard focus traversal. */
enum ToolboxTabDirection
{
  TAB_FORWARD,
  TAB_BACKWARD
};

/** Decode the Dialog Manager Tab character; leave out untouched otherwise. */
inline bool QueryToolboxTabKey(unsigned char charCode, bool shift, ToolboxTabDirection &out)
{
  if (charCode != 0x09)
    return false;
  out = shift ? TAB_BACKWARD : TAB_FORWARD;
  return true;
}
#endif
