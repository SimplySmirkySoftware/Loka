#!/usr/bin/env python3
"""Configure actual Toolbox cores: six incompatible graphs, separate consumers,
and the 68k selection refusal. Scratch projects/builds live outside the tree.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CASES = {
    "direct": "target_link_libraries(app PRIVATE LokaAppleToolboxCore LokaAppleToolboxUnicodeCore)",
    "reverse": "target_link_libraries(app PRIVATE LokaAppleToolboxUnicodeCore LokaAppleToolboxCore)",
    "alias": "add_library(te ALIAS LokaAppleToolboxCore)\ntarget_link_libraries(app PRIVATE te LokaAppleToolboxUnicodeCore)",
    "interface": "add_library(wrapper INTERFACE)\ntarget_link_libraries(wrapper INTERFACE LokaAppleToolboxCore)\ntarget_link_libraries(app PRIVATE wrapper LokaAppleToolboxUnicodeCore)",
    "chain": "target_link_libraries(LokaAppleToolboxUnicodeCore PUBLIC LokaAppleToolboxCore)\nloka_toolbox_unicode(app)",
    "test": "target_link_libraries(app PRIVATE LokaAppleToolboxTestCore LokaAppleToolboxUnicodeCore)",
    "separate": "target_link_libraries(app PRIVATE LokaAppleToolboxCore)\nadd_executable(other main.cpp)\nloka_toolbox_unicode(other)",
    "68k": "loka_toolbox_unicode(app)",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--retro68-build-dir", default=os.environ.get("RETRO68_BUILD_DIR", str(Path.home() / "Retro68-build")))
    args = parser.parse_args()
    root = args.root.resolve()
    with tempfile.TemporaryDirectory(prefix="loka-core-pins-") as directory:
        for case, graph in CASES.items():
            source = Path(directory) / case
            source.mkdir()
            (source / "CMakePresets.json").write_text(
                (root / "CMakePresets.json").read_text().replace("${sourceDir}", root.as_posix()))
            (source / "main.cpp").write_text("int main() { return 0; }\n")
            (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.15)
project(CoreVariantPin LANGUAGES CXX)
set(PROJECT_SOURCE_DIR "{root.as_posix()}")
include("{root.as_posix()}/cmake/LokaWarnings.cmake")
add_library(LokaAppleCommonCarbon INTERFACE)
add_subdirectory("{root.as_posix()}/apple/toolbox" toolbox)
add_executable(app main.cpp)
{graph}
''')
            # Consume the real preset's toolchain/CPU/settings, overriding only
            # source and binary directories. No copied variant implementation.
            preset = "retro68-68k-release" if case == "68k" else "retro68-ppc-release"
            result = subprocess.run(["cmake", "--preset", preset, "-G", "Unix Makefiles", "-S", str(source),
                                     "-B", str(source / "build"),
                                     "-DRETRO68_BUILD_DIR=" + args.retro68_build_dir],
                                    cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            expected = "LOKA_TOOLBOX_UNICODE needs" if case == "68k" else "LOKA_TOOLBOX_CORE_VARIANT"
            valid = result.returncode == 0 if case == "separate" else result.returncode != 0 and expected in result.stdout
            if not valid:
                print(result.stdout)
                raise SystemExit(f"{case}: unexpected configure result {result.returncode}")
            print(f"{case}: {'accepted' if case == 'separate' else 'refused'} (cmake exit {result.returncode})")


if __name__ == "__main__":
    main()
