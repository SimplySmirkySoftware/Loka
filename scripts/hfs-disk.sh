#!/usr/bin/env bash

# Shared Retro68 tool discovery and HFS population. Source from Bash.
find_retro68_tool() {
  local name="$1"
  local candidate

  if [ -n "${RETRO68_TOOLCHAIN_BIN:-}" ] \
    && [ -x "$RETRO68_TOOLCHAIN_BIN/$name" ]; then
    echo "$RETRO68_TOOLCHAIN_BIN/$name"
    return
  fi
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return
  fi
  if [ -n "${RETRO68_BUILD_DIR:-}" ] \
    && [ -x "$RETRO68_BUILD_DIR/toolchain/bin/$name" ]; then
    echo "$RETRO68_BUILD_DIR/toolchain/bin/$name"
    return
  fi
  for candidate in \
    "$HOME/Retro68-build/toolchain/bin/$name" \
    "$HOME/Documents/Projects/Retro68-build/toolchain/bin/$name"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return
    fi
  done
  echo "Error: Retro68 tool not found: $name" >&2
  exit 1
}


# Caller owns the image and private hfsutils HOME, and handles failure cleanup.
# Arguments: home label image partition binary-count binaries... plain-files...
hfs_populate() {
  local hfs_home="$1" label="$2" image="$3" partition="$4" count="$5"
  shift 5
  local path
  HOME="$hfs_home" "$HFORMAT" -l "$label" "$image" "$partition"
  while [ "$count" -gt 0 ]; do
    HOME="$hfs_home" "$HCOPY" -m "$1" :
    shift
    count=$((count - 1))
  done
  for path in "$@"; do
    HOME="$hfs_home" "$HCOPY" -r "$path" :
  done
  HOME="$hfs_home" "$HUMOUNT"
}
