#!/bin/bash
# Evidence requires a clean candidate and a fresh build directory:
#   cmake -S "$source" -B "$fresh" -DCMAKE_BUILD_TYPE=Debug
#   cmake --build "$fresh" --target LokaURLReceiveProbeMacOS -j8
# Never cite dirty/mutant runs as evidence. Requires a logged-in, unlocked GUI.
# The two-second intervals bound, but do not prove, absence of late duplicates.
set -o pipefail
export LC_ALL=C
lock=/tmp/loka-url-receive-probe.lock
log=/tmp/loka-url-receive-probe.log
locked=0
identified=()
verdicts=()
phase=identity
exe=
artifact=

report() { verdicts+=("$phase: $1 ${2:-}"); printf '%s\n' "${verdicts[${#verdicts[@]}-1]}"; }
fail() { report FAIL "$*"; exit 1; }
unavailable() { report UNAVAILABLE "$*"; exit 1; }
# Bound external tools too; Perl is supplied on both Mavericks and Tahoe.
bounded() { perl -e 'alarm 30; exec @ARGV; exit 127' -- "$@"; }
alive() { kill -0 "$1" 2>/dev/null; }
identity() {
  local actual
  actual=$(bounded ps -p "$1" -o comm=) || return 1
  [ "$actual" = "$exe" ]
}
stop_probe() {
  local pid=$1 deadline
  if ! alive "$pid"; then return 0; fi
  if ! identity "$pid"; then
    printf 'FAIL: unidentified leftover pid=%s; not killed\n' "$pid" >&2
    return 1
  fi
  kill -TERM "$pid" || return 1
  deadline=$((SECONDS + 10))
  while alive "$pid"; do
    if [ "$SECONDS" -ge "$deadline" ]; then return 1; fi
    sleep 0.1 || return 1
  done
}
cleanup() {
  local status=$? pid stage entry reached
  trap - EXIT INT TERM
  for pid in "${identified[@]}"; do
    if ! stop_probe "$pid"; then status=1; fi
  done
  if [ "$locked" -eq 1 ]; then
    if [ -f "$log" ]; then
      if ! cat "$log"; then status=1; fi
      if [ "$status" -ne 0 ] && [ -n "$artifact" ]; then
        if cp "$log" "$artifact"; then
          printf 'failure log: %s\n' "$artifact"
        else
          printf 'FAIL: could not preserve log at %s\n' "$artifact" >&2
        fi
      fi
    fi
    rmdir "$lock" || status=1
  fi
  printf '%s\n' "${verdicts[@]}"
  for stage in identity cold warm negative final; do
    reached=0
    for entry in "${verdicts[@]}"; do
      case "$entry" in "$stage:"*) reached=1 ;; esac
    done
    if [ "$reached" -eq 0 ]; then
      printf '%s: UNAVAILABLE not reached\n' "$stage"
      status=1
    fi
  done
  exit "$status"
}
[ "$#" -eq 2 ] || { printf 'usage: %s <source-dir> <bundle>\n' "$0" >&2; exit 2; }
command -v perl >/dev/null || unavailable 'Perl deadline helper missing'
mkdir "$lock" 2>/dev/null || unavailable 'run lock held; do not bypass'
locked=1
trap cleanup EXIT
trap 'report FAIL "interrupted"; exit 1' INT TERM
artifact="$PWD/url-receive-probe-failure-$$.log"
source_dir=$(cd "$1" && pwd -P) || fail 'source directory'
bundle=$(cd "$2" && pwd -P) || fail 'bundle directory'
plist="$bundle/Contents/Info.plist"
name=$(bounded /usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist") || fail 'executable key'
bundle_id=$(bounded /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist") || fail 'bundle id'
revision=$(bounded /usr/libexec/PlistBuddy -c 'Print :LokaProbeRevision' "$plist") || fail 'baked revision'
exe="$bundle/Contents/MacOS/$name"
[ -x "$exe" ] || fail 'executable missing'
status=$(bounded git -C "$source_dir" status --porcelain) || fail 'git status'
[ -z "$status" ] || fail 'dirty checkout: exploratory only'
expected=$(bounded git -C "$source_dir" rev-parse HEAD) || fail 'git revision'
[ "$expected" = "$revision" ] || fail 'bundle revision differs from clean source'
bounded shasum -a 256 "$exe" || fail shasum
bounded sw_vers || fail sw_vers
bounded uname -m || fail architecture
printf 'bundle=%s\nexpected-rev=%s\n' "$bundle" "$expected"
# Escape regexp metacharacters in the bundle path before pgrep.
pattern=$(printf '%s' "$bundle/Contents/MacOS/" | sed 's/[][\.^$*+?(){}|]/\\&/g') || fail 'process pattern'
bounded pgrep -f "$pattern" >/dev/null
result=$?
case "$result" in
  0) unavailable 'bundle process already running; not killed' ;;
  1) ;;
  *) fail 'process inventory failed' ;;
esac
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
bounded "$lsregister" -f "$bundle" || fail 'lsregister -f'
dump=$(bounded "$lsregister" -dump) || fail 'lsregister -dump'
# Records are separated by dashed lines; identifier and path order varies by OS.
paths=$(printf '%s\n' "$dump" | awk -v wanted="$bundle_id" '
  function emit() { if (id == wanted && path != "") print path }
  /^-+$/ { emit(); id=""; path=""; next }
  /^[ \t]*path:/ { sub(/^[ \t]*path:[ \t]*/, ""); path=$0 }
  /^[ \t]*identifier:/ { sub(/^[ \t]*identifier:[ \t]*/, ""); sub(/[ \t]+\(.*$/, ""); id=$0 }
  END { emit() }
' | sort -u) || fail 'registration inventory'
[ "$paths" = "$bundle" ] || unavailable "registered paths are not exactly the requested bundle: $paths"
: > "$log" || fail 'truncate log'
[ ! -s "$log" ] || fail 'log not empty'
report PASS

# Validate every line against the exact prefix expected so far. Re-reading the
# complete log after TERM catches writes between the quiet check and exit.
expected_lines=()
check_log() {
  local line index=0
  while IFS= read -r line || [ -n "$line" ]; do
    [ "$index" -lt "${#expected_lines[@]}" ] || fail "unexpected line: $line"
    [ "$line" = "${expected_lines[$index]}" ] || fail "unexpected line: $line"
    index=$((index + 1))
  done < "$log"
  [ "$index" -eq "${#expected_lines[@]}" ]
}
wait_log() {
  local deadline=$((SECONDS + 15))
  while ! check_log; do
    [ "$SECONDS" -lt "$deadline" ] || fail 'log deadline'
    sleep 0.1 || fail sleep
  done
}
wait_ready() {
  local deadline=$((SECONDS + 15)) line pid prefix count
  count=${#expected_lines[@]}
  while :; do
    line=$(sed -n "$((count + 1))p" "$log") || fail 'read ready'
    if [ -n "$line" ]; then
      prefix=${line%% rev=*}
      pid=${prefix#ready pid=}
      case "$pid" in ''|*[!0-9]*) fail "invalid ready line: $line" ;; esac
      [ "$pid" -gt 1 ] || fail 'invalid pid'
      [ "$line" = "ready pid=$pid rev=$expected exe=$exe" ] || fail 'ready identity mismatch'
      identity "$pid" || fail 'ready process identity mismatch'
      local prior
      for prior in "${identified[@]}"; do
        [ "$prior" != "$pid" ] || fail 'pid reused between launches'
      done
      identified+=("$pid")
      expected_lines+=("$line")
      current_pid=$pid
      return
    fi
    [ "$SECONDS" -lt "$deadline" ] || fail 'ready deadline; any unidentified leftover is not killed'
    sleep 0.1 || fail sleep
  done
}
quiet() {
  local count=0
  while [ "$count" -lt 20 ]; do
    identity "$current_pid" || fail 'probe exited during quiet interval'
    check_log || fail 'incomplete log during quiet interval'
    sleep 0.1 || fail sleep
    count=$((count + 1))
  done
  identity "$current_pid" || fail 'probe exited at end of quiet interval'
  check_log || fail 'incomplete log at end of quiet interval'
}

phase=cold
cold="lokaurlprobe:cold-$RANDOM$$"
bounded open "$cold" || fail 'cold open'
wait_ready
expected_lines+=("received pid=$current_pid clock=1 $cold")
wait_log
quiet
stop_probe "$current_pid" || fail 'cold stop'
check_log || fail 'cold final log'
report PASS

phase=warm
bounded open "$bundle" || fail 'warm launch'
wait_ready
warm="lokaurlprobe:warm-$RANDOM$$"
bounded open "$warm" || fail 'warm open'
expected_lines+=("received pid=$current_pid clock=1 $warm")
wait_log
report PASS

phase=negative
note=
if ! bounded open 'lokaurlprobe:'; then note='no-dispatch: Launch Services refused empty URL'; fi
sentinel="lokaurlprobe:sentinel-$RANDOM$$"
bounded open "$sentinel" || fail 'sentinel open'
expected_lines+=("received pid=$current_pid clock=1 $sentinel")
wait_log
quiet
report PASS "$note"
phase=final
stop_probe "$current_pid" || fail 'warm stop'
check_log || fail 'final log'
report PASS
exit 0
