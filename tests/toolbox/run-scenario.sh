#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
. "$PROJECT_DIR/scripts/retro68-env.sh"

loka_load_retro68_environment "$PROJECT_DIR"

usage() {
  echo "Usage: $0 <example> <scenario from scenarios.txt> [--stage-last | --update-golden | --probe | --structural-audit]" >&2
}

fail_stage() {
  local stage="$1"
  shift
  echo "$stage stage failed: $*" >&2
  echo "Work directory left for inspection: $WORK" >&2
  exit 1
}

find_retro68_tool() {
  local name="$1"
  local candidate

  if [ -n "${RETRO68_TOOLCHAIN_BIN:-}" ] \
    && [ -x "$RETRO68_TOOLCHAIN_BIN/$name" ]; then
    echo "$RETRO68_TOOLCHAIN_BIN/$name"
    return
  fi
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return
  fi
  if [ -n "${RETRO68_BUILD_DIR:-}" ] \
    && [ -x "$RETRO68_BUILD_DIR/toolchain/bin/$name" ]; then
    echo "$RETRO68_BUILD_DIR/toolchain/bin/$name"
    return
  fi
  for candidate in \
    "$HOME/Retro68-build/toolchain/bin/$name" \
    "$HOME/Documents/Projects/Retro68-build/toolchain/bin/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return
    fi
  done
  echo "Retro68 tool not found: $name" >&2
  return 1
}


if [ $# -gt 3 ]; then
  for argument in "${@:3}"; do
    if [ "$argument" = --stage-last ]; then
      WORK="$PROJECT_DIR/build/mame-scenario/$1/$2"
      fail_stage arguments "--stage-last cannot be combined with other flags"
    fi
  done
fi
if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  usage
  exit 2
fi

EXAMPLE="$1"
SCENARIO="$2"
SCENARIO_REGISTRY="$PROJECT_DIR/tests/scenarios/scenarios.txt"
STARTUP_IDENTITY_DECLARATIONS="$PROJECT_DIR/tests/scenarios/startup-golden-identities.txt"
STAGE_LAST=0
UPDATE_GOLDEN=0
STRUCTURAL_AUDIT=0
if [[ ! "$EXAMPLE" =~ ^[a-z0-9][a-z0-9-]*$ ]] \
  || [[ ! "$SCENARIO" =~ ^[a-z0-9][a-z0-9-]*$ ]] \
  || ! grep -Fxq -- "$EXAMPLE $SCENARIO" "$SCENARIO_REGISTRY"; then
  usage
  exit 2
fi
PROBE=0
# Cells with a probe leg in their scenario driver. --probe on any other cell
# would write the key, have no reader, and print a green that verified
# nothing -- a silent skip indistinguishable from coverage.
PROBE_CELLS="helloworld bmi-roundtrip"
if [ $# -eq 3 ]; then
  case "$3" in
    --stage-last) STAGE_LAST=1 ;;
    --update-golden) UPDATE_GOLDEN=1 ;;
    --structural-audit) STRUCTURAL_AUDIT=1 ;;
    --probe)
      if ! printf '%s\n' "$PROBE_CELLS" | grep -Fxq -- "$EXAMPLE $SCENARIO"; then
        echo "no probe leg exists for '$EXAMPLE $SCENARIO'; probe cells: $PROBE_CELLS" >&2
        exit 2
      fi
      PROBE=1
      ;;
    *)
      usage
      exit 2
      ;;
  esac
fi

WORK="$PROJECT_DIR/build/mame-scenario/$EXAMPLE/$SCENARIO"
if [ "$STAGE_LAST" -eq 0 ]; then
  if [ -d "$WORK" ]; then
    if ! rm -rf "$WORK"; then
      fail_stage mame "could not wipe the previous work directory"
    fi
  fi
  if ! mkdir -p "$WORK"; then
    fail_stage mame "could not create the work directory"
  fi

fi

ENV_FILE="${MAME_ENV_FILE:-$PROJECT_DIR/.env-mame}"
if [ -f "$ENV_FILE" ]; then
  set -a
  # shellcheck source=/dev/null
  . "$ENV_FILE"
  set +a
fi

case "$EXAMPLE" in
  scrapbook)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaTestsToolbox68K.bin"
    TARGET="LokaTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=1
    ;;
  helloworld)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaHelloWorldTestsToolbox68K.bin"
    TARGET="LokaHelloWorldTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=2
    ;;
  tutorial)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaTutorialTestsToolbox68K.bin"
    TARGET="LokaTutorialTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=3
    ;;
  minesweeper)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaMineSweeperTestsToolbox68K.bin"
    TARGET="LokaMineSweeperTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=4
    FINDER_SETTLE_TIMEOUT=120
    ;;
  simpletext)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaSimpleTextTest68K.bin"
    TARGET="LokaSimpleTextTest68K_APPL"
    FINDER_TAB_COUNT=2
    ;;
  simpleviewer)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaSimpleViewerTestsToolbox68K.bin"
    TARGET="LokaSimpleViewerTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=3
    ;;
  smirkycard)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/smirkycard/LokaSmirkyCardTest68K.bin"
    TARGET="LokaSmirkyCardTest68K_APPL"
    # SmirkyCard is opt-in, so the vehicle exists only in a build configured with it.
    CONFIGURE_FLAGS=" -DLOKA_BUILD_SMIRKYCARD=ON"
    # 41 dev-disk items: application, 39 PICTs, LokaTest.cfg. Tab walks the
    # window alphabetically from the first item, so the count is the
    # application's alphabetical position minus one: the three IBOOKG3*
    # pictures sort before LokaSmirkyCardTest68K (measured on the rig,
    # 2026-10-19; it was 7 = a full cycle when the application sorted first).
    FINDER_TAB_COUNT=3
    ;;
  smirkbench)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaSmirkBenchTestsToolbox68K.bin"
    TARGET="LokaSmirkBenchTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=4
    ;;
  floppybird)
    APPL="$PROJECT_DIR/build/retro68/68k/Release/tests/toolbox/LokaFloppyBirdTestsToolbox68K.bin"
    TARGET="LokaFloppyBirdTestsToolbox68K_APPL"
    FINDER_TAB_COUNT=4
    ;;
  *)
    fail_stage mame "unsupported example '$EXAMPLE'"
    ;;
esac
AUDIT="$WORK/LokaTestsToolbox.audit"
CAPTURE_RECTANGLE="$WORK/LokaTestsToolbox.capture"
EXPECTED_AUDIT="$PROJECT_DIR/tests/scenarios/expected/$EXAMPLE/$SCENARIO.audit"
RAW_IMAGE="$WORK/$SCENARIO.full-screen.png"
ACTUAL_IMAGE="$WORK/$SCENARIO.png"
# Goldens are rig-local, not tracked: the pixels depend on the local boot
# image's System resources (fonts, control chrome) and contain Apple-rendered
# glyphs, which the licensing rule keeps out of the tree. They survive work
# directory wipes and regenerate with --update-golden; reviewers see the
# captures through the pr-assets evidence branch instead.
GOLDEN="$PROJECT_DIR/build/mame-scenario/golden/$EXAMPLE/$SCENARIO.png"
GOLDEN_BUNDLE="$PROJECT_DIR/build/mame-scenario/golden"
GOLDEN_IDENTITY_HELPER="$PROJECT_DIR/scripts/rig/toolbox/classic_golden_identity.py"
PACKAGE_FIXTURE_GUARD="$PROJECT_DIR/scripts/rig/package_fixture_guard.py"
RIG_DESCRIPTOR="$PROJECT_DIR/scripts/rig/toolbox/rigs/toolbox-maciix.ini"
CAPTURE_ADAPTER="${LOKA_TOOLBOX_CAPTURE_ADAPTER:-mame-screen-snapshot.v2}"
BUILD_PROVENANCE="$(dirname "$APPL")/classic-build-provenance.txt"
CURRENT_IDENTITY="$WORK/classic-golden-identity.txt"
MACHINE_VERDICT="$WORK/machine-verdict.txt"
RUN_PROVENANCE="$WORK/scenario-run-provenance.txt"
RUN_PROVENANCE_HELPER="$PROJECT_DIR/scripts/rig/toolbox/scenario_run_provenance.py"
RUN_MODE=capture
if [ "$PROBE" -eq 1 ]; then RUN_MODE=probe; fi
if [ "$STRUCTURAL_AUDIT" -eq 1 ]; then RUN_MODE=structural-audit; fi
PROVENANCE_ARGUMENTS=(
  --provenance "$RUN_PROVENANCE" --application "$APPL"
  --source-tree "$PROJECT_DIR" --registry "$SCENARIO_REGISTRY"
  --capture-adapter "$CAPTURE_ADAPTER" --mode "$RUN_MODE"
)

stage_golden() {
  if ! python3 "$GOLDEN_IDENTITY_HELPER" stage-capture \
      --bundle "$GOLDEN_BUNDLE" \
      --registry "$SCENARIO_REGISTRY" \
      --declarations "$STARTUP_IDENTITY_DECLARATIONS" \
      --descriptor "$RIG_DESCRIPTOR" \
      --current-identity "$CURRENT_IDENTITY" \
      --capture "$ACTUAL_IMAGE" \
      --application "$APPL" \
      --source-tree "$PROJECT_DIR" \
      --example "$EXAMPLE" \
      --scenario "$SCENARIO"; then
    fail_stage golden "could not stage the complete atomic golden bundle"
  fi
  echo "Reminder: attach before/after visual evidence to the PR."
}

if [ "$STAGE_LAST" -eq 1 ]; then
  if ! provenance_message="$(python3 "$RUN_PROVENANCE_HELPER" verify \
      "${PROVENANCE_ARGUMENTS[@]}" --capture "$ACTUAL_IMAGE" \
      --audit "$AUDIT" --expected-audit "$EXPECTED_AUDIT" 2>&1)"; then
    fail_stage provenance "$provenance_message"
  fi
  stage_golden
  exit 0
fi

if [ ! -f "$APPL" ]; then
  fail_stage mame \
    "missing $APPL; build it with: cmake --preset retro68-68k-release${CONFIGURE_FLAGS:-} && cmake --build --preset retro68-68k-release --target $TARGET"
fi
LRPC="$PROJECT_DIR/build/host/lrpc/lrpc"
if [ "$EXAMPLE" = "scrapbook" ] && [ ! -x "$LRPC" ]; then
  fail_stage mame \
    "missing $LRPC; build it with: cmake -S tools/lrpc -B build/host/lrpc && cmake --build build/host/lrpc"
fi
if [ -z "${MAME_EXECUTABLE:-}" ]; then
  fail_stage mame "set MAME_EXECUTABLE in .env-mame"
fi
if [ -z "${MAME_HDA:-}" ]; then
  fail_stage mame "set MAME_HDA in .env-mame"
fi

MACHINE="${MAME_MACHINE:-maciix}"
RAMSIZE="${MAME_RAMSIZE:-8M}"
IS_WSL=0
if [ -n "${WSL_INTEROP:-}" ]; then IS_WSL=1; fi
winpath() { if [ "$IS_WSL" = "1" ]; then wslpath -w "$1"; else printf '%s' "$1"; fi; }

# .env-mame holds Windows paths on a Windows host; the shell needs the other
# form to copy files and to exec the emulator. Same idiom as mame-debug.sh.
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

if [ ! -f "$MAME_HDA" ]; then
  fail_stage mame "boot hard disk template not found: $MAME_HDA"
fi

BOOT="$WORK/Boot.hd"
DEV="$WORK/LokaDev.hd"
CONFIG="$WORK/LokaTest.cfg"
LAUNCHER="$WORK/mame-launch.lua"
MAME_OUT="$WORK/mame.out"
LAUNCH_LOG="$WORK/mame-launch.log"
HOME_DIR="$WORK/home"
CFG_DIR="$WORK/cfg"
NVRAM_DIR="$WORK/nvram"
SNAPSHOT_DIR="$WORK/snapshot"
DIFF_DIR="$WORK/diff"
HFS_HOME="$WORK/hfs-home"
if ! mkdir -p "$HOME_DIR" "$CFG_DIR" "$NVRAM_DIR" "$SNAPSHOT_DIR" "$DIFF_DIR" "$HFS_HOME"; then
  fail_stage mame "could not create scenario-local runtime directories"
fi

if ! cp -f "$MAME_HDA" "$BOOT"; then
  fail_stage mame "could not copy the boot hard disk template"
fi
# cp reproduces the template's mode and the template is deliberately read-only,
# so the copy has to be made writable: the emulator writing to it is the reason
# it is a copy.
if ! chmod u+w "$BOOT"; then
  fail_stage mame "could not make the boot hard disk copy writable"
fi

# Capture the immutable boot-template content before MAME can write to its
# private copy. Build provenance comes from the clean Retro68 build artifact;
# only the emulator/ROM facts are resolved on the host here.
if [ "$STRUCTURAL_AUDIT" -eq 0 ]; then
  if ! identity_message="$(python3 "$GOLDEN_IDENTITY_HELPER" capture-current \
      --output "$CURRENT_IDENTITY" \
      --build-provenance "$BUILD_PROVENANCE" \
      --descriptor "$RIG_DESCRIPTOR" \
      --mame-executable "$MAME_EXECUTABLE" \
      --rompath "${MAME_ROMPATH:-}" \
      --ram-size "$RAMSIZE" \
      --machine "$MACHINE" \
      --capture-adapter "$CAPTURE_ADAPTER" \
      --boot-hd "$BOOT" 2>&1)"; then
    refusal_reason="${identity_message//$'\n'/ }"
    printf 'machine_verdict=failed-or-not-reached\nruntime_verification=failed-or-not-reached\nrefusal_reason=%s\n' \
      "$refusal_reason" >"$MACHINE_VERDICT" || true
    echo "machine_verdict=failed-or-not-reached" >&2
    echo "$identity_message" >&2
    echo "Work directory left for inspection: $WORK" >&2
    exit 3
  fi
fi
# linger_seconds keeps the scenario window alive after the audit is written,
# so the emulator-side snapshot captures the scene instead of the desktop the
# application would otherwise have quit back to.
# --probe opts the driver into the dirty-replay probes (#436). Off by
# default so tracked cells keep their audits byte-identical.
PROBE_LINE=""
if [ "$PROBE" -eq 1 ]; then
  PROBE_LINE="probe_dirty_replay 1
"
fi
if ! printf 'scenario %s\nlinger_seconds 120\n%s' "$SCENARIO" "$PROBE_LINE" >"$CONFIG"; then
  fail_stage mame "could not write LokaTest.cfg"
fi
# The launcher keys any cell-specific native gesture on the cell's name.
export LOKA_SCENARIO_CELL="$EXAMPLE/$SCENARIO"
if ! cp -f "$SCRIPT_DIR/mame-launch.lua" "$LAUNCHER"; then
  fail_stage mame "could not stage mame-launch.lua"
fi

DEV_DISK_ARGUMENTS=("$APPL")
if [ "$EXAMPLE" = smirkycard ]; then
  # Same manifest grammar as scripts/mame-*-disk*: binary data forks, no ASCII check.
  while IFS=$' \t\r' read -r stem || [ -n "$stem" ]; do
    case "$stem" in ''|\#*) continue ;; esac
    DEV_DISK_ARGUMENTS+=("$PROJECT_DIR/example/SmirkyCard/assets/$stem.PICT")
  done < "$PROJECT_DIR/example/SmirkyCard/disk-pictures.txt"
fi
if [ "$EXAMPLE" = simpletext ] && [ "$SCENARIO" != startup ]; then
  README="$PROJECT_DIR/tests/scenarios/fixtures/simpletext/ReadMe"
  [ -f "$README" ] || fail_stage mame "SimpleText fixture not found: $README"
  cp "$README" "$WORK/ReadMe" || fail_stage mame "could not stage SimpleText ReadMe"
  DEV_DISK_ARGUMENTS+=("$WORK/ReadMe")
  FINDER_TAB_COUNT=3
fi
if [ "$EXAMPLE" = "scrapbook" ]; then
  ASSETS="$PROJECT_DIR/example/ScrapbookUI/ASSETS.LRP"
  if [ ! -f "$ASSETS" ]; then
    fail_stage mame "package not found: $ASSETS"
  fi
  STAGED_ASSETS="$WORK/ASSETS.LRP"
  FIXTURE_REGISTRY="$PROJECT_DIR/tests/scenarios/scrapbook-package-fixtures.txt"
  if ! CORRUPT_BAG="$(python3 "$PACKAGE_FIXTURE_GUARD" plan \
      --registry "$FIXTURE_REGISTRY" --scenario "$SCENARIO" 2>&1)"; then
    fail_stage mame "$CORRUPT_BAG"
  fi
  # The refusal message is worth merging into the value, but only on failure:
  # on success anything the interpreter wrote to stderr rides along and would be
  # handed to lrpc as a bag number. The answer shape is empty or decimal.
  if [ -n "$CORRUPT_BAG" ] && [[ ! "$CORRUPT_BAG" =~ ^[0-9]+$ ]]; then
    fail_stage mame "package fixture plan answered '$CORRUPT_BAG', not a bag number"
  fi
  STAGE_ARGUMENTS=(stage "$ASSETS" -o "$STAGED_ASSETS")
  if [ -n "$CORRUPT_BAG" ]; then
    STAGE_ARGUMENTS+=(--corrupt-bag "$CORRUPT_BAG")
  fi
  if ! "$LRPC" "${STAGE_ARGUMENTS[@]}"; then
    fail_stage mame "could not stage the scenario package"
  fi
  if ! fixture_message="$(python3 "$PACKAGE_FIXTURE_GUARD" verify \
      --registry "$FIXTURE_REGISTRY" --scenario "$SCENARIO" \
      --source "$ASSETS" --staged "$STAGED_ASSETS" 2>&1)"; then
    fail_stage mame "$fixture_message"
  fi
  DEV_DISK_ARGUMENTS+=("$STAGED_ASSETS")
fi
if [ "$EXAMPLE" = simpleviewer ] && [ "$SCENARIO" = startup ]; then
  # Two items on the dev disk (application, LokaTest.cfg): the Finder lands on
  # the application after an even number of Tabs, as the other two-item
  # examples do; the picture cells add a third item and need three.
  FINDER_TAB_COUNT=2
fi
if [ "$EXAMPLE" = simpleviewer ] && [ "$SCENARIO" = open-wide-scroll ]; then
  DEV_DISK_ARGUMENTS+=("$PROJECT_DIR/example/SimpleViewer/assets/Wide.pict")
  FINDER_TAB_COUNT=3
fi
if [ "$EXAMPLE" = simpleviewer ] && [ "$SCENARIO" != startup ] \
    && [ "$SCENARIO" != open-wide-scroll ]; then
  case "$SCENARIO" in
    open-sun) PICT_NAMES=(Sun.pict) ;;
    open-bulb) PICT_NAMES=(Bulb.pict) ;;
    churn-replace) PICT_NAMES=(Sun.pict Bulb.pict); FINDER_TAB_COUNT=4 ;;
    *) fail_stage mame "unsupported SimpleViewer image cell '$SCENARIO'" ;;
  esac
  if ! HMOUNT="$(find_retro68_tool hmount)"; then
    fail_stage mame "hmount is unavailable"
  fi
  if ! HCOPY="$(find_retro68_tool hcopy)"; then
    fail_stage mame "hcopy is unavailable"
  fi
  if ! HUMOUNT="$(find_retro68_tool humount)"; then
    fail_stage mame "humount is unavailable"
  fi
  # Read only the pristine template's data fork; isolate hfsutils mount state.
  if ! HOME="$HFS_HOME" "$HMOUNT" "$MAME_HDA" >"$WORK/picture-hmount.out" 2>&1; then
    fail_stage mame "could not mount the boot template; see $WORK/picture-hmount.out"
  fi
  for PICT_NAME in "${PICT_NAMES[@]}"; do
    STAGED="$WORK/$PICT_NAME"
    if ! HOME="$HFS_HOME" "$HCOPY" -r ":Desktop Folder:Images:$PICT_NAME" "$STAGED" \
        >"$WORK/picture-hcopy.out" 2>&1; then
      HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true
      fail_stage mame "could not extract template picture :Desktop Folder:Images:$PICT_NAME; ensure MAME_HDA carries it; see $WORK/picture-hcopy.out"
    fi
    DEV_DISK_ARGUMENTS+=("$STAGED")
  done
  if ! HOME="$HFS_HOME" "$HUMOUNT" >"$WORK/picture-humount.out" 2>&1; then
    fail_stage mame "could not unmount the boot template; see $WORK/picture-humount.out"
  fi
fi
DEV_DISK_ARGUMENTS+=("$CONFIG")

if ! provenance_message="$(python3 "$RUN_PROVENANCE_HELPER" record \
    "${PROVENANCE_ARGUMENTS[@]}" 2>&1)"; then
  fail_stage provenance "$provenance_message"
fi

# A scenario-local control dir keeps mame-dev-disk.sh's hfsutils state
# (current mounted volume) isolated, so parallel ctest runs of the two
# scenarios cannot cross-mount each other's development disks.
if ! MAME_DEV_HDA="$DEV" MAME_CONTROL_DIR="$WORK/hfs-ctl" \
    "$PROJECT_DIR/scripts/mame-dev-disk.sh" \
    "${DEV_DISK_ARGUMENTS[@]}" >"$WORK/dev-disk.out" 2>&1; then
  fail_stage mame "development disk creation failed; see $WORK/dev-disk.out"
fi

if [ -z "${LOKA_TAB_COUNT:-}" ]; then
  LOKA_TAB_COUNT="$FINDER_TAB_COUNT"
fi
if [ -z "${LOKA_SETTLE_TIMEOUT:-}" ] && [ -n "${FINDER_SETTLE_TIMEOUT:-}" ]; then
  LOKA_SETTLE_TIMEOUT="$FINDER_SETTLE_TIMEOUT"
fi
export LOKA_TAB_COUNT
if [ -n "${LOKA_SETTLE_TIMEOUT:-}" ]; then
  export LOKA_SETTLE_TIMEOUT
fi
export LOKA_SNAP_LOG; LOKA_SNAP_LOG="$(winpath "$LAUNCH_LOG")"
FORWARD="LOKA_SNAP_LOG:LOKA_TAB_COUNT:LOKA_SCENARIO_CELL"
if [ -n "${LOKA_LAUNCH_WAIT:-}" ]; then
  FORWARD="$FORWARD:LOKA_LAUNCH_WAIT"
fi
if [ -n "${LOKA_SETTLE_TIMEOUT:-}" ]; then
  FORWARD="$FORWARD:LOKA_SETTLE_TIMEOUT"
fi
# WSL hands nothing to a Windows process unless it is named here.
export WSLENV="${WSLENV:+$WSLENV:}$FORWARD"

ARGS=(
  "$MACHINE" -ramsize "$RAMSIZE"
  -homepath "$(winpath "$HOME_DIR")"
  -cfg_directory "$(winpath "$CFG_DIR")"
  -nvram_directory "$(winpath "$NVRAM_DIR")"
  -snapshot_directory "$(winpath "$SNAPSHOT_DIR")"
  -diff_directory "$(winpath "$DIFF_DIR")"
  -hard1 "$(winpath "$BOOT")"
  -scsi:5 harddisk -hard2 "$(winpath "$DEV")"
  -video none -sound none -nothrottle -natural -skip_gameinfo
  -autoboot_delay 1 -autoboot_script "$(winpath "$LAUNCHER")"
)
if [ -n "${MAME_ROMPATH:-}" ]; then
  ARGS+=(-rompath "$MAME_ROMPATH")
fi

if timeout 480 "$MAME_EXECUTABLE" "${ARGS[@]}" >"$MAME_OUT" 2>&1; then
  :
else
  mame_status=$?
  if [ "$mame_status" -eq 124 ]; then
    fail_stage mame "timed out after 480 seconds; see $MAME_OUT"
  fi
  fail_stage mame "MAME exited with status $mame_status; see $MAME_OUT"
fi
SETTLE_REACHED=1
if [ ! -f "$LAUNCH_LOG" ] \
  || ! grep -q '^LOKA-SNAP: settled after ' "$LAUNCH_LOG"; then
  SETTLE_REACHED=0
fi


if ! HMOUNT="$(find_retro68_tool hmount)"; then
  fail_stage extract "hmount is unavailable"
fi
if ! HCOPY="$(find_retro68_tool hcopy)"; then
  fail_stage extract "hcopy is unavailable"
fi
if ! HUMOUNT="$(find_retro68_tool humount)"; then
  fail_stage extract "humount is unavailable"
fi
if ! HOME="$HFS_HOME" "$HMOUNT" "$DEV" 1 >"$WORK/hmount.out" 2>&1; then
  fail_stage extract "could not mount the development disk; see $WORK/hmount.out"
fi
# A vehicle that failed an assert leaves the expression here (#1093). Read it
# before the audit, which an early abort never writes, and fail the cell even
# when the abort came after the audit and capture were written.
if HOME="$HFS_HOME" "$HCOPY" -t ":LokaTestsToolbox.assert" "$WORK/LokaTestsToolbox.assert" \
    >"$WORK/hcopy-assert.out" 2>&1; then
  HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true
  fail_stage extract "classic assert failed: $(tr -d '\r' <"$WORK/LokaTestsToolbox.assert")"
fi
if ! HOME="$HFS_HOME" "$HCOPY" -t ":LokaTestsToolbox.audit" "$AUDIT" >"$WORK/hcopy.out" 2>&1; then
  HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true
  fail_stage extract "could not copy LokaTestsToolbox.audit; see $WORK/hcopy.out"
fi
if ! HOME="$HFS_HOME" "$HCOPY" -t ":LokaTestsToolbox.capture" "$CAPTURE_RECTANGLE" \
    >"$WORK/hcopy-capture.out" 2>&1; then
  HOME="$HFS_HOME" "$HUMOUNT" >/dev/null 2>&1 || true
  fail_stage extract "could not copy LokaTestsToolbox.capture; see $WORK/hcopy-capture.out"
fi
if ! HOME="$HFS_HOME" "$HUMOUNT" >"$WORK/humount.out" 2>&1; then
  fail_stage extract "could not unmount the development disk; see $WORK/humount.out"
fi

if [ ! -f "$EXPECTED_AUDIT" ]; then
  fail_stage verdict "missing tracked audit $EXPECTED_AUDIT"
fi
if ! cmp "$EXPECTED_AUDIT" "$AUDIT"; then
  fail_stage verdict "audit differs from $EXPECTED_AUDIT; see $AUDIT"
fi
if ! provenance_message="$(python3 "$RUN_PROVENANCE_HELPER" audit-matched \
    --provenance "$RUN_PROVENANCE" --audit "$AUDIT" 2>&1)"; then
  fail_stage provenance "$provenance_message"
fi
if [ "$SETTLE_REACHED" -ne 1 ]; then
  fail_stage settle "pixel stability was not reached; see $LAUNCH_LOG"
fi

newest_snapshot=""
while IFS= read -r -d '' candidate; do
  if [ -z "$newest_snapshot" ] || [ "$candidate" -nt "$newest_snapshot" ]; then
    newest_snapshot="$candidate"
  fi
done < <(find "$SNAPSHOT_DIR" -type f -name '*.png' -print0)
if [ -z "$newest_snapshot" ]; then
  fail_stage collect "MAME did not write a snapshot PNG under $SNAPSHOT_DIR"
fi
PNG_TOOL="$PROJECT_DIR/tests/scenarios/pngtool.py"
if ! cp -f "$newest_snapshot" "$RAW_IMAGE"; then
  fail_stage collect "could not collect $newest_snapshot"
fi
if [ "$STRUCTURAL_AUDIT" -eq 1 ]; then
  echo "Scenario structural/audit pass: $EXAMPLE/$SCENARIO"
  echo "Pixel verdict: not evaluated (Classic structural/audit mode does not claim a pixel verdict)"
  exit 0
fi

if ! reported_rectangle="$(python3 "$PNG_TOOL" normalize \
    "$RAW_IMAGE" "$CAPTURE_RECTANGLE" "$ACTUAL_IMAGE" 2>&1)"; then
  fail_stage normalize "$reported_rectangle"
fi
if ! provenance_message="$(python3 "$RUN_PROVENANCE_HELPER" capture-ready \
    --provenance "$RUN_PROVENANCE" --capture "$ACTUAL_IMAGE" 2>&1)"; then
  fail_stage provenance "$provenance_message"
fi

if [ "$UPDATE_GOLDEN" -eq 1 ]; then
  stage_golden
  exit 0
fi

if ! identity_message="$(python3 "$GOLDEN_IDENTITY_HELPER" verify \
    --bundle "$GOLDEN_BUNDLE" \
    --registry "$SCENARIO_REGISTRY" \
    --descriptor "$RIG_DESCRIPTOR" \
    --current-identity "$CURRENT_IDENTITY" \
    --example "$EXAMPLE" \
    --scenario "$SCENARIO" 2>&1)"; then
  refusal_reason="${identity_message//$'\n'/ }"
  printf 'machine_verdict=refused\nruntime_verification=passed\nrefusal_reason=%s\n' \
    "$refusal_reason" >"$MACHINE_VERDICT" || true
  echo "machine_verdict=refused" >&2
  echo "$identity_message" >&2
  echo "Work directory left for inspection: $WORK" >&2
  exit 3
fi
if ! baked_rectangle="$(python3 "$PNG_TOOL" alpha-bounds "$GOLDEN" 2>&1)"; then
  refusal_reason="${baked_rectangle//$'\n'/ }"
  printf 'machine_verdict=refused\nruntime_verification=passed\nrefusal_reason=%s\n' \
    "$refusal_reason" >"$MACHINE_VERDICT" || true
  echo "machine_verdict=refused" >&2
  echo "$baked_rectangle" >&2
  echo "Work directory left for inspection: $WORK" >&2
  exit 3
fi
if ! python3 "$PNG_TOOL" compare "$ACTUAL_IMAGE" "$GOLDEN"; then
  python3 "$PNG_TOOL" diff "$GOLDEN" "$ACTUAL_IMAGE" "$DIFF_DIR/$SCENARIO.png" || true
  echo "baked rectangle: $baked_rectangle" >&2
  echo "reported rectangle: $reported_rectangle" >&2
  fail_stage golden "settled snapshot differs from $GOLDEN"
fi

echo "Scenario passed: $EXAMPLE/$SCENARIO"
