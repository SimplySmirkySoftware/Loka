#!/usr/bin/env bash
# Three fresh maciix launches of one 1a-0 measurement vehicle. No golden rebake.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
case "${1:-}" in
  floppybird) APP=LokaMeasureFloppyBird68K; DIRECTORY=tests/toolbox ;;
  smirkbench) APP=LokaMeasureSmirkBench68K; DIRECTORY=tests/toolbox ;;
  tutorial) APP=LokaMeasureTutorial68K; DIRECTORY=tests/toolbox ;;
  main) APP=LokaMeasureMain68K; DIRECTORY=example/SmirkyCard ;;
  *) echo 'Usage: measure-toolbar.sh {floppybird|smirkbench|tutorial|main}' >&2; exit 2 ;;
esac
. "$ROOT/scripts/retro68-env.sh"
loka_load_retro68_environment "$ROOT"
if [ -f "${MAME_ENV_FILE:-$ROOT/.env-mame}" ]; then
  loka_import_environment_file "${MAME_ENV_FILE:-$ROOT/.env-mame}"
fi
export MAME_MACHINE=maciix
export LOKA_RUN_WAIT="${LOKA_RUN_WAIT:-180}"
MAME="${MAME_EXECUTABLE:-mame}"
if [[ "$MAME" =~ ^[A-Za-z]:\\ ]] && command -v wslpath >/dev/null 2>&1; then MAME="$(wslpath -u "$MAME")"; fi
BUILD="$ROOT/build/retro68/68k/Release"
BINARY="$BUILD/$DIRECTORY/$APP.bin"
DEST="$ROOT/build/toolbar-measurement/$1"
mkdir -p "$DEST"
"$MAME" -version > "$DEST/emulator.txt"
ROM_ARGS=()
if [ -n "${MAME_ROMPATH:-}" ]; then ROM_ARGS=(-rompath "$MAME_ROMPATH"); fi
"$MAME" maciix "${ROM_ARGS[@]}" -listroms > "$DEST/roms.txt"
"$MAME" maciix "${ROM_ARGS[@]}" -verifyroms > "$DEST/rom-verification.txt"
python3 "$ROOT/tests/toolbox/toolbar-measurement-report.py" provenance "$BUILD" "$BINARY" "$DEST"
for run in 1 2 3; do
  # Configuration is already exported; do not let the child re-import a different machine or wait.
  MAME_ENV_FILE=/dev/null bash "$ROOT/tests/toolbox/run-standalone.sh" "$BINARY" > "$DEST/run-$run.txt" 2>&1
  cp "$ROOT/build/mame-standalone/$APP/LOG.TXT" "$DEST/run-$run.log"
  python3 "$ROOT/tests/toolbox/toolbar-measurement-report.py" verdict "$DEST/run-$run.log"
done
