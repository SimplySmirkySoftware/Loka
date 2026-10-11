#!/usr/bin/env python3
"""Package and read back the Classic applications with adjacent release data."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
# Release policy, deliberately independent of the development disk's card manifest.
PAYLOADS = (
    ("68k", "ScrapbookUI", "ScrapbookUI68K", "ASSETS.LRP"),
    ("ppc", "ScrapbookUI", "ScrapbookUIPPC", "ASSETS.LRP"),
    ("68k", "SmirkyCard", "LokaSmirkyCard68K", "MAIN.JS"),
)
TOOL_NAMES = ("hformat", "hcopy", "humount", "hmount", "hls", "hattrib")


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


def image_size(binary, data):
    # MacBinary's padding/header overcounts the stored forks. Reserve another
    # 128 KiB for allocation rounding, catalog/extent trees and free space.
    required = binary.stat().st_size + data.stat().st_size + 128 * 1024
    for size in (800 * 1024, 1440 * 1024):
        if required <= size:
            return size
    raise ValueError(f"Payload does not fit a 1440 KiB image with margin: {binary}")


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


def build_image(tools, output, binary, data, name, size):
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
hfs_populate "$5" "$6" "$7" 0 1 "$8" "$9"
''', "classic-disks", str(ROOT / "scripts/hfs-disk.sh"),
                 tools["hformat"], tools["hcopy"], tools["humount"],
                 str(stage), name, str(image), str(binary), str(data)],
                check=True, stdout=subprocess.PIPE, env=env,
            )
            if data.name == "MAIN.JS":
                hfs("hmount", image)
                hfs("hattrib", "-t", "TEXT", "-c", "ttxt", ":MAIN.JS")
                hfs("humount")
            # Reopen the persisted image, not the writer's mounted state.
            hfs("hmount", image)
            listing = hfs("hls", "-1a", ":").splitlines()
            if sorted(listing) != sorted((name, data.name)):
                raise ValueError(f"Unexpected root contents in {name}: {listing}")
            hfs("hcopy", "-m", ":" + name, stage / "app.bin")
            if macbinary(stage / "app.bin") != macbinary(binary):
                raise ValueError(f"Application name, signature or forks differ: {name}")
            if macbinary(binary)[0] != name.encode("ascii"):
                raise ValueError(f"Unexpected application name: {binary}")
            hfs("hcopy", "-r", ":" + data.name, stage / "data.raw")
            if (stage / "data.raw").read_bytes() != data.read_bytes():
                raise ValueError(f"Data readback differs: {data}")
            hfs("hcopy", "-m", ":" + data.name, stage / "data.bin")
            metadata = macbinary(stage / "data.bin")
            if metadata[3] or (data.name == "MAIN.JS" and metadata[1] != b"TEXTttxt"):
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
    for cpu, example, name, filename in PAYLOADS:
        binary = args.build_root.resolve() / f"retro68/{cpu}/Release/example/{example}/{name}.bin"
        data = ROOT / "example" / example / filename
        for path in (binary, data):
            if not path.is_file():
                raise ValueError(f"Missing input: {path}")
        inputs.append((binary, data, name, image_size(binary, data)))
    tools = retro68_tools()
    output = output_directory()
    for binary, data, name, size in inputs:
        build_image(tools, output, binary, data, name, size)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Error: {error}")
