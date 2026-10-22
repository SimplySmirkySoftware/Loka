#!/bin/bash
# Rig check for #1544: the quit Apple Event (Dock Quit, logout, osascript quit) reaches a Loka app's own Quit.
# Runs SimpleText through six cases and prints PASS/FAIL per case. Not part of CI: it needs a logged-in
# console session, System Events keystrokes (Accessibility for the shell that runs it), and an ASCII
# keyboard input source. Usage: tests/macos/check-quit-event.sh <path to LokaSimpleTextMacOS.app>
APP=${1:?usage: $0 <LokaSimpleTextMacOS.app>}
BIN="$APP/Contents/MacOS/LokaSimpleTextMacOS"
# A missing binary would make every case look like "the app ended"; refuse instead.
[ -x "$BIN" ] || { echo "no executable at $BIN" >&2; exit 2; }
OUT=${OUT:-/tmp/loka-quit-event-check}
mkdir -p "$OUT"
FAILS=0
# osascript can hang on Accessibility queries; bound every call.
osa() { ( osascript "$@" > "$OUT/osa.out" 2>&1 & O=$!; ( sleep 12; kill $O 2>/dev/null ) & wait $O 2>/dev/null ); cat "$OUT/osa.out"; }
launch() {
  pkill -x LokaSimpleTextMacOS; sleep 0.5
  "$BIN" > "$OUT/$1.log" 2>&1 & P=$!; sleep 3
  kill -0 $P 2>/dev/null || { echo "FAIL $1: the app did not start"; exit 2; }
  osa -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $P) to true" > /dev/null; sleep 0.3
}
front() { osa -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $P) to true" > /dev/null; sleep 0.3; }
typein() {
  osa -e "tell application \"System Events\" to tell (first process whose unix id is $P) to set focused of text area 1 of scroll area 1 of window 1 to true" > /dev/null; sleep 0.3
  osa -e "tell application \"System Events\" to keystroke \"$1\"" > /dev/null; sleep 0.8
}
key() {
  front
  case "$1" in
    esc) osa -e 'tell application "System Events" to key code 53' > /dev/null ;;
    *) osa -e "tell application \"System Events\" to keystroke \"$1\" using command down" > /dev/null ;;
  esac
  sleep 2
}
quitevent() {
  ( osascript -e 'with timeout of 90 seconds' -e "tell application \"$APP\" to quit" -e 'end timeout' > "$OUT/$1.out" 2>&1
    echo "status=$?" >> "$OUT/$1.out" ) &
  sleep 2.5
}
alive() { kill -0 $P 2>/dev/null; }
reply() { tr '\n' ' ' < "$OUT/$1.out"; }
check() { if [ "$2" = yes ]; then echo "PASS $1"; else echo "FAIL $1: $3"; FAILS=$((FAILS + 1)); fi; }

# 1. Edited, quit event, Cancel: the app and the document stay; the sender is told userCanceled (-128).
launch c1; typein abc; quitevent c1; key esc; sleep 1
r=$(reply c1); ok=no; alive && echo "$r" | grep -q -- '-128' && ok=yes
check "edited + Cancel keeps the app and answers -128" $ok "alive=$(alive && echo y || echo n) reply=[$r]"

# 2. Edited, quit event, Don't Save (Command-D): the app ends and the sender is told success.
launch c2; typein abc; quitevent c2; key d; sleep 1
r=$(reply c2); ok=no; ! alive && echo "$r" | grep -q 'status=0' && ok=yes
check "edited + Don't Save ends the app and answers success" $ok "alive=$(alive && echo y || echo n) reply=[$r]"

# 3. Unedited, quit event: the app ends through Loka's Quit and answers success.
launch c3; quitevent c3; sleep 1
r=$(reply c3); ok=no; ! alive && echo "$r" | grep -q 'status=0' && ok=yes
check "unedited ends the app and answers success" $ok "alive=$(alive && echo y || echo n) reply=[$r]"

# 4. Edited, the close box's confirm is up (Command-W), quit event: the busy document declines (-128);
#    Cancel then keeps the document.
launch c4; typein abc; key w; quitevent c4; key esc; sleep 1
r=$(reply c4); ok=no; alive && echo "$r" | grep -q -- '-128' && ok=yes
check "quit event during the close confirm answers -128 and keeps the app" $ok "alive=$(alive && echo y || echo n) reply=[$r]"

# 5. Edited, two quit events: the second is refused at once; Don't Save answers the first with success.
launch c5; typein abc; quitevent c5a; quitevent c5b; key d; sleep 1
ra=$(reply c5a); rb=$(reply c5b); ok=no
! alive && echo "$ra" | grep -q 'status=0' && echo "$rb" | grep -q -- '-128' && ok=yes
check "second quit event is refused, the first answers success" $ok "first=[$ra] second=[$rb]"

# 6. Control: the menu's Command-Q asks and Don't Save ends the app (no Apple Event involved).
launch c6; typein abc; key q; key d; sleep 1
ok=no; ! alive && ok=yes
check "menu Command-Q then Don't Save ends the app" $ok "alive=$(alive && echo y || echo n)"

pkill -x LokaSimpleTextMacOS
echo "failures=$FAILS"
exit $FAILS
