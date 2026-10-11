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

After the Retro68 Release builds (including the 68K configure with
`-DLOKA_BUILD_SMIRKYCARD=ON`), release assembly runs
`--build-command 'python3 scripts/release/classic-disks.py'` to produce
`build/release/classic-disks/{ScrapbookUI68K,ScrapbookUIPPC,LokaSmirkyCard68K}.dsk`.
Each volume contains its application and the tracked adjacent `ASSETS.LRP` or
`MAIN.JS`; packaging reads back both application forks and the unchanged data
before publishing each image. Plain applications use Retro68's existing disks.
