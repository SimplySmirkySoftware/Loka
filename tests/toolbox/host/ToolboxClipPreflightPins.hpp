#ifndef LOKA_TEST_TOOLBOX_CLIP_PREFLIGHT_PINS_HPP
#define LOKA_TEST_TOOLBOX_CLIP_PREFLIGHT_PINS_HPP
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxPaintSuppressScope.hpp"

namespace
{
  void clipPreflightHelperPins()
  {
    GrafPort port;
    port.clipRgn = &port.native.clipRegionPointer;
    const Rect rect = {10, 10, 20, 20};
    const Rect empty = {10, 10, 10, 20};
    const Rect inverted = {20, 20, 10, 10};
    const Rect edge = {0, 0, 10, 20};
    const Rect wideOpen = {-32767, -32767, 32767, 32767};
    port.native.clip = wideOpen;
    const int before = toolbox_host::regions;
    LOKA_VERIFY(ToolboxPaintClip::intersectionIsEmpty(0, empty));
    LOKA_VERIFY(ToolboxPaintClip::intersectionIsEmpty(0, inverted));
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(0, rect));
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    port.native.clip = rect;
    LOKA_VERIFY((**port.clipRgn).rgnSize == 10);
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    port.native.clip = edge;
    LOKA_VERIFY(ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    port.native.clip = empty;
    LOKA_VERIFY(ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    RgnHandle saved = port.clipRgn;
    port.clipRgn = 0;
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    Region *missing = 0;
    port.clipRgn = &missing;
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    port.clipRgn = saved;
    (**saved).rgnSize = 9;
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    (**saved).rgnSize = 10;
    toolbox_host::regionError = -108;
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    LOKA_VERIFY(MemError() == -108);
    toolbox_host::regionError = 0;
    toolbox_host::rectQueryError = -108;
    LOKA_VERIFY(!ToolboxPaintClip::intersectionIsEmpty(&port, rect));
    LOKA_VERIFY(MemError() == -108);
    toolbox_host::rectQueryError = 0;
    toolbox_host::regionError = 0;
    LOKA_VERIFY(toolbox_host::regions == before);
    std::puts("clip preflight helper pins passed");
  }
} // namespace
#endif
