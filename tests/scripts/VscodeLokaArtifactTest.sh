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
if [[ ! -f out/extension.js ]]; then
  echo '[skip] vscode-loka artifact: out/ not built (run npm run build)'
  if [[ ${LOKA_REQUIRE_VSCODE_ARTIFACT:-0} == 1 ]]; then exit 1; fi
  exit 77
fi
exec node test/artifact.cjs
