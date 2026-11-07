#!/bin/sh
set -eu
exec python3 -I "$(dirname "$0")/run.py" "$1" 11
