#!/usr/bin/env bash
set -euo pipefail

source "${ROOT_DIR}/scripts/apple/lib-xcode.sh"

loka_target_manifest() {
  # target | selection memberships | output shape | executable path
  printf '%s\n' \
    "LokaFloppyBirdMacOS|default|executable|example/FloppyBird/LokaFloppyBirdMacOS" \
    "LokaHelloMacOS|default|executable|example/HelloWorld/LokaHelloMacOS" \
    "LokaMineMacOS|default|executable|example/MineSweeper/LokaMineMacOS" \
    "LokaSimpleViewerMacOS|default,standalone-release|bundle|example/SimpleViewer/LokaSimpleViewerMacOS.app/Contents/MacOS/LokaSimpleViewerMacOS" \
    "LokaSimpleTextMacOS|default,standalone-release|bundle|example/SimpleText/LokaSimpleTextMacOS.app/Contents/MacOS/LokaSimpleTextMacOS" \
    "LokaSmirkyCardMacOS|standalone-release|bundle|example/SmirkyCard/LokaSmirkyCardMacOS.app/Contents/MacOS/LokaSmirkyCardMacOS" \
    "LokaCardShowcaseMacOS|explicit|bundle|example/CardShowcase/LokaCardShowcaseMacOS.app/Contents/MacOS/LokaCardShowcaseMacOS" \
    "LokaSmirkBenchMacOS|default|executable|example/SmirkBench/LokaSmirkBenchMacOS" \
    "LokaLazyListMacOS|default|executable|example/LazyList/LokaLazyListMacOS" \
    "ScrapbookUIMacOS|default|bundle|example/ScrapbookUI/ScrapbookUIMacOS.app/Contents/MacOS/ScrapbookUIMacOS" \
    "LokaTutorialMacOS|default|executable|example/Tutorial/LokaTutorialMacOS" \
    "LokaScrapbookStandaloneFlowMacOS|explicit|bundle|apple/macos/LokaScrapbookStandaloneFlowMacOS.app/Contents/MacOS/LokaScrapbookStandaloneFlowMacOS" \
    "LokaScrapbookStandaloneLoopMacOS|standalone-release|bundle|apple/macos/LokaScrapbookStandaloneLoopMacOS.app/Contents/MacOS/LokaScrapbookStandaloneLoopMacOS" \
    "LokaHelloWorldStandaloneLoopMacOS|standalone-release|bundle|apple/macos/LokaHelloWorldStandaloneLoopMacOS.app/Contents/MacOS/LokaHelloWorldStandaloneLoopMacOS" \
    "LokaTutorialStandaloneLoopMacOS|standalone-release|bundle|apple/macos/LokaTutorialStandaloneLoopMacOS.app/Contents/MacOS/LokaTutorialStandaloneLoopMacOS" \
    "LokaMineSweeperStandaloneLoopMacOS|standalone-release|bundle|apple/macos/LokaMineSweeperStandaloneLoopMacOS.app/Contents/MacOS/LokaMineSweeperStandaloneLoopMacOS" \
    "LokaFloppyBirdStandaloneLoopMacOS|standalone-release|bundle|apple/macos/LokaFloppyBirdStandaloneLoopMacOS.app/Contents/MacOS/LokaFloppyBirdStandaloneLoopMacOS"
}

loka_target_has_selection() {
  local memberships="$1"
  local requested_selection="$2"

  case ",${memberships}," in
    *",${requested_selection},"*) return 0 ;;
  esac
  return 1
}

loka_targets_for_selection() {
  local requested_selection="$1"
  local target_name
  local memberships
  local _output_shape
  local _rel_path

  case "${requested_selection}" in
    ""|*[!a-z0-9-]*)
      echo "error: invalid macOS target selection '${requested_selection}'." >&2
      return 2
      ;;
  esac

  while IFS='|' read -r target_name memberships _output_shape _rel_path; do
    if loka_target_has_selection "${memberships}" "${requested_selection}"; then
      echo "${target_name}"
    fi
  done < <(loka_target_manifest)
}

loka_known_targets() {
  loka_targets_for_selection default
}

loka_requested_or_known_targets() {
  local selected_targets=""

  if [[ -n "${TARGET:-}" && -n "${TARGET_SET:-}" ]]; then
    echo "error: set TARGET or TARGET_SET, not both." >&2
    return 2
  fi
  if [[ -n "${TARGET:-}" ]]; then
    echo "${TARGET}"
    return 0
  fi

  selected_targets="$(loka_targets_for_selection "${TARGET_SET:-default}")"
  if [[ -z "${selected_targets}" ]]; then
    echo "error: unknown or empty macOS target set '${TARGET_SET:-default}'." >&2
    return 2
  fi
  echo "${selected_targets}"
}

loka_find_selected_tool() {
  local tool="$1"
  local found=""

  if command -v xcrun >/dev/null 2>&1; then
    found="$(xcrun -find "${tool}" 2>/dev/null || true)"
    if [[ -n "${found}" && -x "${found}" ]]; then
      echo "${found}"
      return 0
    fi
  fi

  if command -v "${tool}" >/dev/null 2>&1; then
    command -v "${tool}"
    return 0
  fi

  return 1
}

loka_find_first_selected_tool() {
  local tool=""
  local found=""

  for tool in "$@"; do
    found="$(loka_find_selected_tool "${tool}" || true)"
    if [[ -n "${found}" ]]; then
      echo "${found}"
      return 0
    fi
  done

  return 1
}

loka_find_xcode_3_lipo() {
  local candidate=""

  # Xcode 3.2.6 predates xcrun. Its Mac cctools may be absent from
  # /Developer/usr/bin while the same universal lipo is installed with the
  # iPhone platform tools.
  for candidate in \
    /Developer/usr/bin/lipo \
    /Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/lipo; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done
  return 1
}

loka_find_selected_lipo() {
  local candidate=""

  if [[ -n "${LOKA_LIPO_BIN:-}" ]]; then
    if [[ ! -x "${LOKA_LIPO_BIN}" ]]; then
      echo "error: LOKA_LIPO_BIN is not executable: ${LOKA_LIPO_BIN}" >&2
      return 1
    fi
    echo "${LOKA_LIPO_BIN}"
    return 0
  fi

  # Keep every tool in one selected legacy toolchain. On Mavericks,
  # `xcrun -find lipo` can report /usr/bin/lipo successfully even though that
  # shim later refuses /Developer because Xcode 3.2.6 has no xcrun.
  case "${CC:-};${CXX:-}" in
    *"/Developer/usr/bin/"*)
      candidate="$(loka_find_xcode_3_lipo || true)"
      if [[ -n "${candidate}" ]]; then
        echo "${candidate}"
        return 0
      fi
      ;;
  esac

  if command -v xcrun >/dev/null 2>&1; then
    candidate="$(xcrun -find lipo 2>/dev/null || true)"
    if [[ -n "${candidate}" && -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  fi

  candidate="$(loka_find_xcode_3_lipo || true)"
  if [[ -n "${candidate}" ]]; then
    echo "${candidate}"
    return 0
  fi

  if command -v lipo >/dev/null 2>&1; then
    command -v lipo
    return 0
  fi
  return 1
}

loka_find_selected_sdk() {
  local sdk_name="$1"
  local developer_dir=""
  local candidate=""
  local candidates=()

  developer_dir="$(loka_selected_developer_dir || true)"
  if [[ -n "${developer_dir}" ]]; then
    candidates+=("${developer_dir}/SDKs/${sdk_name}")
    candidates+=("${developer_dir}/Platforms/MacOSX.platform/Developer/SDKs/${sdk_name}")
  fi

  candidates+=("/Developer/SDKs/${sdk_name}")
  candidates+=("/Applications/Xcode.app/Contents/Developer/SDKs/${sdk_name}")
  candidates+=("/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/${sdk_name}")

  for candidate in "${candidates[@]}"; do
    if [[ -d "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  return 1
}

loka_target_record() {
  local requested_target="$1"
  local target_name
  local selection
  local output_shape
  local rel_path

  while IFS='|' read -r target_name selection output_shape rel_path; do
    if [[ "${target_name}" == "${requested_target}" ]]; then
      printf '%s|%s|%s|%s\n' \
        "${target_name}" "${selection}" "${output_shape}" "${rel_path}"
      return 0
    fi
  done < <(loka_target_manifest)

  return 1
}

loka_target_rel_path() {
  local requested_target="$1"
  local record

  record="$(loka_target_record "${requested_target}" || true)"
  if [[ -z "${record}" ]]; then
    return 1
  fi
  echo "${record##*|}"
}

loka_cleanup_stale_output_dirs() {
  local build_dir="$1"
  local target_name
  local _selection
  local output_shape
  local rel_path
  local cleanup_rel_path

  while IFS='|' read -r target_name _selection output_shape rel_path; do
    case "${output_shape}" in
      executable) cleanup_rel_path="${rel_path}" ;;
      bundle) cleanup_rel_path="${rel_path%%/Contents/MacOS/*}" ;;
      *)
        echo "error: unknown output shape '${output_shape}' for ${target_name}." >&2
        return 1
        ;;
    esac

    if [[ -d "${build_dir}/${cleanup_rel_path}" ]]; then
      rm -rf "${build_dir:?}/${cleanup_rel_path}"
    fi
  done < <(loka_target_manifest)
}

loka_build_requested_or_known_targets() {
  local root_dir="$1"
  local build_script="${root_dir}/scripts/macos/build.sh"
  local requested_targets=""
  local original_target="${TARGET:-}"
  local t

  requested_targets="$(loka_requested_or_known_targets)" || return
  for t in ${requested_targets}; do
    export TARGET="${t}"
    "${build_script}"
  done
  if [[ -n "${original_target}" ]]; then
    export TARGET="${original_target}"
  else
    unset TARGET || true
  fi
}

loka_merge_requested_or_known_targets_two_arch() {
  local build_root="$1"
  local build_cfg="$2"
  local arch_a="$3"
  local arch_b="$4"
  local requested_targets=""
  local target_name

  mkdir -p "${build_root}/universal"

  requested_targets="$(loka_requested_or_known_targets)" || return
  for target_name in ${requested_targets}; do
    loka_merge_target_archs \
      "${build_root}" "${build_cfg}" "${arch_a};${arch_b}" "${target_name}"
  done
}

loka_merge_target_archs() {
  local build_root="$1"
  local build_cfg="$2"
  local archs_csv="$3"
  local requested_target="$4"
  local record
  local target_name
  local _selection
  local output_shape
  local rel_path
  local bundle_rel_path
  local source_bundle
  local out_bundle
  local out_bin
  local lipo_bin=""
  local staging_root=""
  local staging_bundle
  local arch
  local bin
  local merge_inputs=()
  local IFS=';'

  record="$(loka_target_record "${requested_target}" || true)"
  if [[ -z "${record}" ]]; then
    return
  fi
  IFS='|' read -r target_name _selection output_shape rel_path <<< "${record}"
  IFS=';'
  lipo_bin="$(loka_find_selected_lipo || true)"
  if [[ -z "${lipo_bin}" ]]; then
    echo "error: lipo was not found for ${target_name}." >&2
    return 1
  fi

  for arch in ${archs_csv}; do
    bin="${build_root}/${build_cfg}-${arch}/${rel_path}"
    if [[ ! -f "${bin}" ]]; then
      echo "error: missing required ${arch} input for ${target_name}: ${bin}" >&2
      return 1
    fi
    merge_inputs+=("${bin}")
  done

  if [[ "${#merge_inputs[@]}" -eq 0 ]]; then
    echo "error: no architectures requested for ${target_name}." >&2
    return 1
  fi

  case "${output_shape}" in
    executable)
      out_bin="${build_root}/universal/$(basename "${rel_path}")"
      ;;
    bundle)
      bundle_rel_path="${rel_path%%/Contents/MacOS/*}"
      source_bundle=""
      for arch in ${archs_csv}; do
        if [[ -d "${build_root}/${build_cfg}-${arch}/${bundle_rel_path}" ]]; then
          source_bundle="${build_root}/${build_cfg}-${arch}/${bundle_rel_path}"
          break
        fi
      done
      if [[ -z "${source_bundle}" ]]; then
        echo "error: no source bundle found for ${target_name}." >&2
        return 1
      fi
      out_bundle="${build_root}/universal/$(basename "${bundle_rel_path}")"
      if ! staging_root="$(mktemp -d \
        "${build_root}/universal/.${target_name}.XXXXXX")"; then
        echo "error: could not create a staging directory for ${target_name}." >&2
        return 1
      fi
      staging_bundle="${staging_root}/$(basename "${bundle_rel_path}")"
      if ! cp -R "${source_bundle}" "${staging_bundle}"; then
        rm -rf "${staging_root}"
        return 1
      fi
      out_bin="${staging_bundle}/Contents/MacOS/$(basename "${rel_path}")"
      ;;
    *)
      echo "error: unknown output shape '${output_shape}' for ${target_name}." >&2
      return 1
      ;;
  esac

  if [[ "${#merge_inputs[@]}" -ge 2 ]]; then
    if ! "${lipo_bin}" -create "${merge_inputs[@]}" -output "${out_bin}"; then
      [[ -z "${staging_root}" ]] || rm -rf "${staging_root}"
      return 1
    fi
    if ! "${lipo_bin}" -info "${out_bin}"; then
      [[ -z "${staging_root}" ]] || rm -rf "${staging_root}"
      return 1
    fi
  else
    if ! cp -f "${merge_inputs[0]}" "${out_bin}"; then
      [[ -z "${staging_root}" ]] || rm -rf "${staging_root}"
      return 1
    fi
  fi

  if [[ -n "${staging_root}" ]]; then
    if ! rm -rf "${out_bundle}"; then
      rm -rf "${staging_root}"
      return 1
    fi
    if ! mv "${staging_bundle}" "${out_bundle}"; then
      rm -rf "${staging_root}"
      return 1
    fi
    rm -rf "${staging_root}"
  fi
}

loka_merge_requested_or_known_targets_multi_arch() {
  local build_root="$1"
  local build_cfg="$2"
  local archs_csv="$3"
  local requested_targets=""
  local target_name

  mkdir -p "${build_root}/universal"

  requested_targets="$(loka_requested_or_known_targets)" || return
  for target_name in ${requested_targets}; do
    loka_merge_target_archs \
      "${build_root}" "${build_cfg}" "${archs_csv}" "${target_name}"
  done
}
