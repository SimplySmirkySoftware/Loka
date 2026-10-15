#!/usr/bin/env python3
"""Keep Win32 and macOS geometry API calls in reviewed policy/typed-wrapper files (#818).

This is a literal-call guard, not a C++ type checker or macro resolver. The
allowlist grants file-level trust; review must verify typed wrapper arguments.
Comments/strings are ignored; inactive preprocessor branches are checked.
"""
from pathlib import Path
import re
import sys

# API-specific permissions keep the file-level allowlist from licensing MulDiv
# in a controller, or native windows inside the arithmetic policy.
ALLOWLIST = {
    "win32/src/platform/Win32DisplayScale.cpp": {"MulDiv"},
    # Typed child/layout wrappers and callbacks carrying already-native damage.
    "win32/src/Win32ScenePlatformController.cpp": {
        "MoveWindow", "SetWindowPos", "CreateWindowExW", "InvalidateRect"},
    # Typed top-level wrappers; chrome, desktop origin and #712 clamp stay native.
    "win32/src/Win32Window.cpp": {"MoveWindow", "SetWindowPos", "CreateWindowExW"},
}
# macOS exceptions name each native-only site. Layout writes live in typed
# adapters; allowing NSMakeRect here does not also license a raw setFrame.
MAC_ALLOWLIST = {
    "apple/macos/src/platform/MacProjection.mm": {"NSMakeRect"},
    # SetMacFrame, MacMeasurementBounds, SetMacDocumentHeight, ScrollMacDocument:
    # typed inputs; the latter two preserve AppKit-owned width/clip translation.
    "apple/macos/src/platform/MacNativeGeometry.hpp": {"NSMakeRect", "setFrame", "scrollPoint"},
    # drawRect text centring: native bounds and native measured glyph size.
    "apple/macos/src/context/MacCellContext.mm": {"NSMakeRect"},
    # drawRect IMAGE_FIT_COVER source crop: decoded pixels, never layout units.
    "apple/macos/src/context/MacImageViewContext.mm": {"NSMakeRect"},
    # drawRect edge blend: one-point rows of the view's native bounds.
    "apple/macos/src/context/MacRibbonGroupEdgeContext.mm": {"NSMakeRect"},
    # Constructor: initial document width is AppKit contentSize, height zero.
    # relayout: refresh document width from AppKit, preserving native height.
    "apple/macos/src/context/MacScrollViewContext.mm": {"NSMakeRect", "setFrame"},
    # FrameRectForContent: already-projected/native content plus native chrome.
    # FrameChangedThunk/createNativeWindow: desktop placement and #712 clamp.
    "apple/macos/src/MacWindow.mm": {"NSMakeRect", "setFrame"},
}
# RectSurface sprites call projectDeviceOnly; no raw-API permission is needed.
MAC_CALL = re.compile(r"\b(NSMakeRect)\s*\(|\b(setFrame|setFrameSize|setFrameOrigin|scrollPoint|scrollToPoint)\s*:")
CALL = re.compile(r"\b(MoveWindow|SetWindowPos|CreateWindowExW|InvalidateRect|MulDiv)\s*\(")
NONCODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)
# The window may be named through a no-argument accessor such as this->hwnd().
NULL_DAMAGE = re.compile(r"\s*[^,()]+(?:\(\s*\))?\s*,\s*(?:NULL|0|nullptr)\s*,")
SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".inc", ".inl", ".cc", ".cxx", ".mm", ".m"}


def violations(path, source):
    source = re.sub(r"\\\r?\n", "", source)
    source = NONCODE.sub(lambda m: " " * (len(m[0]) - m[0].count("\n"))
                        + "\n" * m[0].count("\n"), source)
    call = MAC_CALL if path.startswith("apple/macos/") else CALL
    allowlist = MAC_ALLOWLIST if path.startswith("apple/macos/") else ALLOWLIST
    for match in call.finditer(source):
        api = match[1] or match[2]
        # Whole-window invalidation constructs no geometry.
        if api == "InvalidateRect" and NULL_DAMAGE.match(source, match.end()):
            continue
        if api not in allowlist.get(path, set()):
            yield source.count("\n", 0, match.start()) + 1, api


def check_tree(root):
    files = sorted(p for subtree in ("win32/src", "apple/macos/src")
                   for p in (root / subtree).rglob("*")
                   if p.is_file() and p.suffix.lower() in SUFFIXES)
    findings = []
    for path in files:
        relative = path.relative_to(root).as_posix()
        findings.extend(f"{relative}:{line}: native geometry call {api} outside typed wrappers"
                        for line, api in violations(relative, path.read_text(encoding="utf-8")))
    return files, findings


def main():
    files, findings = check_tree(Path(__file__).resolve().parents[2])
    for finding in findings:
        print(finding)
    print(f"check_native_geometry: {len(files)} files, {len(findings)} findings")
    return int(bool(findings))


if __name__ == "__main__":
    sys.exit(main())
