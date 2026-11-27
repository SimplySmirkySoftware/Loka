#!/usr/bin/env python3
"""Check the export projection. Exit 0 = pass, 1 = mismatch, 77 = skipped (unverified)."""
from itertools import groupby
import argparse
import ctypes
import json
import subprocess
import sys

def projection(parser, source):
    forest = []
    stack = [("root", forest)]
    for token in parser.parse(source):
        if token.hidden and token.tag == "p" and stack[-1][0] == "li":
            continue
        if token.type == "inline":
            stack[-1][1].extend(inline_glyphs(token))
        elif token.type == "fence" and not token.info:
            stack[-1][1].append(("pre", plain_glyphs(token.content)))
        elif token.nesting == 1:
            if token.type == "list_item_open" and stack[-1][0] == "ol":
                expected_number = len(stack[-1][1]) + 1
                if token.info != str(expected_number):
                    raise ValueError(f"ordered item {expected_number} has source ordinal {token.info!r}")
            node = (token.tag, [])
            stack[-1][1].append(node)
            stack.append(node)
        elif token.nesting == -1 and stack[-1][0] == token.tag:
            stack.pop()
        else:
            raise ValueError(token.type)
    if len(stack) != 1:
        raise ValueError("unclosed block")
    return forest


def plain_glyphs(text):
    return [(c, 0, "") for c in text]


def inline_glyphs(inline):
    marks = []
    target = ""
    result = []
    tags = {"<em>": "em_open", "</em>": "em_close", "<strong>": "strong_open", "</strong>": "strong_close"}
    for token in inline.children:
        t = tags[token.content] if token.type == "html_inline" else token.type
        if t in ("strong_open", "em_open"):
            marks.append(t[:-5])
        elif t in ("strong_close", "em_close"):
            if marks.pop() != t[:-6]:
                raise ValueError("unbalanced emphasis")
        elif t == "link_open":
            target = token.attrGet("href")
        elif t == "link_close":
            target = ""
        elif t in ("text", "code_inline", "hardbreak"):
            flags = int("em" in marks) + 2 * ("strong" in marks) + 4 * (t == "code_inline")
            result.extend((c, flags, target) for c in ("\n" if t == "hardbreak" else token.content))
        else:
            raise ValueError(t)
    return result


def decode(value):
    return bytes.fromhex(value).decode()


def unsigned_decimal(text):
    maximum = ctypes.c_uint(-1).value
    digits = text.lstrip("0") or "0"
    if (not text.isascii() or not text.isdigit() or len(digits) > len(str(maximum))
            or int(digits) > maximum):
        raise argparse.ArgumentTypeError(f"expected a decimal integer from 0 to {maximum}")
    return int(digits)


def expected_glyphs(glyphs):
    return [(char, int(f), decode(u)) for c, f, u in
            (g.split(":") for g in glyphs.split(",") if g) for char in decode(c)]


def markdown_parser(markdown_it):
    parser = markdown_it.MarkdownIt("commonmark")
    parser.validateLink = lambda url: True
    parser.normalizeLink = parser.normalizeLinkText = lambda url: url
    return parser


def expected_leaf(kind, glyphs):
    return [({"h": "h2", "c": "pre", "p": "p"}[kind], glyphs)] if glyphs or kind != "p" else []


def authored_forest(nodes, parent=None):
    forest = []
    for tag, value in nodes:
        if tag in ("blockquote", "ul", "ol"):
            children = authored_forest(value, tag)
            if children:
                forest.append((tag, children))
        elif value or tag in ("h2", "pre") or (tag == "li" and parent == "ol"):
            if tag == "h2":
                value = value.replace("\n", " ")
            if tag == "pre" and value and not value.endswith("\n"):
                value += "\n"
            forest.append((tag, plain_glyphs(value)))
    return forest


def containers(args, field, parse):
    command = [args.executable, "--document-container-cases", str(args.seed), str(args.count)]
    rows = subprocess.check_output(command, encoding="utf-8").splitlines()
    for index, row in enumerate(rows):
        fields = row.split("\t")
        expected = authored_forest(json.loads(fields[0]))
        source = decode(fields[field])
        actual = parse(source)
        if actual != expected:
            print(f"case {index}, {source!r}, expected {expected!r}, got {actual!r}")
            return 1
    print(f"seed {args.seed}, {len(rows)} authored container forests, zero mismatches")
    return int(len(rows) != args.count)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("executable")
    cli.add_argument("seed", type=unsigned_decimal)
    cli.add_argument("count", type=unsigned_decimal)
    cli.add_argument("--containers", action="store_true")
    args = cli.parse_args()
    try:
        import markdown_it
    except ModuleNotFoundError as error:
        if error.name != "markdown_it":
            raise
        print("[skip] markdown_it not installed")
        sys.exit(77)
    parser = markdown_parser(markdown_it)
    print(f"markdown_it {markdown_it.__version__}, seed {args.seed}", flush=True)
    # Stock maxNesting drops the innermost text at twenty quotes.
    if any(t.type == "inline" and t.content == "x" for t in parser.parse("> " * 20 + "x\n")):
        raise ValueError("stock nesting-limit fixture changed")
    if args.containers:
        return containers(args, 1, lambda source: projection(parser, source))
    data = subprocess.check_output([args.executable, "--document-markdown-cases", str(args.seed), str(args.count)], encoding="utf-8")
    rows = data.splitlines()
    failures = refusals = 0
    for i, line in enumerate(rows):
        kind, glyphs, spelling = line.split("\t")
        expected = expected_glyphs(glyphs)
        # Some CommonMark readers trim code-span padding with Unicode strip().
        # Only whitespace-only code runs with ambiguous ASCII edge padding may refuse.
        strip_only = "\v\f\x1c\x1d\x1e\x1f\x85\u2028\u2029"
        ambiguous = any(c in strip_only and not flags and not target
                        for c, flags, target in expected[:1] + expected[-1:])
        for (flags, _), run in groupby(expected, key=lambda glyph: glyph[1:]):
            body = "".join(c for c, _, _ in run)
            ambiguous |= bool(flags & 4 and body.startswith(" ") and body.endswith(" ")
                              and not body.strip() and body.strip(" "))
        if spelling == "REFUSED":
            if kind != "c" and ambiguous:
                refusals += 1
                continue
            source, actual = None, "unexpected refusal"
        else:
            source = decode(spelling)
            try:
                actual = projection(parser, source)
            except (ValueError, KeyError, IndexError) as error:
                actual = str(error)
        if actual != expected_leaf(kind, expected):
            failures += 1
            if failures <= 5:
                print(i, repr(source), "expected", expected, "got", actual)
    print(f"{len(rows)} cases, {refusals} expected refusals, {failures} mismatches")
    return int(failures != 0 or len(rows) != args.count)


if __name__ == "__main__":
    sys.exit(main())
