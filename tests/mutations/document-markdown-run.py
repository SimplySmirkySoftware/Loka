#!/usr/bin/env python3
"""Run the document Markdown mutations independently. Build failures are not kills."""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MARKDOWN = ROOT / "common/app/doc/Markdown.hpp"
PROJECTION = MARKDOWN.parent / "detail/Projection.hpp"
MUTATIONS = [
    ("prune", "case scene::DOCUMENT_BLOCK_READ:\n            return true;",
     "case scene::DOCUMENT_BLOCK_READ: break;", "Walk"),
    ("parked", "scene::INestable *nest = node->asNestable();",
     "for (unsigned i = 0; scene::Node *branch = node->retainedLifecycleBranch(i); ++i)\n"
     "            if (!WalkDocumentBlocks(branch, reader)) return false;\n"
     "          scene::INestable *nest = node->asNestable();", "Walk"),
    ("escape-heading", "[]<&#~", "[]<&~", "PhysicalLines"),
    ("unsafe-stars", "if (stars)", "if (stars || marks)", "Inline"),
    ("ignore-flanking", "if (stars)", "if (stars || (!touching && !literal))", "Inline"),
    ("partial-body", "!CollectDocumentText(seg.text, this->bytes)",
     "(CollectDocumentText(seg.text, this->bytes), false)", "Failure"),
    ("partial-destination", "!CollectDocumentText(seg.link.text(), target)",
     "(CollectDocumentText(seg.link.text(), target), false)", "Failure"),
    ("edge-emphasis", "AppendRun(projected, plain, run.first, ink.first);", "AppendRun(projected, run.style, run.first, ink.first);", "Inline"),
    ("short-fence", "Backticks(body, 0, body.size(), 3)", 'std::string(3, \'`\')', "Blocks"),
    ("partial-commit", "if (!root || !detail::WalkDocumentBlocks(root, reader))\n          return false;",
     "if (!root || !detail::WalkDocumentBlocks(root, reader)) { out += reader.fragment; return false; }", "Failure"),
    ("split-run", "if (runs.empty() || !(runs.back().style == style))",
     "if (first < end)", "Inline"),
    ("split-link", "if (target != this->targets.c_str() + link)", "if (!target.empty())", "Inline"),
    ("physical-digits", "(digits && (v == '.' || v == ')'))", "(digits && v == ')')", "PhysicalLines"),
    ("image", "(v == '!' && beforeLink)", "(v == '?' && beforeLink)", "PhysicalLines"),
    ("detached", "if (node->lifecycleFact() != scene::NODE_FACT_ATTACHED)",
     "if (!node)", "Seats"),
    ("code-padding", "if (i == end && !onlySpaces)\n                return false;",
     "if (i == end) break;", "Failure"),
    ("bare-strip-space", "&& ExtraStripSpace(v))", "&& v == 0)", "Failure"),
    ("leading-signature", "else if (v == 0xfeff && pos == first)", "else if (v == 0xfeff && pos == end)", "PhysicalLines"),
]


def run(command, log):
    with log.open("wb") as output:
        return subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode


def mutation(entry):
    name, old, new, suffix = entry
    header = PROJECTION if name in {"prune", "parked", "partial-body", "partial-destination",
                                    "edge-emphasis", "split-run", "split-link", "detached"} else MARKDOWN
    original = header.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, "anchor mismatch: " + name
    logs = ROOT / "build/document-markdown-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    test = "testDocumentMarkdown" + suffix

    def check(phase, green):
        if run(["cmake", "--build", "build/Testing", "--target", "LokaContractTests", "-j", "4"],
               logs / f"{name}-{phase}-build.log"):
            raise RuntimeError(f"{name} {phase}: build failure")
        status = run(["build/Testing/LokaContractTests", test], logs / f"{name}-{phase}.log")
        if (status == 0) != green:
            raise RuntimeError(f"{name} {phase}: unexpected {test} exit {status}")

    check("baseline", True)
    try:
        header.write_bytes(source.replace(old, new).encode())
        check("mutant", False)
        print(f"killed {name}: {test}", flush=True)
    finally:
        header.write_bytes(original)
        check("restored", True)
        assert header.read_bytes() == original


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in MUTATIONS])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    for entry in MUTATIONS:
        if args.mutation in ("all", entry[0]):
            mutation(entry)
