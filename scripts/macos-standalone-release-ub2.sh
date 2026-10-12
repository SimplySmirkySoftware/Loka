#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="${PROJECT_DIR}"
. "${SCRIPT_DIR}/macos/lib-common.sh"
. "${SCRIPT_DIR}/macos/standalone-release-stage.sh"
. "${SCRIPT_DIR}/presentation-stage.sh"

if [[ -n "${TARGET:-}" ]]; then
  echo "The UB2 standalone release selects its complete target set; unset TARGET." >&2
  exit 2
fi
if [[ -n "${TARGET_SET:-}" && "${TARGET_SET}" != "standalone-release" ]]; then
  echo "The UB2 standalone release requires TARGET_SET=standalone-release." >&2
  exit 2
fi

export TARGET_SET=standalone-release
export ARCHS="${ARCHS:-arm64;x86_64}"
case "${ARCHS}" in
  arm64\;x86_64|x86_64\;arm64) ;;
  *)
    echo "The UB2 standalone release requires ARCHS=arm64;x86_64." >&2
    exit 2
    ;;
esac
# macOS 10.15 runs no 32-bit code, so it opens neither the shipped Tiger UB1 set
# (ppc;i386) nor an 11.0 build. The x86_64 slice starts at 10.13, the floor of the current SDK, which
# also covers 10.13 and 10.14; clang raises the arm64 slice to 11.0 by itself.
export DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-10.13}"
if [[ "${DEPLOYMENT_TARGET}" != "10.13" ]]; then
  echo "The UB2 standalone release requires DEPLOYMENT_TARGET=10.13." >&2
  exit 2
fi

BUILD_ROOT="${BUILD_DIR:-${PROJECT_DIR}/build/macos-ub2/${BUILD_TYPE:-Release}}"
export BUILD_DIR="${BUILD_ROOT}"
"${SCRIPT_DIR}/macos/build-ub2.sh"

STAGE_ROOT="${PROJECT_DIR}/build/release/macos-ub2"
loka_stage_standalone_release \
  "${BUILD_ROOT}" \
  "${STAGE_ROOT}" \
  tree \
  "${ARCHS}" \
  UB2 \
  modern
echo "Staged UB2 standalone release (${ARCHS}): ${STAGE_ROOT}"
