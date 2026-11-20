#!/usr/bin/env python3
"""Exercise the two-page shipping tool and its refusal fixture in existing directories."""
from html.parser import HTMLParser
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
from xml.etree import ElementTree


NAMES = {"index.html", "trail.html", "site.css"}
PAGES = [("index.html", "The map room", "trail.html"),
         ("trail.html", "Above the tide line", "index.html")]
SHEET = b"body {\r\n  color: navy;\r\n}\r\n"


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.anchors = []
        self.sheets = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "a":
            self.anchors.append(attributes.get("href"))
        if tag == "link":
            self.sheets.append((attributes.get("rel"), attributes.get("href")))


def entries(directory):
    return {p.name for p in directory.iterdir()}


def output(tool, directory, sheet, example):
    subprocess.run([tool, directory, sheet], check=True)
    assert entries(directory) == NAMES
    assert (directory / "site.css").read_bytes() == SHEET
    for name, _, peer in PAGES:
        page = (directory / name).read_bytes()
        assert page.startswith(b'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                               b'<meta name="viewport" content="width=device-width, initial-scale=1">\n')
        links = Links()
        links.feed(page.decode())
        links.close()
        assert links.anchors == [peer] and links.sheets == [("stylesheet", "site.css")]
        assert page == (example / "expected" / name).read_bytes(), name

    modes = lambda: {(directory / name).stat().st_mode & 0o777 for name in NAMES}
    assert modes() == {0o644}, "new files are readable by a server running as another user"
    subprocess.run([tool, directory, example / "site.css"], check=True)
    assert (directory / "site.css").read_bytes() == (example / "site.css").read_bytes()
    assert modes() == {0o644}, "replaced files keep that mode"


def refusal(fixture, directory, sheet):
    good = directory.parent / "control"
    good.mkdir()
    subprocess.run([fixture, "valid", good, sheet], check=True)
    assert entries(good) == NAMES
    for seeded in (False, True):
        before = {name: name.encode() for name in NAMES} if seeded else {}
        for name, data in before.items():
            (directory / name).write_bytes(data)
        assert subprocess.run([fixture, "refuse", directory, sheet]).returncode == 1
        assert {p.name: p.read_bytes() for p in directory.iterdir()} == before


def files(tool, directory, sheet):
    for args in ([], [directory], [directory, sheet, sheet]):
        usage = subprocess.run([tool, *args], capture_output=True)
        assert usage.returncode == 2 and b"Usage: DocumentSite" in usage.stderr
    absent = directory / "missing"
    for source in (absent, directory):  # Linux allows fopen on a directory but fread fails.
        assert subprocess.run([tool, directory, source]).returncode == 1
        assert not entries(directory)
    assert subprocess.run([tool, absent, sheet]).returncode == 1 and not absent.exists()
    for name in NAMES:
        (directory / name).write_bytes(name.encode())
    assert subprocess.run([tool, directory, absent]).returncode == 1
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}
    # Both HTML files fit. The third temporary hits the file size limit at write or close.
    def limit_file_size():
        resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))

    large = sheet.with_name("large.css")
    large.write_bytes(SHEET * 512)
    assert subprocess.run([tool, directory, large], preexec_fn=limit_file_size).returncode == 1
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == {n: n.encode() for n in NAMES}

    css = directory / "site.css"
    css.write_bytes(SHEET)
    unrelated = directory / "keep"
    unrelated.write_bytes(b"keep")
    subprocess.run([tool, directory, css], check=True)
    assert css.read_bytes() == SHEET and unrelated.read_bytes() == b"keep"
    # A directory at the second destination forces rename to fail after index.html was published.
    (directory / "trail.html").unlink()
    (directory / "trail.html").mkdir()
    assert subprocess.run([tool, directory, css]).returncode == 1
    assert entries(directory) == NAMES | {"keep"} and (directory / "trail.html").is_dir()


def parse(tool, directory, sheet, example):
    try:
        import html5lib
    except ModuleNotFoundError as error:
        if error.name != "html5lib":
            raise
        print("[skip] html5lib not installed")
        return 77
    subprocess.run([tool, directory, sheet], check=True)
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
    if not __debug__:  # -O removes every assert here, including the runs inside them
        sys.exit("DocumentSite.py checks with assert; run it without Python optimization")
    case, tool, fixture, example_path = sys.argv[1:]
    example = Path(example_path)
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp) / "out"
        directory.mkdir()
        sheet = Path(temp) / "fixture.css"
        sheet.write_bytes(SHEET)
        if case == "Output":
            output(tool, directory, sheet, example)
        elif case == "Refusal":
            refusal(fixture, directory, sheet)
        elif case == "Files":
            files(tool, directory, sheet)
        elif case == "Parse":
            return parse(tool, directory, sheet, example)
        else:
            raise ValueError(case)
    return 0


if __name__ == "__main__":
    sys.exit(main())
