#!/usr/bin/env bash
set -euo pipefail
script_dir="${BASH_SOURCE[0]%/*}"
cd "$script_dir/../../tools/vscode-loka"
missing_node() {
  local label=skip code=77
  if [[ ${LOKA_REQUIRE_VSCODE_SMOKE:-0} == 1 || ${LOKA_REQUIRE_VSCODE_RUNTIME:-0} == 1 ]]; then
    label=fail code=1
  fi
  echo "[$label] R1 R2 T1 T3 T1' T2: Node >=24 or 22.18+ not found"
  exit "$code"
}
command -v node >/dev/null 2>&1 || missing_node
version=$(node --version) || missing_node
if [[ $version =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]}
  (( major >= 24 || (major == 22 && minor >= 18) )) || missing_node
else
  missing_node
fi
exec node test/smoke/run.mjs
