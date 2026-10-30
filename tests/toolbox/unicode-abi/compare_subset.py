#!/usr/bin/env python3
"""Compare MacTextEditorSubset.h with the SDK's MacTextEditor.h, layout by layout.

Runs only on a machine that has a Retro68 toolchain built with Universal
Interfaces; CI has no SDK and does not run it. Everything derived from the
SDK (generated sources, objects, dumps, logs, the report) is written under
build/toolbox-unicode-abi/ in this repository and nowhere else: that
directory is the only output root, its real path must be that exact
location, and subprocesses run with their working directory and TMPDIR
inside it. No argument chooses an output path.

Method: for each header a layout translation unit stores sizeof, class-ness,
offsetof and constant values in an exported, used, 32-bit array, and a
signature translation unit initializes an exported, used function pointer of
the manifest's type with each routine. Both are compiled (never run) with the
same flags; the array bytes are read back from the object through objdump at
the symbol's offset and decoded big-endian; the undefined symbols of the
signature object must be exactly the manifest's routine names, unmangled.
Three negative controls run every time so that a silent pass is impossible.
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import manifest  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
HEADER_DIR = REPO / "apple" / "toolbox" / "src" / "unicode"
HEADER = HEADER_DIR / "MacTextEditorSubset.h"
OUTPUT_ROOT = REPO / "build" / "toolbox-unicode-abi"
FLAGS = ("-std=gnu++98", "-O2", "-fno-exceptions", "-fno-rtti", "-fno-lto", "-Wall", "-c")
LAYOUT_SYMBOL = "loka_mlte_abi_layout"
SIGNATURE_PREFIX = "loka_mlte_sig_"
SDK_INCLUDE = "<MacTextEditor.h>"
SUBSET_INCLUDE = '"MacTextEditorSubset.h"'


class AbiError(Exception):
    pass


# ---- the output wall --------------------------------------------------------

def output_wall(root, repo):
    """The only accepted output root, after resolving every symlink."""
    expected = repo.resolve() / "build" / "toolbox-unicode-abi"
    build = repo.resolve() / "build"
    if build.exists() and build.resolve() != build:
        raise AbiError(f"refusing output: {build} is a symlink")
    if root.resolve() != expected:
        raise AbiError(f"refusing output outside {expected}: {root}")
    return expected


# ---- generated translation units ------------------------------------------

def layout_labels():
    labels = ["count"]
    for name, _, _, fields in manifest.TYPES:
        labels += [f"sizeof {name}", f"is-class {name}"]
        for field, _, _ in fields:
            labels += [f"offsetof {name}.{field}", f"sizeof {name}.{field}"]
    labels += [f"value {name}" for name, _ in manifest.CONSTANTS]
    return labels


def layout_expected():
    values = [0]
    for _, size, is_class, fields in manifest.TYPES:
        values += [size, 1 if is_class else 0]
        for _, offset, field_size in fields:
            values += [offset, field_size]
    values += [value & 0xFFFFFFFF for _, value in manifest.CONSTANTS]
    values[0] = len(values)
    return values


def layout_source(include):
    elements = ["0"]
    for name, _, _, fields in manifest.TYPES:
        elements += [f"sizeof({name})", f"LOKA_IS_CLASS({name})"]
        for field, _, _ in fields:
            elements += [f"offsetof({name}, {field})", f"sizeof(((const {name} *)0)->{field})"]
    elements += [f"(unsigned long)({name})" for name, _ in manifest.CONSTANTS]
    elements[0] = str(len(elements))
    body = ",\n  ".join(elements)
    return (
        f"#include {include}\n"
        "#include <stddef.h>\n"
        "template <class T> char (&lokaClassProbe(int T::*))[2];\n"
        "template <class T> char lokaClassProbe(...);\n"
        "#define LOKA_IS_CLASS(T) (sizeof(lokaClassProbe<T>(0)) == 2 ? 1UL : 0UL)\n"
        f"extern \"C\" const unsigned long {LAYOUT_SYMBOL}[{len(elements)}] __attribute__((used)) = {{\n"
        f"  {body}\n}};\n")


def signature_source(include, functions=manifest.FUNCTIONS):
    lines = [f"#include {include}"]
    for index, (name, result, params) in enumerate(functions):
        lines.append(f"extern \"C\" {result} (*const {SIGNATURE_PREFIX}{index})({', '.join(params)})"
                     f" __attribute__((used)) = &{name};")
    return "\n".join(lines) + "\n"


def mismatched_functions():
    """Negative control: the first routine with one parameter too many."""
    name, result, params = manifest.FUNCTIONS[0]
    return ((name, result, params + ("int",)),) + manifest.FUNCTIONS[1:]


def mangled_source():
    """Negative control: a routine declared without C linkage."""
    name, result, params = manifest.FUNCTIONS[0]
    return ("struct OpaqueTXNObject;\n"
            f"long {name}(OpaqueTXNObject *);\n"
            f"long (*const {SIGNATURE_PREFIX}0)(OpaqueTXNObject *) __attribute__((used)) = &{name};\n")


# ---- object inspection ------------------------------------------------------

def parse_sections(text):
    """objdump -h: index -> (name, size, vma)."""
    sections = {}
    for match in re.finditer(r"^\s*(\d+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s", text, re.M):
        sections[int(match.group(1))] = (match.group(2), int(match.group(3), 16), int(match.group(4), 16))
    if not sections:
        raise AbiError("objdump -h: no sections")
    return sections


def parse_symbols(text):
    """objdump -t: name -> (section index, value)."""
    symbols = {}
    for match in re.finditer(r"^\[\s*\d+\]\(sec\s+(-?\d+)\).*?\s0x([0-9a-fA-F]+)\s+(\S+)\s*$", text, re.M):
        symbols[match.group(3)] = (int(match.group(1)), int(match.group(2), 16))
    if not symbols:
        raise AbiError("objdump -t: no symbols")
    return symbols


def parse_contents(text):
    """objdump -s -j <section>: address -> byte."""
    data = {}
    for match in re.finditer(r"^\s*([0-9a-fA-F]+) ((?:[0-9a-fA-F]+ ?){1,4})  ", text, re.M):
        address = int(match.group(1), 16)
        raw = bytes.fromhex(match.group(2).replace(" ", ""))
        for i, value in enumerate(raw):
            data[address + i] = value
    if not data:
        raise AbiError("objdump -s: no contents")
    return data


def decode_array(sections, symbols, contents, symbol, count):
    if symbol not in symbols:
        raise AbiError(f"symbol missing: {symbol}")
    section_index, value = symbols[symbol]
    if section_index - 1 not in sections:
        raise AbiError(f"{symbol}: not in a section")
    name, size, vma = sections[section_index - 1]
    if value < vma or value + 4 * count > vma + size:
        raise AbiError(f"{symbol}: {count} words at {value:#x} leave section {name}")
    try:
        raw = bytes(contents[value + i] for i in range(4 * count))
    except KeyError:
        raise AbiError(f"{symbol}: section {name} has no bytes at {value:#x}")
    return list(struct.unpack(f">{count}I", raw)), name


def parse_undefined(text):
    """nm -u: the undefined symbol names."""
    return {match.group(1) for match in re.finditer(r"^\s+U\s+(\S+)\s*$", text, re.M)}


def check_linkage(undefined, functions=manifest.FUNCTIONS):
    expected = {name for name, _, _ in functions}
    mangled = sorted(name for name in undefined if name.startswith("_Z"))
    if mangled:
        raise AbiError(f"C++ linkage where C linkage was declared: {' '.join(mangled)}")
    missing = sorted(expected - undefined)
    extra = sorted(undefined - expected)
    if missing or extra:
        raise AbiError(f"referenced routines differ from the manifest: missing {missing} extra {extra}")


def compare(labels, left, right):
    if not (len(labels) == len(left) == len(right)):
        raise AbiError(f"array lengths differ: labels {len(labels)} sdk {len(left)} subset {len(right)}")
    return [(label, a, b) for label, a, b in zip(labels, left, right) if a != b]


# ---- the toolchain --------------------------------------------------------

class Toolchain:
    def __init__(self, build_dir):
        bin_dir = Path(build_dir) / "toolchain" / "bin"
        self.gxx = bin_dir / "powerpc-apple-macos-g++"
        self.nm = bin_dir / "powerpc-apple-macos-nm"
        self.objdump = bin_dir / "powerpc-apple-macos-objdump"
        for tool in (self.gxx, self.nm, self.objdump):
            if not tool.is_file():
                raise AbiError(f"missing tool: {tool}")

    def run(self, args, cwd, check=True):
        env = dict(os.environ, TMPDIR=str(cwd))
        result = subprocess.run([str(a) for a in args], cwd=str(cwd), env=env,
                                capture_output=True, text=True)
        if check and result.returncode != 0:
            raise AbiError(f"{args[0]} failed:\n{result.stderr}")
        return result


def compile_unit(tools, root, stem, source, include_dirs, expect_success=True):
    cpp = root / f"{stem}.cpp"
    obj = root / f"{stem}.o"
    log = root / f"{stem}.log"
    cpp.write_text(source)
    args = [tools.gxx, *FLAGS] + [f"-I{d}" for d in include_dirs] + [cpp, "-o", obj]
    result = tools.run(args, root, check=False)
    log.write_text(result.stdout + result.stderr)
    if expect_success and result.returncode != 0:
        raise AbiError(f"{stem}: compile failed, see {log}")
    if not expect_success and result.returncode == 0:
        raise AbiError(f"{stem}: negative control compiled, see {log}")
    return obj


def read_layout(tools, root, obj, stem):
    sections = parse_sections(tools.run([tools.objdump, "-h", obj], root).stdout)
    symbols = parse_symbols(tools.run([tools.objdump, "-t", obj], root).stdout)
    if LAYOUT_SYMBOL not in symbols:
        raise AbiError(f"{stem}: symbol missing: {LAYOUT_SYMBOL}")
    section_name = sections[symbols[LAYOUT_SYMBOL][0] - 1][0]
    contents = parse_contents(tools.run([tools.objdump, "-s", "-j", section_name, obj], root).stdout)
    count = len(layout_labels())
    values, _ = decode_array(sections, symbols, contents, LAYOUT_SYMBOL, count)
    if values[0] != count:
        raise AbiError(f"{stem}: array count {values[0]} != {count}")
    undefined = parse_undefined(tools.run([tools.nm, "-u", obj], root).stdout)
    if undefined:
        raise AbiError(f"{stem}: layout unit references {sorted(undefined)}")
    return values


def sdk_header_path(tools, root, include_dirs):
    """Which MacTextEditor.h the SDK unit consumed (evidence only)."""
    cpp = root / "sdk_probe.cpp"
    cpp.write_text(f"#include {SDK_INCLUDE}\n")
    result = tools.run([tools.gxx, "-std=gnu++98", "-M", *[f"-I{d}" for d in include_dirs], cpp], root)
    for token in result.stdout.replace("\\\n", " ").split():
        if token.endswith("MacTextEditor.h"):
            return token
    raise AbiError("the SDK unit did not include MacTextEditor.h")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retro68-build-dir", default=os.environ.get("RETRO68_BUILD_DIR", str(Path.home() / "Retro68-build")),
                        help="Retro68 build output directory (default: $RETRO68_BUILD_DIR or ~/Retro68-build)")
    args = parser.parse_args()
    try:
        root = output_wall(OUTPUT_ROOT, REPO)
        root.mkdir(parents=True, exist_ok=True)
        tools = Toolchain(args.retro68_build_dir)
        if not HEADER.is_file():
            raise AbiError(f"missing header: {HEADER}")
        labels = layout_labels()
        claimed = layout_expected()
        report = [f"sdk header: {sdk_header_path(tools, root, [])}", f"subset header: {HEADER}",
                  f"flags: {' '.join(FLAGS)}"]

        sdk_layout = read_layout(tools, root, compile_unit(tools, root, "sdk_layout", layout_source(SDK_INCLUDE), []), "sdk_layout")
        subset_layout = read_layout(tools, root, compile_unit(tools, root, "subset_layout", layout_source(SUBSET_INCLUDE), [HEADER_DIR]), "subset_layout")
        differences = compare(labels, sdk_layout, subset_layout)
        claim_differences = compare(labels, claimed, subset_layout)

        for stem, include, dirs in (("sdk_signature", SDK_INCLUDE, []), ("subset_signature", SUBSET_INCLUDE, [HEADER_DIR])):
            obj = compile_unit(tools, root, stem, signature_source(include), dirs)
            check_linkage(parse_undefined(tools.run([tools.nm, "-u", obj], root).stdout))

        # Negative controls: each must fail in the predicted way.
        compile_unit(tools, root, "control_signature_sdk", signature_source(SDK_INCLUDE, mismatched_functions()), [], expect_success=False)
        compile_unit(tools, root, "control_signature_subset", signature_source(SUBSET_INCLUDE, mismatched_functions()), [HEADER_DIR], expect_success=False)
        altered = list(sdk_layout)
        altered[-1] ^= 1
        if len(compare(labels, altered, subset_layout)) != len(differences) + 1:
            raise AbiError("control: an altered value was not reported")
        obj = compile_unit(tools, root, "control_mangled", mangled_source(), [])
        try:
            check_linkage(parse_undefined(tools.run([tools.nm, "-u", obj], root).stdout))
        except AbiError as error:
            if "C++ linkage" not in str(error):
                raise
        else:
            raise AbiError("control: a mangled reference was not reported")
        report.append("negative controls: signature mismatch refused (sdk, subset), altered value reported, mangled reference reported")

        for label, a, b in differences:
            report.append(f"DIFF {label}: sdk {a:#x} subset {b:#x}")
        for label, a, b in claim_differences:
            report.append(f"CLAIM {label}: manifest {a:#x} subset {b:#x}")
        summary = (f"MLTE subset ABI: types {len(manifest.TYPES)} / constants {len(manifest.CONSTANTS)} / "
                   f"functions {len(manifest.FUNCTIONS)}, differences {len(differences)}, "
                   f"manifest claims off {len(claim_differences)}")
        report.append(summary)
        (root / "report.txt").write_text("\n".join(report) + "\n")
        print(summary)
        print(f"report: {root / 'report.txt'}")
        return 0 if not differences and not claim_differences else 1
    except AbiError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
