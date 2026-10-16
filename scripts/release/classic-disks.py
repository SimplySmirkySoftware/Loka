#!/usr/bin/env python3
"""Package and read back the Classic applications with adjacent release data.

Images are 800 KiB or 1440 KiB floppies when the payload fits with a 128 KiB
reserve; a larger payload (PPC SmirkyCard) gets a non-floppy image rounded up to
512 KiB, for an emulator or a PowerPC Mac that mounts disk images.
"""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
# (cpu, example, application, files beside it). A None file list means the
# example's disk-scripts.txt: SmirkyCard ships every card script MAIN.JS can
# open, and scripts/mame-*-disk*, example/SmirkyCard/CMakeLists.txt and the
# Win32/macOS release stages (scripts/win32-standalone-flow.ps1,
# scripts/macos/standalone-release-stage.sh) read the same manifest. Keep the
# readers parallel.
PAYLOADS = (
    ("68k", "ScrapbookUI", "ScrapbookUI68K", ("ASSETS.LRP",)),
    ("ppc", "ScrapbookUI", "ScrapbookUIPPC", ("ASSETS.LRP",)),
    ("68k", "SmirkyCard", "LokaSmirkyCard68K", None),
    ("ppc", "SmirkyCard", "LokaSmirkyCardPPC", None),
)
TOOL_NAMES = ("hformat", "hcopy", "humount", "hmount", "hls", "hattrib")
FLOPPY_SIZES = (800 * 1024, 1440 * 1024)
LARGE_IMAGE_STEP = 512 * 1024


def payload_files(example, files):
    """Name the files beside the application, reading the card manifest if asked."""
    if files is not None:
        return list(files)
    manifest = ROOT / "example" / example / "disk-scripts.txt"
    if not manifest.is_file():
        raise ValueError(f"Missing input: {manifest}")
    names = []
    for line in manifest.read_text(encoding="utf-8").splitlines():
        # The shell readers trim space, tab and CR; CMake's STRIP trims the same.
        name = line.strip(" \t\r")
        if not name or name.startswith("#"):
            continue
        if any(separator in name for separator in "/\\:") or name in (".", ".."):
            raise ValueError(f"Card script is not a flat file name: {name!r} in {manifest}")
        if name in names:
            raise ValueError(f"Duplicate card script: {name} in {manifest}")
        names.append(name)
    if not names:
        raise ValueError(f"No card scripts listed in {manifest}")
    # Same whitespace/comment grammar as the adjacent script manifest.
    pictures = ROOT / "example" / example / "disk-pictures.txt"
    if not pictures.is_file():
        raise ValueError(f"Missing input: {pictures}")
    for line in pictures.read_text(encoding="utf-8").splitlines():
        stem = line.strip(" \t\r")
        if stem and not stem.startswith("#"):
            names.append(f"assets/{stem}.PICT")
    return names


def is_card_script(path):
    return path.suffix == ".JS"


def retro68_tools():
    """Use the same environment and discovery order as the development disk."""
    result = subprocess.run(
        ["bash", "-euc", '''
. "$1/scripts/retro68-env.sh"
loka_load_retro68_environment "$1"
. "$1/scripts/hfs-disk.sh"
shift
for tool in "$@"; do find_retro68_tool "$tool"; done
''', "classic-disks", str(ROOT), *TOOL_NAMES],
        check=True, stdout=subprocess.PIPE, text=True,
    )
    return dict(zip(TOOL_NAMES, result.stdout.splitlines()))


def macbinary(path):
    """Read the name, Finder signature and both forks, ignoring transport padding."""
    raw = path.read_bytes()
    if len(raw) < 128 or raw[0] != 0 or not 1 <= raw[1] <= 63:
        raise ValueError(f"Invalid MacBinary header: {path}")
    data_size, resource_size = struct.unpack_from(">II", raw, 83)
    secondary_size = struct.unpack_from(">H", raw, 120)[0]
    data_start = 128 + ((secondary_size + 127) // 128) * 128
    resource_start = data_start + ((data_size + 127) // 128) * 128
    if len(raw) < resource_start + resource_size:
        raise ValueError(f"Truncated MacBinary forks: {path}")
    return (raw[2:2 + raw[1]], raw[65:73],
            raw[data_start:data_start + data_size],
            raw[resource_start:resource_start + resource_size])


def image_size(binary, data_files):
    """Pick the smallest floppy that fits, else a 512 KiB-rounded larger image.

    The larger image is not a floppy. It is for an emulator or a PowerPC
    machine that mounts disk images: PPC SmirkyCard needs about 1.6 MB.
    """
    # MacBinary's padding/header overcounts the stored forks. Reserve another
    # 128 KiB for allocation rounding, catalog/extent trees and free space.
    required = (binary.stat().st_size + sum(data.stat().st_size for data in data_files)
                + 128 * 1024)
    for size in FLOPPY_SIZES:
        if required <= size:
            return size
    return -(-required // LARGE_IMAGE_STEP) * LARGE_IMAGE_STEP


def output_directory():
    output = ROOT / "build/release/classic-disks"
    # No output override: generated payloads stay in this checkout's build/.
    for path in (ROOT / "build", ROOT / "build/release", output):
        if path.is_symlink():
            raise ValueError(f"Refusing symlink output directory: {path}")
    tracked = subprocess.check_output(
        ["git", "-C", str(ROOT), "ls-files", "--", "build/release/classic-disks"])
    if tracked:
        raise ValueError("Refusing tracked output paths in build/release/classic-disks")
    output.mkdir(parents=True, exist_ok=True)
    return output


def build_image(tools, output, binary, data_files, name, size):
    """Own temporary image, mount state and readback; publish only after checking."""
    with tempfile.TemporaryDirectory(prefix=".stage-", dir=output) as directory:
        stage = Path(directory)
        image = stage / (name + ".dsk")
        image.write_bytes(b"")
        with image.open("r+b") as stream:
            stream.truncate(size)
        env = dict(os.environ, HOME=str(stage))

        def hfs(tool, *args):
            return subprocess.check_output(
                [tools[tool], *map(str, args)], env=env, text=True)

        try:
            subprocess.run(
                ["bash", "-euc", '''
. "$1"
HFORMAT="$2" HCOPY="$3" HUMOUNT="$4"
home="$5" label="$6" image="$7" binary="$8"
shift 8
hfs_populate "$home" "$label" "$image" 0 1 "$binary" "$@"
''', "classic-disks", str(ROOT / "scripts/hfs-disk.sh"),
                 tools["hformat"], tools["hcopy"], tools["humount"],
                 str(stage), name, str(image), str(binary), *map(str, data_files)],
                check=True, stdout=subprocess.PIPE, env=env,
            )
            scripts = [data for data in data_files if is_card_script(data)]
            if scripts:
                # A Classic user opens a card script in SimpleText to edit it.
                hfs("hmount", image)
                for data in scripts:
                    hfs("hattrib", "-t", "TEXT", "-c", "ttxt", ":" + data.name)
                hfs("humount")
            # Reopen the persisted image, not the writer's mounted state.
            hfs("hmount", image)
            listing = hfs("hls", "-1a", ":").splitlines()
            if sorted(listing) != sorted([name, *(data.name for data in data_files)]):
                raise ValueError(f"Unexpected root contents in {name}: {listing}")
            hfs("hcopy", "-m", ":" + name, stage / "app.bin")
            if macbinary(stage / "app.bin") != macbinary(binary):
                raise ValueError(f"Application name, signature or forks differ: {name}")
            if macbinary(binary)[0] != name.encode("ascii"):
                raise ValueError(f"Unexpected application name: {binary}")
            for data in data_files:
                raw = stage / (data.name + ".raw")
                encoded = stage / (data.name + ".bin")
                hfs("hcopy", "-r", ":" + data.name, raw)
                if raw.read_bytes() != data.read_bytes():
                    raise ValueError(f"Data readback differs: {data}")
                hfs("hcopy", "-m", ":" + data.name, encoded)
                metadata = macbinary(encoded)
                if metadata[3] or (is_card_script(data) and metadata[1] != b"TEXTttxt"):
                    raise ValueError(f"Unexpected data forks or Finder signature: {data}")
            details = hfs("hls", "-la", ":")
            hfs("humount")
            image.replace(output / image.name)
            print(f"Prepared {name}.dsk ({size} bytes)\n{details}", end="")
        finally:
            subprocess.run([tools["humount"]], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=ROOT / "build",
                        help="read-only build input root (default: checkout build/)")
    args = parser.parse_args()
    inputs = []
    for cpu, example, name, files in PAYLOADS:
        binary = args.build_root.resolve() / f"retro68/{cpu}/Release/example/{example}/{name}.bin"
        data_files = [ROOT / "example" / example / filename
                      for filename in payload_files(example, files)]
        for path in (binary, *data_files):
            if not path.is_file():
                raise ValueError(f"Missing input: {path}")
        inputs.append((binary, data_files, name, image_size(binary, data_files)))
    tools = retro68_tools()
    output = output_directory()
    for binary, data_files, name, size in inputs:
        build_image(tools, output, binary, data_files, name, size)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Error: {error}")
