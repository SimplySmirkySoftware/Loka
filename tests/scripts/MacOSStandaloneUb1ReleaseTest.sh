#!/usr/bin/env bash

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/loka-macos-standalone-ub1-XXXXXX)"
trap 'rm -rf "${TEST_ROOT}"' EXIT

fail() {
  echo "MacOSStandaloneUb1ReleaseTest: $*" >&2
  exit 1
}

FAKE_REPO="${TEST_ROOT}/repo"
mkdir -p \
  "${FAKE_REPO}/scripts/apple" \
  "${FAKE_REPO}/scripts/macos" \
  "${FAKE_REPO}/build/macos-10.4-ub1/universal" \
  "${FAKE_REPO}/build/macos-10.5-ub1/universal"
printf 'project(Loka VERSION 9.9.9 LANGUAGES CXX)\n' > "${FAKE_REPO}/CMakeLists.txt"
cp "${REPO_DIR}/scripts/macos-standalone-release-ub1.sh" "${FAKE_REPO}/scripts/"
cp "${REPO_DIR}/scripts/presentation-stage.sh" "${FAKE_REPO}/scripts/"
cp "${REPO_DIR}/scripts/apple/lib-xcode.sh" "${FAKE_REPO}/scripts/apple/"
cp "${REPO_DIR}/scripts/macos/lib-common.sh" "${FAKE_REPO}/scripts/macos/"
cp "${REPO_DIR}/scripts/macos/standalone-release-stage.sh" \
  "${FAKE_REPO}/scripts/macos/"
mkdir -p "${FAKE_REPO}/example/SmirkyCard"
cp "${REPO_DIR}/example/SmirkyCard/disk-scripts.txt" "${FAKE_REPO}/example/SmirkyCard/"
cp "${REPO_DIR}/example/SmirkyCard/disk-pictures.txt" "${FAKE_REPO}/example/SmirkyCard/"
smirkycard_scripts() {
  local script
  while IFS=$' \t\r' read -r script || [ -n "${script}" ]; do
    case "${script}" in ''|\#*) continue ;; esac
    printf '%s\n' "${script}"
  done < "${FAKE_REPO}/example/SmirkyCard/disk-scripts.txt"
  # Deliberate twin of the script grammar above.
  while IFS=$' \t\r' read -r script || [ -n "${script}" ]; do
    case "${script}" in ''|\#*) continue ;; esac
    printf '%s\n' "Pictures/${script}.PNG"
  done < "${FAKE_REPO}/example/SmirkyCard/disk-pictures.txt"
}

BUILD_LOG="${TEST_ROOT}/build.log"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  'printf "%s|%s\n" "${TARGET_SET:-}" "${ARCHS:-}" >"${LOKA_TEST_BUILD_LOG}"' \
  >"${FAKE_REPO}/scripts/macos/build-10_4.sh"
cp "${FAKE_REPO}/scripts/macos/build-10_4.sh" \
  "${FAKE_REPO}/scripts/macos/build-10_5.sh"
chmod +x \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub1.sh" \
  "${FAKE_REPO}/scripts/macos/build-10_4.sh" \
  "${FAKE_REPO}/scripts/macos/build-10_5.sh"

FAKE_LIPO="${TEST_ROOT}/lipo"
printf '%s\n' \
  '#!/usr/bin/env bash' \
  'set -euo pipefail' \
  '[[ "$1" == "-info" ]]' \
  'case "$2" in' \
  '  *leopard*) echo "Architectures in the fat file: $2 are: ppc7400 i386 x86_64" ;;' \
  '  *) echo "Architectures in the fat file: $2 are: ppc i386" ;;' \
  'esac' \
  >"${FAKE_LIPO}"
chmod +x "${FAKE_LIPO}"

ROOT_DIR="${FAKE_REPO}"
source "${FAKE_REPO}/scripts/macos/lib-common.sh"
UNIVERSAL_ROOT="${FAKE_REPO}/build/macos-10.4-ub1/universal"
while IFS='|' read -r target_name memberships output_shape rel_path; do
  loka_target_has_selection "${memberships}" standalone-release || continue
  case "${output_shape}" in
    executable)
      output_path="${UNIVERSAL_ROOT}/$(basename "${rel_path}")"
      mkdir -p "$(dirname "${output_path}")"
      printf '%s\n' "${target_name}" >"${output_path}"
      chmod +x "${output_path}"
      ;;
    bundle)
      bundle_rel_path="${rel_path%%/Contents/MacOS/*}"
      bundle_root="${UNIVERSAL_ROOT}/$(basename "${bundle_rel_path}")"
      output_path="${bundle_root}/Contents/MacOS/$(basename "${rel_path}")"
      mkdir -p "$(dirname "${output_path}")" "${bundle_root}/Contents/Resources/Pictures"
      printf '%s\n' "metadata-${target_name}" >"${bundle_root}/Contents/Info.plist"
      printf '%s\n' "${target_name}" >"${output_path}"
      chmod +x "${output_path}"
      if [[ "${target_name}" == "LokaScrapbookStandaloneLoopMacOS" ]]; then
        printf '%s\n' assets >"${bundle_root}/Contents/Resources/ASSETS.LRP"
      fi
      if [[ "${target_name}" == "LokaSmirkyCardMacOS" ]]; then
        while IFS= read -r script; do
          printf '// %s\n' "${script}" >"${bundle_root}/Contents/Resources/${script}"
        done < <(smirkycard_scripts)
      fi
      ;;
  esac
done < <(loka_target_manifest)
cp -R "${FAKE_REPO}/build/macos-10.4-ub1/universal/." \
  "${FAKE_REPO}/build/macos-10.5-ub1/universal/"

LOKA_TEST_BUILD_LOG="${BUILD_LOG}" LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub1.sh" tiger >/dev/null

[[ "$(cat "${BUILD_LOG}")" == "standalone-release|ppc;i386" ]] ||
  fail "the Tiger build did not select the complete standalone UB1 set"

RELEASE_ROOT="${FAKE_REPO}/build/release/macos-tiger-ub1"
[[ -x "${RELEASE_ROOT}/LokaSimpleTextMacOS.app/Contents/MacOS/LokaSimpleTextMacOS" ]] ||
  fail "the release omitted SimpleText"
[[ "$(cat "${RELEASE_ROOT}/LokaSimpleTextMacOS.app/Contents/Info.plist")" == "metadata-LokaSimpleTextMacOS" ]] ||
  fail "the release lost SimpleText bundle metadata"
[[ "$(find "${RELEASE_ROOT}" -maxdepth 1 -name '*.app' | wc -l | tr -d ' ')" == "8" ]] ||
  fail "the release did not contain exactly eight application bundles"
[[ -x "${RELEASE_ROOT}/LokaSmirkyCardMacOS.app/Contents/MacOS/LokaSmirkyCardMacOS" ]] ||
  fail "the release omitted SmirkyCard"
while IFS= read -r script; do
  [[ -f "${RELEASE_ROOT}/LokaSmirkyCardMacOS.app/Contents/Resources/${script}" ]] ||
    fail "the release SmirkyCard bundle lost Resources/${script}"
done < <(smirkycard_scripts)
grep -Fq 'LokaSmirkyCardMacOS (experimental) loads MAIN.JS from its bundle' "${RELEASE_ROOT}/README.txt" ||
  fail "the release README omitted SmirkyCard"
grep -Fq 'LokaSimpleViewerMacOS and LokaSimpleTextMacOS remain interactive.' "${RELEASE_ROOT}/README.txt" ||
  fail "the release README omitted the interactive applications"
[[ -x "${RELEASE_ROOT}/LokaSimpleViewerMacOS.app/Contents/MacOS/LokaSimpleViewerMacOS" ]] ||
  fail "the UB1 release omitted SimpleViewer"
[[ "$(cat "${RELEASE_ROOT}/LokaSimpleViewerMacOS.app/Contents/Info.plist")" == "metadata-LokaSimpleViewerMacOS" ]] ||
  fail "the release lost SimpleViewer bundle metadata"
[[ ! -e "${RELEASE_ROOT}/LokaSimpleViewerMacOS" ]] ||
  fail "the release retained an unbundled SimpleViewer executable"
[[ -x "${RELEASE_ROOT}/LokaHelloWorldStandaloneLoopMacOS.app/Contents/MacOS/LokaHelloWorldStandaloneLoopMacOS" ]] ||
  fail "the UB1 release omitted an autonomous loop"
[[ -f "${RELEASE_ROOT}/LokaScrapbookStandaloneLoopMacOS.app/Contents/Resources/ASSETS.LRP" ]] ||
  fail "the UB1 release lost Scrapbook assets"
[[ "$(find "${RELEASE_ROOT}" -maxdepth 1 -name '*StandaloneLoopMacOS.app' | wc -l | tr -d ' ')" == "5" ]] ||
  fail "the UB1 release did not contain exactly five loop bundles"
grep -Fq 'Architectures: ppc;i386' "${RELEASE_ROOT}/README.txt" ||
  fail "the UB1 release README did not record its architectures"
grep -q '^Loka 9.9.9 ' "${RELEASE_ROOT}/README.txt" ||
  fail "the staged README does not carry the fixture source version (label must derive from CMakeLists, not a hardcode)"

LOKA_TEST_BUILD_LOG="${BUILD_LOG}" LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub1.sh" leopard >/dev/null
[[ "$(cat "${BUILD_LOG}")" == "standalone-release|ppc;i386;x86_64" ]] ||
  fail "the Leopard build did not select the complete standalone UB1 set"
LEOPARD_RELEASE_ROOT="${FAKE_REPO}/build/release/macos-leopard-ub1"
grep -Fq 'Architectures: ppc7400;i386;x86_64' \
  "${LEOPARD_RELEASE_ROOT}/README.txt" ||
  fail "the Leopard stage did not validate and record the GCC 4.2 ppc7400 subtype"

cp "${RELEASE_ROOT}/README.txt" "${TEST_ROOT}/previous-readme"
rm -rf "${UNIVERSAL_ROOT}/LokaHelloWorldStandaloneLoopMacOS.app"
set +e
LOKA_TEST_BUILD_LOG="${BUILD_LOG}" LOKA_LIPO_BIN="${FAKE_LIPO}" \
  "${FAKE_REPO}/scripts/macos-standalone-release-ub1.sh" tiger >/dev/null 2>&1
FAILURE_STATUS=$?
set -e
[[ "${FAILURE_STATUS}" -ne 0 ]] ||
  fail "an incomplete UB1 input set was reported as success"
cmp -s "${TEST_ROOT}/previous-readme" "${RELEASE_ROOT}/README.txt" ||
  fail "an incomplete UB1 input set replaced the previous release"

echo "MacOSStandaloneUb1ReleaseTest: PASS"
