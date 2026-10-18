#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ROOT_DIR="${REPO_DIR}"
source "${REPO_DIR}/scripts/macos/lib-common.sh"

fail() {
  echo "MacOSBuildTargetsTest: $*" >&2
  exit 1
}

EXPECTED_TARGETS=$'LokaFloppyBirdMacOS\nLokaHelloMacOS\nLokaMineMacOS\nLokaSimpleViewerMacOS\nLokaSimpleTextMacOS\nLokaCardShowcaseMacOS\nLokaSmirkBenchMacOS\nLokaLazyListMacOS\nScrapbookUIMacOS\nLokaTutorialMacOS'
ACTUAL_TARGETS="$(loka_known_targets)"
[[ "${ACTUAL_TARGETS}" == "${EXPECTED_TARGETS}" ]] ||
  fail "default target manifest does not contain the ten shipping examples"

EXPECTED_STANDALONE_RELEASE_TARGETS=$'LokaSimpleViewerMacOS\nLokaSimpleTextMacOS\nLokaSmirkyCardMacOS\nLokaScrapbookStandaloneLoopMacOS\nLokaHelloWorldStandaloneLoopMacOS\nLokaTutorialStandaloneLoopMacOS\nLokaMineSweeperStandaloneLoopMacOS\nLokaFloppyBirdStandaloneLoopMacOS'
ACTUAL_STANDALONE_RELEASE_TARGETS="$(loka_targets_for_selection standalone-release)"
[[ "${ACTUAL_STANDALONE_RELEASE_TARGETS}" == "${EXPECTED_STANDALONE_RELEASE_TARGETS}" ]] ||
  fail "standalone-release target set does not contain five loops plus SimpleViewer, SimpleText and SmirkyCard"

SCRAPBOOK_REL_PATH="example/ScrapbookUI/ScrapbookUIMacOS.app/Contents/MacOS/ScrapbookUIMacOS"
[[ "$(loka_target_rel_path ScrapbookUIMacOS)" == "${SCRAPBOOK_REL_PATH}" ]] ||
  fail "ScrapbookUIMacOS executable path does not point inside its bundle"
[[ "$(loka_target_rel_path LokaScrapbookStandaloneFlowMacOS)" == \
  "apple/macos/LokaScrapbookStandaloneFlowMacOS.app/Contents/MacOS/LokaScrapbookStandaloneFlowMacOS" ]] ||
  fail "explicit-only standalone Flow bundle path changed"

TEST_ROOT="$(mktemp -d /tmp/loka-macos-build-targets-XXXXXX)"
trap 'rm -rf "${TEST_ROOT}"' EXIT

FAKE_ROOT="${TEST_ROOT}/fake-root"
mkdir -p "${FAKE_ROOT}/scripts/macos"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'printf "%s\n" "${TARGET:-}" >> "${LOKA_TEST_BUILD_LOG}"' \
  > "${FAKE_ROOT}/scripts/macos/build.sh"
chmod +x "${FAKE_ROOT}/scripts/macos/build.sh"

export LOKA_TEST_BUILD_LOG="${TEST_ROOT}/targets.log"
unset TARGET || true
unset TARGET_SET || true
loka_build_requested_or_known_targets "${FAKE_ROOT}"
[[ "$(cat "${LOKA_TEST_BUILD_LOG}")" == "${EXPECTED_TARGETS}" ]] ||
  fail "default build loop did not request all ten shipping examples"

: >"${LOKA_TEST_BUILD_LOG}"
export TARGET_SET=standalone-release
loka_build_requested_or_known_targets "${FAKE_ROOT}"
[[ "$(cat "${LOKA_TEST_BUILD_LOG}")" == "${EXPECTED_STANDALONE_RELEASE_TARGETS}" ]] ||
  fail "standalone-release build loop did not request its complete application set"
unset TARGET_SET

set +e
CONFLICT_OUTPUT="$(TARGET=LokaHelloMacOS TARGET_SET=standalone-release \
  loka_requested_or_known_targets 2>&1)"
CONFLICT_STATUS=$?
UNKNOWN_OUTPUT="$(TARGET_SET=unknown-set \
  loka_requested_or_known_targets 2>&1)"
UNKNOWN_STATUS=$?
set -e
[[ "${CONFLICT_STATUS}" -ne 0 && "${CONFLICT_OUTPUT}" == *"not both"* ]] ||
  fail "TARGET and TARGET_SET conflict was not refused clearly"
[[ "${UNKNOWN_STATUS}" -ne 0 && "${UNKNOWN_OUTPUT}" == *"unknown or empty"* ]] ||
  fail "an unknown target set was not refused clearly"

STALE_ROOT="${TEST_ROOT}/stale"
mkdir -p \
  "${STALE_ROOT}/example/HelloWorld/LokaHelloMacOS" \
  "${STALE_ROOT}/example/ScrapbookUI/ScrapbookUIMacOS.app/Contents/Resources" \
  "${STALE_ROOT}/apple/macos/LokaScrapbookStandaloneFlowMacOS.app/Contents/MacOS"
loka_cleanup_stale_output_dirs "${STALE_ROOT}"
[[ ! -e "${STALE_ROOT}/example/HelloWorld/LokaHelloMacOS" ]] ||
  fail "plain-executable stale output directory was not removed"
[[ ! -e "${STALE_ROOT}/example/ScrapbookUI/ScrapbookUIMacOS.app" ]] ||
  fail "ScrapbookUI stale bundle was not removed at its bundle root"
[[ ! -e "${STALE_ROOT}/apple/macos/LokaScrapbookStandaloneFlowMacOS.app" ]] ||
  fail "explicit-only stale bundle was not removed at its bundle root"

FAKE_BIN="${TEST_ROOT}/bin"
mkdir -p "${FAKE_BIN}"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'if [[ "${LOKA_TEST_LIPO_FAIL:-0}" == "1" ]]; then exit 73; fi' \
  'if [[ "$1" == "-info" ]]; then exit 0; fi' \
  'output=""' \
  'while [[ "$#" -gt 0 ]]; do' \
  '  if [[ "$1" == "-output" ]]; then output="$2"; shift 2; else shift; fi' \
  'done' \
  'printf "universal\n" > "${output}"' \
  > "${FAKE_BIN}/lipo"
chmod +x "${FAKE_BIN}/lipo"
export LOKA_LIPO_BIN="${FAKE_BIN}/lipo"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'printf "%s\n" "$*" >>"${LOKA_TEST_CMAKE_LOG}"' \
  > "${FAKE_BIN}/cmake"
chmod +x "${FAKE_BIN}/cmake"
export PATH="${FAKE_BIN}:${PATH}"

export LOKA_TEST_CMAKE_LOG="${TEST_ROOT}/cmake.log"
unset TARGET || true
TARGET_SET=standalone-release \
  BUILD_DIR="${TEST_ROOT}/standalone-cmake" \
  "${REPO_DIR}/scripts/macos/build.sh"
grep -Fq -- '-DLOKA_ENABLE_STANDALONE_TARGETS=ON' "${LOKA_TEST_CMAKE_LOG}" ||
  fail "standalone-release did not register its gated CMake targets"
grep -Fq -- '-DLOKA_BUILD_SMIRKYCARD=ON' "${LOKA_TEST_CMAKE_LOG}" ||
  fail "standalone-release did not configure SmirkyCard"
! grep -Fq -- 'FETCHCONTENT_SOURCE_DIR_SMIRKYCARD_QUICKJS' "${LOKA_TEST_CMAKE_LOG}" ||
  fail "build.sh passed a QuickJS source override nobody asked for"

: >"${LOKA_TEST_CMAKE_LOG}"
TARGET=LokaHelloMacOS BUILD_DIR="${TEST_ROOT}/default-cmake" \
  "${REPO_DIR}/scripts/macos/build.sh"
! grep -Fq -- '-DLOKA_BUILD_SMIRKYCARD=ON' "${LOKA_TEST_CMAKE_LOG}" ||
  fail "a default build configured SmirkyCard"

: >"${LOKA_TEST_CMAKE_LOG}"
LOKA_QUICKJS_SOURCE_DIR="${TEST_ROOT}/quickjs src" TARGET_SET=standalone-release \
  BUILD_DIR="${TEST_ROOT}/standalone-cmake" \
  "${REPO_DIR}/scripts/macos/build.sh"
grep -Fq -- "-DFETCHCONTENT_SOURCE_DIR_SMIRKYCARD_QUICKJS=${TEST_ROOT}/quickjs src" "${LOKA_TEST_CMAKE_LOG}" ||
  fail "LOKA_QUICKJS_SOURCE_DIR did not reach the QuickJS FetchContent override"

stage_arch() {
  local build_root="$1"
  local arch="$2"
  local requested_selection="${3:-default}"
  local target_name
  local memberships
  local output_shape
  local rel_path
  local target_path
  local bundle_root

  while IFS='|' read -r target_name memberships output_shape rel_path; do
    loka_target_has_selection "${memberships}" "${requested_selection}" || continue
    target_path="${build_root}/Release-${arch}/${rel_path}"
    mkdir -p "$(dirname "${target_path}")"
    printf '%s\n' "${arch}" > "${target_path}"
    if [[ "${output_shape}" == "bundle" ]]; then
      bundle_root="${target_path%%/Contents/MacOS/*}"
      mkdir -p "${bundle_root}/Contents/Resources"
      printf '%s\n' "assets-${arch}" > "${bundle_root}/Contents/Resources/ASSETS.LRP"
    fi
  done < <(loka_target_manifest)
}

REGRESSION_FAILURES=()

ATOMIC_ROOT="${TEST_ROOT}/atomic"
stage_arch "${ATOMIC_ROOT}" ppc
stage_arch "${ATOMIC_ROOT}" i386
ATOMIC_BUNDLE="${ATOMIC_ROOT}/universal/ScrapbookUIMacOS.app"
mkdir -p "${ATOMIC_BUNDLE}/Contents/MacOS" "${ATOMIC_BUNDLE}/Contents/Resources"
printf '%s\n' "previous-universal" > "${ATOMIC_BUNDLE}/Contents/MacOS/ScrapbookUIMacOS"
printf '%s\n' "previous-assets" > "${ATOMIC_BUNDLE}/Contents/Resources/ASSETS.LRP"
set +e
TARGET=ScrapbookUIMacOS LOKA_TEST_LIPO_FAIL=1 \
  loka_merge_requested_or_known_targets_two_arch \
  "${ATOMIC_ROOT}" Release ppc i386
ATOMIC_STATUS=$?
set -e
[[ "${ATOMIC_STATUS}" -ne 0 ]] ||
  REGRESSION_FAILURES+=("a failing lipo was reported as success")
[[ "$(cat "${ATOMIC_BUNDLE}/Contents/MacOS/ScrapbookUIMacOS")" == "previous-universal" ]] ||
  REGRESSION_FAILURES+=("a failing lipo replaced the previous bundle executable")
[[ "$(cat "${ATOMIC_BUNDLE}/Contents/Resources/ASSETS.LRP")" == "previous-assets" ]] ||
  REGRESSION_FAILURES+=("a failing lipo replaced the previous bundle resources")

MISSING_ROOT="${TEST_ROOT}/missing-arch"
stage_arch "${MISSING_ROOT}" ppc
mkdir -p "${MISSING_ROOT}/universal"
printf '%s\n' "previous-universal" > "${MISSING_ROOT}/universal/LokaHelloMacOS"
set +e
MISSING_OUTPUT="$(TARGET=LokaHelloMacOS \
  loka_merge_requested_or_known_targets_two_arch \
  "${MISSING_ROOT}" Release ppc i386 2>&1)"
MISSING_STATUS=$?
set -e
[[ "${MISSING_STATUS}" -ne 0 ]] ||
  REGRESSION_FAILURES+=("a merge with no i386 input was reported as success")
[[ "${MISSING_OUTPUT}" == *"i386"* ]] ||
  REGRESSION_FAILURES+=("the missing-input error did not name i386")
[[ "$(cat "${MISSING_ROOT}/universal/LokaHelloMacOS")" == "previous-universal" ]] ||
  REGRESSION_FAILURES+=("a missing i386 input published the thin ppc executable")

if [[ "${#REGRESSION_FAILURES[@]}" -ne 0 ]]; then
  printf 'MacOSBuildTargetsTest: %s\n' "${REGRESSION_FAILURES[@]}" >&2
  exit 1
fi

TWO_ARCH_ROOT="${TEST_ROOT}/two-arch"
stage_arch "${TWO_ARCH_ROOT}" ppc
stage_arch "${TWO_ARCH_ROOT}" i386
unset TARGET || true
loka_merge_requested_or_known_targets_two_arch \
  "${TWO_ARCH_ROOT}" Release ppc i386
[[ "$(cat "${TWO_ARCH_ROOT}/universal/LokaHelloMacOS")" == "universal" ]] ||
  fail "two-arch merge regressed a plain executable"
[[ "$(cat "${TWO_ARCH_ROOT}/universal/ScrapbookUIMacOS.app/Contents/MacOS/ScrapbookUIMacOS")" == "universal" ]] ||
  fail "two-arch merge did not place ScrapbookUI's executable inside a bundle"
[[ "$(cat "${TWO_ARCH_ROOT}/universal/ScrapbookUIMacOS.app/Contents/Resources/ASSETS.LRP")" == "assets-ppc" ]] ||
  fail "two-arch merge did not preserve ScrapbookUI's Resources"

MULTI_ARCH_ROOT="${TEST_ROOT}/multi-arch"
stage_arch "${MULTI_ARCH_ROOT}" ppc
stage_arch "${MULTI_ARCH_ROOT}" i386
stage_arch "${MULTI_ARCH_ROOT}" x86_64
unset TARGET || true
loka_merge_requested_or_known_targets_multi_arch \
  "${MULTI_ARCH_ROOT}" Release 'ppc;i386;x86_64'
[[ "$(cat "${MULTI_ARCH_ROOT}/universal/LokaHelloMacOS")" == "universal" ]] ||
  fail "multi-arch merge regressed a plain executable"
[[ "$(cat "${MULTI_ARCH_ROOT}/universal/ScrapbookUIMacOS.app/Contents/MacOS/ScrapbookUIMacOS")" == "universal" ]] ||
  fail "multi-arch merge did not place ScrapbookUI's executable inside a bundle"
[[ "$(cat "${MULTI_ARCH_ROOT}/universal/ScrapbookUIMacOS.app/Contents/Resources/ASSETS.LRP")" == "assets-ppc" ]] ||
  fail "multi-arch merge did not preserve ScrapbookUI's Resources"

STANDALONE_ROOT="${TEST_ROOT}/standalone-release"
stage_arch "${STANDALONE_ROOT}" ppc standalone-release
stage_arch "${STANDALONE_ROOT}" i386 standalone-release
export TARGET_SET=standalone-release
loka_merge_requested_or_known_targets_two_arch \
  "${STANDALONE_ROOT}" Release ppc i386
unset TARGET_SET
[[ "$(cat "${STANDALONE_ROOT}/universal/LokaSimpleViewerMacOS.app/Contents/MacOS/LokaSimpleViewerMacOS")" == "universal" ]] ||
  fail "standalone-release merge omitted SimpleViewer"
[[ "$(cat "${STANDALONE_ROOT}/universal/LokaSimpleTextMacOS.app/Contents/MacOS/LokaSimpleTextMacOS")" == "universal" ]] ||
  fail "standalone-release merge omitted SimpleText"
[[ "$(cat "${STANDALONE_ROOT}/universal/LokaSmirkyCardMacOS.app/Contents/MacOS/LokaSmirkyCardMacOS")" == "universal" ]] ||
  fail "standalone-release merge omitted SmirkyCard"
[[ "$(cat "${STANDALONE_ROOT}/universal/LokaHelloWorldStandaloneLoopMacOS.app/Contents/MacOS/LokaHelloWorldStandaloneLoopMacOS")" == "universal" ]] ||
  fail "standalone-release merge omitted an autonomous loop bundle"
[[ "$(cat "${STANDALONE_ROOT}/universal/LokaScrapbookStandaloneLoopMacOS.app/Contents/Resources/ASSETS.LRP")" == "assets-ppc" ]] ||
  fail "standalone-release merge did not preserve Scrapbook loop resources"
[[ ! -e "${STANDALONE_ROOT}/universal/LokaHelloMacOS" ]] ||
  fail "standalone-release merge leaked a normal shipping executable"

echo "MacOSBuildTargetsTest: PASS"
