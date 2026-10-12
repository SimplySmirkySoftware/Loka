# v0.0.6 RC `a536c4e0` evidence

- `matrix/`: logs from the RC matrix run on 2026-10-12 (verify only; no bundle was re-baked).
  - Classic: L0 summary and the 29 MAME cells.
  - Win32 VM: ctest output and the 18 per-cell logs.
  - Tahoe: summaries, plus the Scrapbook rerun after the host `lrpc` build.
  - Mavericks: the Tiger UB1 summary.
- `rehearsal1/`: `assemble.py` manifests for the five archives, each assembled at `37a0cb8f` (`release/0.0.6`) under the local tag `v0.0.6-rehearsal1`.
  - The UB2 manifest is the second UB2 run. It uses `ditto --norsrc --noextattr`, so the macOS provenance attribute no longer enters the zip as 37 AppleDouble entries.

Home-directory paths and user names are replaced with `~` / `user`.
