#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

usage() {
  echo "Usage: $0 <example> <scenario from scenarios.txt> [--update-golden|--ci-structural|--inspect]" >&2
}

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  usage
  exit 2
fi

EXAMPLE="$1"
SCENARIO="$2"
SCENARIO_REGISTRY="$PROJECT_DIR/tests/scenarios/scenarios.txt"
STARTUP_IDENTITY_DECLARATIONS="$PROJECT_DIR/tests/scenarios/startup-golden-identities.txt"
if [[ ! "$EXAMPLE" =~ ^[a-z0-9][a-z0-9-]*$ ]] \
  || [[ ! "$SCENARIO" =~ ^[a-z0-9][a-z0-9-]*$ ]] \
  || ! grep -Fxq -- "$EXAMPLE $SCENARIO" "$SCENARIO_REGISTRY"; then
  usage
  exit 2
fi

# The remaining SmirkBench cells are Toolbox counter probes.
if [ "$EXAMPLE" = smirkbench ] && [ "$SCENARIO" != startup ] && [ "$SCENARIO" != attributed-editor-line ]; then
  usage
  exit 2
fi

# Real ADB Tab traversal is a Toolbox-only cell.
if [ "$EXAMPLE/$SCENARIO" = helloworld/bmi-tab ]; then
  usage
  exit 2
fi

MODE="verify"
RUN_MODE="flow"
if [ $# -eq 3 ]; then
  case "$3" in
    --update-golden) MODE="update" ;;
    --ci-structural) MODE="structural" ;;
    --inspect) MODE="inspect"; RUN_MODE="inspect" ;;
    *) usage; exit 2 ;;
  esac
fi

WORK="${LOKA_MACOS_SCENARIO_WORK:-$PROJECT_DIR/build/macos-scenario/$EXAMPLE/$SCENARIO}"
GOLDEN_DIR="$PROJECT_DIR/build/macos-scenario/golden/$EXAMPLE"
GOLDEN="$GOLDEN_DIR/$SCENARIO.png"
GOLDEN_PROFILE="$GOLDEN_DIR/$SCENARIO.profile"
case "$EXAMPLE" in
  scrapbook) TARGET="LokaScrapbookScenarioMacOS" ;;
  helloworld) TARGET="LokaHelloWorldScenarioMacOS" ;;
  tutorial) TARGET="LokaTutorialScenarioMacOS" ;;
  minesweeper) TARGET="LokaMineSweeperScenarioMacOS" ;;
  smirkbench) TARGET="LokaSmirkBenchScenarioMacOS" ;;
  floppybird) TARGET="LokaFloppyBirdScenarioMacOS" ;;
  *) usage; exit 2 ;;
esac
APP="${LOKA_MACOS_SCENARIO_APP:-$PROJECT_DIR/build/macos/Debug/apple/macos/$TARGET.app}"
BINARY="$APP/Contents/MacOS/$TARGET"
EXPECTED="$PROJECT_DIR/tests/scenarios/expected/$EXAMPLE/$SCENARIO.audit"
# SmirkBench's historical startup audit contains Toolbox-only paint counters.
if [ "$EXAMPLE/$SCENARIO" = "smirkbench/startup" ]; then
  EXPECTED="$PROJECT_DIR/tests/scenarios/desktop-expected/smirkbench/startup.audit"
fi
PNG_TOOL="$PROJECT_DIR/tests/scenarios/pngtool.py"
GOLDEN_IDENTITY_GUARD="$PROJECT_DIR/scripts/rig/golden_identity_guard.py"
PACKAGE_FIXTURE_GUARD="$PROJECT_DIR/scripts/rig/package_fixture_guard.py"
CAPTURE_PROFILE_GUARD="$PROJECT_DIR/scripts/rig/capture_profile_guard.py"
RIG_DESCRIPTOR_DIRECTORY="$PROJECT_DIR/scripts/rig/macos/rigs"
WORK_DIR_TOOL="$PROJECT_DIR/tests/macos/validate-work-dir.py"
PYTHON3="${PYTHON3:-python3}"
SCREENCAPTURE="${SCREENCAPTURE:-/usr/sbin/screencapture}"

# Everything the run is expected to leave in the work directory, so a refusal
# can say how far the app actually got. runner.log is reported separately: its
# size is the fact that separates "hung before writing anything" from "stopped
# partway", and neither is visible from a bare deadline message (#466).
WORK_REQUIRED_ARTIFACTS=(
  actual.audit
  actual.png
  actual.profile
  settle-a.png
  settle-b.png
  runner.log
)
WORK_CENSUS_ENTRIES=(
  LokaTest.cfg
  app.pid
  stage
  ready
  "${WORK_REQUIRED_ARTIFACTS[@]}"
  complete
)

# Set once this run has emptied and recreated $WORK. Before that the directory
# still holds the previous run's artifacts, and reporting those as progress
# would be worse than saying nothing: a refusal that never launched the app
# would list a completion marker.
WORK_IS_THIS_RUNS=0

describe_work_state() {
  if [ ! -d "$WORK" ]; then
    echo "Work state: the work directory does not exist"
    return
  fi
  if [ "$WORK_IS_THIS_RUNS" -eq 0 ]; then
    echo "Work state: this run stopped before it reset the work directory, so what is in it belongs to the previous run"
    return
  fi
  if [ ! -f "$WORK/runner.log" ]; then
    echo "Work state: runner.log absent -- the app was never launched"
  elif [ ! -s "$WORK/runner.log" ]; then
    # No inference is drawn from an empty log: a scenario app that runs to the
    # end normally writes nothing to it either. What separates a run that died
    # early from one that nearly finished is the artifact census below.
    echo "Work state: runner.log 0 bytes"
  else
    local bytes last
    bytes="$(wc -c <"$WORK/runner.log" | tr -d ' ')"
    last="$(tr -d '\r' <"$WORK/runner.log" | tail -n 1 | cut -c1-200)"
    echo "Work state: runner.log $bytes bytes, last line: $last"
  fi
  local present=() absent=() entry
  for entry in "${WORK_CENSUS_ENTRIES[@]}"; do
    if [ "$entry" = "runner.log" ]; then
      # already reported above, with its size
      continue
    fi
    if [ -e "$WORK/$entry" ]; then
      present+=("$entry")
    else
      absent+=("$entry")
    fi
  done
  echo "Present: ${present[*]:-(nothing)}"
  echo "Absent: ${absent[*]:-(nothing)}"
}

fail_stage() {
  local stage="$1"
  shift
  echo "$stage stage failed: $*" >&2
  echo "Artifacts: $WORK" >&2
  describe_work_state >&2
  exit 1
}

resolve_rig_descriptor() {
  local name="${LOKA_MACOS_RIG:-}"
  if [ -z "$name" ]; then
    fail_stage profile \
      "LOKA_MACOS_RIG is unset; set LOKA_MACOS_RIG=<name> for a descriptor in $RIG_DESCRIPTOR_DIRECTORY"
  fi
  if [[ ! "$name" =~ ^[a-z0-9][a-z0-9.-]*$ ]] || [[ "$name" == *..* ]]; then
    fail_stage profile \
      "invalid LOKA_MACOS_RIG name '$name'; expected a name matching ^[a-z0-9][a-z0-9.-]*$ without '..'"
  fi
  RIG_DESCRIPTOR="$RIG_DESCRIPTOR_DIRECTORY/$name.ini"
  if [ ! -f "$RIG_DESCRIPTOR" ]; then
    fail_stage profile \
      "no tracked rig descriptor at $RIG_DESCRIPTOR; this rail's goldens would be pinned to an undeclared environment. Add $RIG_DESCRIPTOR declaring the [capture] fields this machine reports in $WORK/actual.profile."
  fi
}

verify_capture_environment() {
  if ! "$PYTHON3" "$CAPTURE_PROFILE_GUARD" \
      --descriptor "$RIG_DESCRIPTOR" --profile "$WORK/actual.profile"; then
    fail_stage profile \
      "capture environment is not the one $RIG_DESCRIPTOR declares"
  fi
}

publish_verified() {
  printf 'runner-verified\n' >"$WORK/verified.tmp"
  mv -f "$WORK/verified.tmp" "$WORK/verified"
}

if [ ! -x "$BINARY" ]; then
  fail_stage build "missing $BINARY; run: cmake --preset macos-debug && cmake --build --preset macos-scenarios"
fi
if [ "$EXAMPLE" = "scrapbook" ]; then
  LRPC="$PROJECT_DIR/build/host/lrpc/lrpc"
  SOURCE_ASSETS="$PROJECT_DIR/example/ScrapbookUI/assets/ASSETS-modern.LRP"
  FIXTURE_REGISTRY="$PROJECT_DIR/tests/scenarios/scrapbook-package-fixtures.txt"
  # tools/lrpc pins no generator, so where the binary lands depends on the one
  # cmake picks. A bare configure takes Unix Makefiles here and writes
  # build/host/lrpc/lrpc, but an exported CMAKE_GENERATOR of Xcode -- which
  # scripts/macos/gen-xcodeproj.sh makes a normal habit -- writes a per-config
  # subdirectory instead. Look there too, the way the Win32 rail already does,
  # so the remediation below is never printed to someone who ran it correctly.
  if [ ! -x "$LRPC" ] && [ -x "$PROJECT_DIR/build/host/lrpc/Debug/lrpc" ]; then
    LRPC="$PROJECT_DIR/build/host/lrpc/Debug/lrpc"
  fi
  if [ ! -x "$LRPC" ]; then
    fail_stage stage \
      "missing $LRPC; build it with: cmake -S tools/lrpc -B build/host/lrpc && cmake --build build/host/lrpc"
  fi
  if [ ! -f "$SOURCE_ASSETS" ]; then
    fail_stage stage "package not found: $SOURCE_ASSETS"
  fi
fi
if [ ! -f "$EXPECTED" ]; then
  fail_stage verdict "missing tracked audit $EXPECTED"
fi
if ! WORK="$("$PYTHON3" "$WORK_DIR_TOOL" "$PROJECT_DIR/build" "$WORK")"; then
  fail_stage setup "work directory must resolve strictly below $PROJECT_DIR/build"
fi

if [ -d "$WORK" ]; then
  rm -rf "$WORK"
fi
mkdir -p "$WORK"
WORK_IS_THIS_RUNS=1
if [ "$EXAMPLE" = "scrapbook" ]; then
  if ! mkdir -p "$WORK/stage"; then
    fail_stage stage "could not create $WORK/stage"
  fi
  STAGED_APP="$WORK/stage/$TARGET.app"
  if ! cp -R "$APP" "$STAGED_APP"; then
    fail_stage stage "could not copy $APP to $STAGED_APP"
  fi
  STAGED_ASSETS="$STAGED_APP/Contents/Resources/ASSETS.LRP"
  if ! CORRUPT_BAG="$("$PYTHON3" "$PACKAGE_FIXTURE_GUARD" plan \
      --registry "$FIXTURE_REGISTRY" --scenario "$SCENARIO" 2>&1)"; then
    fail_stage stage "$CORRUPT_BAG"
  fi
  # The refusal message is worth merging into the value, but only on failure:
  # on success anything the interpreter wrote to stderr rides along and would be
  # handed to lrpc as a bag number. The answer shape is empty or decimal.
  if [ -n "$CORRUPT_BAG" ] && [[ ! "$CORRUPT_BAG" =~ ^[0-9]+$ ]]; then
    fail_stage stage "package fixture plan answered '$CORRUPT_BAG', not a bag number"
  fi
  STAGE_ARGUMENTS=(stage "$SOURCE_ASSETS" -o "$STAGED_ASSETS")
  if [ -n "$CORRUPT_BAG" ]; then
    STAGE_ARGUMENTS+=(--corrupt-bag "$CORRUPT_BAG")
  fi
  if ! "$LRPC" "${STAGE_ARGUMENTS[@]}"; then
    fail_stage stage "could not stage the scenario package"
  fi
  if ! fixture_message="$("$PYTHON3" "$PACKAGE_FIXTURE_GUARD" verify \
      --registry "$FIXTURE_REGISTRY" --scenario "$SCENARIO" \
      --source "$SOURCE_ASSETS" --staged "$STAGED_ASSETS" 2>&1)"; then
    fail_stage stage "$fixture_message"
  fi
  # BINARY carried the original bundle through the preflight above; from
  # here it names the staged copy. cp does not promise the mode bits, and a
  # non-executable copy would otherwise surface as "scenario app exited
  # non-zero" from inside the launch subshell, pointing away from staging.
  BINARY="$STAGED_APP/Contents/MacOS/$TARGET"
  if [ ! -x "$BINARY" ]; then
    fail_stage stage "staged $BINARY is not executable"
  fi
fi
printf 'scenario %s\ncapture_dir %s\n' "$SCENARIO" "$WORK" >"$WORK/LokaTest.cfg"

if [ "${LOKA_MACOS_SCENARIO_CAPTURE_DESKTOP:-0}" = "1" ]; then
  if ! "$SCREENCAPTURE" -x "$WORK/desktop-before.png"; then
    fail_stage capture "could not capture desktop-before.png"
  fi
fi

(
  cd "$WORK"
  LOKA_MACOS_SCENARIO_MODE="$RUN_MODE" exec "$BINARY" >runner.log 2>&1
) &
APP_PID=$!
printf '%s\n' "$APP_PID" >"$WORK/app.pid"

cleanup() {
  if [ "${LOKA_MACOS_SCENARIO_RETAIN_ON_FAILURE:-0}" = "1" ]; then
    echo "Retaining scenario app pid $APP_PID after failure" >&2
    return
  fi
  if kill -0 "$APP_PID" 2>/dev/null; then
    kill "$APP_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

deadline=$((SECONDS + 120))
HELD_CAPTURED=0
while kill -0 "$APP_PID" 2>/dev/null; do
  if [ "$SECONDS" -ge "$deadline" ]; then
    fail_stage process "timed out after 120 seconds"
  fi
  if [ "$MODE" = "inspect" ] && [ "$HELD_CAPTURED" -eq 0 ] && [ -f "$WORK/ready" ]; then
    if [ "$(tr -d '\r\n' <"$WORK/ready")" != "inspection-ready" ]; then
      fail_stage ready "atomic inspect marker is invalid"
    fi
    if [ "${LOKA_MACOS_SCENARIO_CAPTURE_DESKTOP:-0}" = "1" ]; then
      if ! "$SCREENCAPTURE" -x "$WORK/desktop-after.png"; then
        fail_stage capture "could not capture held desktop-after.png"
      fi
    fi
    HELD_CAPTURED=1
  fi
  sleep 0.1
done
if ! wait "$APP_PID"; then
  fail_stage process "scenario app exited non-zero; see $WORK/runner.log"
fi
trap - EXIT

if [ "${LOKA_MACOS_SCENARIO_CAPTURE_DESKTOP:-0}" = "1" ] && [ "$MODE" != "inspect" ]; then
  if ! "$SCREENCAPTURE" -x "$WORK/desktop-after.png"; then
    fail_stage capture "could not capture desktop-after.png"
  fi
fi

if [ ! -f "$WORK/complete" ] || [ "$(tr -d '\r\n' <"$WORK/complete")" != "artifacts-ready" ]; then
  fail_stage completion "atomic completion marker is missing or invalid"
fi
for artifact in "${WORK_REQUIRED_ARTIFACTS[@]}"; do
  if [ ! -f "$WORK/$artifact" ]; then
    fail_stage artifacts "missing $artifact"
  fi
done
if ! cmp "$EXPECTED" "$WORK/actual.audit"; then
  fail_stage verdict "actual.audit differs from $EXPECTED"
fi

if [ "$MODE" = "inspect" ]; then
  if [ "$HELD_CAPTURED" -eq 0 ]; then
    fail_stage ready "inspect run exited without a ready marker"
  fi
  echo "Scenario inspect pass: $EXAMPLE $SCENARIO"
  echo "Pixel verdict: not evaluated (inspect uses human presentation evidence)"
  publish_verified
  exit 0
fi

if [ "$MODE" = "structural" ]; then
  echo "Scenario structural pass: $EXAMPLE $SCENARIO"
  echo "Pixel verdict: not evaluated (hosted CI has no persistent rig golden)"
  publish_verified
  exit 0
fi

resolve_rig_descriptor

if [ "$MODE" = "update" ]; then
  verify_capture_environment
  update_arguments=()
  if [ -n "${LOKA_GOLDEN_UPDATE_ROOT:-}" ]; then
    update_arguments=(--update-root "$LOKA_GOLDEN_UPDATE_ROOT")
  fi
  if ! "$PYTHON3" "$GOLDEN_IDENTITY_GUARD" \
      --registry "$SCENARIO_REGISTRY" \
      --declarations "$STARTUP_IDENTITY_DECLARATIONS" \
      --golden-root "$PROJECT_DIR/build/macos-scenario/golden" \
      ${update_arguments[@]+"${update_arguments[@]}"} \
      --capture "$WORK/actual.png" \
      --example "$EXAMPLE" \
      --scenario "$SCENARIO"; then
    fail_stage golden "settled capture failed the startup-identity contract"
  fi
  mkdir -p "$GOLDEN_DIR"
  cp -f "$WORK/actual.png" "$GOLDEN"
  cp -f "$WORK/actual.profile" "$GOLDEN_PROFILE"
  echo "Updated macOS rig golden: $GOLDEN"
  exit 0
fi

verify_capture_environment
if [ ! -f "$GOLDEN" ] || [ ! -f "$GOLDEN_PROFILE" ]; then
  fail_stage golden "missing rig-local golden/profile; rerun with --update-golden"
fi
if ! cmp -s "$WORK/actual.profile" "$GOLDEN_PROFILE"; then
  fail_stage profile "rig profile differs from $GOLDEN_PROFILE"
fi
if ! "$PYTHON3" "$PNG_TOOL" compare "$WORK/actual.png" "$GOLDEN"; then
  "$PYTHON3" "$PNG_TOOL" diff "$GOLDEN" "$WORK/actual.png" "$WORK/diff.png" || true
  fail_stage golden "actual pixels differ from $GOLDEN"
fi

echo "Scenario passed: $EXAMPLE $SCENARIO"
publish_verified
