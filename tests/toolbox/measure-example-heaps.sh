#!/usr/bin/env bash
# Partition sizing rail for the Classic examples (#1103).
#
#   tests/toolbox/measure-example-heaps.sh [--cpu 68k|ppc] [--bin PATH] [--partition K] [--catch-syserror] [example ...]
#
# For each example, boot MAME, launch the production build, play a fixed
# workload, and report the partition it needs:
#
#   need = peak live bytes of the application zone (mame-measure-heap.lua)
#        + the partition's fixed part (stack, A5 world, zone header), read
#          as the declared preferred size minus the zone's maximum size
#
# and the values the Size.r files are set from: minimum = need x 1.1 rounded
# up to 32K, preferred = need x 1.5 rounded up to 64K. The formula was checked
# against real boundaries on 2026-10-03: HelloWorld (need 392K) dies at launch
# in 384K and runs in 416K; LazyList (434K) dies in 416K and runs in 448K;
# MineSweeper (440K) aborts in 432K and runs in 440K. The values are for 68K
# only. --cpu ppc measures on pmac6100 / Mac OS 8.1 (a probe image, not the
# scenario verdict machine) with virtual memory off, where the Process Manager
# adds the code fragment to the partition; it also prints the peak without the
# code section, which bounds the virtual-memory-on case. The PPC Size.r values
# are not set from the formula (#1109): they record both numbers.
#
# The rail does not build. Build first:
#   cmake --build --preset retro68-68k-release (or retro68-ppc-release)
# One example takes about three minutes; separate invocations may run in
# parallel because each example has its own work directory.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ALL_EXAMPLES=(tutorial floppybird smirkbench scrapbook helloworld lazylist minesweeper simpleviewer simpletext)
fail() { echo "measure-example-heaps: $*" >&2; exit 1; }

BIN_OVERRIDE="" PARTITION="" CATCH_SYSERROR=0 CPU=68k
examples=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --cpu)
      [ "$#" -ge 2 ] || fail '--cpu requires 68k or ppc'
      case "$2" in 68k|ppc) CPU="$2" ;; *) fail '--cpu must be 68k or ppc' ;; esac
      shift 2 ;;
    --bin|--partition)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 requires a value"
      if [ "$1" = --bin ]; then BIN_OVERRIDE="$2"; else PARTITION="$2"; fi
      shift 2 ;;
    --catch-syserror) CATCH_SYSERROR=1; shift ;;
    --*) fail "unknown option '$1'" ;;
    *) examples+=("$1"); shift ;;
  esac
done
if [ -n "$BIN_OVERRIDE$PARTITION" ]; then
  [ "${#examples[@]}" -eq 1 ] || fail '--bin/--partition require exactly one example'
fi
if [ -n "$PARTITION" ]; then
  [[ "$PARTITION" =~ ^[1-9][0-9]{0,6}$ ]] && [ "$PARTITION" -le 2097151 ] \
    || fail '--partition must be 1..2097151 KiB'
fi
if [ -n "$BIN_OVERRIDE" ]; then
  [ -f "$BIN_OVERRIDE" ] || fail "missing $BIN_OVERRIDE"
  BIN_OVERRIDE="$(cd "$(dirname "$BIN_OVERRIDE")" && pwd)/$(basename "$BIN_OVERRIDE")"
fi
[ "${#examples[@]}" -gt 0 ] || examples=("${ALL_EXAMPLES[@]}")

. "$PROJECT_DIR/scripts/retro68-env.sh"
loka_load_retro68_environment "$PROJECT_DIR"
ENV_FILE="$PROJECT_DIR/.env-mame"
[ "$CPU" != ppc ] || ENV_FILE="$PROJECT_DIR/.env-mame-pmac6100"
ENV_FILE="${MAME_ENV_FILE:-$ENV_FILE}"
[ -f "$ENV_FILE" ] && loka_import_environment_file "$ENV_FILE"
# The PPC probe is calibrated to pmac6100 with its Mac OS 8.1 image (low-memory
# signature, Finder icon positions), so it takes that machine only; 68K keeps
# accepting any 68K machine, as before --cpu existed.
case "$CPU:${MAME_MACHINE:-maciix}" in
  ppc:pmac6100) ;;
  ppc:*) fail "--cpu ppc is calibrated to pmac6100; MAME_MACHINE=${MAME_MACHINE:-maciix}" ;;
  68k:pmac*) fail "--cpu 68k cannot run on the PPC machine MAME_MACHINE=$MAME_MACHINE" ;;
esac
if [ "$CPU" = ppc ]; then
  export LOKA_LAUNCH_WAIT="${LOKA_LAUNCH_WAIT-150}"
fi
normalize_host_path() {
  if [[ "$1" =~ ^[A-Za-z]:\\ ]] && command -v wslpath >/dev/null 2>&1; then wslpath -u "$1"; else printf '%s' "$1"; fi
}
winpath() {
  if [ -n "${WSL_INTEROP:-}" ]; then wslpath -w "$1"; else printf '%s' "$1"; fi
}
retro68_tool() {
  local candidate
  for candidate in "${RETRO68_TOOLCHAIN_BIN:-}/$1" "${RETRO68_BUILD_DIR:-$HOME/Retro68-build}/toolchain/bin/$1"; do
    [ -x "$candidate" ] && { printf '%s\n' "$candidate"; return; }
  done
  command -v "$1" || fail "Retro68 tool not found: $1"
}
MAME_EXECUTABLE="$(normalize_host_path "${MAME_EXECUTABLE:-mame}")"
MAME_HDA="$(normalize_host_path "${MAME_HDA:-}")"
[ -f "$MAME_HDA" ] || fail 'MAME_HDA must point to the boot template'
RELEASE="$PROJECT_DIR/build/retro68/$CPU/Release/example"
# pmac6100 / Mac OS 8.1: where the Finder puts the LokaDev icon, and the
# application icons that do not land in the window's first grid cell.
PPC_DISK_ICON="591 243"
# Fresh LokaDev windows list their items by name in a 127-pixel grid.
# The first cell's icon sits over the middle of its name, so a long name
# moves it right.
declare -A PPC_APP_ICON=([scrapbook]="172 88" [simpletext]="172 88" [smirkbench]="61 88" [simpleviewer]="65 88")

# Workloads use the production window positions on maciix (640x480). Tab
# counts are the Finder's landing for that disk's item set.
minesweeper_steps() {
  local steps="" round i cell
  for round in 1 2; do
    for i in $(seq 0 63); do
      cell=$(( (i * 37 + round * 11) % 64 ))
      steps+="c $((44 + 24 * (cell % 8))) $((99 + 21 * (cell / 8)));"
    done
    steps+="s;c 130 73;w 3;"
  done
  printf '%s' "${steps}s;"
}
lazylist_steps() {
  local steps="c 412 111;c 412 111;c 412 111;c 566 111;c 566 111;c 97 111;c 254 111;w 2;s;" i
  for i in $(seq 1 12); do steps+="c 634 330;w 1;"; done
  steps+="s;"
  for i in $(seq 1 12); do steps+="c 634 170;w 1;"; done
  printf '%s' "${steps}s;"
}

measure() {
  local example="$1" dir app sizer tabs steps extras=()
  case "$example" in
    tutorial) dir=Tutorial app=LokaTutorial68K; tabs=1; steps="w 10;s;" ;;
    floppybird) dir=FloppyBird app=LokaFloppyBird68K; tabs=1
      steps="K space;w 1;$(printf 'k space;w 1;%.0s' 1 2 3 4 5 6 7)w 5;s;w 10;s;" ;;
    smirkbench) dir=SmirkBench app=LokaSmirkBench68K; tabs=1
      steps="$(printf 'c 113 103;%.0s' 1 2 3 4 5)w 10;s;" ;;
    scrapbook) dir=ScrapbookUI app=ScrapbookUI68K; tabs=2
      extras=("$PROJECT_DIR/example/ScrapbookUI/ASSETS.LRP")
      steps="$(printf 'c 290 287;w 2;%.0s' 1 2 3 4)s;$(printf 'c 129 287;w 2;%.0s' 1 2 3 4)s;" ;;
    helloworld) dir=HelloWorld app=LokaHello68K; tabs=1
      steps="K space;w 1;c 157 157;c 157 157;c 157 157;c 157 205;c 157 229;c 157 229;c 345 133;w 2;c 345 280;k five;k zero;w 2;s;c 449 368;c 449 368;c 449 368;w 2;s;" ;;
    lazylist) dir=LazyList app=LokaLazyList68K; tabs=1; steps="$(lazylist_steps)" ;;
    minesweeper) dir=MineSweeper app=LokaMine68K; tabs=1; steps="$(minesweeper_steps)" ;;
    simpletext) dir=SimpleText app=LokaSimpleText68K; tabs=1
      # Open a document at the editor's caps through the real dialog (the first
      # Down selects Desktop DF, the second Large), edit it, and save it.
      steps="w 5;K space;K o;w 4;$(printf 'k down;%.0s' 1 2)s;k ret;w 10;s;c 80 100;k l;k o;k ret;k l;k ret;w 2;K s;w 5;s;" ;;
    simpleviewer) dir=SimpleViewer app=LokaSimpleViewer68K; tabs=3
      # The open dialog lists Desktop DB and Desktop DF first: four Downs reach
      # Sun.pict and five reach Zbulb.pict (Bulb.pict renamed to sort last).
      steps="c 128 71;w 4;$(printf 'k down;%.0s' 1 2 3 4)k ret;w 8;s;c 128 143;w 2;c 128 167;w 2;c 128 191;w 2;s;"
      steps+="c 128 71;w 4;$(printf 'k down;%.0s' 1 2 3 4 5)k ret;w 8;s;c 128 143;w 2;c 128 191;w 2;s;" ;;
    smirkycard) dir=SmirkyCard app=LokaSmirkyCard68K; tabs=1 # opt-in, so not in ALL_EXAMPLES
      # Manifest readers in scripts/mame-*-disk* stay parallel.
      local manifest entry
      for manifest in disk-scripts disk-pictures; do
        while IFS=$' \t\r' read -r entry || [ -n "$entry" ]; do
          case "$entry" in ''|\#*) continue ;; esac
          if [ "$manifest" = disk-scripts ]; then extras+=("$PROJECT_DIR/example/SmirkyCard/$entry")
          else extras+=("$PROJECT_DIR/example/SmirkyCard/assets/$entry.PICT"); fi
        done < "$PROJECT_DIR/example/SmirkyCard/$manifest.txt"
      done
      # MAIN.JS's ./LAPTOP.JS button, then 20 pane switches on the band's lead
      # (Wide/Tall); maciix needs several seconds to draw a pane. Snapshots
      # after the 1st, 2nd and 20th show both panes and the return.
      steps="w 10;s;c 270 356;w 15;s;c 92 93;w 10;s;c 92 93;w 10;s;$(printf 'c 92 93;w 10;%.0s' $(seq 1 18))s;" ;;
    *) fail "unknown example '$example' (known: ${ALL_EXAMPLES[*]})" ;;
  esac
  [ "$CPU" != ppc ] || app="${app%68K}PPC"
  # Mac OS 8.1 launches with double-clicks (mame-measure-heap.lua): the app's
  # icon in a fresh LokaDev window, first grid cell unless an example says so.
  local launch_icon="${PPC_APP_ICON[$example]:-45 88}"
  sizer="$PROJECT_DIR/example/$dir/Size.r"
  local bin="${BIN_OVERRIDE:-$RELEASE/$dir/$app.bin}"
  [ -f "$bin" ] || fail "missing $bin; build retro68-$CPU-release first"
  # The preferred size is the first value in the selected Classic branch.
  local preferred_k
  if [ -n "$PARTITION" ]; then
    preferred_k="$PARTITION"
  elif [ -n "$BIN_OVERRIDE" ]; then
    preferred_k="$(python3 "$SCRIPT_DIR/patch-macbinary-size.py" --preferred-k "$bin")"
  else
    if [ "$CPU" = ppc ]; then
      preferred_k="$(awk '/^#elif LOKA_CLASSIC_68K/{classic=1; next} classic && /^#else/{block=1; next} block && /^#/{exit} block && /\* *1024/{gsub(/[^0-9*]/,""); split($0,p,"*"); print p[1]; exit}' "$sizer")"
    else
      preferred_k="$(awk '/^#elif LOKA_CLASSIC_68K/{block=1; next} block && /^#/{exit} block && /\* *1024/{gsub(/[^0-9*]/,""); split($0,p,"*"); print p[1]; exit}' "$sizer")"
    fi
    [[ "$preferred_k" =~ ^[0-9]+$ ]] || fail "could not read the preferred size from $sizer"
  fi

  local work="$PROJECT_DIR/build/mame-measure/$example"
  [ "$CPU" != ppc ] || work+="-ppc"
  if [ -n "$BIN_OVERRIDE$PARTITION" ] || [ "$CATCH_SYSERROR" -eq 1 ]; then
    local label="$example"
    [ "$CPU" != ppc ] || label+="-ppc"
    [ -z "$BIN_OVERRIDE" ] || label+="-$(basename "$bin" | tr -c '[:alnum:]._-' '_')"
    [ -z "$PARTITION" ] || label+="-${PARTITION}K"
    mkdir -p "$PROJECT_DIR/build/mame-measure"
    work="$(mktemp -d "$PROJECT_DIR/build/mame-measure/${label}.XXXXXX")"
  else
    rm -rf "$work"
  fi
  mkdir -p "$work"/{home,cfg,nvram,snapshot,diff,hfs-ctl,hfs-home}
  if [ -n "$PARTITION" ]; then
    python3 "$SCRIPT_DIR/patch-macbinary-size.py" "$bin" "$work/$app.bin" "$PARTITION"
    bin="$work/$app.bin"
  fi
  if [ "$example" = simpleviewer ]; then
    local hmount hcopy humount
    hmount="$(retro68_tool hmount)"; hcopy="$(retro68_tool hcopy)"; humount="$(retro68_tool humount)"
    # The pictures live on the maciix boot template; the PPC probe image has
    # none, so PPC borrows them from the 68K environment's template.
    local pictures="$MAME_HDA"
    if [ "$CPU" = ppc ]; then
      pictures="$(loka_import_environment_file "$PROJECT_DIR/.env-mame" && normalize_host_path "${MAME_HDA:-}")"
      [ -f "$pictures" ] || fail "simpleviewer on ppc reads its pictures from .env-mame's MAME_HDA; none found"
    fi
    HOME="$work/hfs-home" "$hmount" "$pictures" >"$work/picture-hmount.out" 2>&1 || fail "could not mount the boot template"
    HOME="$work/hfs-home" "$hcopy" -r ":Desktop Folder:Images:Sun.pict" "$work/Sun.pict" >/dev/null 2>&1 \
      && HOME="$work/hfs-home" "$hcopy" -r ":Desktop Folder:Images:Bulb.pict" "$work/Zbulb.pict" >/dev/null 2>&1 \
      || { HOME="$work/hfs-home" "$humount" >/dev/null 2>&1 || true; fail "boot template lacks :Desktop Folder:Images:Sun.pict / Bulb.pict"; }
    HOME="$work/hfs-home" "$humount" >/dev/null 2>&1 || true
    extras=("$work/Sun.pict" "$work/Zbulb.pict")
  fi
  if [ "$example" = simpletext ]; then
    # 255 rows of 31 bytes plus LF separators: 8159 bytes, inside the 256-row
    # and 8 KiB document caps, so the peak covers the largest openable file.
    local row
    : >"$work/Large"
    for row in $(seq 1 255); do
      printf 'Row %03d of the largest document' "$row" | cut -c1-31 >>"$work/Large.rows"
    done
    head -c -1 "$work/Large.rows" >"$work/Large"
    rm -f "$work/Large.rows"
    extras=("$work/Large")
  fi
  . "$PROJECT_DIR/scripts/mame-boot-copy.sh"
  loka_prepare_boot_copy "$MAME_HDA" "$work/Boot.hd" >/dev/null
  MAME_DEV_HDA="$work/LokaDev.hd" MAME_CONTROL_DIR="$work/hfs-ctl" \
    "$PROJECT_DIR/scripts/mame-dev-disk.sh" "$bin" "${extras[@]}" >"$work/dev-disk.out" 2>&1 \
    || fail "dev disk creation; see $work/dev-disk.out"
  if [ "$example" = simpletext ]; then
    # A raw copy has no type, and Save refuses a non-TEXT file without writing;
    # TEXT/ttxt lets Command-S measure the write path too.
    local hmount hattrib humount
    hmount="$(retro68_tool hmount)"; hattrib="$(retro68_tool hattrib)"; humount="$(retro68_tool humount)"
    HOME="$work/hfs-home" "$hmount" "$work/LokaDev.hd" >"$work/large-hmount.out" 2>&1 \
      && HOME="$work/hfs-home" "$hattrib" -t TEXT -c ttxt :Large >/dev/null 2>&1 \
      || { HOME="$work/hfs-home" "$humount" >/dev/null 2>&1 || true; fail "could not mark Large as TEXT/ttxt"; }
    HOME="$work/hfs-home" "$humount" >/dev/null 2>&1 || true
  fi
  cp "$SCRIPT_DIR/mame-measure-heap.lua" "$work/mame-measure-heap.lua"
  local args=(
    "${MAME_MACHINE:-maciix}" -ramsize "${MAME_RAMSIZE:-8M}"
    -homepath "$(winpath "$work/home")" -cfg_directory "$(winpath "$work/cfg")"
    -nvram_directory "$(winpath "$work/nvram")" -snapshot_directory "$(winpath "$work/snapshot")"
    -diff_directory "$(winpath "$work/diff")" -hard1 "$(winpath "$work/Boot.hd")"
    -scsi:5 harddisk -hard2 "$(winpath "$work/LokaDev.hd")"
    -video none -sound none -nothrottle -natural -skip_gameinfo
    -autoboot_delay 1 -autoboot_script "$(winpath "$work/mame-measure-heap.lua")"
  )
  local rompath="${MAME_ROMPATH:-}"
  if [ "$CATCH_SYSERROR" -eq 1 ] && [ -n "$rompath" ]; then
    # MAME separates search paths with semicolons. Keep caller-relative ROM
    # entries meaningful after the debugger switches to its capture directory.
    local remaining="$rompath" path separator=""
    rompath=""
    while :; do
      path="${remaining%%;*}"
      case "$path" in
        /*|[A-Za-z]:*|\\*) ;; # Already absolute for the host/MAME.
        *) path="$(winpath "$PWD/$path")" ;;
      esac
      rompath+="$separator$path"
      [[ "$remaining" == *';'* ]] || break
      remaining="${remaining#*;}"
      separator=';'
    done
  fi
  [ -n "$rompath" ] && args+=(-rompath "$rompath")
  local status=0 executable="$MAME_EXECUTABLE"
  if [ "$CATCH_SYSERROR" -eq 1 ]; then
    printf 'go\n' >"$work/debug.cmd"
    args+=(-debug -debugger none -log -debuglog -debugscript "$(winpath "$work/debug.cmd")")
    # Resolve a relative executable before switching cwd for debugger saves.
    if [[ "$executable" == */* ]] && [[ "$executable" != /* ]]; then
      executable="$(cd "$(dirname "$executable")" && pwd)/$(basename "$executable")"
    fi
  fi
  (
    if [ "$CATCH_SYSERROR" -eq 1 ]; then cd "$work" || exit 1; fi
    if [ "$CPU" = ppc ]; then
      export LOKA_APP_NAME="$app" LOKA_LAUNCH_CLICKS="$PPC_DISK_ICON $launch_icon"
    fi
    LOKA_CATCH_SYSERROR="$CATCH_SYSERROR" LOKA_SNAP_LOG="$(winpath "$work/measure.log")" LOKA_TAB_COUNT="$tabs" LOKA_STEPS="$steps" \
      WSLENV="${WSLENV:+$WSLENV:}LOKA_CATCH_SYSERROR:LOKA_SNAP_LOG:LOKA_TAB_COUNT:LOKA_STEPS${LOKA_LAUNCH_WAIT+:LOKA_LAUNCH_WAIT}${LOKA_APP_NAME+:LOKA_APP_NAME}${LOKA_LAUNCH_CLICKS+:LOKA_LAUNCH_CLICKS}" \
      timeout 1200 "$executable" "${args[@]}" >"$work/mame.out" 2>&1 </dev/null
  ) || status=$?
  if [ "$CATCH_SYSERROR" -eq 1 ]; then
    grep 'SYSERR ' "$work/error.log" 2>/dev/null || echo 'no SysError'
    local saved
    for saved in error.log stack.bin heap.bin lowmem.bin; do
      [ ! -f "$work/$saved" ] || printf '%s\n' "$work/$saved"
    done
  fi
  [ "$status" -eq 0 ] || fail "$example: MAME exited with status $status; see $work/mame.out"
  local log
  log="$(tr -d '\r' < "$work/measure.log")"
  grep -qx 'LOKA-MEASURE: complete' <<<"$log" || fail "$example: probe did not complete; see $work/measure.log"
  local live max
  live="$(sed -n 's/^PEAK live=\([0-9]*\) .*/\1/p' <<<"$log")"
  max="$(sed -n 's/^launched max=\([0-9]*\)$/\1/p' <<<"$log")"
  [ -n "$live" ] && [ -n "$max" ] || fail "$example: the application did not survive the workload; see $work/measure.log and $work/snapshot"
  awk -v ex="$example" -v live="$live" -v max="$max" -v pref="$preferred_k" '
  # Exact integer ceiling: num and den are whole byte counts, so no binary
  # fraction (1.1 has none) can push a value that lands on a boundary past it.
  function ceil_div(num, den) { return int((num + den - 1) / den) }
  BEGIN {
    fixed = pref * 1024 - max; need = live + fixed
    min = 32 * ceil_div(need * 11, 10 * 1024 * 32); best = 64 * ceil_div(need * 3, 2 * 1024 * 64)
    printf "%-13s need=%5.1fK (live %d + fixed %d)  minimum=%dK  preferred=%dK  (declared preferred %dK)\n",
      ex, need / 1024, live, fixed, min, best, pref
  }'
  if [ "$CPU" = ppc ]; then
    # With virtual memory off (the probe image) the Process Manager adds the
    # code fragment to the partition, so the need above holds only there.
    # With it on the code section is file-mapped instead; the peak without it
    # bounds that case.
    local code
    code="$(pef_code_bytes "$bin")" || fail "$example: could not read the PEF code section of $bin"
    awk -v ex="$example" -v live="$live" -v code="$code" 'BEGIN {
      printf "%-13s virtual memory on: peak without the %d-byte code section = %.1fK\n", ex, code, (live - code) / 1024
    }'
  fi
}

# Bytes of the code sections in the PEF container that a PPC MacBinary
# carries in its data fork.
pef_code_bytes() {
  python3 - "$1" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
fork = data[128:128 + int.from_bytes(data[83:87], 'big')]
if fork[:8] != b'Joy!peff':
    sys.exit('no PEF container in the data fork')
count = struct.unpack('>H', fork[32:34])[0]
print(sum(struct.unpack('>I', fork[48 + 28 * i:52 + 28 * i])[0]
          for i in range(count) if fork[40 + 28 * i + 24] == 0))
PY
}

for example in "${examples[@]}"; do
  measure "$example"
done
