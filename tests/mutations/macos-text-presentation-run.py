#!/usr/bin/env python3
"""Exercise one independent source mutation per rebuild on a positive macOS rail."""
import argparse
import os
from pathlib import Path
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LEAF = ROOT / "apple/macos/src/context/MacAttributedTextContext.mm"
CANDIDATE = "testMacTextPresentationCandidate"
OWNER = "testMacTextPresentationOwner"
CHECK = "testMacTextPresentationCheck"
PIXELS = "testMacTextPresentationPixels"
PREDICATE = "testMacTextPresentationPredicate"


def mutations():
    # Each named test must kill the mutation except where explicitly marked.
    def kill(name, old, new, test):
        return name, old, new, [(test, False)]

    # Keep clear and successful installs correct, but strand the prior record on a failed replacement.
    install_prefix = ("  presentation_->dropInstalled();\n  NSTextStorage *storage = [self lokaTextStorage];\n"
                      "  if (!value)\n  {\n    [storage deleteCharactersInRange:NSMakeRange(0, [storage length])];\n"
                      "    return YES;\n  }\n  [storage setAttributedString:value];\n#ifdef TEST_BUILD\n"
                      "  if (loka::testing::refuseMacTextPresentationStep())\n    return NO;\n#endif")
    late_drop = install_prefix.replace("  presentation_->dropInstalled();\n", "").replace(
        "    [storage delete", "    presentation_->dropInstalled();\n    [storage delete")
    return [
        kill("late-install-invalidation", install_prefix, late_drop + "\n  presentation_->dropInstalled();",
             "testMacTextPresentationFailedReplacement"),
        kill("runtime-always-false", "return [NSView instancesRespondToSelector:@selector(wantsUpdateLayer)];",
             "return false;", "testMacTextLayerCapability"),
        kill("actions-enabled", "  [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];",
             "", "testMacTextPresentationLayerActions"),
        kill("install-door", "  presentation_->dropInstalled();", "", OWNER),
        kill("width-door", "presentation_->dropImage();\n    [[self lokaTextContainer]",
             "[[self lokaTextContainer]", OWNER),
        kill("window-move-door", "presentation_->dropImage();\n  }\n  [super viewWillMove",
             "\n  }\n  [super viewWillMove", OWNER),
        kill("retirement-door", "    this->clearProjection();\n    [(NSView *)this->view_ remove",
             "    this->projection_.clear();\n    [(NSView *)this->view_ remove", OWNER),
        kill("native-width", "    [[self lokaTextContainer] setContainerSize:NSMakeSize(width, kMacTextContainerHeight)];",
             "", OWNER),
        kill("commit-installed", " || !presentation_->installed()", "", OWNER),
        kill("commit-layer", "if (!layer || !window)", "if (!window)", CHECK),
        kill("commit-window", "if (!layer || !window)", "if (!layer)", OWNER),
        kill("translated-origin-layer-display", " && NSEqualPoints(bounds.origin, NSZeroPoint)", "",
             "testMacTextPresentationTranslatedOrigin"),
        kill("commit-origin", "!NSEqualPoints(bounds.origin, NSZeroPoint) || ", "", CHECK),
        kill("read-window", "  if (!window)\n    return NO;\n", "", OWNER),
        kill("check-layer", " || !now.layer ||", " ||", CHECK),
        kill("check-contents", " || now.contents != (id)this->image_", "", CHECK),
        kill("check-scale", " || now.scale != this->scale_", "", CHECK),
        kill("check-window-scale", "now.windowScale != this->scale_ || ", "", CHECK),
        kill("check-size", " || !NSEqualSizes(now.bounds.size, this->size_)", "", CHECK),
        kill("check-origin", " || !NSEqualPoints(now.bounds.origin, NSZeroPoint)", "", CHECK),
        kill("check-contents-rect", " || !CGRectEqualToRect(now.contentsRect, CGRectMake(0, 0, 1, 1))", "", CHECK),
        kill("check-gravity", "![now.gravity isEqualToString:this->gravity_] || ", "", CHECK),
        kill("check-transform", " || !now.identity", "", CHECK),
        kill("recompute-gravity", "isEqualToString:this->gravity_",
             "isEqualToString:([now.layer contentsAreFlipped] ? kCAGravityBottomLeft : kCAGravityTopLeft)", CHECK),
        kill("pixel-axis", "CGContextTranslateCTM(graphics, 0, height)",
             "CGContextTranslateCTM(graphics, 0, bounds.size.height * scale)", PIXELS),
        kill("pixel-scale", "CGContextScaleCTM(graphics, scale, -scale)", "CGContextScaleCTM(graphics, 2 * scale, -2 * scale)", PIXELS),
        kill("layer-placement", "objc_msgSend)(view, @selector(setLayerContentsPlacement:), 11)",
             "objc_msgSend)(view, @selector(setLayerContentsPlacement:), 0)", PIXELS),
        kill("image-placement", "? kCAGravityBottomLeft : kCAGravityTopLeft",
             "? kCAGravityTopLeft : kCAGravityBottomLeft", PIXELS),
        kill("ungated-layer", "if (MacTextLayerCapable())\n    {\n      [self setWantsLayer:YES];",
             "{\n      [self setWantsLayer:YES];", "testMacTextLayerCapability"),
        kill("failed-seal-stale-record", "if (!image)\n  {\n    presentation_->dropImage();",
             "if (!image)\n  {", OWNER),
        kill("failed-seal-clears-contents", "if (!image)\n  {\n    presentation_->dropImage();",
             "if (!image)\n  {\n    [layer setContents:nil];\n    presentation_->dropImage();", OWNER),
        kill("budget-cap", "height <= 16u", "height < 16u", "testMacTextPresentationBudget"),
        kill("dimension-cap", "w <= 4096", "w < 4096", "testMacTextPresentationBudget"),
        # Exclusive install/reset doors keep these equal in a reachable candidate.
        # A synthetic predicate fixture can supply a mismatched pair independently.
        ("fact-snapshot", "fact.value.hasSameSegmentValues(snapshot) && ", "", [(CANDIDATE, True), (PREDICATE, False)]),
        ("width-equality", "hit.containerWidth == fact.width && ", "", [(CANDIDATE, True), (PREDICATE, False)]),
        kill("snapshot-current", " && snapshot.hasSameSegmentValues(current)", "", CANDIDATE),
        # Initialize the dummy run on the no-link leg, so this mutant tests acceptance without reading indeterminate data.
        kill("link-required", "current.queryLinkRun(hit.source.first(), run)",
             "(current.queryLinkRun(hit.source.first(), run) || (run.first = run.last = hit.source.first(), true))",
             CANDIDATE),
        kill("cluster-coverage", " && run.last >= hit.source.last()", "", CANDIDATE),
        kill("snapshot-publication", "    this->snapshot_ = value;", "", CANDIDATE),
        # A cleared projection gates the candidate before snapshot comparison. Read the released handle separately.
        ("snapshot-reset", "  this->snapshot_ = loka::app::AttributedString();", "",
         [(CANDIDATE, True), ("testMacTextPresentationSnapshotLifetime", False)]),
    ]


def exercise(args, entry, original):
    name, old, new, tests = entry
    source = original.decode("utf-8")
    assert source.count(old) == 1, name
    directory = ROOT / "build/macos-text-presentation-mutations" / args.rig / name
    directory.mkdir(parents=True, exist_ok=True)

    def execute(command, log):
        with (directory / log).open("wb") as output:
            return subprocess.call(command, cwd=str(ROOT), stdout=output, stderr=subprocess.STDOUT)

    def build(phase):
        if execute(shlex.split(args.build), phase + "-build.log"):
            raise RuntimeError("build failed: " + str(directory / (phase + "-build.log")))

    def run(test, phase):
        log = phase + "-" + test + ".log"
        status = execute(shlex.split(args.test) + [test], log)
        data = (directory / log).read_bytes()
        if (b"[skip] no window for the checked preparation" in data or
                b"[skip] this artifact/runtime has no L" in data):
            raise RuntimeError("missing positive fixture: " + str(directory / log))
        if status and (phase != "mutant" or b"LOKA_VERIFY failed:" not in data):
            raise RuntimeError("test/launcher failed: " + str(directory / log))
        return status == 0

    build("baseline")
    for test, _ in tests:
        run(test, "baseline")
    matched = True
    try:
        LEAF.write_bytes(source.replace(old, new).encode("utf-8"))
        build("mutant")
        for test, survival in tests:
            survived = run(test, "mutant")
            print("{} {} {}: {} (expected {})".format(args.rig, name, test,
                  "survived" if survived else "killed", "survival" if survival else "kill"), flush=True)
            matched = (survived == survival) and matched
    finally:
        LEAF.write_bytes(original)
        assert LEAF.read_bytes() == original
        build("restored")
        for test, _ in tests:
            run(test, "restored")
    return matched


def main():
    entries = mutations()
    original = LEAF.read_bytes()
    source = original.decode("utf-8")
    for name, old, _, _ in entries:
        assert source.count(old) == 1, name
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rig", required=True)
    parser.add_argument("--build", default=os.environ.get("LOKA_MUTATION_BUILD"))
    parser.add_argument("--test", default=os.environ.get("LOKA_MUTATION_TEST"))
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [entry[0] for entry in entries])
    args = parser.parse_args()
    if not args.build or not args.test:
        parser.error("supply --build and --test, or LOKA_MUTATION_BUILD and LOKA_MUTATION_TEST")

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    matched = True
    for entry in entries:
        if args.mutation in ("all", entry[0]):
            matched = exercise(args, entry, original) and matched
    return 0 if matched else 1


if __name__ == "__main__":
    raise SystemExit(main())
