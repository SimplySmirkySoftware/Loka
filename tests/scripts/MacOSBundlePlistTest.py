#!/usr/bin/env python3
"""Check generated macOS app bundles, including unbuilt CMake targets."""

import pathlib
import plistlib
import sys


def check_bundles(build_root):
    bundles = sorted(build_root.rglob("*.app"))
    if not bundles:
        raise ValueError("No application bundles found in " + str(build_root))
    for bundle in bundles:
        with (bundle / "Contents" / "Info.plist").open("rb") as source:
            info = plistlib.load(source)
        if info.get("NSHighResolutionCapable") is not True:
            raise ValueError(str(bundle) + ": high-resolution capability is not true")
        if info.get("NSRequiresAquaSystemAppearance") is not False:
            raise ValueError(str(bundle) + ": dark appearance opt-in is not declared")
        if info.get("CFBundleExecutable") != bundle.stem:
            raise ValueError(str(bundle) + ": executable metadata does not match target")
        if info.get("CFBundlePackageType") != "APPL":
            raise ValueError(str(bundle) + ": application package type is missing")
    print("MacOSBundlePlistTest: checked {} bundles".format(len(bundles)))


if __name__ == "__main__":
    check_bundles(pathlib.Path(sys.argv[1]))
