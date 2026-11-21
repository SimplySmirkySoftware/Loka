#!/usr/bin/env python3
"""Build each private writer mutation, run its named CTest, and restore the green candidate."""
import argparse
from pathlib import Path
import signal

from document_mutations import run


HEADER = Path("tools/site/SiteWriter.hpp")
MUTATIONS = [
    ("title-escape", "Render", [
        ("doc::detail::AppendHtmlByte(staged, bytes[i], false);", "staged += bytes[i];")]),
    ("charset", "Output", [(r'<meta charset=\"utf-8\">\n', "")]),
    ("unstaged", "Render", [
        ('std::string staged = "<!doctype', 'std::string &staged = out;\n    staged += "<!doctype'),
        ("out += staged;", "")]),
    ("early-first-page", "Refusal", [
        ('  LOKA_VERIFY(!outputs.add("index.html", "duplicate"));',
         '  if (!document_site::writeSite(argv[2], argv[3], outputs))\n    return 1;\n'
         '  LOKA_VERIFY(!outputs.add("index.html", "duplicate"));')]),
    ("duplicate-name", "Refusal", [("if (page->first == name)", "if (false && page->first == name)")]),
    ("publish-as-staged", "Files", [
        ("return file->stage(bytes);", "return file->stage(bytes) && file->publish();"),
        ('files.stage(directory, "site.css", css) && files.publish()', 'files.stage(directory, "site.css", css)')]),
    ("any-name", "Refusal", [("if (!isPageName(name))", "if (name.empty())")]),
    ("any-suffix", "Refusal", [("name.compare(name.size() - suffix, suffix, Suffix) != 0", "false")]),
    ("ignore-stylesheet", "Output", [
        ('std::fopen(path, "rb")', '(static_cast<void>(path), std::fopen("../../tools/site/site.css", "rb"))')]),
]


def mutation(root, name, suffix, edits):
    path = root / ("tests/DocumentSiteFixture.cpp" if name == "early-first-page" else HEADER)
    original = path.read_bytes()
    source = original.decode()
    for old, new in edits:
        if source.count(old) != 1:
            raise ValueError(f"{name}: expected a unique anchor {old!r}")
        source = source.replace(old, new)
    logs = root / "build/document-site-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    test = "testDocumentSite" + suffix

    def check(phase, green):
        build_log = logs / f"{name}-{phase}-build.log"
        if run(root, ["cmake", "--build", "build/Testing", "--target", "DocumentSite", "DocumentSiteFixture",
                      "-j8"], build_log):
            raise RuntimeError(f"{name}: build failed; see {build_log}")
        test_log = logs / f"{name}-{phase}.log"
        status = run(root, ["ctest", "--test-dir", "build/Testing", "--output-on-failure", "--no-tests=error",
                            "-R", f"^{test}$"], test_log)
        if (status == 0) != green:
            raise RuntimeError(f"{name}: unexpected {test} exit {status}; see {test_log}")

    check("baseline", True)
    try:
        path.write_text(source)
        check("mutant", False)
    finally:
        path.write_bytes(original)
        check("restored", True)
    print(f"killed {name}: {test}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="repository root")
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in MUTATIONS])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    for name, suffix, edits in MUTATIONS:
        if args.mutation in ("all", name):
            mutation(args.root.resolve(), name, suffix, edits)


if __name__ == "__main__":
    main()
