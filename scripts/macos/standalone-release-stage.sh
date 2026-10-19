#!/usr/bin/env bash

# Stages the target manifest's complete standalone-release selection from an
# already-built tree. The caller supplies the source layout because legacy
# split builds flatten their lipo outputs while modern CMake builds retain the
# target-relative paths.
# The canonical source version, read from the top-level CMake project() call
# so staged payload labels cannot drift from the version being built (the
# hardcoded labels this replaces went stale at the 0.0.4 -> 0.0.5 bump).
loka_source_version() {
  local stage_dir root version
  stage_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  root="$(cd "${stage_dir}/../.." && pwd)"
  version="$(sed -n 's/^project(Loka VERSION \([0-9.][0-9.]*\) LANGUAGES CXX)$/\1/p' "${root}/CMakeLists.txt")"
  if [ -z "${version}" ]; then
    echo "Could not read the Loka source version from ${root}/CMakeLists.txt" >&2
    return 1
  fi
  printf '%s\n' "${version}"
}

# SmirkyCard's card scripts, in copy order. This reader stays parallel with
# example/SmirkyCard/CMakeLists.txt (which copies them into the bundle's
# Resources), scripts/mame-*-disk*, scripts/release/classic-disks.py and
# scripts/win32-standalone-flow.ps1: blank lines and full-line # comments are
# skipped, surrounding spaces, tabs and CRs are trimmed.
loka_smirkycard_manifest() {
  local manifest="$1" suffix="$2"
  local stage_dir root script
  stage_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  root="$(cd "${stage_dir}/../.." && pwd)"
  while IFS=$' \t\r' read -r script || [ -n "${script}" ]; do
    case "${script}" in ''|\#*) continue ;; esac
    printf '%s\n' "${script}${suffix}"
  done < "${root}/example/SmirkyCard/${manifest}"
}

loka_populate_standalone_release_stage() {
  local destination="$1"
  local target_name=""
  local record=""
  local _memberships=""
  local output_shape=""
  local rel_path=""
  local bundle_rel_path=""
  local source_item=""
  local destination_item=""
  local destination_binary=""
  local arch=""
  local release_archs=()
  local count=0

  case "${LOKA_STANDALONE_SOURCE_LAYOUT}" in
    flat|tree) ;;
    *)
      echo "Unknown standalone release source layout '${LOKA_STANDALONE_SOURCE_LAYOUT}'." >&2
      return 2
      ;;
  esac

  for target_name in $(loka_targets_for_selection standalone-release); do
    record="$(loka_target_record "${target_name}")"
    IFS='|' read -r target_name _memberships output_shape rel_path <<< "${record}"
    case "${output_shape}" in
      executable)
        if [[ "${LOKA_STANDALONE_SOURCE_LAYOUT}" == "flat" ]]; then
          source_item="${LOKA_STANDALONE_SOURCE_ROOT}/$(basename "${rel_path}")"
        else
          source_item="${LOKA_STANDALONE_SOURCE_ROOT}/${rel_path}"
        fi
        destination_item="${destination}/$(basename "${rel_path}")"
        if [[ ! -f "${source_item}" ]]; then
          echo "${LOKA_STANDALONE_FORMAT_NAME} release executable not found: ${source_item}" >&2
          return 1
        fi
        cp "${source_item}" "${destination_item}"
        destination_binary="${destination_item}"
        ;;
      bundle)
        bundle_rel_path="${rel_path%%/Contents/MacOS/*}"
        if [[ "${LOKA_STANDALONE_SOURCE_LAYOUT}" == "flat" ]]; then
          source_item="${LOKA_STANDALONE_SOURCE_ROOT}/$(basename "${bundle_rel_path}")"
        else
          source_item="${LOKA_STANDALONE_SOURCE_ROOT}/${bundle_rel_path}"
        fi
        destination_item="${destination}/$(basename "${bundle_rel_path}")"
        if [[ ! -d "${source_item}" ]]; then
          echo "${LOKA_STANDALONE_FORMAT_NAME} release bundle not found: ${source_item}" >&2
          return 1
        fi
        if [[ -x /usr/bin/ditto ]]; then
          /usr/bin/ditto "${source_item}" "${destination_item}"
        else
          cp -R "${source_item}" "${destination_item}"
        fi
        destination_binary="${destination_item}/Contents/MacOS/$(basename "${rel_path}")"
        ;;
      *)
        echo "Unknown standalone release output shape '${output_shape}' for ${target_name}." >&2
        return 2
        ;;
    esac

    if [[ ! -f "${destination_binary}" ]]; then
      echo "Staged ${LOKA_STANDALONE_FORMAT_NAME} executable not found: ${destination_binary}" >&2
      return 1
    fi
    IFS=';' read -r -a release_archs <<< "${LOKA_STANDALONE_STAGED_ARCHS}"
    for arch in "${release_archs[@]}"; do
      if ! loka_binary_contains_arch "${destination_binary}" "${arch}"; then
        echo "Staged ${LOKA_STANDALONE_FORMAT_NAME} executable does not contain ${arch}: ${destination_binary}" >&2
        return 1
      fi
    done
    count=$((count + 1))
  done

  if [[ "${count}" -ne 8 ]]; then
    echo "The ${LOKA_STANDALONE_FORMAT_NAME} standalone release must contain five loops plus SimpleViewer, SimpleText and SmirkyCard; found ${count}." >&2
    return 1
  fi
  # SmirkyCard loads its card scripts from its own bundle.
  local script=""
  local scripts=""
  scripts="$(loka_smirkycard_manifest disk-scripts.txt '')" || return 1
  if [[ -z "${scripts}" ]]; then
    echo "The SmirkyCard script manifest lists no scripts." >&2
    return 1
  fi
  local pictures=""
  pictures="$(loka_smirkycard_manifest disk-pictures.txt .PNG | sed 's|^|Pictures/|')" || return 1
  scripts="${scripts}${pictures:+$'\n'}${pictures}"
  # One trimmed manifest line is one filename: no word splitting or globbing.
  while IFS= read -r script; do
    if [[ ! -f "${destination}/LokaSmirkyCardMacOS.app/Contents/Resources/${script}" ]]; then
      echo "Staged ${LOKA_STANDALONE_FORMAT_NAME} SmirkyCard bundle lacks Resources/${script}." >&2
      return 1
    fi
  done <<< "${scripts}"
  local source_version
  source_version="$(loka_source_version)" || return 1
  printf '%s\n' \
    "Loka ${source_version} ${LOKA_STANDALONE_FORMAT_NAME} Release applications" \
    '' \
    "Profile: ${LOKA_STANDALONE_PROFILE_NAME}" \
    "Architectures: ${LOKA_STANDALONE_STAGED_ARCHS}" \
    '' \
    'The five StandaloneLoop applications run their UI tour repeatedly.' \
    'Quit a loop application to stop it. LokaSimpleViewerMacOS and LokaSimpleTextMacOS remain interactive.' \
    'LokaSmirkyCardMacOS (experimental) loads MAIN.JS from its bundle; its buttons open MINES.JS and VIEWER.JS.' \
    >"${destination}/README.txt"
}

loka_stage_standalone_release() (
  set -euo pipefail
  if [[ $# -ne 6 ]]; then
    echo "loka_stage_standalone_release requires source, destination, layout, architectures, format, and profile." >&2
    return 2
  fi
  # Keep one callback configuration inside this operation's subshell while the
  # shared presentation transaction retains its established two-argument API.
  LOKA_STANDALONE_SOURCE_ROOT="$1"
  LOKA_STANDALONE_STAGE_ROOT="$2"
  LOKA_STANDALONE_SOURCE_LAYOUT="$3"
  LOKA_STANDALONE_STAGED_ARCHS="$4"
  LOKA_STANDALONE_FORMAT_NAME="$5"
  LOKA_STANDALONE_PROFILE_NAME="$6"
  loka_replace_stage_directory \
    "${LOKA_STANDALONE_STAGE_ROOT}" \
    loka_populate_standalone_release_stage
)
