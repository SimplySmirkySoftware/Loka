#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${ROOT_DIR}/scripts/macos/lib-common.sh"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
# Default to Xcode's standard architecture preset so modern Xcode versions can
# choose the right host/default universal set. Override this when you need an
# explicit arch list, for example:
#   ARCHS="arm64;x86_64" DEPLOYMENT_TARGET=11.0 ./scripts/macos/gen-xcodeproj.sh
# For older single-arch experiments, for example:
#   ARCHS="i386" DEPLOYMENT_TARGET=10.5 ./scripts/macos/gen-xcodeproj.sh
ARCH_MODE="${ARCHS:-xcode-standard}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-11}"
OSX_SYSROOT="${OSX_SYSROOT:-macosx}"
CMAKE_OSX_ARCHITECTURES_VALUE="${ARCH_MODE}"
XCODE_ARCHS="${XCODE_ARCHS:-}"
DISPLAY_ARCHS="${ARCH_MODE}"

case "${ARCH_MODE}" in
  xcode-standard|archs-standard)
    ARCH_MODE="xcode-standard"
    DISPLAY_ARCHS='$(ARCHS_STANDARD)'
    CMAKE_OSX_ARCHITECTURES_VALUE=""
    if [[ -z "${XCODE_ARCHS}" ]]; then
      XCODE_ARCHS='$(ARCHS_STANDARD)'
    fi
    ;;
esac

BUILD_VARIANT="${ARCH_MODE//[^A-Za-z0-9_.-]/-}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-xcodeproj/${BUILD_TYPE}-${BUILD_VARIANT}}"

echo "[gen-xcodeproj] ROOT_DIR=${ROOT_DIR}"
echo "[gen-xcodeproj] BUILD_DIR=${BUILD_DIR}"
echo "[gen-xcodeproj] ARCH_MODE=${ARCH_MODE} ARCHS=${DISPLAY_ARCHS} DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
if [[ -n "${XCODE_ARCHS}" ]]; then
  echo "[gen-xcodeproj] XCODE_ARCHS=${XCODE_ARCHS}"
fi

loka_require_full_xcode_for_generator "gen-xcodeproj"
loka_require_cmake_xcode_generator

CMAKE_ARGS=(
  "-S" "${ROOT_DIR}"
  "-B" "${BUILD_DIR}"
  "-G" "Xcode"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
)

if [[ -n "${CMAKE_OSX_ARCHITECTURES_VALUE}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_VALUE}")
else
  CMAKE_ARGS+=("-U" "CMAKE_OSX_ARCHITECTURES")
fi

if [[ -n "${XCODE_ARCHS}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_XCODE_ATTRIBUTE_ARCHS=${XCODE_ARCHS}")
fi

if [[ -n "${CC:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_C_COMPILER=${CC}")
fi

if [[ -n "${CXX:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=${CXX}")
fi

CMAKE_ARGS+=("-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}")

loka_append_smirkycard_cmake_args
if [[ "${LOKA_BUILD_SMIRKYCARD:-0}" == "1" ]]; then
  echo "[gen-xcodeproj] LOKA_BUILD_SMIRKYCARD=1 (SmirkyCard targets included)"
fi
echo "[gen-xcodeproj] running: cmake ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}"

echo "Generated Xcode project in:"
echo "  ${BUILD_DIR}"
echo "Arch mode: ${ARCH_MODE}"
echo "Archs: ${DISPLAY_ARCHS}"
if [[ -n "${XCODE_ARCHS}" ]]; then
  echo "Xcode ARCHS: ${XCODE_ARCHS}"
fi
echo "Deployment target: ${DEPLOYMENT_TARGET}"
