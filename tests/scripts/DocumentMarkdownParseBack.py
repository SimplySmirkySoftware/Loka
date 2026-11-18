#!/usr/bin/env python3
"""Check the export projection. Exit 0 = pass, 1 = mismatch, 77 = skipped (unverified)."""
from itertools import groupby
import argparse
import ctypes
import subprocess
import sys

def projection(parser, source):
    tokens = parser.parse(source)
    if not tokens:
        return "", []
    if any(t.type == "html_block" for t in tokens):
        raise ValueError("html_block")
    if len(tokens) == 1 and tokens[0].type == "fence" and not tokens[0].info:
        return "c", [(c, 0, "") for c in tokens[0].content]
    opening, inline, closing = tokens
    kind = {"paragraph_open": "p", "heading_open": "h"}[opening.type]
    if inline.type != "inline" or closing.tag != opening.tag or (kind == "h" and opening.tag != "h2"):
        raise ValueError("unexpected block")
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
    return kind, result


def decode(value):
    return bytes.fromhex(value).decode()


def unsigned_decimal(text):
    maximum = ctypes.c_uint(-1).value
    digits = text.lstrip("0") or "0"
    if (not text.isascii() or not text.isdigit() or len(digits) > len(str(maximum))
            or int(digits) > maximum):
        raise argparse.ArgumentTypeError(f"expected a decimal integer from 0 to {maximum}")
    return int(digits)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("executable")
    cli.add_argument("seed", type=unsigned_decimal)
    cli.add_argument("count", type=unsigned_decimal)
    args = cli.parse_args()
    try:
        import markdown_it
    except ModuleNotFoundError as error:
        if error.name != "markdown_it":
            raise
        print("[skip] markdown_it not installed")
        sys.exit(77)
    parser = markdown_it.MarkdownIt("commonmark")
    parser.validateLink = lambda url: True
    parser.normalizeLink = parser.normalizeLinkText = lambda url: url
    print(f"markdown_it {markdown_it.__version__}, seed {args.seed}", flush=True)
    data = subprocess.check_output([args.executable, "--document-markdown-cases", str(args.seed), str(args.count)], text=True)
    rows = data.splitlines()
    failures = refusals = 0
    for i, line in enumerate(rows):
        kind, glyphs, spelling = line.split("\t")
        expected = [(char, int(f), decode(u)) for c, f, u in
                    (g.split(":") for g in glyphs.split(",") if g) for char in decode(c)]
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
        if actual != (kind if expected or kind != "p" else "", expected):
            failures += 1
            if failures <= 5:
                print(i, repr(source), "expected", expected, "got", actual)
    print(f"{len(rows)} cases, {refusals} expected refusals, {failures} mismatches")
    return int(failures != 0 or len(rows) != args.count)


if __name__ == "__main__":
    sys.exit(main())
