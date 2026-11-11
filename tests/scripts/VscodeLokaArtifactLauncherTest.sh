#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "${BASH_SOURCE[0]%/*}" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/bin" "$scratch/repo/tests/scripts" "$scratch/repo/tools/vscode-loka/test"
cp "$script_dir/VscodeLokaArtifactTest.sh" "$scratch/repo/tests/scripts/"
launcher="$scratch/repo/tests/scripts/VscodeLokaArtifactTest.sh"
check() {
  local expected=$1 node_required=$2 artifact_required=$3 result=0
  PATH="$scratch/bin" LOKA_REQUIRE_NODE="$node_required" LOKA_REQUIRE_VSCODE_ARTIFACT="$artifact_required" \
    /bin/bash "$launcher" >"$scratch/log" 2>&1 || result=$?
  [[ $result == "$expected" ]] || { cat "$scratch/log"; echo "expected $expected, got $result"; exit 1; }
  cat "$scratch/log"
  echo "artifact launcher fixture: exit $result (require node=$node_required artifact=$artifact_required)"
}
check 77 0 0
check 1 1 0
for version in v22.17.0 v23.11.0; do
  printf '#!/bin/bash\necho %s\n' "$version" >"$scratch/bin/node"
  chmod +x "$scratch/bin/node"
  check 77 0 0
  check 1 1 0
done
for version in v22.18.0 v24.19.0; do
  cat >"$scratch/bin/node" <<EOF_NODE
#!/bin/bash
if [[ \$1 == --version ]]; then echo '$version'; exit 0; fi
[[ \$# == 1 && \$1 == test/artifact.cjs ]] || exit 8
exit 0
EOF_NODE
  chmod +x "$scratch/bin/node"
  check 77 0 0
  grep -Fx '[skip] vscode-loka artifact: out/ not built (run npm run build)' "$scratch/log" >/dev/null
  check 1 1 1
  mkdir -p "$scratch/repo/tools/vscode-loka/out"
  touch "$scratch/repo/tools/vscode-loka/out/extension.js"
  check 0 1 1
  rm -rf "$scratch/repo/tools/vscode-loka/out"
done
mkdir -p "$scratch/repo/tools/vscode-loka/out"
touch "$scratch/repo/tools/vscode-loka/out/extension.js"
sed -i 's/exit 0$/exit 9/' "$scratch/bin/node"
check 9 0 0
echo 'vscode-loka artifact launcher fixtures: passed'
