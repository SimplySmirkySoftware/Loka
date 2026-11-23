#!/usr/bin/env python3
"""Read DocumentPage as CommonMark and check the quoted image and following paragraph."""
import subprocess
import sys

from DocumentMarkdownParseBack import markdown_parser


def main():
    try:
        import markdown_it
        import html5lib
    except ModuleNotFoundError as error:
        if error.name not in ("markdown_it", "html5lib"):
            raise
        print(f"[skip] {error.name} not installed")
        return 77

    markdown = subprocess.check_output([sys.argv[1]], encoding="utf-8")
    html = markdown_parser(markdown_it).render(markdown)
    tree = html5lib.parseFragment(html, namespaceHTMLElements=False)
    quote = tree.find("blockquote")
    if quote is None or [child.tag for child in quote] != ["img", "p"]:
        raise ValueError(html)
    if quote[0].attrib != {"src": "map.png", "alt": "Trail & ridge", "width": "480", "height": "320"}:
        raise ValueError(quote[0].attrib)
    if quote[1].text != "Record the trail conditions.":
        raise ValueError(html)
    return 0


if __name__ == "__main__":
    sys.exit(main())
