#!/usr/bin/env python3
"""Compare each parsed HTML5 fragment with its independently authored forest."""
import argparse
from itertools import groupby
import subprocess
import sys

from DocumentMarkdownParseBack import containers, decode, expected_glyphs, expected_leaf, unsigned_decimal


def html_projection(html5lib, source):
    fragment = html5lib.parseFragment(source, container="div", namespaceHTMLElements=False)
    def blocks(parent, lead=""):
        # The fragment root carries no text; an emitted container carries exactly its formatting LF.
        if (parent.text or "") != lead:
            raise ValueError("unexpected container text")
        result = []
        for block in parent:
            if block.tail != "\n":
                raise ValueError("missing block separator")
            if block.tag in ("ul", "blockquote"):
                value = blocks(block, "\n")
            elif block.tag == "pre":
                if len(block) != 1 or block.text or block[0].tag != "code" or block[0].tail or len(block[0]):
                    raise ValueError("unexpected code block")
                value = [(c, 0, "") for c in block[0].text or ""]
            elif block.tag in ("h2", "li", "p"):
                value = inline_glyphs(block)
            else:
                raise ValueError(block.tag)
            result.append((block.tag, value))
        return result
    return blocks(fragment)


def inline_glyphs(block):
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
    if sorted(anchors) != list(anchor_ranges(rows)):
        raise ValueError("anchor boundaries")
    return rows


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
    cli.add_argument("--containers", action="store_true")
    args = cli.parse_args()
    try:
        import html5lib
    except ModuleNotFoundError as error:
        if error.name != "html5lib":
            raise
        print("[skip] html5lib not installed")
        return 77
    if args.containers:
        return containers(args, 2, lambda source: html_projection(html5lib, source))
    lines = subprocess.check_output([args.executable, "--document-html-cases", str(args.seed),
                                     str(args.count)], encoding="utf-8").splitlines()
    comparisons = 0
    for i, line in enumerate(lines):
        kind, glyphs, md, html = line.split("\t")
        expected = expected_glyphs(glyphs)
        try:
            actual = html_projection(html5lib, decode(html))
            if actual != expected_leaf(kind, expected):
                raise ValueError(actual)
            comparisons += 1
        except (ValueError, IndexError, KeyError) as error:
            print(f"seed {args.seed}, case {i}: {line!r}: {error}")
            return 1
    print(f"seed {args.seed}, {len(lines)} HTML fragments, {comparisons} authored forest comparisons")
    return int(len(lines) != args.count)


if __name__ == "__main__":
    sys.exit(main())
