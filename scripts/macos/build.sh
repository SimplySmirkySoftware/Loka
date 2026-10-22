#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${ROOT_DIR}/scripts/macos/lib-common.sh"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-10.6}"
GENERATOR="${GENERATOR:-}"
ARCHS="${ARCHS:-}"
TARGET="${TARGET:-}"
OSX_SYSROOT="${OSX_SYSROOT:-}"
MAC_OS_10_4="${MAC_OS_10_4:-${MAC_LEGACY:-0}}"
MAC_OS_10_4_SYSROOT="${MAC_OS_10_4_SYSROOT:-${LEGACY_SYSROOT:-}}"

if [[ "${ARCHS}" == *";"* ]]; then
  if [[ "${CC:-}" == *"gcc-4."* ]] || [[ "${CXX:-}" == *"g++-4."* ]]; then
    echo "error: gcc-4.x cannot compile with dependency generation and multiple -arch flags in one build step." >&2
    echo "Use an arch-splitting wrapper script instead:" >&2
    echo "  ./scripts/macos/build-10_4.sh" >&2
    echo "  ./scripts/macos/build-10_5.sh" >&2
    echo "Or run single-arch with ARCHS=<one arch> ./scripts/macos/build.sh" >&2
    exit 1
  fi
fi

if [[ "${MAC_OS_10_4}" == "1" ]]; then
  BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos-10.4/${BUILD_TYPE}}"
else
  BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/macos/${BUILD_TYPE}}"
fi

if [[ -z "${GENERATOR}" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
  else
    GENERATOR="Unix Makefiles"
  fi
fi

CMAKE_ARGS=(
  "-S" "${ROOT_DIR}"
  "-B" "${BUILD_DIR}"
  "-G" "${GENERATOR}"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
)

if [[ -n "${CC:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_C_COMPILER=${CC}")
fi

if [[ -n "${CXX:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_CXX_COMPILER=${CXX}")
fi

if [[ -n "${ARCHS}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=${ARCHS}")
fi

if [[ -n "${OSX_SYSROOT}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}")
fi

if [[ "${MAC_OS_10_4}" == "1" ]]; then
  CMAKE_ARGS+=("-DLOKA_MAC_OS_10_4=ON")
  if [[ -n "${MAC_OS_10_4_SYSROOT}" ]]; then
    CMAKE_ARGS+=("-DLOKA_MAC_OS_10_4_SYSROOT=${MAC_OS_10_4_SYSROOT}")
  fi
fi

if [[ "${TARGET_SET:-}" == "standalone-release" ]]; then
  CMAKE_ARGS+=("-DLOKA_ENABLE_STANDALONE_TARGETS=ON")
  # The release ships SmirkyCard beside the loops (experimental).
  LOKA_BUILD_SMIRKYCARD=1
fi
loka_append_smirkycard_cmake_args

cmake "${CMAKE_ARGS[@]}"

if [[ -n "${TARGET}" ]]; then
  cmake --build "${BUILD_DIR}" --target "${TARGET}"
else
  cmake --build "${BUILD_DIR}"
fi
