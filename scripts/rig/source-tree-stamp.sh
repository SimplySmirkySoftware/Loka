#!/usr/bin/env bash
# Deliberate platform twin: tests/win32/SourceTreeStamp.ps1. Only this shell
# helper has a content fallback; the Win32 rig always has git.
# CLI: source-tree-stamp.sh id|withdraw|publish [stamp] [source-root]
set -euo pipefail

source_id() (
  cd "$root" || exit 1
  git_command="$(command -v git || true)"
  if [ -n "$git_command" ] && [ "$("$git_command" rev-parse --is-inside-work-tree 2>/dev/null)" = true ]; then
    # A subshell scopes the index override, including every failure path.
    git_dir="$("$git_command" rev-parse --absolute-git-dir)" || exit 1
    scratch="$(mktemp -d "${TMPDIR:-/tmp}/loka-source-index.XXXXXX")" || exit 1
    trap 'rm -rf "$scratch"' EXIT
    trap 'exit 1' HUP INT TERM
    export GIT_INDEX_FILE="$scratch/index"
    if [ -f "$git_dir/index" ]; then
      cp "$git_dir/index" "$GIT_INDEX_FILE" || exit 1
    else
      "$git_command" read-tree HEAD || exit 1
    fi
    "$git_command" add -u || exit 1
    "$git_command" write-tree || exit 1
  else
    # Sort relative paths bytewise; refuse newlines before the line-based sort.
    # Temp files live outside the source walk. Only regular files contribute.
    scratch="$(mktemp -d "${TMPDIR:-/tmp}/loka-source-content.XXXXXX")" || exit 1
    trap 'rm -rf "$scratch"' EXIT
    trap 'exit 1' HUP INT TERM
    find . \( -path ./build -o -name .git \) -prune -o -type f -print0 >"$scratch/paths0" || exit 1
    while IFS= read -r -d '' path; do
      case "$path" in *$'\n'*) echo 'source stamp: newline in filename' >&2; exit 1 ;; esac
      printf '%s\n' "$path"
    done <"$scratch/paths0" >"$scratch/paths" || exit 1
    LC_ALL=C sort "$scratch/paths" >"$scratch/sorted" || exit 1
    while IFS= read -r path; do
      digest="$(shasum -a 256 <"$path")" || exit 1
      printf '%s  %s\n' "${digest%% *}" "$path"
    done <"$scratch/sorted" >"$scratch/listing" || exit 1
    digest="$(shasum -a 256 <"$scratch/listing")" || exit 1
    printf 'content:%s\n' "${digest%% *}"
  fi
)

operation="${1:-}"
case "$operation" in
  id) root="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"; source_id ;;
  withdraw|publish)
    stamp="${2:?stamp path required}"
    root="${3:-$(cd "$(dirname "$0")/../.." && pwd)}"
    # Publication owns its temporary and the before record through all exits.
    temporary="$stamp.$$.tmp"
    cleanup() {
      rm -f "$temporary" || true
      if [ "$operation" = publish ]; then rm -f "$stamp.before" || true; fi
    }
    trap cleanup EXIT
    trap 'exit 0' HUP INT TERM
    update_stamp() {
      rm -f "$stamp" || return 1
      if [ "$operation" = withdraw ]; then
        rm -f "$stamp.before" || return 1
        id="$(source_id)" || return 1
        printf '%s\n' "$id" >"$temporary" || return 1
        mv -f "$temporary" "$stamp.before" || return 1
      else
        [ -f "$stamp.before" ] || return 1
        before="$(cat "$stamp.before")" || return 1
        id="$(source_id)" || return 1
        [ "$before" = "$id" ] || return 1
        printf '%s\n' "$id" >"$temporary" || return 1
        mv -f "$temporary" "$stamp" || return 1
        echo "Scenario source tree $id"
      fi
    }
    if ! update_stamp; then echo 'No source stamp: capture/publication failed; build again' >&2; fi
    exit 0 ;;
  *) echo "Usage: $0 id [source-root] | withdraw|publish stamp [source-root]" >&2; exit 2 ;;
esac
