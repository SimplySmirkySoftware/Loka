#!/usr/bin/env python3
"""Run independent TextKit hit mutations. Supply argv strings for the rig's
build and test commands. The runner appends the registered name to the latter.
"""
import argparse
from pathlib import Path
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CONTEXT = ROOT / "apple/macos/src/context/MacAttributedTextContext.mm"


def mutations(source):
    def block(start, end):
        assert source.count(start) == 1, start
        return source[source.index(start):source.index(end, source.index(start))]

    closure = block("  const NSRange roundTrip", "  if (!chars.length)")
    hidden = block("  NSUInteger visible =", "  NSParagraphStyle *style")
    truncation = block("  NSParagraphStyle *style = [[self", "  const NSRect fragment")
    geometry = block("  const NSRect fragment =", "  *out = chars;")
    unsupported = block("    if (![manager respondsToSelector:@selector(truncatedGlyph", "\n  }")
    intersection = block("    if (truncated.location", "\n  }")
    presence = "if (!view || !this->projection_.value())\n    return MAC_HIT_REFUSED;"
    provenance = "if (!this->projection_.queryProvenance(chars.location, NSMaxRange(chars), source))\n    return MAC_HIT_REFUSED;"
    # Last column lists the expected survivors. The line witness is decided by
    # the baseline test's explicit skip message independently on each host.
    both = ("tahoe", "mavericks")
    return [
        ("max-x-inclusive", "p.x < NSMaxX(rect)", "p.x <= NSMaxX(rect)", "Edges", ()),
        ("max-y-inclusive", "p.y < NSMaxY(rect)", "p.y <= NSMaxY(rect)", "Edges", ()),
        ("min-x-exclusive", "p.x >= NSMinX(rect)", "p.x > NSMinX(rect)", "Edges", both),
        ("min-y-exclusive", "p.y >= NSMinY(rect)", "p.y > NSMinY(rect)", "Edges", ()),
        ("partial-layout", block("  if (!NSEqualRanges([manager glyphRangeForTextContainer", "  if (!n)"),
         "", "Refusals", ()),
        ("out-of-range-glyph", "  if (g >= n)\n    return MAC_HIT_NONE;\n", "", "Edges", both),
        ("closure", closure, "  (void)actual;\n", "Clusters", both),
        ("any-hidden", hidden, "  for (NSUInteger i = glyphs.location; i < NSMaxRange(glyphs); ++i)\n"
         "    if ([manager notShownAttributeForGlyphAtIndex:i]) return MAC_HIT_NONE;\n\n", "Clusters", ()),
        ("allow-hidden", hidden, "", "Whitespace", ()),
        ("allow-truncated", intersection, "    (void)truncated;", "Truncation", ("mavericks",)),
        ("allow-unsupported", unsupported,
         "    if ([manager respondsToSelector:@selector(truncatedGlyphRangeInLineFragmentForGlyphAtIndex:)])\n"
         "    {\n" + unsupported[unsupported.index("    const NSRange truncated"):] + "\n    }",
         "Truncation", ("tahoe",)),
        ("omit-line", "if (i == k || !ContainsHitPoint(line, p))", "(void)line;\n  if (i == k)", "Edges", "witness"),
        ("omit-rect", block("  NSUInteger k = 0;", "    return MAC_HIT_NONE;\n\n  *out"),
         "  if (!ContainsHitPoint(line, p))\n", "Edges", ()),
        ("provenance-none", provenance, provenance.replace("MAC_HIT_REFUSED", "MAC_HIT_NONE"), "Clusters", ()),
        ("native-width", "{source, [view appliedWidth]}", "{source, [[view lokaTextContainer] containerSize].width}",
         "Refusals", ()),
        ("absent-none", presence, presence.replace("MAC_HIT_REFUSED", "MAC_HIT_NONE"), "Refusals", ()),
        ("allow-gutter", block("  if (p.x >= NSMaxX(fragment)", "  NSUInteger k = 0;"),
         "  (void)fragment;\n", "Bidi", ()),
        ("truncation-before-hidden", hidden + truncation, truncation + hidden, "Truncation", ("tahoe",)),
        ("geometry-before-truncation", truncation + geometry, geometry + truncation, "Truncation", ("tahoe",)),
    ]


def run_mutation(args, entry, original):
    name, old, new, group, survivors = entry
    source = original.decode("utf-8")
    assert source.count(old) == 1, "anchor mismatch: " + name
    test = "testMacTextHit" + group
    logs = ROOT / "build/macos-text-hit-mutations" / args.rig
    logs.mkdir(parents=True, exist_ok=True)

    def check(phase):
        build_log = logs / (name + "-" + phase + "-build.log")
        with build_log.open("wb") as output:
            status = subprocess.call(shlex.split(args.build), cwd=str(ROOT), stdout=output, stderr=subprocess.STDOUT)
        if status:
            raise RuntimeError("build failed: " + str(build_log))
        test_log = logs / (name + "-" + phase + ".log")
        with test_log.open("wb") as output:
            status = subprocess.call(shlex.split(args.test) + [test], cwd=str(ROOT), stdout=output,
                                     stderr=subprocess.STDOUT)
        log = test_log.read_bytes()
        if status and (phase != "mutant" or b"LOKA_VERIFY failed:" not in log):
            raise RuntimeError("test or launcher failed: " + str(test_log))
        return status, log

    _, baseline = check("baseline")
    if survivors == "witness":
        expected = b"[skip] no shown candidate rectangle outside its used line" in baseline
    else:
        expected = args.rig in survivors
    try:
        CONTEXT.write_bytes(source.replace(old, new).encode("utf-8"))
        status, _ = check("mutant")
    finally:
        CONTEXT.write_bytes(original)
        assert CONTEXT.read_bytes() == original
        check("restored")
    print("{} {}: {} by {} (expected {})".format(args.rig, name, "survived" if not status else "killed",
                                                 test, "survival" if expected else "kill"), flush=True)
    return (status == 0) == expected


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    original = CONTEXT.read_bytes()
    entries = mutations(original.decode("utf-8"))
    parser.add_argument("--rig", choices=("tahoe", "mavericks"), required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--test", required=True)
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [e[0] for e in entries])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    matched = True
    for entry in entries:
        if args.mutation in ("all", entry[0]):
            matched = run_mutation(args, entry, original) and matched
    raise SystemExit(0 if matched else 1)
