#!/usr/bin/env bash
# Launch MAME for a source-level debugging session and report how to attach.
#
#   scripts/mame-debug.sh find   <appl.bin> <word0> <word1> <link>
#   scripts/mame-debug.sh attach <appl.bin> <word0> <word1> <link> <base>
#
# `find` boots the application once and prints its runtime load base. `attach`
# does the same but leaves the machine running with the gdbstub enabled, then
# waits for the listener and hands over to gdb with symbols already relocated.
#
# The pattern words and link address identify a function with no relocations;
# see "Find the runtime load base" in docs/MAME_DEVELOPMENT.md for the readelf
# recipe that picks one.
#
# Everything mutable lands under build/mame-debug/ so the configured boot disk
# is never used as writable runtime state.
#
# Early and deliberately narrow. What it does not do, and why, so anyone
# extending it knows which parts are decisions and which are simply absent:
#
#   - 68K only. The method itself does not carry to PowerPC; see "Why this
#     does not carry over to PPC" in docs/MAME_DEVELOPMENT.md.
#   - Two phases rather than one. gdb needs the base before it can place a
#     breakpoint by symbol, and the base is only discoverable once the
#     application has loaded. Collapsing this would need the stub to survive
#     a reconnect, which it does not (#182).
#   - Overlaps mame-run.sh in loading .env-mame and normalising paths. That
#     launcher delegates to PowerShell under WSL and does no scenario
#     isolation, so it is not reusable here; only the idiom is shared. Worth
#     merging if a third launcher ever appears.
#   - Leaves build/mame-debug/ in place between runs, so the boot disk copy
#     and generated development disk are reused. Delete the directory to
#     start clean.
#   - Drives no input. Reaching a control that needs a click is blocked on
#     #182; keyboard-driven navigation is what the Lua scenarios use.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

MODE="${1:-}"
APPL="${2:-}"
W0="${3:-}"
W1="${4:-}"
LINK="${5:-}"
BASE="${6:-}"

if [ "$MODE" != "find" ] && [ "$MODE" != "attach" ]; then
  echo "usage: $0 find|attach <appl.bin> <word0> <word1> <link> [base]" >&2
  exit 2
fi
if [ -z "$APPL" ] || [ -z "$W0" ] || [ -z "$W1" ] || [ -z "$LINK" ]; then
  echo "usage: $0 find|attach <appl.bin> <word0> <word1> <link> [base]" >&2
  exit 2
fi
if [ "$MODE" = "attach" ] && [ -z "$BASE" ]; then
  echo "attach needs the base from a previous 'find' run" >&2
  exit 2
fi

ENV_FILE="${MAME_ENV_FILE:-$PROJECT_DIR/.env-mame}"
if [ -f "$ENV_FILE" ]; then
  set -a
  # shellcheck source=/dev/null
  . "$ENV_FILE"
  set +a
fi
: "${MAME_EXECUTABLE:?set MAME_EXECUTABLE in .env-mame}"
: "${MAME_HDA:?set MAME_HDA in .env-mame}"
MACHINE="${MAME_MACHINE:-maciix}"
RAMSIZE="${MAME_RAMSIZE:-8M}"
PORT="${MAME_DEBUG_PORT:-23946}"

WORK="$PROJECT_DIR/build/mame-debug"
mkdir -p "$WORK/home"

# On WSL the emulator is a Windows binary: it cannot read a script through a
# \\wsl.localhost path, and it needs Windows-shaped paths. Stage what it reads
# next to the disks and translate.
IS_WSL=0
if [ -n "${WSL_INTEROP:-}" ]; then IS_WSL=1; fi
winpath() { if [ "$IS_WSL" = "1" ]; then wslpath -w "$1"; else printf '%s' "$1"; fi; }

# .env-mame holds Windows paths on a Windows host; the shell needs the other
# form to copy files and to exec the emulator. Same idiom as mame-dev-disk.sh.
normalize_host_path() {
  local path="$1"
  if [[ "$path" =~ ^[A-Za-z]:\\ ]] && command -v wslpath >/dev/null 2>&1; then
    wslpath -u "$path"
  else
    printf '%s' "$path"
  fi
}
MAME_EXECUTABLE="$(normalize_host_path "$MAME_EXECUTABLE")"
MAME_HDA="$(normalize_host_path "$MAME_HDA")"

cp -f "$SCRIPT_DIR/mame-find-base.lua" "$WORK/mame-find-base.lua"
cp -f "$PROJECT_DIR/tests/toolbox/mame-keys.lua" "$WORK/mame-keys.lua"

# A scenario-local copy of the boot disk; the configured one stays an input.
BOOT="$WORK/Boot.hd"
[ -f "$BOOT" ] || cp -f "$MAME_HDA" "$BOOT"
# Unconditional: cp reproduces the template's mode and the template is
# deliberately read-only, and a copy an earlier run left behind can be
# read-only too. MAME writing to it is the reason it is a copy; normalising the
# mode leaves the persisted session state untouched.
chmod u+w "$BOOT"

DEV="$WORK/LokaDev.hd"
# LOKA_DEV_DATA carries one plain data file per non-empty line. Each path
# reaches mame-dev-disk.sh as its own argument even with spaces or glob
# characters. Data-driven apps read these files from beside themselves
# (ScrapbookUI's ASSETS.LRP) and refuse before the code under debug without them.
DEV_DATA=()
if [ -n "${LOKA_DEV_DATA:-}" ]; then
  while IFS= read -r f; do
    [ -n "$f" ] && DEV_DATA+=("$f")
  done <<< "$LOKA_DEV_DATA"
fi
MAME_DEV_HDA="$DEV" "$SCRIPT_DIR/mame-dev-disk.sh" "$APPL" \
  ${DEV_DATA[@]+"${DEV_DATA[@]}"} >/dev/null

LOG="$WORK/find-base.log"
rm -f "$LOG"

export LOKA_PATTERN_WORD0="$W0" LOKA_PATTERN_WORD1="$W1" LOKA_PATTERN_LINK="$LINK"
export LOKA_GDB_LOG; LOKA_GDB_LOG="$(winpath "$LOG")"
FORWARD="LOKA_PATTERN_WORD0:LOKA_PATTERN_WORD1:LOKA_PATTERN_LINK:LOKA_GDB_LOG"
if [ -n "${LOKA_TAB_COUNT:-}" ]; then
  export LOKA_TAB_COUNT
  FORWARD="$FORWARD:LOKA_TAB_COUNT"
fi
if [ "$MODE" = "attach" ]; then
  export LOKA_STAY_ALIVE=1
  FORWARD="$FORWARD:LOKA_STAY_ALIVE"
fi
# WSL hands nothing to a Windows process unless it is named here.
export WSLENV="${WSLENV:+$WSLENV:}$FORWARD"

ARGS=(
  "$MACHINE" -ramsize "$RAMSIZE"
  -homepath "$(winpath "$WORK/home")"
  -cfg_directory "$(winpath "$WORK/home/cfg")"
  -nvram_directory "$(winpath "$WORK/home/nvram")"
  -snapshot_directory "$(winpath "$WORK/home/snap")"
  -diff_directory "$(winpath "$WORK/home/diff")"
  -hard1 "$(winpath "$BOOT")"
  -scsi:5 harddisk -hard2 "$(winpath "$DEV")"
  -video none -sound none -nothrottle -natural -skip_gameinfo
  -autoboot_delay 1 -autoboot_script "$(winpath "$WORK/mame-find-base.lua")"
)
[ -n "${MAME_ROMPATH:-}" ] && ARGS+=(-rompath "$MAME_ROMPATH")

if [ "$MODE" = "find" ]; then
  "$MAME_EXECUTABLE" "${ARGS[@]}" >"$WORK/mame.out" 2>&1 || true
  grep -E "BASE|NOT found" "$LOG" || { echo "no result; see $LOG and $WORK/mame.out" >&2; exit 1; }
  exit 0
fi

# Current MAME binds the gdbstub to 127.0.0.1 by default (the older builds
# this rig was proven on listened on every interface), and WSL2 NAT cannot
# reach the Windows loopback -- the attach times out with the listener
# visibly LISTENING. Only in that mode is the Windows host the WSL default
# gateway (the vEthernet address), so only there is the gateway a valid
# bind address. WSL1 and WSL2 mirrored networking share the Windows
# loopback, and their default route is the LAN router, which Windows does
# not own as a bind address -- keep the loopback default for them. wslinfo
# is absent on WSL1; any failure therefore reads as "not NAT".
# MAME_DEBUG_HOST overrides the derivation entirely.
STUB_HOST="${MAME_DEBUG_HOST:-}"
if [ -z "$STUB_HOST" ] && [ "$IS_WSL" = "1" ]; then
  if [ "$(wslinfo --networking-mode 2>/dev/null)" = "nat" ]; then
    STUB_HOST="$(ip route 2>/dev/null | awk '/^default/{print $3; exit}')"
  fi
fi
STUB_HOST="${STUB_HOST:-127.0.0.1}"
# The stub is unauthenticated remote control of the emulated machine; a
# wildcard bind would expose it on every interface, which the docs promise
# never happens -- including through the override door.
case "$STUB_HOST" in
  0.0.0.0 | ::)
    echo "refusing to bind the gdbstub to every interface ($STUB_HOST); set MAME_DEBUG_HOST to one address" >&2
    exit 2
    ;;
esac
ARGS+=(-debug -debugger gdbstub -debugger_host "$STUB_HOST" -debugger_port "$PORT")
"$MAME_EXECUTABLE" "${ARGS[@]}" >"$WORK/mame.out" 2>&1 &
MAME_PID=$!
# The stub refuses a second connection (#182), so a MAME that outlives its
# gdb session can only squat on the debug port and break the next run. Reap
# it on every exit path: a scripted quit, an interactive quit, and the
# missing-ELF bail-out below.
cleanup() { kill "$MAME_PID" 2>/dev/null || true; wait "$MAME_PID" 2>/dev/null || true; }
trap cleanup EXIT

# Wait for the listener by reading socket state. Opening a connection here
# would consume the stub's single slot; attaching before it is up fails with
# "Connection reset by peer".
echo "waiting for the gdbstub on port $PORT" >&2
for _ in $(seq 120); do
  if [ "$IS_WSL" = "1" ]; then
    netstat.exe -an 2>/dev/null | grep -q "$PORT.*LISTENING" && break
  else
    ss -ltn 2>/dev/null | grep -q ":$PORT" && break
  fi
  sleep 1
done
sleep 4

ELF="${APPL%.bin}.code.bin.gdb"
[ -f "$ELF" ] || { echo "no debug ELF beside $APPL (build with the retro68-68k-dwarf preset)" >&2; exit 1; }

echo "attaching gdb; symbols at $BASE" >&2
# LOKA_GDB_SCRIPT appends a second command file after the attach script, so
# a scripted (non-interactive) session can plant its own breakpoints and quit.
# Not exec: this shell must survive gdb to reap the emulator it started.
# Hand gdb the exact address the stub was bound to, so the two sides can
# never derive different hosts (mame-attach.gdb's own gateway fallback stays
# for sessions attached by hand).
LOKA_ELF="$ELF" LOKA_BASE="$BASE" LOKA_GDB_PORT="$PORT" LOKA_GDB_HOST="$STUB_HOST" \
  gdb-multiarch -q -x "$SCRIPT_DIR/mame-attach.gdb" ${LOKA_GDB_SCRIPT:+-x "$LOKA_GDB_SCRIPT"}
