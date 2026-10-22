#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${ROOT_DIR}/scripts/macos/lib-common.sh"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-10.5}"
ARCHS="${ARCHS:-xcode-standard-32-64}"
BUILD_VARIANT="${ARCHS//[^A-Za-z0-9_.-]/-}"
CMAKE_OSX_ARCHITECTURES_VALUE="${ARCHS}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-xcodeproj-leopard-ub1/${BUILD_TYPE}-${BUILD_VARIANT}}"
OSX_SYSROOT="${OSX_SYSROOT:-macosx}"
XCODE_ARCHS="${XCODE_ARCHS:-}"
XCODE_VALID_ARCHS="${XCODE_VALID_ARCHS:-}"

case "${ARCHS}" in
  xcode-standard-32-64)
    ARCHS='$(ARCHS_STANDARD_32_64_BIT)'
    CMAKE_OSX_ARCHITECTURES_VALUE=""
    if [[ -z "${XCODE_ARCHS}" ]]; then
      XCODE_ARCHS='$(ARCHS_STANDARD_32_64_BIT)'
    fi
    ;;
esac

loka_require_full_xcode_for_generator "gen-xcodeproj-leopard-ub1" "5"
loka_require_cmake_xcode_generator

CMAKE_ARGS=(
  "-S" "${ROOT_DIR}"
  "-B" "${BUILD_DIR}"
  "-G" "Xcode"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}"
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
  "-DCMAKE_SUPPRESS_REGENERATION=ON"
  "-DLOKA_MACOS_EXPLICIT_NO_ARC_FLAGS=OFF"
  "-DLOKA_MACOS_SUPPRESS_ARCLITE_LINK=ON"
  "-DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC=NO"
  "-DCMAKE_XCODE_ATTRIBUTE_CLANG_LINK_OBJC_RUNTIME=NO"
  "-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO"
)

if [[ -n "${CMAKE_OSX_ARCHITECTURES_VALUE}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES_VALUE}")
else
  CMAKE_ARGS+=("-U" "CMAKE_OSX_ARCHITECTURES")
fi

if [[ -n "${XCODE_ARCHS}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_XCODE_ATTRIBUTE_ARCHS=${XCODE_ARCHS}")
fi

if [[ -n "${XCODE_VALID_ARCHS}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_XCODE_ATTRIBUTE_VALID_ARCHS=${XCODE_VALID_ARCHS}")
fi

if [[ -n "${CC:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_C_COMPILER=${CC}")
fi

if [[ -n "${CXX:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=${CXX}")
fi

loka_append_smirkycard_cmake_args legacy-c

echo "[gen-xcodeproj-leopard-ub1] ROOT_DIR=${ROOT_DIR}"
echo "[gen-xcodeproj-leopard-ub1] BUILD_DIR=${BUILD_DIR}"
echo "[gen-xcodeproj-leopard-ub1] SDK=${OSX_SYSROOT}"
echo "[gen-xcodeproj-leopard-ub1] ARCHS=${ARCHS} DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
if [[ -n "${XCODE_ARCHS}" ]]; then
  echo "[gen-xcodeproj-leopard-ub1] XCODE_ARCHS=${XCODE_ARCHS}"
fi
if [[ -n "${XCODE_VALID_ARCHS}" ]]; then
  echo "[gen-xcodeproj-leopard-ub1] XCODE_VALID_ARCHS=${XCODE_VALID_ARCHS}"
fi
if [[ "${LOKA_BUILD_SMIRKYCARD:-0}" == "1" ]]; then
  echo "[gen-xcodeproj-leopard-ub1] LOKA_BUILD_SMIRKYCARD=1 (SmirkyCard targets included)"
fi
echo "[gen-xcodeproj-leopard-ub1] running: cmake ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}"

echo "Generated Leopard UB1 Xcode project in:"
echo "  ${BUILD_DIR}"
echo "SDK: ${OSX_SYSROOT}"
echo "Archs: ${ARCHS}"
if [[ -n "${XCODE_ARCHS}" ]]; then
  echo "Xcode ARCHS: ${XCODE_ARCHS}"
fi
echo "Deployment target: ${DEPLOYMENT_TARGET}"
