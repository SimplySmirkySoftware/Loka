# Release archive assembly

`assemble.py` creates a zip and adjacent `.manifest.txt` from one explicit file
allowlist. It resolves a local tag, checks out its peeled commit into the single
canonical path `/tmp/loka-release-assembly-worktree`, verifies that checkout is
clean, runs any requested build commands there, and removes the worktree after
collection. The fixed checkout path is recorded in the manifest because Retro68
68K binaries embed absolute source paths.

Each non-comment allowlist line is tab-separated:

```text
git<TAB>path-at-tag[<TAB>path-in-archive]
build<TAB>named-build-output[<TAB>path-in-archive]
```

`git` files must be tracked at the resolved tag commit. `build` files must be
created by a supplied build command. Paths are relative to the canonical
checkout, archive paths default to source paths, and directories and symlinks
are refused. The archive is written from these entries alone, then reopened and
checked for exact membership and content hashes.

For example, with `TAG` set to the release tag being assembled:

```sh
python3 scripts/release/assemble.py \
  --tag "$TAG" \
  --allowlist "release-$TAG.txt" \
  --archive "/tmp/Loka-$TAG.zip" \
  --build-command 'cmake --preset retro68-68k-release' \
  --build-command 'cmake --build --preset retro68-68k-release'
```

The current LRPK gate refuses any `.LRP` or `.LRPK` package that is not itself
Git-tracked at the tag commit. Follow-up: make `lrpc` emit an input manifest at
pack time and make this assembler refuse a package unless every declared input
is Git-tracked at that commit. Until that lands, release review must check the
tracked-input rule by hand for each listed package.

The canonical path makes repeated 68K builds use the same embedded source path.
Follow-up: add `-ffile-prefix-map`-style normalization to the Retro68 build so
binary hashes can be compared across machines without depending on that path.

After the Retro68 Release builds (both the 68K and the PPC configure with
`-DLOKA_BUILD_SMIRKYCARD=ON`), release assembly runs
`--build-command 'python3 scripts/release/classic-disks.py'` to produce
`build/release/classic-disks/{ScrapbookUI68K,ScrapbookUIPPC,LokaSmirkyCard68K,LokaSmirkyCardPPC}.dsk`.
Each volume contains its application and the tracked files beside it:
ScrapbookUI's `ASSETS.LRP`, or every SmirkyCard card script listed in
`example/SmirkyCard/disk-scripts.txt` (the manifest the development disks and
the macOS and Win32 builds read), stored as SimpleText text, plus the
data-fork `.PICT` pictures listed by stem in `example/SmirkyCard/disk-pictures.txt`. Packaging reads
back both application forks and every unchanged data file before publishing
each image. Each image is the smallest floppy that holds the payload plus a
128 KiB reserve (800 KiB, then 1440 KiB); a larger payload gets an image
rounded up to the next 512 KiB, which is not a floppy and is meant for an
emulator or a PowerPC Mac that mounts disk images (PPC SmirkyCard: 2 MiB).
Plain applications use Retro68's existing disks.

## Windows archives via a remote builder

Run the assembler on WSL with a Windows SSH config alias and an absolute path
to a rig-local `.cmd` environment script. For the two Windows allowlist runs,
use these build commands respectively, with `X64_BUILDER` and `X86_BUILDER`
set to the caller's SSH config aliases. The inner quotes preserve the Windows
path through the assembler's POSIX shell:

```sh
--build-command "bash scripts/release/win32-remote-stage.sh $X64_BUILDER x64 modern 'C:\loka-setup\release-env-x64.cmd'"
--build-command "bash scripts/release/win32-remote-stage.sh $X86_BUILDER x86 xp 'C:\loka-setup\release-env-x86-xp.cmd'"
```

The local prerequisites are Bash, Git, Python 3 and key-authenticated `ssh`/`scp`.
The host argument is an SSH config alias. Environment paths may contain letters,
digits, spaces, dots, underscores and dashes. The remote helper uses Windows
PowerShell and .NET ZipFile; the builder does not need Git or Python.

The rig-local environment script is called in the same `cmd /c` process that
launches the release stage. It must return nonzero on failure and leave `cl`,
`cmake`, `ninja`, the selected compiler/SDK variables (including
`VCToolsVersion` and `WindowsSDKVersion`), and compiler include/library paths
ready for the requested architecture/profile. Use `exit /b`, not `exit`, and
do not discard the environment with `setlocal`. Put the chosen CMake and Ninja
on PATH **after** calling the Visual Studio environment batch file, so bundled
Visual Studio tools do not override that selection. Installation paths and SDK
selection belong in that rig-local script.

The command refuses a dirty or non-root checkout, archives HEAD, and stages in
`C:\loka-release\<sha12>-<arch>-<profile>`, where `<sha12>` is the first 12
hex digits of HEAD. It removes a previous remote work directory of that name
before starting; do not run the same tuple on one host concurrently. Failed
work is retained there for inspection; successful work is removed. Files return
through a temporary directory and are checked against the remote relative-path,
size and SHA-256 inventory before publication.
Existing local output is preserved on transfer/verification failure, with
rollback on publication I/O failure; publication is not crash-atomic and expects
one local writer.

Modern output is `build/release/win32-x64/` and XP-profile output is
`build/release/win32-x86-xp/`. Their adjacent `*.toolchain.txt` files record the
compiler/SDK environment, CMake version, OS version and PowerShell version.
Include those files as `build` entries in the corresponding allowlists. The
archive manifest records the build command; `*.toolchain.txt` records the
builder's toolchain identity. Neither is runtime compatibility evidence.
