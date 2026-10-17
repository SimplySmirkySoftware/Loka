#!/usr/bin/env python3
"""Refuse literal Pattern objects outside the aligned wrapper (#1398).

The rule is about the token that follows the type name, not about C++
declarations: after `Pattern` (and any const/volatile), a name or `(name`
declares a value and is refused, while `*` and `&` borrow and are accepted.
`new Pattern` and any typedef spelling Pattern are refused. Declarator lists
are not parsed, so a value listed after a borrowed declarator
(`Pattern *p, value;`) passes: declare a Pattern value in its own statement.
Comments/strings are ignored; inactive preprocessor branches are checked.
File-level trust in the wrapper still requires review of its alignment contract.
"""
from pathlib import Path
import re
import sys

# The union's Pattern member shares storage with its long alignment member.
ALLOWLIST = {"apple/toolbox/src/ToolboxAlignedPattern.hpp"}
NONCODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)
SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".inc", ".inl", ".cc", ".cxx", ".mm", ".m"}
# Shared core, scenario/standalone code and shared tests also compile for Classic.
SUBTREES = ("apple/toolbox", "common", "example", "tests")
# Host fakes define struct Pattern themselves and do not compile for Classic.
EXCLUDED = ("tests/toolbox/host/", "tests/win32/", "tests/macos/", "tests/platform/null/")
PATTERN = re.compile(r"\bPattern\b")
QUALIFIERS = r"(?:(?:const|volatile|static)\s+)*"
VALUE = re.compile(r"\s*" + QUALIFIERS + r"(?:\(\s*)?(?!(?:const|volatile|static)\b)[A-Za-z_]\w*\b")
POINTER_TYPE = re.compile(r"\s*" + QUALIFIERS + r"[*&]")
NEW = re.compile(r"\bnew\s+(?:\([^;{}]*\)\s*)?" + QUALIFIERS + r"(?:::)?\s*$")
TYPEDEF = re.compile(r"\btypedef\b[^;{}]*$")


def violations(path, source):
    if path in ALLOWLIST:
        return
    source = re.sub(r"\\\r?\n", "", source)
    source = NONCODE.sub(lambda m: " " * (len(m[0]) - m[0].count("\n"))
                        + "\n" * m[0].count("\n"), source)
    for match in PATTERN.finditer(source):
        prefix = source[:match.start()]
        tail = source[match.end():]
        # sizeof and pointer casts have no named value after the type.
        if (TYPEDEF.search(prefix)
                or (NEW.search(prefix) and not POINTER_TYPE.match(tail))
                or VALUE.match(tail)):
            yield source.count("\n", 0, match.start()) + 1, "Pattern"


def check_tree(root):
    files = sorted(p for subtree in SUBTREES
                   for p in (root / subtree).rglob("*")
                   if p.is_file() and p.suffix.lower() in SUFFIXES
                   and not p.relative_to(root).as_posix().startswith(EXCLUDED))
    findings = []
    for path in files:
        relative = path.relative_to(root).as_posix()
        findings.extend(f"{relative}:{line}: Pattern object declared by value; use ToolboxAlignedPattern (#1398)"
                        for line, _ in violations(relative, path.read_text(encoding="utf-8")))
    return files, findings


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    files, findings = check_tree(root)
    for finding in findings:
        print(finding)
    print(f"check_toolbox_pattern_alignment: {len(files)} files, {len(findings)} findings")
    return int(bool(findings))


if __name__ == "__main__":
    sys.exit(main())
