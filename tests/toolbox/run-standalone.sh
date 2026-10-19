#!/usr/bin/env bash
# Run a self-quitting Classic MacBinary app; preserve LOG.TXT bytes for audit.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
usage() {
  echo "Usage: $0 <app .bin> [extra plain files…] [--expect <audit file>]" >&2
  exit 2
}
fail() { echo "Standalone run failed: $*" >&2; exit 1; }
[ "$#" -gt 0 ] || usage
APP="$1"; shift
[[ "$APP" = *.bin ]] && [ -f "$APP" ] || usage
APP_NAME="$(basename "$APP" .bin)"
[[ "$APP_NAME" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || usage
EXTRA_FILES=()
EXPECTED=""
# HFS names are case insensitive. Refuse collisions so staged item count and
# Finder navigation agree, and never seed the output file from an input.
NAMES=("$(printf '%s' "$APP_NAME" | tr '[:upper:]' '[:lower:]')" log.txt)
while [ "$#" -gt 0 ]; do
  case "$1" in
    --expect)
      [ "$#" -eq 2 ] && [ -f "$2" ] || usage
      EXPECTED="$2"
      break ;;
    -*) usage ;;
    *)
      [ -f "$1" ] || usage
      name="$(basename "$1")"
      [ "${#name}" -le 31 ] && [[ "$name" != *:* ]] || usage
      folded_name="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
      for existing in "${NAMES[@]}"; do
        [ "$folded_name" != "$existing" ] || usage
      done
      NAMES+=("$folded_name")
      EXTRA_FILES+=("$1") ;;
  esac
  shift
done

. "$PROJECT_DIR/scripts/retro68-env.sh"
loka_load_retro68_environment "$PROJECT_DIR"
ENV_FILE="${MAME_ENV_FILE:-$PROJECT_DIR/.env-mame}"
if [ -f "$ENV_FILE" ]; then
  loka_import_environment_file "$ENV_FILE"
fi
normalize_host_path() {
  if [[ "$1" =~ ^[A-Za-z]:\\ ]] && command -v wslpath >/dev/null 2>&1; then
    wslpath -u "$1"
  else
    printf '%s' "$1"
  fi
}
winpath() {
  if [ -n "${WSL_INTEROP:-}" ]; then wslpath -w "$1"; else printf '%s' "$1"; fi
}
find_retro68_tool() {
  local name="$1" candidate
  if [ -n "${RETRO68_TOOLCHAIN_BIN:-}" ] && [ -x "$RETRO68_TOOLCHAIN_BIN/$name" ]; then
    printf '%s\n' "$RETRO68_TOOLCHAIN_BIN/$name"; return
  fi
  if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return; fi
  for candidate in "${RETRO68_BUILD_DIR:-$HOME/Retro68-build}/toolchain/bin/$name" \
      "$HOME/Retro68-build/toolchain/bin/$name" \
      "$HOME/Documents/Projects/Retro68-build/toolchain/bin/$name"; do
    if [ -x "$candidate" ]; then printf '%s\n' "$candidate"; return; fi
  done
  fail "Retro68 tool not found: $name"
}
MAME_EXECUTABLE="$(normalize_host_path "${MAME_EXECUTABLE:-mame}")"
MAME_HDA="$(normalize_host_path "${MAME_HDA:-}")"
[ -f "$MAME_HDA" ] || fail 'MAME_HDA must point to the boot template'
# Finder Tab order is a per-disk fact (run-scenario.sh records it per cell: an
# app plus LokaTest.cfg needs 2 Tabs for HelloWorld but 3 for Tutorial). Only
# the app alone is known to need one; with extra files the caller states it.
if [ -z "${LOKA_TAB_COUNT:-}" ]; then
  [ "${#EXTRA_FILES[@]}" -eq 0 ] \
    || fail 'extra files change the Finder Tab order; set LOKA_TAB_COUNT for this disk'
  LOKA_TAB_COUNT=1
fi
export LOKA_TAB_COUNT
export LOKA_RUN_WAIT="${LOKA_RUN_WAIT:-90}"
[[ "$LOKA_TAB_COUNT" =~ ^[1-9][0-9]*$ ]] || usage
[[ "$LOKA_RUN_WAIT" =~ ^[1-9][0-9]*$ ]] || usage
HMOUNT="$(find_retro68_tool hmount)"
HCOPY="$(find_retro68_tool hcopy)"
HUMOUNT="$(find_retro68_tool humount)"

WORK="$PROJECT_DIR/build/mame-standalone/$APP_NAME"
rm -rf "$WORK"
mkdir -p "$WORK"/{home,cfg,nvram,snapshot,diff,hfs-home,hfs-ctl}
BOOT="$WORK/Boot.hd"
DEV="$WORK/LokaDev.hd"
. "$PROJECT_DIR/scripts/mame-boot-copy.sh"
loka_prepare_boot_copy "$MAME_HDA" "$BOOT"
MAME_DEV_HDA="$DEV" MAME_CONTROL_DIR="$WORK/hfs-ctl" \
  "$PROJECT_DIR/scripts/mame-dev-disk.sh" "$APP" "${EXTRA_FILES[@]}" \
  >"$WORK/dev-disk.out" 2>&1 || fail "dev disk creation; see $WORK/dev-disk.out"

export LOKA_STANDALONE=1
export LOKA_SNAP_LOG; LOKA_SNAP_LOG="$(winpath "$WORK/launch.log")"
export WSLENV="${WSLENV:+$WSLENV:}LOKA_SNAP_LOG:LOKA_TAB_COUNT:LOKA_RUN_WAIT:LOKA_STANDALONE:LOKA_LAUNCH_WAIT:LOKA_PROBE_CLICKS"
ARGS=(
  "${MAME_MACHINE:-maciix}" -ramsize "${MAME_RAMSIZE:-8M}"
  -homepath "$(winpath "$WORK/home")"
  -cfg_directory "$(winpath "$WORK/cfg")"
  -nvram_directory "$(winpath "$WORK/nvram")"
  -snapshot_directory "$(winpath "$WORK/snapshot")"
  -diff_directory "$(winpath "$WORK/diff")"
  -hard1 "$(winpath "$BOOT")"
  -scsi:5 harddisk -hard2 "$(winpath "$DEV")"
  -video none -sound none -nothrottle -natural -skip_gameinfo
  -autoboot_delay 1 -autoboot_script "$(winpath "$SCRIPT_DIR/mame-launch.lua")"
)
if [ -n "${MAME_ROMPATH:-}" ]; then ARGS+=(-rompath "$MAME_ROMPATH"); fi
if timeout 600 "$MAME_EXECUTABLE" "${ARGS[@]}" >"$WORK/mame.out" 2>&1; then
  :
else
  status=$?
  fail "MAME exited with status $status; see $WORK/mame.out"
fi
# A Lua error can leave MAME exiting successfully without reaching the wait.
# Windows MAME writes the Lua log with CRLF line ends.
[ -f "$WORK/launch.log" ] && tr -d '\r' < "$WORK/launch.log" | grep -qx 'LOKA-SNAP: standalone wait complete' \
  || fail "standalone wait did not complete; see $WORK/launch.log and $WORK/mame.out"

HOME="$WORK/hfs-home" "$HMOUNT" "$DEV" 1 >"$WORK/hmount.out" 2>&1 \
  || fail "could not mount dev disk; see $WORK/hmount.out"
trap 'HOME="$WORK/hfs-home" "$HUMOUNT" >/dev/null 2>&1 || true' EXIT
HOME="$WORK/hfs-home" "$HCOPY" -r :LOG.TXT "$WORK/LOG.TXT" >"$WORK/hcopy.out" 2>&1 \
  || fail "could not copy LOG.TXT; see $WORK/hcopy.out"
HOME="$WORK/hfs-home" "$HUMOUNT" >"$WORK/humount.out" 2>&1 \
  || fail "could not unmount dev disk; see $WORK/humount.out"
trap - EXIT
[ -f "$WORK/LOG.TXT" ] || fail "missing $WORK/LOG.TXT"
echo "=== $APP_NAME LOG.TXT ==="
# Display Classic line endings readably; comparison always uses original bytes.
tr '\r' '\n' <"$WORK/LOG.TXT"
if [ -n "$EXPECTED" ]; then
  cmp "$EXPECTED" "$WORK/LOG.TXT" || fail "LOG.TXT differs from $EXPECTED"
  echo "Standalone audit passed: $APP_NAME"
fi
echo "Run artifacts: $WORK"
