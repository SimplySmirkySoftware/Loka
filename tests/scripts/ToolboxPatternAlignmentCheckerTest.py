#!/usr/bin/env python3
"""Positive and negative controls for the literal Classic Pattern wall."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("checker", ROOT / "tools/ci/check_toolbox_pattern_alignment.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class ToolboxPatternAlignmentCheckerTest(unittest.TestCase):
    def test_values_are_refused(self):
        for source in (
            "Pattern global;", "namespace X { Pattern global; }",
            "struct X { Pattern member; };", "void f() { Pattern pattern; }",
            "static const Pattern kToolboxGradientPatterns[9] = {{{0x00, ...}}, ...};",
            "static volatile ::Pattern table[9];", "::Pattern const value = {};",
            "Pattern volatile value;", "Pattern first, second;", "Pattern (value);",
            "Pattern *pointer, value;", "Pattern &reference = other, value;",
            # Initializers with calls, braces or nested commas before a value (#1408 review).
            "Pattern *p = currentPattern(), value;", "Pattern &r = pick(a, b), value;",
            "Pattern *p = table[i], *q = {0}, value;",
            "new Pattern;", "new ::Pattern[9];", "new const Pattern();",
            "new (storage) volatile ::Pattern;",
            "typedef Pattern Hidden;", "typedef const ::Pattern Hidden[9];",
            "typedef Pattern *HiddenPointer;",
            "#if 0\nPattern value;\n#endif",
        ):
            with self.subTest(source=source):
                expected_line = 2 if source.startswith("#if") else 1
                self.assertEqual(list(checker.violations("apple/toolbox/src/New.cpp", source)),
                                 [(expected_line, "Pattern")])

    def test_borrowed_and_nonobjects_are_accepted(self):
        for source in (
            "const Pattern *p;", "Pattern &r = other;", "Pattern const *p;",
            "void draw(const Pattern *pattern, int width);",
            "void draw(Pattern &pattern, Other width);",
            "void draw(Pattern const *pattern, int width);",
            "new Pattern *;", "new const ::Pattern *[9];",
            "volatile ::Pattern *p;", "Pattern *p, *q;", "Pattern *p[9];",
            "Pattern *p = currentPattern(), *q = pick(a, b);",
            "sizeof(Pattern);", "sizeof(::Pattern);", "sizeof(const Pattern);",
            "(Pattern *)ptr;", "static_cast<const Pattern *>(ptr);",
            "x.pattern; x.pat; qd.gray; qd.black; qd.white;",
            "PatternFoo x; ToolboxAlignedPattern kToolboxGradientPatterns[9];",
            '// Pattern value;\n/* Pattern value; */\n"Pattern value;";\n\'P\';',
        ):
            with self.subTest(source=source):
                self.assertEqual(list(checker.violations("apple/toolbox/src/New.cpp", source)), [])

    def test_exact_file_permission(self):
        self.assertEqual(checker.ALLOWLIST, {"apple/toolbox/src/ToolboxAlignedPattern.hpp"})
        for path in ("apple/toolbox/src/ToolboxAlignedPattern.hpp",):
            self.assertEqual(list(checker.violations(path, "Pattern pattern;")), [])
            self.assertEqual(list(checker.violations(path + ".copy.hpp", "Pattern pattern;")),
                             [(1, "Pattern")])

    def test_comments_lines_and_splicing(self):
        self.assertEqual(list(checker.violations("example/X/src/New.hpp",
                         '/* Pattern fake;\n */\nPat\\\ntern /* gap */\nvalue;')),
                         [(3, "Pattern")])

    def test_tree_scope_and_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            included = ("apple/toolbox/src/New.hpp", "apple/toolbox/include/New.h",
                        "tests/toolbox/src/New.cpp", "example/X/src/New.CPP",
                        "common/core/New.inl", "tests/scenarios/New.cpp",
                        "tests/standalone/New.cpp", "tests/TestingHooks.cpp")
            excluded = ("tests/toolbox/host/Quickdraw.h", "tests/win32/host/New.cpp",
                        "tests/macos/New.mm", "tests/platform/null/New.cpp",
                        "apple/macos/src/New.mm", "win32/src/New.cpp", "example/X/note.txt")
            for name in included + excluded:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("\nPattern value;", encoding="utf-8")
            files, findings = checker.check_tree(root)
            self.assertEqual({p.relative_to(root).as_posix() for p in files}, set(included))
            self.assertEqual(findings, sorted(
                f"{p}:2: Pattern object declared by value; use ToolboxAlignedPattern (#1398)"
                for p in included))


if __name__ == "__main__":
    unittest.main()
