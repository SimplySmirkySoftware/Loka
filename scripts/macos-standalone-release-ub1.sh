#!/usr/bin/env bash

set -euo pipefail

PROFILE="${1:-}"
case "${PROFILE}" in
  tiger|10.4)
    PROFILE_NAME="tiger"
    DEFAULT_ARCHS="ppc;i386"
    BUILD_ROOT_NAME="macos-10.4-ub1"
    BUILD_SCRIPT_NAME="build-10_4.sh"
    ;;
  leopard|10.5)
    PROFILE_NAME="leopard"
    DEFAULT_ARCHS="ppc;i386;x86_64"
    BUILD_ROOT_NAME="macos-10.5-ub1"
    BUILD_SCRIPT_NAME="build-10_5.sh"
    ;;
  *)
    echo "Usage: $0 tiger|leopard" >&2
    exit 2
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="${PROJECT_DIR}"
. "${SCRIPT_DIR}/macos/lib-common.sh"
. "${SCRIPT_DIR}/macos/standalone-release-stage.sh"
. "${SCRIPT_DIR}/presentation-stage.sh"

if [[ -n "${TARGET:-}" ]]; then
  echo "The UB1 standalone release selects its complete target set; unset TARGET." >&2
  exit 2
fi
if [[ -n "${TARGET_SET:-}" && "${TARGET_SET}" != "standalone-release" ]]; then
  echo "The UB1 standalone release requires TARGET_SET=standalone-release." >&2
  exit 2
fi

export TARGET_SET=standalone-release
export ARCHS="${ARCHS:-${DEFAULT_ARCHS}}"
if [[ -z "${LOKA_LIPO_BIN:-}" ]]; then
  LOKA_LIPO_BIN="$(loka_find_selected_lipo || true)"
  if [[ -z "${LOKA_LIPO_BIN}" ]]; then
    echo "lipo was not found for the UB1 release." >&2
    exit 1
  fi
  export LOKA_LIPO_BIN
fi
if [[ "${PROFILE_NAME}" == "tiger" && "${ARCHS}" != "ppc;i386" ]]; then
  echo "The Tiger UB1 release requires ARCHS=ppc;i386 so its two slices can be merged." >&2
  exit 2
fi
if [[ "${PROFILE_NAME}" == "leopard" ]]; then
  case ";${ARCHS};" in
    *";ppc;"*|*";ppc7400;"*) ;;
    *)
      echo "The Leopard UB1 release requires a ppc or ppc7400 slice." >&2
      exit 2
      ;;
  esac
  case ";${ARCHS};" in
    *";i386;"*) ;;
    *)
      echo "The Leopard UB1 release requires an i386 slice." >&2
      exit 2
      ;;
  esac
fi

"${SCRIPT_DIR}/macos/${BUILD_SCRIPT_NAME}"

BUILD_ROOT="${PROJECT_DIR}/build/${BUILD_ROOT_NAME}"
UNIVERSAL_ROOT="${BUILD_ROOT}/universal"
STAGE_ROOT="${PROJECT_DIR}/build/release/macos-${PROFILE_NAME}-ub1"

ub1_staged_arch_name() {
  local requested_arch="$1"

  # GCC 4.2 records the Leopard `-arch ppc` slice as the ppc7400 Mach-O
  # subtype. Tiger's GCC 4.0 output remains the generic ppc subtype.
  if [[ "${PROFILE_NAME}" == "leopard" && "${requested_arch}" == "ppc" ]]; then
    echo ppc7400
  else
    echo "${requested_arch}"
  fi
}

ub1_staged_archs() {
  local requested_arch=""
  local staged_archs=()
  local requested_archs=()

  IFS=';' read -r -a requested_archs <<< "${ARCHS}"
  for requested_arch in "${requested_archs[@]}"; do
    staged_archs+=("$(ub1_staged_arch_name "${requested_arch}")")
  done
  local IFS=';'
  echo "${staged_archs[*]}"
}

loka_stage_standalone_release \
  "${UNIVERSAL_ROOT}" \
  "${STAGE_ROOT}" \
  flat \
  "$(ub1_staged_archs)" \
  UB1 \
  "${PROFILE_NAME}"
echo "Staged UB1 standalone release ($(ub1_staged_archs)): ${STAGE_ROOT}"
