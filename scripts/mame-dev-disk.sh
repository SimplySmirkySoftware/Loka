#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="${MAME_ENV_FILE:-$PROJECT_DIR/.env-mame}"
. "$SCRIPT_DIR/retro68-env.sh"

loka_load_retro68_environment "$PROJECT_DIR"

import_mame_environment() {
  local path="$1"
  loka_import_environment_file "$path"
}

normalize_host_path() {
  local path="$1"
  if [[ "$path" =~ ^[A-Za-z]:\\ ]] && command -v wslpath >/dev/null 2>&1; then
    wslpath -u "$path"
  else
    printf '%s' "$path"
  fi
}

# Caller-provided values outrank the ones in .env-mame: launchers
# (mame-debug.sh, tests/toolbox/run-scenario.sh) pass a scenario-local
# MAME_DEV_HDA -- and a scenario-local MAME_CONTROL_DIR, whose hfsutils
# subdirectory holds the mounted-volume state that concurrent builders
# must not share -- and re-importing the file must not redirect either.
PRESET_DEV_HDA="${MAME_DEV_HDA:-}"
PRESET_CONTROL_DIR="${MAME_CONTROL_DIR:-}"
if [ -f "$ENV_FILE" ]; then
  import_mame_environment "$ENV_FILE"
fi
if [ -n "$PRESET_DEV_HDA" ]; then
  MAME_DEV_HDA="$PRESET_DEV_HDA"
fi
if [ -n "$PRESET_CONTROL_DIR" ]; then
  MAME_CONTROL_DIR="$PRESET_CONTROL_DIR"
fi

if [ $# -lt 1 ]; then
  echo "Usage: $0 --all | --all-loops | --all-flows | <Retro68-MacBinary-file> [plain-data-file ...]" >&2
  exit 2
fi

case "${LOKA_MAME_CPU:-68k}" in
  68k) cpu=68k; suffix=68K ;;
  ppc) cpu=ppc; suffix=PPC ;;
  *) echo "Invalid LOKA_MAME_CPU: expected 68k or ppc" >&2; exit 2 ;;
esac
MACBINARY_PATHS=()
# Keep the batch selection aligned with mame-dev-disk.ps1.
if [ "$1" = "--all-loops" ] || [ "$1" = "--all-flows" ]; then
  [ "$#" -eq 1 ] || exit 2
  family=Loop
  [ "$1" != "--all-flows" ] || family=Flow
  batch_root="$PROJECT_DIR/build/retro68/${cpu}/Standalone/Release/tests/toolbox"
  for app in Scrapbook Hello Tutorial Mine Floppy; do
    MACBINARY_PATHS+=("$batch_root/Loka${app}Standalone${family}${suffix}.bin")
  done
  set -- "$batch_root/ASSETS.LRP"
elif [ "$1" = "--all" ]; then
  [ "$#" -eq 1 ] || exit 2
  for app in HelloWorld/LokaHello MineSweeper/LokaMine SimpleViewer/LokaSimpleViewer SimpleText/LokaSimpleText FloppyBird/LokaFloppyBird SmirkBench/LokaSmirkBench LazyList/LokaLazyList Tutorial/LokaTutorial ScrapbookUI/ScrapbookUI SmirkyCard/LokaSmirkyCard; do
    MACBINARY_PATHS+=("$PROJECT_DIR/build/retro68/${cpu}/Release/example/${app}${suffix}.bin")
  done
  set -- "$PROJECT_DIR/build/retro68/${cpu}/Release/example/ScrapbookUI/ASSETS.LRP"
  # Manifest readers in mame-*-disk*, release/classic-disks.py, the Win32/macOS
  # release stages and SmirkyCard/CMakeLists.txt stay parallel.
  while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
    case "$script" in ''|\#*) continue ;; esac
    set -- "$@" "$PROJECT_DIR/example/SmirkyCard/$script"
  done < "$PROJECT_DIR/example/SmirkyCard/disk-scripts.txt"
  # hfs_copy_data stages these canonical asset paths in Pictures/.
  while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
    case "$script" in ''|\#*) continue ;; esac
    set -- "$@" "$PROJECT_DIR/example/SmirkyCard/assets/$script.PICT"
  done < "$PROJECT_DIR/example/SmirkyCard/disk-pictures.txt"
else
  MACBINARY_PATHS+=("$1")
  shift
fi
MAME_HDA="${MAME_HDA:-}"
MAME_HOMEPATH="${MAME_HOMEPATH:-$HOME/.mame}"
MAME_CONTROL_DIR="${MAME_CONTROL_DIR:-$MAME_HOMEPATH/loka}"
MAME_DEV_HDA="${MAME_DEV_HDA:-$PROJECT_DIR/build/mame-dev/LokaDev.hd}"
MAME_HDA="$(normalize_host_path "$MAME_HDA")"
MAME_HOMEPATH="$(normalize_host_path "$MAME_HOMEPATH")"
MAME_CONTROL_DIR="$(normalize_host_path "$MAME_CONTROL_DIR")"
MAME_DEV_HDA="$(normalize_host_path "$MAME_DEV_HDA")"

for binary in "${MACBINARY_PATHS[@]}"; do
  if [ ! -f "$binary" ]; then
    echo "Error: Retro68 MacBinary file not found: $binary" >&2
    exit 1
  fi
done
for plain_data_path in "$@"; do
  if [ ! -f "$plain_data_path" ]; then
    echo "Error: plain data file not found: $plain_data_path" >&2
    exit 1
  fi
done
if [ -z "$MAME_HDA" ] || [ ! -f "$MAME_HDA" ]; then
  echo "Error: MAME_HDA must point to the boot hard disk template" >&2
  exit 1
fi

. "$SCRIPT_DIR/hfs-disk.sh"

HFORMAT="$(find_retro68_tool hformat)"
HCOPY="$(find_retro68_tool hcopy)"
HUMOUNT="$(find_retro68_tool humount)"
HFS_HOME="$MAME_CONTROL_DIR/hfsutils"
TEMPORARY_DISK="$MAME_DEV_HDA.$$"

mkdir -p "$(dirname "$MAME_DEV_HDA")" "$HFS_HOME"
cp "$MAME_HDA" "$TEMPORARY_DISK"
# hformat opens the image read-write; cp reproduced the template's mode and the
# template is deliberately read-only.
chmod u+w "$TEMPORARY_DISK"

cleanup() {
  HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true
  rm -f "$TEMPORARY_DISK"
}
trap cleanup EXIT

hfs_populate "$HFS_HOME" LokaDev "$TEMPORARY_DISK" 1 \
  "${#MACBINARY_PATHS[@]}" "${MACBINARY_PATHS[@]}" "$@"
mv -f "$TEMPORARY_DISK" "$MAME_DEV_HDA"
trap - EXIT

echo "Prepared SCSI development disk: $MAME_DEV_HDA"
