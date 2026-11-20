#!/usr/bin/env python3
"""Compare the HTML5 fragment, authored projection and CommonMark tokens for each generated block."""
import argparse
from itertools import groupby
import subprocess
import sys

from DocumentMarkdownParseBack import decode, expected_glyphs, markdown_parser, projection, unsigned_decimal


def html_projection(html5lib, source):
    fragment = html5lib.parseFragment(source, container="div", namespaceHTMLElements=False)
    if fragment.text or len(fragment) > 1:
        raise ValueError("unexpected fragment content")
    if not len(fragment):
        return "", [], []
    block = fragment[0]
    if block.tail != "\n":
        raise ValueError("missing block separator")
    if block.tag == "pre":
        if (len(block) != 1 or block.text or block[0].tag != "code" or block[0].tail or len(block[0])):
            raise ValueError("unexpected code block")
        return "c", [(c, 0, "") for c in block[0].text or ""], []

    kind = {"p": "p", "h2": "h"}[block.tag]  # The generator authors level 2.
    rows, anchors = [], []

    def text(value, flags, target):
        rows.extend((c, flags, target) for c in value or "")

    def visit(node, flags, target):
        text(node.text, flags, target)
        for child in node:
            if child.tag == "br":
                text("\n", flags, target)
            elif child.tag == "a":
                href = child.attrib["href"]
                start = len(rows)
                visit(child, flags, href)
                anchors.append((start, len(rows), href))
            else:
                visit(child, flags | {"em": 1, "strong": 2, "code": 4}[child.tag], target)
            text(child.tail, flags, target)

    visit(block, 0, "")
    return kind, rows, anchors


def anchor_ranges(rows):
    start = 0
    for target, group in groupby(rows, key=lambda row: row[2]):
        end = start + sum(1 for _ in group)
        if target:
            yield start, end, target
        start = end


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("executable")
    cli.add_argument("seed", type=unsigned_decimal)
    cli.add_argument("count", type=unsigned_decimal)
    args = cli.parse_args()
    try:
        import html5lib
    except ModuleNotFoundError as error:
        if error.name != "html5lib":
            raise
        print("[skip] html5lib not installed")
        return 77
    try:
        import markdown_it
    except ModuleNotFoundError as error:
        if error.name != "markdown_it":
            raise
        print("[skip] markdown_it token comparison")
        parser = None
    else:
        parser = markdown_parser(markdown_it)
    lines = subprocess.check_output([args.executable, "--document-html-cases", str(args.seed),
                                     str(args.count)], text=True).splitlines()
    comparisons = 0
    for i, line in enumerate(lines):
        kind, glyphs, md, html = line.split("\t")
        expected = expected_glyphs(glyphs)
        block = kind if expected or kind != "p" else ""
        try:
            actual, rows, anchors = html_projection(html5lib, decode(html))
            if (actual, rows) != (block, expected) or anchors != list(anchor_ranges(expected)):
                raise ValueError((actual, rows, anchors))
            if parser and md != "REFUSED":
                if projection(parser, decode(md)) != (actual, rows):
                    raise ValueError("CommonMark token mismatch")
                comparisons += 1
        except (ValueError, IndexError, KeyError) as error:
            print(f"seed {args.seed}, case {i}: {line!r}: {error}")
            return 1
    print(f"seed {args.seed}, {len(lines)} HTML fragments, {comparisons} token comparisons")
    return int(len(lines) != args.count)


if __name__ == "__main__":
    sys.exit(main())
