#!/usr/bin/env python3
"""Exercise the published Loka pages through the shipping CLI."""
from html.parser import HTMLParser
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit
from xml.etree.ElementTree import TreeBuilder


REPO = "https://github.com/SimplySmirkySoftware/Loka"
PAGES = {
    "index.html": ("Loka", "Loka", [
        "start.html", REPO + "/blob/main/README.md#supported-target-environments",
        REPO + "/blob/main/docs/ProgrammingGuide.en.md", REPO, REPO + "/releases"]),
    "start.html": ("Get started with Loka", "Get started", [
        REPO + "/blob/main/docs/environments.md", "index.html"]),
}
COMMANDS = ["cmake --preset testing", "cmake --build --preset testing", "ctest --preset testing"]


class PageParser(HTMLParser):
    """Collect the writer's HTML elements with decoded character data."""
    def __init__(self):
        super().__init__()
        self.tree = TreeBuilder()

    def handle_starttag(self, tag, attrs):
        self.tree.start(tag, dict(attrs))
        if tag in ("meta", "link", "img"):
            self.tree.end(tag)

    def handle_endtag(self, tag):
        self.tree.end(tag)

    def handle_data(self, data):
        self.tree.data(data)


def page_tree(html):
    parser = PageParser()
    parser.feed(html)
    parser.close()
    return parser.tree.close()


def files(directory, sheet):
    assert {p.name for p in directory.iterdir()} == set(PAGES) | {"site.css"}
    for path in directory.iterdir():
        assert path.is_file() and stat.S_IMODE(path.stat().st_mode) == 0o644
        assert path.stat().st_size > 0
    assert (directory / "site.css").read_bytes() == sheet.read_bytes()


def pages(directory):
    for name, (title, heading, destinations) in PAGES.items():
        root = page_tree((directory / name).read_text(encoding="utf-8"))
        assert [node.text for node in root.findall("head/title")] == [title]
        assert [node.text for node in root.iter("h1")] == [heading]
        assert [n.get("href") for n in root.findall('head/link[@rel="stylesheet"]')] == ["site.css"]
        blocks = list(root.iter("pre"))
        assert len(blocks) == 1
        code = "".join(blocks[0].itertext())
        assert code.strip()
        if name == "start.html":
            assert code.splitlines() == COMMANDS
            assert [n.text for n in root.findall(".//p/code")] == ["example"]
        assert len(root.findall(".//ul/li/a")) == len(destinations)


def images_are_local(root, emitted):
    for image in root.iter("img"):
        src = image.get("src")
        if not src or "/" in src or urlsplit(src).scheme or src not in emitted:
            return False
    return True


def links(directory):
    emitted = {p.name for p in directory.iterdir() if p.is_file()}
    control = page_tree('<p><img src="map.png"></p>')
    assert images_are_local(control, {"map.png"})
    assert not images_are_local(control, set())
    for name, (_, _, expected) in PAGES.items():
        root = page_tree((directory / name).read_text(encoding="utf-8"))
        assert images_are_local(root, emitted), name
        assert [a.get("href") for a in root.iter("a")] == expected
        assert [a.get("href") for a in root.findall(".//ul/li/a")] == expected
        for node in root.iter():
            if "href" in node.attrib:
                href = node.attrib["href"]
                url = urlsplit(href)
                if url.scheme or url.netloc:
                    assert href.startswith(REPO)
                else:
                    target = (directory / url.path).resolve()
                    assert target.parent == directory.resolve() and target.is_file()


def parse(directory):
    try:
        import html5lib
    except ModuleNotFoundError as error:
        if error.name != "html5lib":
            raise
        print("[skip] html5lib not installed")
        return 77
    for name in PAGES:
        root = html5lib.HTMLParser(strict=True, namespaceHTMLElements=False).parse(
            (directory / name).read_text(encoding="utf-8"))
        assert root.findtext("body/main/h1") == PAGES[name][1]
    return 0


def main():
    if not __debug__:
        sys.exit("LokaSite.py checks with assert; run it without Python optimization")
    case, tool, stylesheet = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp) / "out"
        directory.mkdir()
        sheet = Path(stylesheet)
        if case == "Files":
            sheet = Path(temp) / "custom.css"
            sheet.write_bytes(b"/* supplied by the caller */\r\n" + Path(stylesheet).read_bytes())
        subprocess.run([tool, directory, sheet], check=True)
        if case == "Files":
            files(directory, sheet)
        elif case == "Pages":
            pages(directory)
        elif case == "Links":
            links(directory)
        elif case == "Parse":
            return parse(directory)
        else:
            raise ValueError(case)
    return 0


if __name__ == "__main__":
    sys.exit(main())
