#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "${BASH_SOURCE[0]%/*}" && pwd)
launcher="$script_dir/VscodeLokaTest.sh"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/bin"
check() {
  local expected=$1 required=$2 result=0
  PATH="$scratch/bin" LOKA_REQUIRE_NODE="$required" /bin/bash "$launcher" >"$scratch/log" 2>&1 || result=$?
  if [[ $result != "$expected" ]]; then
    cat "$scratch/log"
    echo "expected $expected, got $result" >&2
    exit 1
  fi
  if [[ $expected != 0 && $expected != 9 ]]; then
    grep -Fx '[skip] vscode-loka: node >= 22.18 not found' "$scratch/log" >/dev/null
  fi
}
check 77 0
check 1 1
for version in v20.19.0 v22.17.0 v23.11.0; do
  cat >"$scratch/bin/node" <<EOF_NODE
#!/bin/bash
echo '$version'
EOF_NODE
  chmod +x "$scratch/bin/node"
  check 77 0
  check 1 1
done
for version in v22.18.0 v24.19.0 v25.0.0; do
  cat >"$scratch/bin/node" <<EOF_NODE
#!/bin/bash
if [[ \$1 == --version ]]; then echo '$version'; exit 0; fi
[[ \$1 == --test ]] || exit 8
shift
(( \$# > 0 )) || exit 8
for file in "\$@"; do
  [[ \$file == test/*.test.ts && -f \$file ]] || exit 8
done
exit 0
EOF_NODE
  chmod +x "$scratch/bin/node"
  check 0 0
  check 0 1
done
cat >"$scratch/bin/node" <<'EOF_NODE'
#!/bin/bash
if [[ $1 == --version ]]; then echo v24.19.0; exit 0; fi
exit 9
EOF_NODE
chmod +x "$scratch/bin/node"
check 9 0
mkdir -p "$scratch/empty/tests/scripts" "$scratch/empty/tools/vscode-loka/test"
cp "$launcher" "$scratch/empty/tests/scripts/"
result=0
PATH="$scratch/bin" /bin/bash "$scratch/empty/tests/scripts/VscodeLokaTest.sh" >"$scratch/empty.log" 2>&1 || result=$?
[[ $result == 1 ]] || { echo "empty list exit=$result"; exit 1; }
grep -Fx 'vscode-loka: no source tests found' "$scratch/empty.log" >/dev/null
echo 'vscode-loka launcher fixtures: passed (absent, old, 23, supported, required, test failure, empty list)'
