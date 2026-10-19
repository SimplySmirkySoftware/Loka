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

PRESET_BOOT_HDA="${MAME_BOOT_HDA:-}"
PRESET_CONTROL_DIR="${MAME_CONTROL_DIR:-}"
if [ -f "$ENV_FILE" ]; then
  import_mame_environment "$ENV_FILE"
fi
if [ -n "$PRESET_BOOT_HDA" ]; then
  MAME_BOOT_HDA="$PRESET_BOOT_HDA"
fi
if [ -n "$PRESET_CONTROL_DIR" ]; then
  MAME_CONTROL_DIR="$PRESET_CONTROL_DIR"
fi

MAME_MACHINE="${MAME_MACHINE:-macplus}"
MAME_HDA="${MAME_HDA:-}"
MAME_HOMEPATH="${MAME_HOMEPATH:-$HOME/.mame}"
MAME_CONTROL_DIR="${MAME_CONTROL_DIR:-$MAME_HOMEPATH/loka}"
MAME_BOOT_HDA="${MAME_BOOT_HDA:-$PROJECT_DIR/build/mame-run/$MAME_MACHINE/Boot.hd}"
MAME_HDA="$(normalize_host_path "$MAME_HDA")"
MAME_HOMEPATH="$(normalize_host_path "$MAME_HOMEPATH")"
MAME_CONTROL_DIR="$(normalize_host_path "$MAME_CONTROL_DIR")"
MAME_BOOT_HDA="$(normalize_host_path "$MAME_BOOT_HDA")"

# shellcheck source=mame-boot-copy.sh
. "$SCRIPT_DIR/mame-boot-copy.sh"

STAGE_ALL=0
MACBINARY_PATHS=()
PLAIN_DATA_PATHS=()

if [ $# -ge 1 ] && { [ "$1" = "--all" ] || [ "$1" = "-a" ]; }; then
  STAGE_ALL=1
  RELEASE_BUILD_ROOT="$PROJECT_DIR/build/retro68/68k/Release/example"
  ALL_APPS=(
    "$RELEASE_BUILD_ROOT/HelloWorld/LokaHello68K.bin"
    "$RELEASE_BUILD_ROOT/MineSweeper/LokaMine68K.bin"
    "$RELEASE_BUILD_ROOT/SimpleViewer/LokaSimpleViewer68K.bin"
    "$RELEASE_BUILD_ROOT/SimpleText/LokaSimpleText68K.bin"
    "$RELEASE_BUILD_ROOT/FloppyBird/LokaFloppyBird68K.bin"
    "$RELEASE_BUILD_ROOT/SmirkBench/LokaSmirkBench68K.bin"
    "$RELEASE_BUILD_ROOT/LazyList/LokaLazyList68K.bin"
    "$RELEASE_BUILD_ROOT/Tutorial/LokaTutorial68K.bin"
    "$RELEASE_BUILD_ROOT/ScrapbookUI/ScrapbookUI68K.bin"
    "$RELEASE_BUILD_ROOT/SmirkyCard/LokaSmirkyCard68K.bin"
  )
  for app in "${ALL_APPS[@]}"; do
    if [ ! -f "$app" ]; then
      echo "Error: Application binary not found: $app (build Retro68 68K first)" >&2
      exit 1
    fi
    MACBINARY_PATHS+=("$app")
  done
  SCRAPBOOK_ASSETS="$PROJECT_DIR/example/ScrapbookUI/ASSETS.LRP"
  if [ -f "$SCRAPBOOK_ASSETS" ]; then
    PLAIN_DATA_PATHS+=("$SCRAPBOOK_ASSETS")
  fi
  # Manifest readers in mame-*-disk*, release/classic-disks.py, the Win32/macOS
  # release stages and SmirkyCard/CMakeLists.txt stay parallel.
  while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
    case "$script" in ''|\#*) continue ;; esac
    smirkycard_script="$PROJECT_DIR/example/SmirkyCard/$script"
    if [ -f "$smirkycard_script" ]; then
      PLAIN_DATA_PATHS+=("$smirkycard_script")
    fi
  done < "$PROJECT_DIR/example/SmirkyCard/disk-scripts.txt"
  # Same grammar as the adjacent script manifest; pictures are data forks.
  while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
    case "$script" in ''|\#*) continue ;; esac
    smirkycard_script="$PROJECT_DIR/example/SmirkyCard/assets/$script.PICT"
    if [ -f "$smirkycard_script" ]; then
      PLAIN_DATA_PATHS+=("$smirkycard_script")
    fi
  done < "$PROJECT_DIR/example/SmirkyCard/disk-pictures.txt"
elif [ $# -ge 1 ]; then
  MACBINARY_PATHS+=("$1")
  shift
  for plain_data_path in "$@"; do
    if [ ! -f "$plain_data_path" ]; then
      echo "Error: plain data file not found: $plain_data_path" >&2
      exit 1
    fi
    PLAIN_DATA_PATHS+=("$plain_data_path")
  done
else
  echo "Usage: $0 --all | <Retro68-MacBinary-file> [plain-data-file ...]" >&2
  exit 2
fi

for bin in "${MACBINARY_PATHS[@]}"; do
  if [ ! -f "$bin" ]; then
    echo "Error: Retro68 MacBinary file not found: $bin" >&2
    exit 1
  fi
done

. "$SCRIPT_DIR/hfs-disk.sh"

HMOUNT="$(find_retro68_tool hmount)"
HCOPY="$(find_retro68_tool hcopy)"
HLS="$(find_retro68_tool hls)"
HMKDIR="$(find_retro68_tool hmkdir)"
HUMOUNT="$(find_retro68_tool humount)"
HFS_HOME="$MAME_CONTROL_DIR/hfsutils"

mkdir -p "$(dirname "$MAME_BOOT_HDA")" "$HFS_HOME"
loka_prepare_boot_copy "$MAME_HDA" "$MAME_BOOT_HDA"

STAGING="$MAME_BOOT_HDA.staging"
if [ -e "$STAGING" ]; then
  echo 'staging: failed (destination needs attention)' >&2
  exit 1
fi
MOUNTED=0
cleanup_staging() {
  status=$?
  trap - EXIT
  if [ "$MOUNTED" -eq 1 ]; then HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true; fi
  if [ "$status" -ne 0 ]; then
    rm -f "$STAGING"
    echo 'staging: failed (previous disk kept)' >&2
  fi
  exit "$status"
}
trap cleanup_staging EXIT
cp -f "$MAME_BOOT_HDA" "$STAGING"
chmod u+w "$STAGING"

MOUNTED=1
HOME="$HFS_HOME" "$HMOUNT" "$STAGING"

DEST_DIR=":Loka:"
if ! HOME="$HFS_HOME" "$HLS" -d :Loka >/dev/null 2>&1; then
  HOME="$HFS_HOME" "$HMKDIR" :Loka
fi

for bin in "${MACBINARY_PATHS[@]}"; do
  HOME="$HFS_HOME" "$HCOPY" -m "$bin" "$DEST_DIR"
done

# bash 3.2 (macOS) treats an empty array expansion as unbound under set -u.
hfs_copy_data "$HFS_HOME" "$DEST_DIR" ${PLAIN_DATA_PATHS[@]+"${PLAIN_DATA_PATHS[@]}"}

HOME="$HFS_HOME" "$HUMOUNT"
MOUNTED=0
mv -f "$STAGING" "$MAME_BOOT_HDA"
trap - EXIT

if [ "$STAGE_ALL" -eq 1 ]; then
  echo "Staged all 68K applications to boot disk ($DEST_DIR): $MAME_BOOT_HDA"
else
  echo "Staged to boot disk ($DEST_DIR): $MAME_BOOT_HDA"
fi
