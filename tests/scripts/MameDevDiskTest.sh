#!/usr/bin/env bash

set -euo pipefail

TEST_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$TEST_SCRIPT_DIR/../.." && pwd)"
SUBJECT="${LOKA_MAME_DEV_DISK_SH:-$REPO_DIR/scripts/mame-dev-disk.sh}"

SANDBOX="$(mktemp -d)"

cleanup() {
  rm -rf "$SANDBOX"
}
trap cleanup EXIT

mkdir -p "$SANDBOX/bin" "$SANDBOX/home"
touch "$SANDBOX/app.bin" "$SANDBOX/first data" "$SANDBOX/second-data"
printf 'boot-template\n' > "$SANDBOX/boot.hd"

cat > "$SANDBOX/bin/hformat" <<'EOF'
#!/usr/bin/env bash
printf 'hformat' >> "$MAME_DEV_DISK_TEST_LOG"
printf ' <%s>' "$@" >> "$MAME_DEV_DISK_TEST_LOG"
printf '\n' >> "$MAME_DEV_DISK_TEST_LOG"
EOF

cat > "$SANDBOX/bin/hcopy" <<'EOF'
#!/usr/bin/env bash
printf 'hcopy' >> "$MAME_DEV_DISK_TEST_LOG"
printf ' <%s>' "$@" >> "$MAME_DEV_DISK_TEST_LOG"
printf '\n' >> "$MAME_DEV_DISK_TEST_LOG"
EOF

cat > "$SANDBOX/bin/humount" <<'EOF'
#!/usr/bin/env bash
printf 'humount\n' >> "$MAME_DEV_DISK_TEST_LOG"
EOF

chmod +x "$SANDBOX/bin/hformat" "$SANDBOX/bin/hcopy" "$SANDBOX/bin/humount"

fail() {
  printf 'FAIL: %s\n' "$1" >&2
  exit 1
}

run_case() {
  local case_name="$1"
  shift

  MAME_DEV_DISK_TEST_LOG="$SANDBOX/$case_name.log" \
  MAME_ENV_FILE="$SANDBOX/missing.env" \
  MAME_HDA="$SANDBOX/boot.hd" \
  MAME_HOMEPATH="$SANDBOX/home" \
  MAME_DEV_HDA="$SANDBOX/$case_name.hd" \
  RETRO68_TOOLCHAIN_BIN="$SANDBOX/bin" \
    /bin/bash "$SUBJECT" "$SANDBOX/app.bin" "$@" >/dev/null

  [ -f "$SANDBOX/$case_name.hd" ] ||
    fail "$case_name did not produce the development disk"
}

# macOS ships Bash 3.2, where expanding an empty array under set -u fails.
# The no-data form is the one used by SimpleViewer's VS Code task.
run_case no-data
# The temporary suffix is the script process ID, so compare the stable calls
# separately and only require the hformat target to have the expected prefix.
grep -F "hformat <-l> <LokaDev> <$SANDBOX/no-data.hd." \
  "$SANDBOX/no-data.log" >/dev/null || fail "no-data hformat arguments changed"
grep -Fx "hcopy <-m> <$SANDBOX/app.bin> <:>" \
  "$SANDBOX/no-data.log" >/dev/null || fail "no-data app copy changed"
[ "$(grep -c '^hcopy ' "$SANDBOX/no-data.log")" -eq 1 ] ||
  fail "no-data case copied an unexpected plain-data file"

run_case with-data "$SANDBOX/first data" "$SANDBOX/second-data"
grep -Fx "hcopy <-r> <$SANDBOX/first data> <:>" \
  "$SANDBOX/with-data.log" >/dev/null || fail "spaced data path was not preserved"
grep -Fx "hcopy <-r> <$SANDBOX/second-data> <:>" \
  "$SANDBOX/with-data.log" >/dev/null || fail "second data path was not copied"
[ "$(grep -c '^hcopy ' "$SANDBOX/with-data.log")" -eq 3 ] ||
  fail "with-data case made an unexpected number of copies"

printf 'ok: mame-dev-disk supports zero or more plain-data files\n'

# Exercise the real batch writer against a complete, isolated example tree.
mkdir -p "$SANDBOX/repo/scripts" "$SANDBOX/repo/example/SmirkyCard"
cp "$SUBJECT" "$SANDBOX/repo/scripts/mame-dev-disk.sh"
cp "$REPO_DIR/scripts/hfs-disk.sh" "$SANDBOX/repo/scripts/"
cp "$REPO_DIR/scripts/retro68-env.sh" "$SANDBOX/repo/scripts/"
cp "$REPO_DIR/scripts/env-file.sh" "$SANDBOX/repo/scripts/"
for app in HelloWorld/LokaHello MineSweeper/LokaMine SimpleViewer/LokaSimpleViewer SimpleText/LokaSimpleText FloppyBird/LokaFloppyBird SmirkBench/LokaSmirkBench LazyList/LokaLazyList Tutorial/LokaTutorial ScrapbookUI/ScrapbookUI SmirkyCard/LokaSmirkyCard; do
  binary="$SANDBOX/repo/build/retro68/68k/Release/example/${app}68K.bin"
  mkdir -p "$(dirname "$binary")"
  touch "$binary"
done
touch "$SANDBOX/repo/build/retro68/68k/Release/example/ScrapbookUI/ASSETS.LRP"
touch "$SANDBOX/repo/example/SmirkyCard/MAIN.JS" "$SANDBOX/repo/example/SmirkyCard/MINES.JS" "$SANDBOX/repo/example/SmirkyCard/VIEWER.JS" "$SANDBOX/repo/example/SmirkyCard/LAPTOP.JS"
cp "$REPO_DIR/example/SmirkyCard/disk-scripts.txt" "$SANDBOX/repo/example/SmirkyCard/"
cp "$REPO_DIR/example/SmirkyCard/disk-pictures.txt" "$SANDBOX/repo/example/SmirkyCard/"
cp -R "$REPO_DIR/example/SmirkyCard/assets" "$SANDBOX/repo/example/SmirkyCard/"
run_all() {
  MAME_DEV_DISK_TEST_LOG="$SANDBOX/all.log" \
  MAME_ENV_FILE="$SANDBOX/missing.env" \
  MAME_HDA="$SANDBOX/boot.hd" \
  MAME_HOMEPATH="$SANDBOX/home" \
  MAME_DEV_HDA="$SANDBOX/all.hd" \
  RETRO68_TOOLCHAIN_BIN="$SANDBOX/bin" \
    /bin/bash "$SANDBOX/repo/scripts/mame-dev-disk.sh" "${1:---all}" >/dev/null
}
run_all
[ "$(grep -c '^hformat ' "$SANDBOX/all.log")" -eq 1 ] || fail "All reformatted more than once"
[ "$(grep -c '^hcopy <-m>' "$SANDBOX/all.log")" -eq 10 ] || fail "All must copy ten apps"
[ "$(grep -c '^hcopy <-r>' "$SANDBOX/all.log")" -eq 44 ] || fail "All must copy all 44 assets"
for script in MAIN.JS MINES.JS VIEWER.JS LAPTOP.JS; do
  grep -Fx "hcopy <-r> <$SANDBOX/repo/example/SmirkyCard/$script> <:>" "$SANDBOX/all.log" >/dev/null ||
    fail "All omitted SmirkyCard $script"
done
while read -r stem; do
  case "$stem" in ''|\#*) continue ;; esac
  grep -F "$SANDBOX/repo/example/SmirkyCard/assets/$stem.PICT" "$SANDBOX/all.log" >/dev/null || fail "missing picture $stem"
done < "$REPO_DIR/example/SmirkyCard/disk-pictures.txt"
# A manifest-only addition must retain the original calls and append one script.
# Exercise CRLF, whitespace, comments, and an unterminated final line as well.
printf '\r\n  # extra card\r\n\r\n  FOURTH.JS  ' >> "$SANDBOX/repo/example/SmirkyCard/disk-scripts.txt"
touch "$SANDBOX/repo/example/SmirkyCard/FOURTH.JS"
: > "$SANDBOX/all.log"
run_all
[ "$(grep -c '^hcopy <-r>' "$SANDBOX/all.log")" -eq 45 ] || fail "manifest addition did not copy 45 assets"
for script in MAIN.JS MINES.JS VIEWER.JS LAPTOP.JS FOURTH.JS; do
  grep -Fx "hcopy <-r> <$SANDBOX/repo/example/SmirkyCard/$script> <:>" "$SANDBOX/all.log" >/dev/null ||
    fail "extended manifest omitted $script"
done
cp "$SANDBOX/all.hd" "$SANDBOX/previous.hd"
for script in MAIN.JS MINES.JS VIEWER.JS LAPTOP.JS FOURTH.JS; do
  mv "$SANDBOX/repo/example/SmirkyCard/$script" "$SANDBOX/missing.js"
  if run_all 2>/dev/null; then
    fail "All accepted missing $script"
  fi
  cmp "$SANDBOX/all.hd" "$SANDBOX/previous.hd" || fail "failed All replaced the previous disk"
  mv "$SANDBOX/missing.js" "$SANDBOX/repo/example/SmirkyCard/$script"
done
printf 'ok: All copies ten apps and assets in one disk transaction\n'

for cpu in 68k ppc; do
export LOKA_MAME_CPU="$cpu"
suffix=68K
[ "$cpu" != ppc ] || suffix=PPC
batch_root="$SANDBOX/repo/build/retro68/$cpu/Standalone/Release/tests/toolbox"
mkdir -p "$batch_root"
touch "$batch_root/ASSETS.LRP"
for family in Loop Flow; do
  for app in Scrapbook Hello Tutorial Mine Floppy; do
    touch "$batch_root/Loka${app}Standalone${family}${suffix}.bin"
  done
done
for family in Loop Flow; do
  option=--all-loops
  [ "$family" != Flow ] || option=--all-flows
  : > "$SANDBOX/all.log"
  run_all "$option"
  [ "$(grep -c '^hformat ' "$SANDBOX/all.log")" -eq 1 ] || fail "$family reformatted more than once"
  [ "$(grep -c '^hcopy <-m>' "$SANDBOX/all.log")" -eq 5 ] || fail "$family must copy five apps"
  for app in Scrapbook Hello Tutorial Mine Floppy; do
    grep -Fx "hcopy <-m> <$batch_root/Loka${app}Standalone${family}${suffix}.bin> <:>" "$SANDBOX/all.log" >/dev/null ||
      fail "$family omitted $app"
  done
  grep -Fx "hcopy <-r> <$batch_root/ASSETS.LRP> <:>" "$SANDBOX/all.log" >/dev/null ||
    fail "$family omitted Scrapbook assets"
done
printf 'ok: All Loops and All Flows each copy five apps and shared assets\n'
done
