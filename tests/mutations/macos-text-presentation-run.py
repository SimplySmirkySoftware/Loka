#!/usr/bin/env python3
"""Exercise the independent L and W presentation mutations on the macOS witness rigs."""
import argparse
import os
import platform
import re
from collections import namedtuple
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


def layer_mutations():
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
    storage_write = "  [storage setAttributedString:value];"
    return [
        kill("early-storage-drop", install_prefix,
             late_drop.replace(storage_write, storage_write + "\n  presentation_->dropInstalled();"),
             "testMacTextPresentationFailedReplacement"),
        kill("late-install-invalidation", install_prefix, late_drop + "\n  presentation_->dropInstalled();",
             "testMacTextPresentationFailedReplacement"),
        kill("runtime-always-false", "return [NSView instancesRespondToSelector:@selector(wantsUpdateLayer)];",
             "return false;", "testMacTextLayerCapability"),
        kill("actions-enabled", "  [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];",
             "", "testMacTextPresentationLayerActions"),
        kill("install-door", "  presentation_->dropInstalled();", "", OWNER),
        kill("width-door", "presentation_->dropPresented();\n    [[self lokaTextContainer]",
             "[[self lokaTextContainer]", OWNER),
        kill("window-move-door", "presentation_->dropPresented();\n  }\n  [super viewWillMove",
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
        kill("check-contents", " || now.contents != (id)this->record_.image", "", CHECK),
        kill("check-scale", "|| now.scale != this->record_.scale ", "", CHECK),
        kill("check-window-scale", " || now.windowScale != this->record_.scale", "", CHECK),
        kill("check-size", "|| !NSEqualSizes(now.bounds.size, this->record_.size) ", "", CHECK),
        kill("check-origin", " || !NSEqualPoints(now.bounds.origin, NSZeroPoint)", "", CHECK),
        kill("check-contents-rect", " || !CGRectEqualToRect(now.contentsRect, CGRectMake(0, 0, 1, 1))", "", CHECK),
        kill("check-gravity", "![now.gravity isEqualToString:this->record_.gravity] || ", "", CHECK),
        kill("check-transform", " || !now.identity", "", CHECK),
        kill("recompute-gravity", "isEqualToString:this->record_.gravity",
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
        kill("failed-seal-stale-record", "if (!image)\n  {\n    presentation_->dropPresented();",
             "if (!image)\n  {", OWNER),
        kill("failed-seal-clears-contents", "if (!image)\n  {\n    presentation_->dropPresented();",
             "if (!image)\n  {\n    [layer setContents:nil];\n    presentation_->dropPresented();", OWNER),
        kill("budget-cap", "height <= 16u", "height < 16u", "testMacTextPresentationBudget"),
        kill("height-cap", "h <= 4096", "h < 4096", "testMacTextPresentationBudget"),
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


CONTROLLER = ROOT / "apple/macos/src/MacScenePlatformController.mm"
Mutation = namedtuple("Mutation", "name path old new tests scope")
W = "testMacTextWindow"
TAHOE_SKIP = "[skip] Tahoe layers every view; W positives need an unlayered window"
HIT = "AttributedAccess::prepareTextHit(AttributedContext(node), p, hit, fact) == expected"
RUN = "AttributedAccess::queryPressCandidate(context, point, run) == (target != 0)"
NONE = "AttributedAccess::presentedPath(f.view) == AttributedAccess::NONE"
SIZE = "![f.view queryPresentedFact:&fact]"
TRANSFORM = "paintTransform.ty == [raw pixelsHigh] && paintTransform.a == scale && paintTransform.d == -scale"
PARENT = 'false && "protected child action returned"'
DEATH = {W + "Interval": [("installDuringW", "occupied_"), ("dropDuringW", "occupied_")],
         "testMacPassRepaintNoNestedLayout": [("nestedResize", "MacPassRepaint")]}

# An unrelated verification failure, even in the correct test, is a rig/setup failure.
L_ORACLE = {
    "early-storage-drop": "probe.observed && !probe.sawInstalled && !probe.sawImage",
    "late-install-invalidation": "![layout.view lokaHasInstalledText]",
    "runtime-always-false": "MacTextLayerCapable() == (MacTextLayerCompiledIn() && [NSView instancesRespondToSelector:@selector(wantsUpdateLayer)])",
    "actions-enabled": "probe.actionsEnabled == 0",
    "install-door": "![view lokaHasInstalledText] && ![view lokaHasPresentationImage]",
    "width-door": "![view lokaHasPresentationImage] && [view lokaHasInstalledText]",
    "window-move-door": "![view window] && ![view lokaHasPresentationImage]",
    "retirement-door": "![view window] && ![view lokaHasInstalledText] && ![view lokaHasPresentationImage]",
    "native-width": ("NSEqualSizes([AttributedAccess::container(view) containerSize], NSMakeSize(width, 1.0e7))",
                     "[view queryPresentedFact:&fact]"),
    "commit-installed": 'false && "unexpected layer read"',
    "commit-layer": 'false && "unexpected geometry read"',
    "commit-window": 'false && "unexpected geometry read"',
    "translated-origin-layer-display": ("![view wantsUpdateLayer]", "submitted.foregrounds && submitted.foreground.length"),
    "commit-origin": "![view lokaHasPresentationImage]",
    "read-window": 'false && "unexpected layer read"',
    "check-layer": "![view lokaFactWithAbsentLayer]",
    "check-contents": HIT, "check-scale": HIT, "check-window-scale": HIT,
    "check-size": HIT, "check-origin": HIT, "check-contents-rect": HIT, "check-gravity": HIT,
    "check-transform": HIT, "recompute-gravity": HIT,
    "pixel-axis": TRANSFORM, "pixel-scale": TRANSFORM,
    "layer-placement": "((NSInteger(*)(id, SEL))objc_msgSend)(view, @selector(layerContentsPlacement)) == 11",
    "image-placement": "[[layer contentsGravity] isEqualToString:flipped ? kCAGravityBottomLeft : kCAGravityTopLeft]",
    "ungated-layer": "!wants",
    "failed-seal-stale-record": "![view lokaHasPresentationImage] && ![view queryPresentedFact:&fact]",
    "failed-seal-clears-contents": "[[view layer] contents] == old",
    "budget-cap": "MacTestTextBudget(NSMakeSize(2048, 2048), 1)",
    "height-cap": "MacTestTextBudget(NSMakeSize(1, 4096), 1)",
    "dimension-cap": "MacTestTextBudget(NSMakeSize(4096, 1), 1)",
    "fact-snapshot": "!MacTestEligibleRun(hit, fact, value, value)",
    "width-equality": "!MacTestEligibleRun(hit, fact, value, value)",
    "snapshot-current": RUN, "link-required": RUN, "cluster-coverage": RUN, "snapshot-publication": RUN,
    "snapshot-reset": "!AttributedAccess::complete(context) && AttributedAccess::snapshot(context).segmentCount() == 0",
}


def uncapped_cover():
    # Use growing scratch in this mutant so removing the policy cap is distinct from
    # the single unchecked write exercised under ASan.
    source = LEAF.read_text()
    start = source.index("  bool MacRectsCover(")
    old = source[start:source.index("  /** Native evidence", start)]
    new = old
    storage = new[new.index("    enum"):new.index("    for (NSInteger r")]
    edits = [
        (storage, "    std::vector<NSRect> remaining(1, target), next;\n"),
        ("r < count && pieces", "r < count && !remaining.empty()"),
        ("unsigned used = 0;", "next.clear();"),
        ("unsigned i = 0; i < pieces", "size_t i = 0; i < remaining.size()"),
        ("if (used == capacity)\n            return false;\n          next[used++] = p;", "next.push_back(p);"),
        ("if (used == capacity)\n              return false;\n            next[used++] = strips[j];", "next.push_back(strips[j]);"),
        ("pieces = used;\n      NSRect *swap = remaining;\n      remaining = next;\n      next = swap;", "remaining.swap(next);"),
        ("return pieces == 0;", "return remaining.empty();"),
    ]
    for anchor, replacement in edits:
        assert new.count(anchor) == 1, anchor
        new = new.replace(anchor, replacement)
    return old, new


def mutations():
    result = [Mutation(name, LEAF, old, new,
                       [(test, None if survive else L_ORACLE[name]) for test, survive in tests], "both")
              for name, old, new, tests in layer_mutations()]

    def add(name, old, new, pin, oracle, scope="W", path=LEAF):
        test = W + pin if pin != "NoNestedLayout" else "testMacPassRepaintNoNestedLayout"
        result.append(Mutation(name, path, old, new, [(test, oracle)], scope))

    add("skip-context", "return current && current == [window graphicsContext];", "return current != nil;",
        "Context", NONE)
    add("dirty-cover", "MacRectsCover(rects, count, NSIntersectionRect([self visibleRect], bounds))",
        "MacRectsCover(&dirty, 1, NSIntersectionRect([self visibleRect], bounds))", "Damage", "obs.count == 2 && " + NONE)
    capped, uncapped = uncapped_cover()
    add("drop-cap", capped, uncapped, "Cover", "!MacTestRectsCover(cuts, 17, target)", "both")
    add("untouched-append", "if (used == capacity)\n            return false;\n          next[used++] = p;",
        "next[used++] = p;", "Cover", "ASan:MacRectsCover", "asan")
    add("empty-cover", "if (NSIsEmptyRect(target))\n      return false;",
        "if (NSIsEmptyRect(target))\n      return true;", "Cover", "!MacTestRectsCover(&target, 1, NSZeroRect)", "both")
    add("ignore-inherited-layer", "    if (MacLateBoundLayer(self))\n      return NO;\n", "", "LayerTransition", SIZE)
    add("layered-to-W", "MacTextLayerCapable() || MacLateBoundLayer(self)", "MacTextLayerCapable()",
        "Routing", 'false && "entered W evidence gathering"')
    add("capable-to-W", "!window || MacTextLayerCapable() || ", "!window || ",
        "Routing", 'false && "entered W evidence gathering"')
    add("omit-window-glyphs", "[self drawTextInRect:band];", "(void)band;", "Pixels", "FindGlyphInk(f.layout, 0, oldInk)")
    add("omit-layout-drop", "  [view dropWindowPresentation];\n", "", "LayoutVisit", NONE)
    add("omit-pass-repaint", "  this->repaintAfterPlacement();", "", "PassRepaint",
        "trace.entries + 1 == MacTestRepaintEntries()", path=CONTROLLER)
    add("omit-root-mark", "  [root setNeedsDisplay:YES];", "", "PassRepaint", 'trace.sends == "MD"', path=CONTROLLER)
    add("repaint-L", "  if (MacTextLayerCapable())\n    return;\n", "", "PassScope", "trace.sends.empty()", "both", CONTROLLER)
    add("omit-hide-drop", "  if (next != loka::app::scene::NODE_FACT_ATTACHED)\n"
        "    [(LokaAttributedTextView *)this->view_ dropWindowPresentation];\n", "", "Drops", NONE + " && " + SIZE)
    add("clear-L-on-placement", "      if (this->record_.path == MacPresentedRecord::WINDOW)\n        this->dropPresented();",
        "      this->dropPresented();", "LayerRecordKept", "[view queryPresentedFact:&fact] && [[view layer] contents] == image", "both")
    add("keep-partial-after-resize", "else if (!presentation_->matchesWindowSize(bounds.size))\n    presentation_->dropPresented();",
        "", "Size", NONE)
    add("ignore-window-size", "if (!window || !this->matchesWindowSize(size))",
        "(void)size;\n      if (!window || this->record_.path != MacPresentedRecord::WINDOW)", "Size", SIZE)
    add("drop-same-size-partial", "else if (!presentation_->matchesWindowSize(bounds.size))", "else", "Damage",
        "!obs.covered && AttributedAccess::presentedPath(f.view) == AttributedAccess::WINDOW")
    add("uninstalled-window", "!presentation_->installed() || ![self drawUsesWindowContext:window]",
        "![self drawUsesWindowContext:window]", "Uninstalled", NONE)
    retain = "      if (next.image)\n        CGImageRetain(next.image);"
    release = "      if (this->record_.image)\n        CGImageRelease(this->record_.image);"
    add("release-first", retain + "\n" + release, release + "\n" + retain, "Record", 'std::strcmp(order, "+-") == 0', "both")
    add("retain-leak-on-window", release, release.replace("if (this->record_.image)", "if (this->record_.image && next.path != MacPresentedRecord::WINDOW)"),
        "Record", "delta == 1", "both")
    add("omit-window-interval", "  MacTextPresentation::Interval drawInterval(*presentation_);", "",
        "Interval", "death:installDuringW", "audit-W")
    add("omit-drop-idle", "    presentation_->assertIdle();\n    presentation_->dropWindowPresented();",
        "    presentation_->dropWindowPresented();", "Interval", "death:dropDuringW", "audit-W")
    add("read-layer-first", "  if (!window)\n    return NO;\n  if (!MacTextLayerCapable())",
        "  if (MacLateBoundLayer(self) && !window) return NO;\n  if (!window) return NO;\n  if (!MacTextLayerCapable())",
        "Detached", 'false && "unexpected layer read"')
    focus = "    if (rebuildContexts)\n    {\n      restoreFocusedEditField();\n    }\n  }"
    add("premature-repaint", focus + "\n  this->repaintAfterPlacement();", "  this->repaintAfterPlacement();\n" + focus,
        "PassRepaint", "t.entries == MacTestRepaintEntries() && !MacTestInsideRepaint()", path=CONTROLLER)
    add("omit-nesting-audit", '  assert(!this->passRepainting_ && "MacPassRepaint");', "",
        "NoNestedLayout", "death:nested layout reached", "audit-W", CONTROLLER)
    add("clear-on-foreign-draw", "if (!presentation_->installed() || ![self drawUsesWindowContext:window])\n    return;",
        "if (!presentation_->installed() || ![self drawUsesWindowContext:window]) { presentation_->dropPresented(); return; }",
        "Context", "AttributedAccess::presentedPath(f.view) == AttributedAccess::WINDOW")
    return result


def check_completion(test, data, tahoe, no_audit):
    if test in DEATH and no_audit:
        if not re.search(r"^\[skip\] .*requires? lifecycle audit and assertions$", data, re.M):
            raise ValueError("missing audit skip")
        return
    w_fixture = ((test.startswith(W) and test[len(W):] not in ("Cover", "Record", "PassScope", "LayerRecordKept"))
                 or test == "testMacPassRepaintNoNestedLayout")
    marker = "[complete] " + test
    if tahoe and w_fixture:
        if TAHOE_SKIP not in data.splitlines():
            raise ValueError("missing Tahoe skip")
        marker += " N"
    elif test in DEATH:
        for action, diagnostic in DEATH[test]:
            if not any(line.startswith("[child " + action + "]") and diagnostic in line for line in data.splitlines()):
                raise ValueError("missing child diagnostic: " + action)
    if marker not in data.splitlines():
        raise ValueError("missing " + marker)


def killed(oracle, data):
    if isinstance(oracle, tuple):
        # Hosts stop at different assertions of the same pin; each alternative is that pin's own text.
        return any(killed(alternative, data) for alternative in oracle)
    if oracle.startswith("death:"):
        action = oracle[len("death:"):]
        if not all(line in data.splitlines() for line in ("[returned] " + action, "[parent failure] " + action + " returned")):
            return False
        oracle = PARENT
    if oracle == "ASan:MacRectsCover":
        return ("AddressSanitizer: stack-buffer-overflow" in data
                and re.search(r"^\s*#\d+ .*MacRectsCover", data, re.M))
    return any(line.endswith("LOKA_VERIFY failed: " + oracle) for line in data.splitlines())


def exercise(args, entry, first=False, last=False):
    original = entry.path.read_bytes()
    source = original.decode("utf-8")
    old = entry.old
    assert source.count(old) == 1, entry.name
    directory = ROOT / "build/macos-text-presentation-mutations" / args.rig / entry.name
    directory.mkdir(parents=True, exist_ok=True)
    tahoe = args.host == "tahoe"
    if ((entry.scope == "asan" and not args.asan) or (entry.scope == "audit-W" and args.no_audit)):
        print("not applicable on this rig: " + entry.name, flush=True)
        return
    def execute(command, log):
        with log.open("wb") as output:
            return subprocess.call(command, cwd=str(ROOT), stdout=output, stderr=subprocess.STDOUT)
    def build(phase):
        log = directory / (phase + "-build.log")
        if execute(shlex.split(args.build), log):
            raise RuntimeError("build failed: " + str(log))
    def run(test, phase, oracle=None):
        log = directory / (phase + "-" + test + ".log")
        status = execute(shlex.split(args.test) + [test], log)
        data = log.read_bytes().decode("utf-8", "replace")
        if oracle:
            if not status or not killed(oracle, data):
                raise RuntimeError("missing predicted kill or unrelated failure: " + str(log))
        else:
            if status:
                raise RuntimeError("test/launcher failed: " + str(log))
            try:
                check_completion(test, data, tahoe, args.no_audit)
            except ValueError as error:
                raise RuntimeError("{}: {}".format(log, error))

    # The complete sweep also checks pins which are not the first killer of any mutant.
    registry = (ROOT / "tests/MacOSContractTestRegistry.inc").read_text()
    pins = re.findall(r"^LOKA_TEST\((testMac(?:TextWindow\w+|PassRepaintNoNestedLayout))\)$", registry, re.M)
    selected = [test for test, _ in entry.tests]
    build("baseline")
    for test in sorted(set(selected + (pins if first else []))):
        run(test, "baseline")
    try:
        entry.path.write_bytes(source.replace(old, entry.new).encode("utf-8"))
        entry.path.touch()
        build("mutant")
        for test, oracle in entry.tests:
            unavailable = tahoe and entry.scope in ("W", "audit-W")
            run(test, "mutant", None if unavailable else oracle)
            result = "not applicable on this rig" if unavailable else ("killed" if oracle else "expected survival")
            print("{} {} {}: {}".format(args.rig, entry.name, test, result), flush=True)
    finally:
        entry.path.write_bytes(original)
        entry.path.touch()
        assert entry.path.read_bytes() == original
        build("restored")
        for test in sorted(set(selected + (pins if last else []))):
            run(test, "restored")


def main():
    entries = mutations()
    for entry in entries:
        source = entry.path.read_text()
        old = entry.old
        assert source.count(old) == 1, entry.name
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rig", required=True)
    parser.add_argument("--host", choices=["tahoe", "mavericks"])
    parser.add_argument("--asan", action="store_true", help="run the untouched append with an ASan build")
    parser.add_argument("--no-audit", action="store_true", help="binary omits audit or assertions")
    parser.add_argument("--build", default=os.environ.get("LOKA_MUTATION_BUILD"))
    parser.add_argument("--test", default=os.environ.get("LOKA_MUTATION_TEST"))
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [entry.name for entry in entries])
    args = parser.parse_args()
    if not args.host:
        args.host = {"13": "mavericks", "25": "tahoe"}.get(platform.release().split(".")[0])
    if not args.host or not args.build or not args.test:
        parser.error("supply the host, build and test commands")

    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    selected = [entry for entry in entries if args.mutation in ("all", entry.name)]
    for index, entry in enumerate(selected):
        exercise(args, entry, index == 0, index + 1 == len(selected))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
