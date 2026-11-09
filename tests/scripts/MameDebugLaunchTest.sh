#!/usr/bin/env bash

set -euo pipefail

TEST_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$TEST_SCRIPT_DIR/../.." && pwd)"
SUBJECT="${LOKA_MAME_DEBUG_SH:-$REPO_DIR/scripts/mame-debug.sh}"

SANDBOX="$(mktemp -d)"
export SANDBOX

cleanup() {
  if [ -f "$SANDBOX/mame.pid" ]; then
    mame_pid="$(cat "$SANDBOX/mame.pid")"
    if kill -0 "$mame_pid" 2>/dev/null; then
      kill "$mame_pid" 2>/dev/null || true
      /bin/sleep 0.05
      kill -9 "$mame_pid" 2>/dev/null || true
    fi
  fi
  rm -rf "$SANDBOX"
}
trap cleanup EXIT

mkdir -p "$SANDBOX/scripts" "$SANDBOX/bin" "$SANDBOX/tests/toolbox"
printf '%s\n' '-- shared keys fixture' >"$SANDBOX/tests/toolbox/mame-keys.lua"
cp "$SUBJECT" "$SANDBOX/scripts/mame-debug.sh"
touch "$SANDBOX/scripts/mame-find-base.lua" "$SANDBOX/scripts/mame-attach.gdb"

cat >"$SANDBOX/scripts/mame-dev-disk.sh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$SANDBOX/devdisk.argv"
: > "$MAME_DEV_HDA"
EOF

cat >"$SANDBOX/bin/gdb-multiarch" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$SANDBOX/gdb.argv"
exit "${FAKE_GDB_EXIT:-0}"
EOF

cat >"$SANDBOX/bin/ss" <<'EOF'
#!/usr/bin/env bash
[ -f "$SANDBOX/mame.ready" ] || exit 0
printf '%s\n' 'LISTEN 0 1 127.0.0.1:12399 0.0.0.0:*'
EOF

# Not instant: the listener-wait loop calls sleep between ss/netstat polls,
# and a zero-cost stub would let 120 iterations burn through faster than
# fake MAME can start under load. 0.05s keeps a full timeout path bounded
# (~6s) while giving the gate real time.
cat >"$SANDBOX/bin/sleep" <<'EOF'
#!/usr/bin/env bash
exec /bin/sleep 0.05
EOF

# WSL-branch stand-ins: the launcher consults wslinfo for the networking
# mode, ip for the default route, wslpath for path translation, and
# netstat.exe for the listener wait.
cat >"$SANDBOX/bin/wslinfo" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "${FAKE_WSL_MODE:-nat}"
EOF

cat >"$SANDBOX/bin/ip" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' 'default via 10.1.2.3 dev eth0'
EOF

cat >"$SANDBOX/bin/wslpath" <<'EOF'
#!/usr/bin/env bash
shift
printf '%s' "$1"
EOF

cat >"$SANDBOX/bin/netstat.exe" <<'EOF'
#!/usr/bin/env bash
[ -f "$SANDBOX/mame.ready" ] || exit 0
printf '%s\n' '  TCP    10.1.2.3:12399  0.0.0.0:0  LISTENING'
EOF

# The launcher backgrounds the emulator and its EXIT trap kills it as soon
# as the (stubbed, instant) gdb returns. On a loaded runner that SIGTERM can
# land before this script has written its observation files — the harness
# then reports "fake MAME did not record its PID" (seen once on the CI ASan
# runner). The ready marker plus the gated ss/netstat stubs below turn the
# launcher's listener wait into a real synchronization point: LISTENING is
# only reported once every observation file exists.
cat >"$SANDBOX/fake-mame" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$SANDBOX/mame.argv"
printf '%s\n' "${WSLENV:-}" > "$SANDBOX/mame.wslenv"
printf '%s\n' "$$" > "$SANDBOX/mame.pid"
: > "$SANDBOX/mame.ready"
exec /bin/sleep 300
EOF

chmod +x \
  "$SANDBOX/scripts/mame-debug.sh" \
  "$SANDBOX/scripts/mame-dev-disk.sh" \
  "$SANDBOX/bin/gdb-multiarch" \
  "$SANDBOX/bin/ss" \
  "$SANDBOX/bin/sleep" \
  "$SANDBOX/bin/wslinfo" \
  "$SANDBOX/bin/ip" \
  "$SANDBOX/bin/wslpath" \
  "$SANDBOX/bin/netstat.exe" \
  "$SANDBOX/fake-mame"

touch "$SANDBOX/dummy.hda" "$SANDBOX/app.bin"
cat >"$SANDBOX/mame.env" <<EOF
MAME_EXECUTABLE=$SANDBOX/fake-mame
MAME_HDA=$SANDBOX/dummy.hda
MAME_DEBUG_PORT=12399
EOF

export PATH="$SANDBOX/bin:$PATH"

fail() {
  printf 'FAIL: %s %s\n' "$1" "$2"
  exit 1
}

pass() {
  printf 'ok: %s\n' "$1"
}

assert_argv() {
  local case_name="$1"
  local actual="$2"
  local expected="$SANDBOX/$case_name.expected.argv"
  shift 2

  printf '%s\n' "$@" > "$expected"
  diff -u "$expected" "$actual" ||
    fail "$case_name" "argv did not match"
}

assert_mame_dead() {
  local case_name="$1"
  local mame_pid
  local attempt

  [ -f "$SANDBOX/mame.pid" ] ||
    fail "$case_name" "fake MAME did not record its PID"
  mame_pid="$(cat "$SANDBOX/mame.pid")"

  for attempt in $(seq 40); do
    if ! kill -0 "$mame_pid" 2>/dev/null; then
      return
    fi
    /bin/sleep 0.05
  done

  fail "$case_name" "fake MAME PID $mame_pid is still alive"
}

run_case() {
  local case_name="$1"
  local gdb_exit="$2"
  local expected_exit="$3"
  local with_elf="$4"
  local actual_exit
  shift 4

  rm -f \
    "$SANDBOX/mame.pid" \
    "$SANDBOX/mame.argv" \
    "$SANDBOX/mame.wslenv" \
    "$SANDBOX/mame.ready" \
    "$SANDBOX/app.code.bin.gdb" \
    "$SANDBOX/devdisk.argv" \
    "$SANDBOX/gdb.argv"
  if [ "$with_elf" = "yes" ]; then
    touch "$SANDBOX/app.code.bin.gdb"
  fi

  if FAKE_GDB_EXIT="$gdb_exit" MAME_ENV_FILE="$SANDBOX/mame.env" \
      env -u WSL_INTEROP -u LOKA_DEV_DATA -u LOKA_GDB_SCRIPT "$@" \
        bash "$SANDBOX/scripts/mame-debug.sh" \
        attach "$SANDBOX/app.bin" 4feffefc 48e71e30 00029372 0x00700000 \
        >"$SANDBOX/$case_name.log" 2>&1; then
    actual_exit=0
  else
    actual_exit=$?
  fi

  [ "$actual_exit" -eq "$expected_exit" ] ||
    fail "$case_name" "expected exit $expected_exit, got $actual_exit"
  assert_mame_dead "$case_name"
}

# The gdbstub bind address travels as an adjacent "-debugger_host <value>"
# pair in the emulator argv; newer MAME defaults it to the loopback, which
# only a same-host debugger can reach.
assert_stub_host() {
  local case_name="$1"
  local expected="$2"
  [ -f "$SANDBOX/mame.argv" ] ||
    fail "$case_name" "fake MAME did not record its argv"
  grep -A1 -Fx -- '-debugger_host' "$SANDBOX/mame.argv" | tail -1 |
    grep -Fxq -- "$expected" ||
    fail "$case_name" "expected -debugger_host $expected in the emulator argv"
}

run_case reap-on-quit 0 0 yes
assert_argv \
  reap-on-quit \
  "$SANDBOX/gdb.argv" \
  -q \
  -x \
  "$SANDBOX/scripts/mame-attach.gdb"
# Outside WSL the loopback default stays: same host, nothing was broken.
assert_stub_host reap-on-quit 127.0.0.1
pass reap-on-quit

run_case \
  stub-host-override \
  0 \
  0 \
  yes \
  "MAME_DEBUG_HOST=10.9.8.7"
assert_stub_host stub-host-override 10.9.8.7
pass stub-host-override

# Only WSL2 NAT substitutes the gateway; WSL1 (no wslinfo) and mirrored
# networking share the Windows loopback and must keep it. The run_case
# helper unsets WSL_INTEROP, so these cases re-set it to enter the branch.
# The Finder tab count is a per-disk fact the caller owns; when set it must
# cross the WSL boundary to the emulator's autoboot Lua, which only happens
# for names listed in WSLENV.
run_case \
  tab-count-forwarded \
  0 \
  0 \
  yes \
  "LOKA_TAB_COUNT=4"
grep -q "LOKA_TAB_COUNT" "$SANDBOX/mame.wslenv" ||
  fail tab-count-forwarded "LOKA_TAB_COUNT did not reach WSLENV"
pass tab-count-forwarded

run_case \
  wsl-nat-gateway \
  0 \
  0 \
  yes \
  "WSL_INTEROP=1" "FAKE_WSL_MODE=nat"
assert_stub_host wsl-nat-gateway 10.1.2.3
pass wsl-nat-gateway

run_case \
  wsl-mirrored-loopback \
  0 \
  0 \
  yes \
  "WSL_INTEROP=1" "FAKE_WSL_MODE=mirrored"
assert_stub_host wsl-mirrored-loopback 127.0.0.1
pass wsl-mirrored-loopback

# A wildcard override must be refused before the emulator ever starts:
# the stub is unauthenticated remote control, and the docs promise the
# launcher never binds every interface. No run_case here -- the refusal
# exits before fake MAME records a PID, which run_case would treat as a
# failure to reap.
rm -f "$SANDBOX/mame.pid" "$SANDBOX/mame.argv" "$SANDBOX/mame.wslenv" \
  "$SANDBOX/mame.ready" "$SANDBOX/app.code.bin.gdb"
touch "$SANDBOX/app.code.bin.gdb"
if MAME_ENV_FILE="$SANDBOX/mame.env" \
    env -u WSL_INTEROP -u LOKA_DEV_DATA -u LOKA_GDB_SCRIPT \
      MAME_DEBUG_HOST=0.0.0.0 \
      bash "$SANDBOX/scripts/mame-debug.sh" \
      attach "$SANDBOX/app.bin" 4feffefc 48e71e30 00029372 0x00700000 \
      >"$SANDBOX/wildcard-rejected.log" 2>&1; then
  fail wildcard-rejected "a wildcard bind override was accepted"
fi
grep -q "refusing to bind the gdbstub to every interface" \
  "$SANDBOX/wildcard-rejected.log" ||
  fail wildcard-rejected "the refusal did not explain itself"
[ ! -f "$SANDBOX/mame.pid" ] ||
  fail wildcard-rejected "the emulator was launched despite the refusal"
pass wildcard-rejected

run_case reap-on-gdb-failure 3 3 yes
pass reap-on-gdb-failure
run_case reap-on-missing-elf 0 1 no
pass reap-on-missing-elf

run_case \
  data-files-arrive-intact \
  0 \
  0 \
  yes \
  "LOKA_DEV_DATA=$SANDBOX/data one.lrp
$SANDBOX/glob*2.lrp"
assert_argv \
  data-files-arrive-intact \
  "$SANDBOX/devdisk.argv" \
  "$SANDBOX/app.bin" \
  "$SANDBOX/data one.lrp" \
  "$SANDBOX/glob*2.lrp"
pass data-files-arrive-intact

touch "$SANDBOX/extra cmds.gdb"
run_case \
  gdb-script-appended \
  0 \
  0 \
  yes \
  "LOKA_GDB_SCRIPT=$SANDBOX/extra cmds.gdb"
assert_argv \
  gdb-script-appended \
  "$SANDBOX/gdb.argv" \
  -q \
  -x \
  "$SANDBOX/scripts/mame-attach.gdb" \
  -x \
  "$SANDBOX/extra cmds.gdb"
pass gdb-script-appended

cmp "$SANDBOX/tests/toolbox/mame-keys.lua" "$SANDBOX/build/mame-debug/mame-keys.lua"
