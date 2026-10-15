#!/usr/bin/env python3
"""Positive and negative controls for the literal native geometry wall."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("checker", ROOT / "tools/ci/check_native_geometry.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class NativeGeometryCheckerTest(unittest.TestCase):
    def test_each_api_is_refused_in_unreviewed_files(self):
        for api in ("MoveWindow", "SetWindowPos", "CreateWindowExW", "InvalidateRect", "MulDiv"):
            with self.subTest(api=api):
                self.assertEqual(list(checker.violations("win32/src/context/New.cpp",
                    f"#if 0\n::{api} /* gap */ (hwnd, &rect, 1);\n#endif")), [(2, api)])

    def test_permissions_are_api_specific_and_exact_paths(self):
        for path, apis in checker.ALLOWLIST.items():
            for api in apis:
                self.assertEqual(list(checker.violations(path, f"{api}(x);")), [])
                self.assertTrue(list(checker.violations(path + ".copy.cpp", f"{api}(x);")))
        self.assertTrue(list(checker.violations("win32/src/Win32Window.cpp", "MulDiv(1, 2, 3);")))

    def test_null_invalidation_and_noncode(self):
        source = '''// MoveWindow(x);
/* SetWindowPos(x); */
const char *s = "CreateWindowExW(x)";
InvalidateRect(hwnd, NULL, TRUE);
InvalidateRect(hwnd, 0, TRUE);
InvalidateRect(hwnd, &r, TRUE);
InvalidateRect(this->hwnd(), NULL, TRUE);
InvalidateRect(this->hwnd(), &r, TRUE);
InvalidateRect(windowFor(r), NULL, TRUE);
'''
        self.assertEqual(list(checker.violations("win32/src/context/New.cpp", source)),
                         [(6, "InvalidateRect"), (8, "InvalidateRect"), (9, "InvalidateRect")])

    def test_header_multiline_and_spliced_calls(self):
        self.assertEqual(list(checker.violations("win32/src/New.hpp", "Move\\\nWindow\n(hwnd);")), [(1, "MoveWindow")])

    def test_macos_calls_and_policy(self):
        for api, call in (("NSMakeRect", "NSMakeRect(0, 0, x, y)"),
                          ("setFrame", "[view setFrame:rect]"),
                          ("setFrameSize", "[view setFrameSize:size]"),
                          ("setFrameOrigin", "[view setFrameOrigin:point]"),
                          ("scrollPoint", "[view scrollPoint:point]"),
                          ("scrollToPoint", "[clip scrollToPoint:point]")):
            with self.subTest(api=api):
                self.assertEqual(list(checker.violations("apple/macos/src/New.mm", call)), [(1, api)])
        self.assertEqual(list(checker.violations("apple/macos/src/platform/MacProjection.mm",
                                               "NSMakeRect(0, 0, x, y)")), [])
        self.assertTrue(list(checker.violations("apple/macos/src/platform/MacProjection.mm",
                                              "[view setFrame:rect]")))

    def test_macos_exact_api_permissions(self):
        for path, apis in checker.MAC_ALLOWLIST.items():
            for api in apis:
                call = "NSMakeRect(0, 0, 1, 1)" if api == "NSMakeRect" else f"[v {api}:r]"
                self.assertEqual(list(checker.violations(path, call)), [])
                self.assertTrue(list(checker.violations(path + ".copy.mm", call)))
        self.assertTrue(list(checker.violations("apple/macos/src/context/MacCellContext.mm",
                                              "[v setFrame:r]")))

    def test_macos_noncode_multiline_and_tree(self):
        source = '// [v setFrame:r];\n@"NSMakeRect(0,0,1,1)";\n[v setFrame /* gap */ : r];'
        self.assertEqual(list(checker.violations("apple/macos/src/New.mm", source)), [(3, "setFrame")])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("apple/macos/src/New.mm", "apple/macos/src/New.hpp",
                         "apple/toolbox/src/New.mm"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("NSMakeRect(0, 0, 1, 1);", encoding="utf-8")
            files, findings = checker.check_tree(root)
            self.assertEqual(len(files), 2)
            self.assertEqual(len(findings), 2)

    def test_tree_scope(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("win32/src/New.hpp", "win32/src/nested/New.cpp", "tests/Win32Fixture.cpp"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("MoveWindow(hwnd, 0, 0, 1, 1, TRUE);", encoding="utf-8")
            files, findings = checker.check_tree(root)
            self.assertEqual(len(files), 2)
            self.assertEqual(len(findings), 2)


if __name__ == "__main__":
    unittest.main()
