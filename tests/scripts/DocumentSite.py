#!/usr/bin/env python3
"""Exercise the two-page shipping tool and its refusal fixture in existing directories."""
from html.parser import HTMLParser
from pathlib import Path
import resource
import struct
import subprocess
import sys
import tempfile
import zlib
from xml.etree import ElementTree


NAMES = {"index.html", "trail.html", "site.css", "map.png"}
PAGES = [("index.html", "The map room", "trail.html"),
         ("trail.html", "Above the tide line", "index.html")]
SHEET = b"body {\r\n  color: navy;\r\n}\r\n"


def map_png():
    """Draw a ridge path alongside tidal flats in a small RGB PNG."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    width, height = 48, 32
    pixels = bytearray()
    for y in range(height):
        shore = 8 + abs(y - 12) // 2
        pixels.append(0)  # Unfiltered scanline.
        for x in range(width):
            if x < shore:
                pixels.extend((96, 152, 173))
            elif shore + 5 <= x < shore + 7:
                pixels.extend((119, 81, 46))
            else:
                pixels.extend((223, 215, 175))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.anchors = []
        self.sheets = []
        self.images = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "a":
            self.anchors.append(attributes.get("href"))
        if tag == "img":
            self.images.append((attributes.get("src"), attributes.get("alt")))
        if tag == "link":
            self.sheets.append((attributes.get("rel"), attributes.get("href")))


def entries(directory):
    return {p.name for p in directory.iterdir()}


def output(tool, directory, sheet, image, example, stylesheet):
    subprocess.run([tool, directory, sheet, image], check=True)
    assert entries(directory) == NAMES
    assert (directory / "map.png").read_bytes() == image.read_bytes()
    assert (directory / "site.css").read_bytes() == SHEET
    for name, _, peer in PAGES:
        page = (directory / name).read_bytes()
        assert page.startswith(b'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                               b'<meta name="viewport" content="width=device-width, initial-scale=1">\n')
        links = Links()
        links.feed(page.decode())
        links.close()
        assert links.anchors == [peer] and links.sheets == [("stylesheet", "site.css")]
        for src, _ in links.images:
            assert src.endswith((".png", ".jpg", ".gif")) and (directory / src).is_file()
        if name == "trail.html":
            assert links.images == [("map.png", "The ridge path above the tidal flats")]
        assert page == (example / "expected" / name).read_bytes(), name

    modes = lambda: {(directory / name).stat().st_mode & 0o777 for name in NAMES}
    assert modes() == {0o644}, "new files are readable by a server running as another user"
    (directory / "map.png").write_bytes(b"previous image")
    subprocess.run([tool, directory, stylesheet, image], check=True)
    assert (directory / "site.css").read_bytes() == (stylesheet).read_bytes()
    assert (directory / "map.png").read_bytes() == image.read_bytes()
    assert modes() == {0o644}, "replaced files keep that mode"


def refusal(fixture, directory, sheet):
    good = directory.parent / "control"
    good.mkdir()
    subprocess.run([fixture, "valid", good, sheet], check=True)
    names = NAMES | {"third.html"}
    assert entries(good) == names
    for name, title in (("index.html", "First"), ("trail.html", "Second"), ("third.html", "Third landmark")):
        assert f"<title>{title}</title>" in (good / name).read_text()
    assert (good / "site.css").read_bytes() == SHEET
    assert (good / "map.png").read_bytes() == SHEET
    for seeded in (False, True):
        before = {name: name.encode() for name in names} if seeded else {}
        for name, data in before.items():
            (directory / name).write_bytes(data)
        for mode in ("refuse", "blank", "empty", "image-only"):
            assert subprocess.run([fixture, mode, directory, sheet]).returncode == 1
            assert {p.name: p.read_bytes() for p in directory.iterdir()} == before


def files(tool, directory, sheet, image):
    for args in ([], [directory], [directory, sheet], [directory, sheet, image, sheet]):
        usage = subprocess.run([tool, *args], capture_output=True)
        assert usage.returncode == 2 and b"Usage: DocumentSite" in usage.stderr
    absent = directory / "missing"
    for source in (absent, directory):  # Linux allows fopen on a directory but fread fails.
        assert subprocess.run([tool, directory, source, image]).returncode == 1
        assert not entries(directory)
    assert subprocess.run([tool, absent, sheet, image]).returncode == 1 and not absent.exists()
    for name in NAMES:
        (directory / name).write_bytes(name.encode())
    assert subprocess.run([tool, directory, absent, image]).returncode == 1
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}
    oversized = sheet.with_name("size-boundary")
    limit = 8 * 1024 * 1024
    with oversized.open("wb") as stream:
        stream.truncate(limit + 1)
    for source in (absent, directory, oversized):
        assert subprocess.run([tool, directory, sheet, source]).returncode == 1
        assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}

    # Both pages and the PNG fit. The fourth temporary (CSS) hits the limit at write or close.
    def limit_file_size():
        resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))

    large = sheet.with_name("large.css")
    large.write_bytes(SHEET * 512)
    assert subprocess.run([tool, directory, large, image], preexec_fn=limit_file_size).returncode == 1
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}
    # An admitted image that cannot be staged: the third temporary hits the limit and nothing is published.
    assert subprocess.run([tool, directory, sheet, large], preexec_fn=limit_file_size).returncode == 1
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}

    subprocess.run([tool, directory, oversized, image], check=True)
    assert (directory / "site.css").read_bytes() == oversized.read_bytes()
    with oversized.open("r+b") as stream:
        stream.truncate(limit)
    subprocess.run([tool, directory, sheet, oversized], check=True)
    assert (directory / "map.png").read_bytes() == oversized.read_bytes()

    css = directory / "site.css"
    css.write_bytes(SHEET)
    unrelated = directory / "keep"
    unrelated.write_bytes(b"keep")
    subprocess.run([tool, directory, css, image], check=True)
    assert css.read_bytes() == SHEET and unrelated.read_bytes() == b"keep"
    # A directory at the second destination forces rename to fail after index.html was published.
    (directory / "index.html").write_bytes(b"old page")
    (directory / "map.png").write_bytes(b"old image")
    (directory / "trail.html").unlink()
    (directory / "trail.html").mkdir()
    assert subprocess.run([tool, directory, css, image]).returncode == 1
    assert (directory / "index.html").read_bytes().startswith(b"<!doctype html>")
    assert (directory / "map.png").read_bytes() == b"old image"
    assert entries(directory) == NAMES | {"keep"} and (directory / "trail.html").is_dir()


def parse(tool, directory, sheet, image, example):
    try:
        import html5lib
    except ModuleNotFoundError as error:
        if error.name != "html5lib":
            raise
        print("[skip] html5lib not installed")
        return 77
    subprocess.run([tool, directory, sheet, image], check=True)
    for name, title, _ in PAGES:
        root = html5lib.parse((directory / name).read_text(encoding="utf-8"), namespaceHTMLElements=False)
        assert root.attrib["lang"] == "en" and root.findtext("head/title") == title
        main = root.find("body/main")
        assert main is not None
        golden = (example / "expected" / name).read_text(encoding="utf-8")
        fragment = golden.split("<main>", 1)[1].split("</main>", 1)[0]
        forest = html5lib.parseFragment(fragment, namespaceHTMLElements=False)
        assert (main.text or "") == (forest.text or "")
        assert [ElementTree.tostring(child) for child in main] == [ElementTree.tostring(child) for child in forest]
    return 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--write-png":
        Path(sys.argv[2]).write_bytes(map_png())
        return 0
    if not __debug__:  # -O removes every assert here, including the runs inside them
        sys.exit("DocumentSite.py checks with assert; run it without Python optimization")
    case, tool, fixture, example_path, stylesheet = sys.argv[1:]
    example = Path(example_path)
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp) / "out"
        directory.mkdir()
        sheet = Path(temp) / "fixture.css"
        sheet.write_bytes(SHEET)
        image = Path(temp) / "source.png"
        image.write_bytes(map_png())
        if case == "Output":
            output(tool, directory, sheet, image, example, Path(stylesheet))
        elif case == "Refusal":
            refusal(fixture, directory, sheet)
        elif case == "Files":
            files(tool, directory, sheet, image)
        elif case == "Parse":
            return parse(tool, directory, sheet, image, example)
        else:
            raise ValueError(case)
    return 0


if __name__ == "__main__":
    sys.exit(main())
