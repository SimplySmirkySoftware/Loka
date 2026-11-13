#!/usr/bin/env bash
set -euo pipefail
script_dir="${BASH_SOURCE[0]%/*}"
cd "$script_dir/../../tools/vscode-loka"
missing() {
  local label=skip code=77
  if [[ ${LOKA_REQUIRE_VSCODE_SMOKE:-0} == 1 || ${LOKA_REQUIRE_VSCODE_RUNTIME:-0} == 1 ]]; then
    label=fail code=1
  fi
  echo "[$label] R1 R2 R3 R4 R5 T1 K3 K2 K1 T3 T1' T2: ${1} not found"
  exit "$code"
}
command -v node >/dev/null 2>&1 || missing "Node >=24 or 22.18+"
version=$(node --version) || missing "Node >=24 or 22.18+"
if [[ $version =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]}
  (( major >= 24 || (major == 22 && minor >= 18) )) || missing "Node >=24 or 22.18+"
else
  missing "Node >=24 or 22.18+"
fi
command -v python3 >/dev/null 2>&1 || missing python3
exec python3 test/smoke/supervisor.py node test/smoke/run.mjs
