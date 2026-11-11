#!/usr/bin/env bash
set -euo pipefail
script_dir="${BASH_SOURCE[0]%/*}"
cd "$script_dir/../../tools/vscode-loka"
skip() {
  echo '[skip] vscode-loka: node >= 22.18 not found'
  if [[ ${LOKA_REQUIRE_NODE:-0} == 1 ]]; then exit 1; fi
  exit 77
}
command -v node >/dev/null 2>&1 || skip
version=$(node --version) || skip
if [[ $version =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  major=${BASH_REMATCH[1]}
  minor=${BASH_REMATCH[2]}
  (( major >= 24 || (major == 22 && minor >= 18) )) || skip
else
  skip
fi
shopt -s nullglob
tests=(test/*.test.ts)
if (( ${#tests[@]} == 0 )); then
  echo 'vscode-loka: no source tests found' >&2
  exit 1
fi
exec node --test "${tests[@]}"
