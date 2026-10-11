#!/usr/bin/env bash
# Called at the clean release checkout root; host configuration stays on the rig.
set -euo pipefail
refuse() { echo "win32 remote stage refused: $*" >&2; exit 1; }
[[ $# == 4 ]] || refuse 'usage: win32-remote-stage.sh <ssh-host> <x64|x86> <modern|xp> <remote-env-cmd>'
host=$1 arch=$2 profile=$3 environment=$4
[[ $host =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]*$ ]] || refuse 'ssh-host must be an SSH config alias'
[[ $arch == x64 || $arch == x86 ]] || refuse 'architecture must be x64 or x86'
[[ $profile == modern || $profile == xp ]] || refuse 'profile must be modern or xp'
[[ $profile != xp || $arch == x86 ]] || refuse 'xp requires x86'
# A literal drive path, also safe inside a CALL line (which expands twice).
[[ $environment =~ ^[a-zA-Z]:\\[a-zA-Z0-9\ ._\\-]+\.cmd$ ]] || refuse 'remote-env-cmd must be an absolute drive path to a .cmd file (letters, digits, spaces, dot, underscore, dash)'
root=$(git rev-parse --show-toplevel 2>/dev/null) || refuse 'run at the top of a Git checkout'
[[ $(pwd -P) == "$root" ]] || refuse 'run at the top of a Git checkout'
status=$(git status --porcelain --untracked-files=all)
[[ -z $status ]] || refuse 'Git tree must be clean'
sha=$(git rev-parse --verify HEAD)
name=win32-$arch
[[ $profile != xp ]] || name+=-xp
# Twelve hex digits keep the deepest object path under MAX_PATH on Windows 8.1.
remote="C:/loka-release/${sha:0:12}-$arch-$profile"
mkdir -p build/release
scratch=$(mktemp -d "$root/build/release/.remote-stage.XXXXXX")
finish() {
    result=$?
    rm -rf -- "$scratch"
    if (( result != 0 )); then
        echo "win32 remote stage failed; remote work retained at $host:$remote" >&2
    fi
}
trap finish EXIT
# Encode arguments as PowerShell literals, then UTF-16LE. No user text reaches
# the remote default shell except the base64 alphabet.
remote_run() {
    local encoded
    encoded=$(python3 - "$1" "$remote" "$arch" "$profile" "$environment" <<'PY'
import base64, pathlib, sys
mode, work, arch, profile, environment = sys.argv[1:]
def literal(value):
    return "'" + value.replace("'", "''") + "'"
if mode == 'Prepare':
    script = '& {\n' + pathlib.Path('scripts/release/win32-remote-stage.ps1').read_text() + '\n}'
else:
    script = '& ' + literal(work + '/remote.ps1')
script += ' -Mode ' + literal(mode) + ' -WorkDirectory ' + literal(work)
script += ' -Architecture ' + literal(arch) + ' -TargetProfile ' + literal(profile)
script += ' -EnvironmentCommand ' + literal(environment) + '\nexit $LASTEXITCODE\n'
print(base64.b64encode(script.encode('utf-16le')).decode('ascii'))
PY
)
    ssh -o BatchMode=yes "$host" "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded"
}
git archive --format=zip HEAD > "$scratch/source.zip"
remote_run Prepare
scp -o BatchMode=yes "$scratch/source.zip" "$host:$remote/source.zip"
scp -o BatchMode=yes scripts/release/win32-remote-stage.ps1 "$host:$remote/remote.ps1"
remote_run Unpack
remote_run Stage
remote_run List
for file in stage.zip inventory.json toolchain.txt; do
    scp -o BatchMode=yes "$host:$remote/$file" "$scratch/$file"
done
python3 scripts/release/win32-remote-stage-verify.py "$scratch" "$root/build/release/$name"
remote_run Cleanup
echo "Staged $name from $sha"
