#!/usr/bin/env python3
"""Refuse Objective-C runtime names that Tiger's runtime does not export (#1302).

The Tiger build (GCC 4.0, MacOSX10.4u.sdk) links against the Objective-C 1
runtime. Some source forms need an Objective-C 2.0 runtime helper: a
synthesized retain or copy setter (objc_setProperty), for-in
(objc_enumerationMutation), a block (_NSConcreteStackBlock), @autoreleasepool.
They compile on every toolchain and fail only on the Tiger link. Hosted CI has
no Tiger toolchain, so this check reads the undefined symbols of the macOS
library and example objects that the current compiler built. It refuses every
runtime name that is in neither table below, so an unknown name fails.

This detects source forms. It is not Tiger evidence. The current compiler
spells some Tiger-safe forms differently (SAME_SOURCE_FORM). API availability
per selector, SDK type differences and GCC 4.0's own parser stay with the legacy
build on the Mavericks rig.

Usage: check_macos_objc1_runtime.py <macOS build dir> [--cxx COMPILER]
"""

from pathlib import Path
import argparse
import re
import shutil
import subprocess
import sys
import tempfile


# Objective-C runtime entry points, the blocks runtime, and the Objective-C
# exception personality. C++ names are mangled (__Z...) and do not match.
RUNTIME_NAME = re.compile(
    r"^_+(?:objc_|class_|sel_|method_|ivar_|object_|protocol_|property_|NSConcrete|Block_)"
    r"|^___objc_personality")

# Exported by MacOSX10.4u.sdk/usr/lib/libobjc.A.dylib (Xcode 3.2.6), read with
# `nm -g` on the Mavericks rig on 2026-10-13. Only names Loka references are
# listed. Before adding a name, confirm that it is in that table.
TIGER_RUNTIME = {
    "_objc_msgSend",
    "_objc_msgSend_stret",
}

# Names the current compiler emits for a source form that Tiger supports under
# another spelling. Each entry gives the Tiger form and why the two match.
# GCC 4.0's spelling was read from the g40 objects of the same sources.
SAME_SOURCE_FORM = {
    "_objc_msgSendSuper2": ("objc_msgSendSuper", "[super message] in the non-fragile ABI"),
    "__objc_empty_cache": ("no reference", "a non-fragile class definition's method cache; "
                           "GCC 4.0's fragile class definitions reference nothing"),
    "_objc_alloc": ("+alloc", "clang converts the message to a runtime call"),
    "_objc_alloc_init": ("+alloc then -init", "clang converts the messages to a runtime call"),
    "_objc_retain": ("-retain", "clang converts the message to a runtime call"),
    "_objc_release": ("-release", "clang converts the message to a runtime call"),
    "_objc_autorelease": ("-autorelease", "clang converts the message to a runtime call"),
    "_objc_opt_class": ("-class", "clang converts the message to a runtime call"),
    "_objc_opt_isKindOfClass": ("-isKindOfClass:", "clang converts the message to a runtime call"),
    "_objc_opt_respondsToSelector": ("-respondsToSelector:",
                                     "clang converts the message to a runtime call"),
}
# Per-selector stubs (_objc_msgSend$name) that the linker turns into a plain send.
SAME_SOURCE_FORM_PREFIX = ("_objc_msgSend$", ("objc_msgSend", "per-selector message stub"))

# A synthesized retain property: the probe must be refused, or the check is
# blind. The assign property beside it must not be: assign needs no helper.
PROBE_SOURCE = """\
#import <Foundation/Foundation.h>
@interface LokaObjC1Probe : NSObject
{
  NSObject *held_;
  NSObject *borrowed_;
}
@property(nonatomic, retain) NSObject *held;
@property(nonatomic, assign) NSObject *borrowed;
@end
@implementation LokaObjC1Probe
@synthesize held = held_;
@synthesize borrowed = borrowed_;
@end
"""


def allowed(name):
    if name in TIGER_RUNTIME or name in SAME_SOURCE_FORM:
        return True
    return name.startswith(SAME_SOURCE_FORM_PREFIX[0])


def undefined_runtime_names(path):
    """Yield (object, name) for each undefined runtime name in an object or archive."""
    out = subprocess.run(["nm", "-u", str(path)], check=True, capture_output=True, text=True).stdout
    member = path.name
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        # Archive output names each member as "lib.a(member.o):".
        if line.endswith(":"):
            member = line[:-1]
            continue
        if RUNTIME_NAME.match(line):
            yield member, line


def refusals(paths):
    found = []
    for path in paths:
        for member, name in undefined_runtime_names(path):
            if not allowed(name):
                found.append((name, member))
    return found


def run_probe(cxx):
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "probe.mm"
        obj = Path(tmp) / "probe.o"
        source.write_text(PROBE_SOURCE)
        subprocess.run([cxx, "-x", "objective-c++", "-fno-objc-arc", "-c", str(source), "-o", str(obj)],
                       check=True)
        names = sorted({name for name, _ in refusals([obj])})
    if not names or any(not name.startswith("_objc_setProperty") for name in names):
        print("objc1-runtime: probe FAILED: the synthesized retain probe should be refused for "
              "objc_setProperty only, got %s" % (names or "nothing"))
        return False
    print("objc1-runtime: probe refused %s" % ", ".join(names))
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--cxx", default=shutil.which("clang++") or "c++")
    args = parser.parse_args()

    if not run_probe(args.cxx):
        return 1

    library = args.build_dir / "apple" / "macos" / "libLokaMacOSCore.a"
    if not library.is_file():
        print("objc1-runtime: FAILED: %s is missing; build the macos-debug preset first" % library)
        return 1
    examples = sorted(p for p in (args.build_dir / "example").rglob("*.o"))
    if not examples:
        print("objc1-runtime: FAILED: no example objects under %s" % (args.build_dir / "example"))
        return 1

    found = refusals([library] + examples)
    print("objc1-runtime: scanned %s and %d example objects" % (library.name, len(examples)))
    if found:
        print("objc1-runtime: FAILED: these runtime names are not exported by Tiger's libobjc.")
        print("  A synthesized retain or copy property, for-in, a block or @autoreleasepool needs")
        print("  the Objective-C 2.0 runtime (#1302). A name that Tiger does export goes into")
        print("  TIGER_RUNTIME after checking the 10.4u SDK's libobjc.A.dylib.")
        for name, member in sorted(set(found)):
            print("  %s  <- %s" % (name, member))
        return 1
    print("objc1-runtime: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
