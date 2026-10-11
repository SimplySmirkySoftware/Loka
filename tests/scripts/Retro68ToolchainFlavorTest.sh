#!/usr/bin/env bash
# Pins the Retro68 toolchain's flavor wall (#1191): 68K and RetroPPC configure,
# and any other RETRO68_PPC_FLAVOR is refused even when its toolchain file
# exists, so the refusal is the guard's and not a missing file's.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUBJECT="$REPO_DIR/cmake/toolchains/Retro68.cmake"
SANDBOX="$(mktemp -d)"
trap 'rm -rf "$SANDBOX"' EXIT

fail() {
  echo "Retro68ToolchainFlavorTest failed: $*" >&2
  exit 1
}

mkdir -p "$SANDBOX/stubs" "$SANDBOX/project"
for stub in retro68 retroppc retrocarbon; do
  printf 'set(LOKA_STUB_TOOLCHAIN %s)\n' "$stub" > "$SANDBOX/stubs/$stub.toolchain.cmake"
done
cat > "$SANDBOX/project/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(Retro68ToolchainFlavorProbe LANGUAGES NONE)
message(STATUS "stub toolchain: ${LOKA_STUB_TOOLCHAIN}")
CMAKE

configure() {
  local name="$1"
  shift
  cmake -S "$SANDBOX/project" -B "$SANDBOX/build-$name" \
    -DCMAKE_TOOLCHAIN_FILE="$SUBJECT" \
    -DRETRO68_TOOLCHAIN_DIR="$SANDBOX/stubs" "$@" > "$SANDBOX/$name.log" 2>&1
}

configure m68k -DRETRO68_CPU=m68k || fail "68K was refused: $(cat "$SANDBOX/m68k.log")"
grep -q 'stub toolchain: retro68$' "$SANDBOX/m68k.log" || fail "68K did not load retro68.toolchain.cmake"

configure ppc -DRETRO68_CPU=ppc || fail "RetroPPC was refused: $(cat "$SANDBOX/ppc.log")"
grep -q 'stub toolchain: retroppc$' "$SANDBOX/ppc.log" || fail "PPC did not load retroppc.toolchain.cmake"

if configure carbon -DRETRO68_CPU=ppc -DRETRO68_PPC_FLAVOR=retrocarbon; then
  fail "RETRO68_PPC_FLAVOR=retrocarbon configured"
fi
grep -q 'RETRO68_PPC_FLAVOR=retrocarbon is not supported' "$SANDBOX/carbon.log" \
  || fail "retrocarbon was refused for another reason: $(cat "$SANDBOX/carbon.log")"
if grep -q 'stub toolchain: retrocarbon' "$SANDBOX/carbon.log"; then
  fail "retrocarbon.toolchain.cmake was loaded"
fi

echo "Retro68ToolchainFlavorTest passed"
