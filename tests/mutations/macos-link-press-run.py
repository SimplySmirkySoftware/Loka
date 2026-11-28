#!/usr/bin/env python3
"""Run the macOS link-press pins and their independent source mutations."""
import argparse
from collections import namedtuple
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RAIL = ROOT / "apple/macos/src"
LEAF = RAIL / "context/MacAttributedTextContext.mm"
REGISTRY = ROOT / "tests/MacOSContractTestRegistry.inc"
PREFIX = "testMacLinkPress"
Mutation = namedtuple("Mutation", "name old new test oracle")


def mutations():
    invalidate = "  this->invalidatePress();\n"
    clear = "  [(LokaAttributedTextView *)view installValue:nil snapshot:0 extent:0];\n"
    revoke = ("    if (next == loka::app::scene::NODE_FACT_RETIRED)\n"
              "      [(LokaAttributedTextView *)this->view_ setContext:0];\n")
    drop = "    [(LokaAttributedTextView *)this->view_ dropWindowPresentation];\n"
    transition = "    this->links_.invalidatePress();\n"
    window = "    NSWindow *window = [view window];\n    if (!window || [event window] != window || ![window isVisible])\n      return false;\n"
    nested = "    this->links_.invalidatePress();\n    return;\n  }\n  MacPressedLink "
    publish = "  this->node_->writeActivatedLink(candidate.run.first());"
    query = "  if (!this->queryPressCandidate(point, now) || p.invalidated()"
    # Seed only the mutant, so reading the untouched out parameter is deterministic and defined.
    ignore_query = "  now.first = now.last = 0;\n  this->queryPressCandidate(point, now);\n  if (p.invalidated()"
    absent = "!session.candidate_.take(payload)"
    result = [
        Mutation("take-down", "  MacPressedLink previous;\n  p.takeCandidate(previous);", "",
                 "Modifiers", "f.writes == 0"),
        Mutation("nested-down", nested + "previous;", nested.replace(transition, "") + "previous;",
                 "Nested", "f.writes == 0"),
        Mutation("nested-up", nested + "candidate;", nested.replace(transition, "") + "candidate;",
                 "Nested", "f.writes == 0"),
        Mutation("omit-clear", invalidate + clear, clear, "Rebuild", "f.writes == 0"),
        Mutation("omit-failed-clear", "      this->links_.clear(this->view_);", "", "FailedClear", absent),
        Mutation("late-clear", invalidate + clear, clear + invalidate, "Placement", absent),
        Mutation("omit-detach", transition + revoke, revoke, "Detach", "f.writes == 0"),
        Mutation("late-detach", transition + revoke + drop, revoke + drop + transition, "Placement", absent),
        Mutation("keep-context", revoke, "", "Lifetime", "r.entries == 1 && r.drops == 1 && ![f.view superview]"),
        Mutation("omit-window", window, "", "Window", "f.writes == 0"),
        Mutation("window-identity", window, window.replace("[event window] != window || ", ""),
                 "Window", "f.writes == 0"),
        Mutation("window-visible", window, window.replace(" || ![window isVisible]", ""),
                 "Visibility", "f.writes == 0"),
        Mutation("visible-rect", "    return ContainsHitPoint([view visibleRect], point);",
                 "    return true;", "Points", "f.writes == 0"),
        Mutation("query-result", query, ignore_query, "FalseQuery", "f.writes == 0"),
        Mutation("event-number", " || number != candidate.number)", " || (static_cast<void>(number), false))",
                 "Identity", "f.writes == 0"),
        Mutation("run-identity", "|| !(loka::app::internal::PressedRun(now) == candidate.run) ", "",
                 "Run", "f.writes == 0"),
        Mutation("publish-before-close", "  p.close();\n" + publish, publish + "\n  p.close();",
                 "Observer", "f.writes == 2"),
    ]
    for method, body, oracle in (
            ("mouseDown", "  MacAttributedTextContext *context", "[f.root downs] == 0"),
            ("mouseDragged", "  (void)event;", "[f.root drags] == 0"),
            ("mouseUp", "  MacAttributedTextContext *context", "[f.root ups] == 0")):
        start = "- (void){}:(NSEvent *)event\n{{\n".format(method)
        result.append(Mutation("forward-" + method, start + body,
                               start + "  [super {}:event];\n".format(method) + body, "Consumption", oracle))
    return result


def source_pins(source):
    adapters = re.findall(r"- \(void\)(mouseDown|mouseUp):[^\n]+\n\{([^}]+)\}", source)
    assert len(adapters) == 2 and all(body.count("[self context]") == 1 for _, body in adapters)
    assert len(re.findall(r"\[\w+\s+context\]", source)) == 2
    assert not re.search(r"^- \(.*?\)(?:rightMouseDown|otherMouseDown|menuForEvent):", source, re.M)
    assert len(re.findall(r"\bcontext_\b", source)) == 3  # declaration, private getter and setter
    forbidden = re.compile(r"\b(?:setNextResponder|addLocalMonitorForEventsMatchingMask|nextEventMatchingMask|sendEvent):"
                           r"|\[[^\n;]+\bmouseUp:")
    for path in RAIL.rglob("*"):
        if path.suffix in (".mm", ".m", ".hpp", ".cpp"):
            assert not forbidden.search(path.read_text()), path


def execute(command, log):
    with log.open("wb") as output:
        return subprocess.call(command, cwd=str(ROOT), stdout=output, stderr=subprocess.STDOUT)


def main():
    entries = mutations()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="only verify unique anchors and dispatch source pins")
    parser.add_argument("--rig", choices=("tahoe", "mavericks"))
    parser.add_argument("--build", default=os.environ.get("LOKA_MUTATION_BUILD"))
    parser.add_argument("--test", default=os.environ.get("LOKA_MUTATION_TEST"))
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m.name for m in entries])
    args = parser.parse_args()
    original = LEAF.read_bytes()
    source = original.decode("utf-8")
    source_pins(source)
    pins = re.findall(r"^LOKA_TEST\((testMacLinkPress\w+)\)$", REGISTRY.read_text(), re.M)
    for m in entries:
        old = m.old
        assert source.count(old) == 1, m.name
        assert PREFIX + m.test in pins
    if args.check:
        print("{} unique mutations, {} registered pins; dispatch source pins passed".format(len(entries), len(pins)))
        return
    if not args.rig or not args.build or not args.test:
        parser.error("supply --rig plus build and test commands (--build/--test or LOKA_MUTATION_BUILD/LOKA_MUTATION_TEST)")
    directory = ROOT / "build/macos-link-press-mutations" / args.rig
    directory.mkdir(parents=True, exist_ok=True)

    def build(phase):
        log = directory / (phase + "-build.log")
        if execute(shlex.split(args.build), log):
            raise RuntimeError("build failed: " + str(log))

    def test(phase, name, oracle=None):
        log = directory / (phase + "-" + name + ".log")
        status = execute(shlex.split(args.test) + [name], log)
        lines = log.read_bytes().decode("utf-8", "replace").splitlines()
        if oracle:
            if not status or not any(line.endswith("LOKA_VERIFY failed: " + oracle) for line in lines):
                raise RuntimeError("missing predicted kill: " + str(log))
        elif status or "[complete] " + name not in lines:
            raise RuntimeError("baseline/restore did not complete: " + str(log))
        if not oracle and name == PREFIX + "Repaint" and args.rig == "mavericks":
            if "[run] unlayered window repaint" not in lines:
                raise RuntimeError("missing Mavericks W witness: " + str(log))
        for line in lines:
            if line.startswith("[skip]"):
                print(line, flush=True)
                if not (name == PREFIX + "Repaint" and args.rig == "tahoe"):
                    raise RuntimeError("incomplete rig evidence: " + str(log))

    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    build("baseline")
    for name in pins:
        test("baseline", name)
    for m in entries:
        if args.mutation not in ("all", m.name):
            continue
        try:
            LEAF.write_bytes(source.replace(m.old, m.new).encode("utf-8"))
            build(m.name)
            test(m.name, PREFIX + m.test, m.oracle)
            print("{}: killed by {}{}".format(m.name, PREFIX, m.test), flush=True)
        finally:
            LEAF.write_bytes(original)
            assert LEAF.read_bytes() == original
            build(m.name + "-restored")
            test(m.name + "-restored", PREFIX + m.test)
    for name in pins:
        test("restored", name)


if __name__ == "__main__":
    main()
