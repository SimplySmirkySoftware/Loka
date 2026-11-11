#!/usr/bin/env bash
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
HELPER="$REPO_DIR/scripts/rig/source-tree-stamp.sh"
SANDBOX="$(mktemp -d)"
trap 'rm -rf "$SANDBOX"' EXIT
fail() { echo "SourceTreeStampTest: $*" >&2; exit 1; }
mkdir -p "$SANDBOX/tree" "$SANDBOX/tmp"
export TMPDIR="$SANDBOX/tmp"
cd "$SANDBOX/tree"
git init -q
git config user.email test@example.invalid
git config user.name Test
printf 'original\n' >tracked
git add tracked
git commit -qm initial
id() { bash "$HELPER" id "$PWD"; }
clean="$(id)"
[ "$clean" = "$(git rev-parse 'HEAD^{tree}')" ] || fail clean
cp .git/index "$SANDBOX/index"
printf 'edited\n' >tracked
[ "$(id)" != "$clean" ] || fail edit
printf 'original\n' >tracked
[ "$(id)" = "$clean" ] || fail revert
printf 'ignored\n' >untracked
[ "$(id)" = "$clean" ] || fail untracked
cmp .git/index "$SANDBOX/index" || fail 'real index changed'
printf 'added\n' >added
git add added
added="$(id)"
[ "$added" != "$clean" ] || fail added
printf 'working\n' >added
[ "$(id)" != "$added" ] || fail 'staged content used'
git reset -q HEAD added
rm added
cp .git/index "$SANDBOX/index"
export GIT_INDEX_FILE="$SANDBOX/foreign-index"
[ "$(id)" = "$clean" ] || fail 'caller index used'
[ "$GIT_INDEX_FILE" = "$SANDBOX/foreign-index" ] || fail 'caller index lost'
[ ! -e "$GIT_INDEX_FILE" ] || fail 'caller index written'
unset GIT_INDEX_FILE
cmp .git/index "$SANDBOX/index" || fail 'index altered'
# Missing checkout index is seeded from HEAD.
mv .git/index "$SANDBOX/saved-index"
[ "$(id)" = "$clean" ] || fail 'missing index'
mv "$SANDBOX/saved-index" .git/index
real_git="$(command -v git)"
mkdir "$SANDBOX/first" "$SANDBOX/second"
printf '#!/bin/bash\nprintf called >>"$SANDBOX/git-calls"\nexec "%s" "$@"\n' "$real_git" >"$SANDBOX/first/git"
printf '#!/bin/bash\nexit 99\n' >"$SANDBOX/second/git"
chmod +x "$SANDBOX/first/git" "$SANDBOX/second/git"
export SANDBOX
[ "$(PATH="$SANDBOX/first:$SANDBOX/second:$PATH" id)" = "$clean" ] || fail 'first git'
[ -s "$SANDBOX/git-calls" ] || fail 'git wrapper unused'
stamp="$SANDBOX/stamp"
bash "$HELPER" withdraw "$stamp" "$PWD"
[ ! -e "$stamp" ] && [ -s "$stamp.before" ] || fail withdraw
bash "$HELPER" publish "$stamp" "$PWD"
[ "$(cat "$stamp")" = "$clean" ] && [ ! -e "$stamp.before" ] || fail publish
bash "$HELPER" withdraw "$stamp" "$PWD"
printf 'edited\n' >tracked
bash "$HELPER" publish "$stamp" "$PWD"
[ ! -e "$stamp" ] && [ ! -e "$stamp.before" ] || fail 'mid-build edit'
printf stale >"$stamp"
bash "$HELPER" publish "$stamp" "$PWD"
[ ! -e "$stamp" ] || fail 'publish without withdraw'
# Operational git failure must not silently switch identity kinds.
printf '#!/bin/bash\nif [ "$1" = add ]; then touch "$GIT_INDEX_FILE.lock"; exit 1; fi\nexec "%s" "$@"\n' "$real_git" >"$SANDBOX/first/git"
if PATH="$SANDBOX/first:$PATH" id; then fail 'git failure accepted'; fi
[ -z "$(ls -A "$TMPDIR")" ] || fail 'temporary index/lock leaked'
mkdir "$SANDBOX/copy"
cd "$SANDBOX/copy"
printf 'space\n' >'with space'
content="$(id)"
case "$content" in content:????????????????????????????????????????????????????????????????) ;; *) fail prefix ;; esac
[ "$(id)" = "$content" ] || fail stability
file_hash="$(shasum -a 256 <'with space')"
listing_hash="$(printf '%s  ./with space\n' "${file_hash%% *}" | shasum -a 256)"
[ "$content" = "content:${listing_hash%% *}" ] || fail 'digest recipe'
# A git executable that cannot answer selects the same content fallback.
printf '#!/bin/bash\nexit 1\n' >"$SANDBOX/first/git"
[ "$(PATH="$SANDBOX/first:$PATH" id)" = "$content" ] || fail 'no git fallback'
mkdir build .git
printf excluded >build/file
printf excluded >.git/file
[ "$(id)" = "$content" ] || fail exclusions
printf edited >'with space'
[ "$(id)" != "$content" ] || fail 'content edit'
edited="$(id)"
printf new >new-file
[ "$(id)" != "$edited" ] || fail 'content untracked'
printf newline >$'bad\nname'
if id; then fail 'newline accepted'; fi
rm "$PWD/"$'bad\nname'
printf '#!/bin/bash\nexit 1\n' >"$SANDBOX/first/shasum"
chmod +x "$SANDBOX/first/shasum"
if PATH="$SANDBOX/first:$PATH" id; then fail 'unanswerable accepted'; fi
bash "$HELPER" withdraw "$stamp" "$PWD"
PATH="$SANDBOX/first:$PATH" bash "$HELPER" publish "$stamp" "$PWD"
[ ! -e "$stamp" ] && [ ! -e "$stamp.before" ] || fail 'failed capture cleanup'
[ -z "$(ls -A "$TMPDIR")" ] || fail 'content temporary leaked'
# Refusing storage must still leave the build successful, even during cleanup.
mkdir "$SANDBOX/refused" "$SANDBOX/refused.before"
bash "$HELPER" withdraw "$SANDBOX/refused" "$PWD"
bash "$HELPER" publish "$SANDBOX/refused" "$PWD"
echo 'source tree stamp tests passed'
