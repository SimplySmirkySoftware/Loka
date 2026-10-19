#!/usr/bin/env bash
# Launch-only coverage: real staging helpers, stubbed MAME and hfsutils.
set -euo pipefail
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SANDBOX="$(mktemp -d)"
export SANDBOX
trap 'rm -rf "$SANDBOX"' EXIT
fail() { echo "ToolboxStandaloneRunnerTest failed: $*" >&2; cat "$SANDBOX/output" >&2; exit 1; }
mkdir -p "$SANDBOX/repo/tests/toolbox" "$SANDBOX/repo/scripts" "$SANDBOX/tools" "$SANDBOX/inputs"
cp "$REPO_DIR/tests/toolbox/"{run-standalone.sh,mame-launch.lua} "$SANDBOX/repo/tests/toolbox/"
cp "$REPO_DIR/scripts/"{mame-boot-copy.sh,mame-dev-disk.sh,hfs-disk.sh,retro68-env.sh,env-file.sh} "$SANDBOX/repo/scripts/"
cat >"$SANDBOX/tools/mame" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$@" >"$SANDBOX/argv"
printf '%s\n' "$LOKA_TAB_COUNT" "$LOKA_STANDALONE" "$LOKA_RUN_WAIT" "${LOKA_LAUNCH_WAIT:-}" "$WSLENV" >"$SANDBOX/env"
while [ "$#" -gt 0 ]; do
  if [ "$1" = -hard1 ]; then
    cmp "$2" "$MAME_HDA"
    printf 'guest writes' >"$2"
  fi
  shift
done
[ "${FAKE_MAME_FAIL:-0}" = 0 ] || exit 7
if [ "${FAKE_LUA_FAIL:-0}" = 0 ]; then
  printf 'LOKA-SNAP: standalone wait complete\r\n' >"$LOKA_SNAP_LOG"
fi
SH
cat >"$SANDBOX/tools/hcopy" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf '%s|%s|%s|%s\n' "$HOME" "$1" "$2" "$3" >>"$SANDBOX/copies"
if [ "$2" = :LOG.TXT ]; then
  [ "${FAKE_MISSING:-0}" = 0 ] || exit 1
  cp "$SANDBOX/actual" "$3"
fi
SH
for tool in hformat hmount humount hmkdir; do
  cat >"$SANDBOX/tools/$tool" <<'SH'
#!/usr/bin/env bash
printf '%s|%s|%s\n' "${0##*/}" "$HOME" "$*" >>"$SANDBOX/mounts"
SH
done
cat >"$SANDBOX/tools/wslpath" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$SANDBOX/wslpaths"
printf '%s' "$2"
SH
chmod +x "$SANDBOX/tools/"*
export PATH="$SANDBOX/tools:$PATH"
export RETRO68_TOOLCHAIN_BIN="$SANDBOX/tools"
export RETRO68_ENV_FILE="$SANDBOX/no-retro-env"
export MAME_ENV_FILE="$SANDBOX/mame.env"
printf 'template' >"$SANDBOX/template.hd"
printf 'app' >"$SANDBOX/inputs/Runner.bin"
printf 'picture' >"$SANDBOX/inputs/Sun pict"
printf 'extra' >"$SANDBOX/inputs/Second"
printf 'log text=ok\nterminal status=succeeded\n' >"$SANDBOX/actual"
cp "$SANDBOX/actual" "$SANDBOX/expected.audit"
cat >"$MAME_ENV_FILE" <<ENV
MAME_HDA="$SANDBOX/template.hd"
MAME_EXECUTABLE="$SANDBOX/tools/mame"
MAME_MACHINE=maciix
MAME_RAMSIZE=8M
MAME_ROMPATH="$SANDBOX/rom path"
MAME_DEV_HDA="$SANDBOX/do-not-use-dev.hd"
MAME_CONTROL_DIR="$SANDBOX/do-not-use-control"
ENV
unset WSL_INTEROP LOKA_TAB_COUNT LOKA_RUN_WAIT LOKA_LAUNCH_WAIT
RUNNER="$SANDBOX/repo/tests/toolbox/run-standalone.sh"
APP="$SANDBOX/inputs/Runner.bin"
WORK="$SANDBOX/repo/build/mame-standalone/Runner"
run() {
  local expected_status="$1" status=0; shift
  bash "$RUNNER" "$@" >"$SANDBOX/output" 2>&1 || status=$?
  [ "$status" -eq "$expected_status" ] || fail "expected exit $expected_status, got $status"
}
for mode in native wsl; do
  if [ "$mode" = wsl ]; then export WSL_INTEROP=stub; else unset WSL_INTEROP; fi
  run 0 "$APP" --expect "$SANDBOX/expected.audit"
  [ "$(head -1 "$SANDBOX/env")" = 1 ] || fail 'one item must use one Tab'
  [ "$(sed -n '2p' "$SANDBOX/env")" = 1 ] || fail 'standalone mode missing'
  grep -Fxq 90 "$SANDBOX/env" || fail 'default wait missing'
  grep -Fxq "$WORK/Boot.hd" "$SANDBOX/argv" || fail 'boot copy not used'
  grep -Fxq "$WORK/LokaDev.hd" "$SANDBOX/argv" || fail 'private dev disk not used'
  grep -Fxq "$SANDBOX/repo/tests/toolbox/mame-launch.lua" "$SANDBOX/argv" || fail 'shared Lua not used'
  grep -Fxq "$SANDBOX/rom path" "$SANDBOX/argv" || fail 'ROM path split'
  grep -Fq 'LOKA_STANDALONE:LOKA_LAUNCH_WAIT' "$SANDBOX/env" || fail 'WSL forwarding missing'
  grep -Fq "$WORK/hfs-home|-r|:LOG.TXT|$WORK/LOG.TXT" "$SANDBOX/copies" || fail 'raw retrieval missing'
  grep -Fq "humount|$WORK/hfs-home|" "$SANDBOX/mounts" || fail 'retrieval not unmounted'
  grep -Fq "$WORK/hfs-ctl/hfsutils|-m|$APP|:" "$SANDBOX/copies" || fail 'app not staged as MacBinary'
  cmp "$SANDBOX/actual" "$WORK/LOG.TXT" || fail 'audit bytes changed'
  grep -Fq 'terminal status=succeeded' "$SANDBOX/output" || fail 'log not printed'
  [ "$(cat "$SANDBOX/template.hd")" = template ] || fail 'template modified'
  touch "$WORK/stale"
  run 1 "$APP" "$SANDBOX/inputs/Sun pict" --expect "$SANDBOX/expected.audit"
  grep -Fq 'set LOKA_TAB_COUNT' "$SANDBOX/output" || fail 'extra files without a Tab count must be refused'
  LOKA_TAB_COUNT=2 run 0 "$APP" "$SANDBOX/inputs/Sun pict" --expect "$SANDBOX/expected.audit"
  [ ! -e "$WORK/stale" ] || fail 'work directory not wiped'
  [ "$(head -1 "$SANDBOX/env")" = 2 ] || fail 'stated Tab count not forwarded'
  grep -Fq "|-r|$SANDBOX/inputs/Sun pict|:" "$SANDBOX/copies" || fail 'plain file not staged intact'
  mkdir -p "$SANDBOX/repo/example/SmirkyCard/assets"
  picture="$SANDBOX/repo/example/SmirkyCard/assets/PB180C.PICT"
  printf 'picture' >"$picture"
  LOKA_TAB_COUNT=2 run 0 "$APP" "$picture" --expect "$SANDBOX/expected.audit"
  grep -Fq "|-r|$picture|:Pictures:" "$SANDBOX/copies" || fail 'standalone picture folder missing'
  grep -Fq "hmkdir|$WORK/hfs-ctl/hfsutils|:Pictures" "$SANDBOX/mounts" || fail 'Pictures not created'
  LOKA_TAB_COUNT=4 LOKA_RUN_WAIT=17 LOKA_LAUNCH_WAIT=12 run 0 "$APP"
  [ "$(head -1 "$SANDBOX/env")" = 4 ] || fail 'Tab override ignored'
  grep -Fxq 17 "$SANDBOX/env" || fail 'run wait override ignored'
  grep -Fxq 12 "$SANDBOX/env" || fail 'boot wait override ignored'
  echo "[pass] $mode launch, staging, retrieval, comparison and overrides"
done
[ -s "$SANDBOX/wslpaths" ] || fail 'WSL path branch not exercised'
unset WSL_INTEROP
run 2
run 2 "$APP" --unknown
run 2 "$APP" --expect
run 2 "$APP" --expect "$SANDBOX/missing"
run 2 "$APP" "$SANDBOX/missing"
run 2 "$APP" --expect "$SANDBOX/expected.audit" extra
run 2 "$APP" "$SANDBOX/inputs/Second" "$SANDBOX/inputs/Second"
touch "$SANDBOX/inputs/log.txt"
run 2 "$APP" "$SANDBOX/inputs/log.txt"
LOKA_TAB_COUNT=0 run 2 "$APP"
LOKA_RUN_WAIT=bad run 2 "$APP"
printf 'log text=ok\rterminal status=succeeded\r' >"$SANDBOX/actual"
run 1 "$APP" --expect "$SANDBOX/expected.audit"
grep -Fq 'LOG.TXT differs' "$SANDBOX/output" || fail 'byte mismatch not diagnosed'
FAKE_MISSING=1 run 1 "$APP"
[ ! -f "$WORK/LOG.TXT" ] || fail 'stale audit survived failed retrieval'
grep -Fq 'could not copy LOG.TXT' "$SANDBOX/output" || fail 'missing audit not diagnosed'
tail -1 "$SANDBOX/mounts" | grep -Fq "humount|$WORK/hfs-home|" || fail 'failed retrieval leaked mount'
FAKE_MAME_FAIL=1 run 1 "$APP"
grep -Fq 'MAME exited with status 7' "$SANDBOX/output" || fail 'MAME failure not diagnosed'
FAKE_LUA_FAIL=1 run 1 "$APP"
grep -Fq 'standalone wait did not complete' "$SANDBOX/output" || fail 'Lua failure accepted'
echo '[pass] invalid arguments, byte mismatch, missing audit and launch failures'
