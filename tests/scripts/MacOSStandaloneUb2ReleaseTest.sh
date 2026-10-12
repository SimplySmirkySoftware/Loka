#!/usr/bin/env bash

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/loka-macos-standalone-ub2-XXXXXX)"
trap 'rm -rf "${TEST_ROOT}"' EXIT

fail() {
  echo "MacOSStandaloneUb2ReleaseTest: $*" >&2
  exit 1
}

FAKE_REPO="${TEST_ROOT}/repo"
mkdir -p \
  "${FAKE_REPO}/scripts/apple" \
  "${FAKE_REPO}/scripts/macos"
printf 'project(Loka VERSION 9.9.9 LANGUAGES CXX)\n' > "${FAKE_REPO}/CMakeLists.txt"
cp "${REPO_DIR}/scripts/macos-standalone-release-ub2.sh" "${FAKE_REPO}/scripts/"
cp "${REPO_DIR}/scripts/presentation-stage.sh" "${FAKE_REPO}/scripts/"
cp "${REPO_DIR}/scripts/apple/lib-xcode.sh" "${FAKE_REPO}/scripts/apple/"
cp "${REPO_DIR}/scripts/macos/lib-common.sh" "${FAKE_REPO}/scripts/macos/"
cp "${REPO_DIR}/scripts/macos/standalone-release-stage.sh" \
  "${FAKE_REPO}/scripts/macos/"

BUILD_LOG="${TEST_ROOT}/build.log"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  'printf "%s|%s|%s\n" "${TARGET_SET:-}" "${ARCHS:-}" "${BUILD_DIR:-}" >"${LOKA_TEST_BUILD_LOG}"' \
  >"${FAKE_REPO}/scripts/macos/build-ub2.sh"
chmod +x \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub2.sh" \
  "${FAKE_REPO}/scripts/macos/build-ub2.sh"

FAKE_LIPO="${TEST_ROOT}/lipo"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  '[[ "$1" == "-info" ]]' \
  'echo "Architectures in the fat file: $2 are: arm64 x86_64"' \
  >"${FAKE_LIPO}"
chmod +x "${FAKE_LIPO}"

ROOT_DIR="${FAKE_REPO}"
source "${FAKE_REPO}/scripts/macos/lib-common.sh"
DEFAULT_BUILD_ROOT="${FAKE_REPO}/build/macos-ub2/Release"
BUILD_ROOT="${FAKE_REPO}/custom build/Release"

stage_fake_build() {
  local build_root="$1"
  local marker="$2"
  while IFS='|' read -r target_name memberships output_shape rel_path; do
    loka_target_has_selection "${memberships}" standalone-release || continue
    output_path="${build_root}/${rel_path}"
    mkdir -p "$(dirname "${output_path}")"
    printf '%s\n' "${marker}-${target_name}" >"${output_path}"
    chmod +x "${output_path}"
    if [[ "${output_shape}" == "bundle" ]]; then
      bundle_root="${output_path%%/Contents/MacOS/*}"
      mkdir -p "${bundle_root}/Contents/Resources"
      printf '%s\n' "metadata-${target_name}" >"${bundle_root}/Contents/Info.plist"
      if [[ "${target_name}" == "LokaScrapbookStandaloneLoopMacOS" ]]; then
        printf '%s\n' "${marker}-assets" \
          >"${bundle_root}/Contents/Resources/ASSETS.LRP"
      fi
    fi
  done < <(loka_target_manifest)
}

stage_fake_build "${DEFAULT_BUILD_ROOT}" stale
stage_fake_build "${BUILD_ROOT}" custom

BUILD_DIR="${BUILD_ROOT}" LOKA_TEST_BUILD_LOG="${BUILD_LOG}" \
  LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub2.sh" >/dev/null

[[ "$(cat "${BUILD_LOG}")" == "standalone-release|arm64;x86_64|${BUILD_ROOT}" ]] ||
  fail "the UB2 build did not select the complete standalone target set"

RELEASE_ROOT="${FAKE_REPO}/build/release/macos-ub2"
[[ -x "${RELEASE_ROOT}/LokaSimpleTextMacOS.app/Contents/MacOS/LokaSimpleTextMacOS" ]] ||
  fail "the release omitted SimpleText"
[[ "$(cat "${RELEASE_ROOT}/LokaSimpleTextMacOS.app/Contents/Info.plist")" == "metadata-LokaSimpleTextMacOS" ]] ||
  fail "the release lost SimpleText bundle metadata"
[[ "$(find "${RELEASE_ROOT}" -maxdepth 1 -name '*.app' | wc -l | tr -d ' ')" == "7" ]] ||
  fail "the release did not contain exactly seven application bundles"
grep -Fq 'LokaSimpleViewerMacOS and LokaSimpleTextMacOS remain interactive.' "${RELEASE_ROOT}/README.txt" ||
  fail "the release README omitted the interactive applications"
[[ -x "${RELEASE_ROOT}/LokaSimpleViewerMacOS.app/Contents/MacOS/LokaSimpleViewerMacOS" ]] ||
  fail "the UB2 release omitted SimpleViewer"
[[ "$(cat "${RELEASE_ROOT}/LokaSimpleViewerMacOS.app/Contents/Info.plist")" == "metadata-LokaSimpleViewerMacOS" ]] ||
  fail "the release lost SimpleViewer bundle metadata"
[[ ! -e "${RELEASE_ROOT}/LokaSimpleViewerMacOS" ]] ||
  fail "the release retained an unbundled SimpleViewer executable"
[[ "$(cat "${RELEASE_ROOT}/LokaSimpleViewerMacOS.app/Contents/MacOS/LokaSimpleViewerMacOS")" == \
  "custom-LokaSimpleViewerMacOS" ]] ||
  fail "the UB2 release staged stale output instead of the configured BUILD_DIR"
[[ -x "${RELEASE_ROOT}/LokaHelloWorldStandaloneLoopMacOS.app/Contents/MacOS/LokaHelloWorldStandaloneLoopMacOS" ]] ||
  fail "the UB2 release omitted an autonomous loop"
[[ -f "${RELEASE_ROOT}/LokaScrapbookStandaloneLoopMacOS.app/Contents/Resources/ASSETS.LRP" ]] ||
  fail "the UB2 release lost Scrapbook assets"
[[ "$(find "${RELEASE_ROOT}" -maxdepth 1 -name '*StandaloneLoopMacOS.app' | wc -l | tr -d ' ')" == "5" ]] ||
  fail "the UB2 release did not contain exactly five loop bundles"
grep -Fq 'Architectures: arm64;x86_64' "${RELEASE_ROOT}/README.txt" ||
  fail "the UB2 release README did not record both architectures"

grep -q "^Loka 9.9.9 " "${RELEASE_ROOT}/README.txt" \
  || fail "the staged README does not carry the fixture source version (label must derive from CMakeLists, not a hardcode)"
cp "${RELEASE_ROOT}/README.txt" "${TEST_ROOT}/previous-readme"
rm -rf "${BUILD_ROOT}/apple/macos/LokaHelloWorldStandaloneLoopMacOS.app"
set +e
BUILD_DIR="${BUILD_ROOT}" LOKA_TEST_BUILD_LOG="${BUILD_LOG}" \
  LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub2.sh" >/dev/null 2>&1
FAILURE_STATUS=$?
set -e
[[ "${FAILURE_STATUS}" -ne 0 ]] ||
  fail "an incomplete UB2 input set was reported as success"
cmp -s "${TEST_ROOT}/previous-readme" "${RELEASE_ROOT}/README.txt" ||
  fail "an incomplete UB2 input set replaced the previous release"

set +e
INVALID_OUTPUT="$(ARCHS=x86_64 BUILD_DIR="${BUILD_ROOT}" \
  LOKA_TEST_BUILD_LOG="${BUILD_LOG}" \
  LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub2.sh" 2>&1)"
INVALID_STATUS=$?
set -e
[[ "${INVALID_STATUS}" -eq 2 && "${INVALID_OUTPUT}" == *'ARCHS=arm64;x86_64'* ]] ||
  fail "a thin UB2 release architecture set was not refused clearly"

echo "MacOSStandaloneUb2ReleaseTest: PASS"
