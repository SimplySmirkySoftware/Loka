#ifndef LOKA_TOOLBOX_SCROLL_KEY_HPP
#define LOKA_TOOLBOX_SCROLL_KEY_HPP

/** View-only keys, separate from character and caret input. */
enum ToolboxScrollKey
{
  SCROLL_PAGE_UP,
  SCROLL_PAGE_DOWN,
  SCROLL_HOME,
  SCROLL_END
};

/** Decode ADB scroll keys; leave out untouched for every other code. */
inline bool QueryToolboxScrollKey(unsigned char keyCode, ToolboxScrollKey &out)
{
  switch (keyCode)
  {
  case 0x74:
    out = SCROLL_PAGE_UP;
    return true;
  case 0x79:
    out = SCROLL_PAGE_DOWN;
    return true;
  case 0x73:
    out = SCROLL_HOME;
    return true;
  case 0x77:
    out = SCROLL_END;
    return true;
  }
  return false;
}
#endif
