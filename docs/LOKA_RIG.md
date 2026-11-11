# Loka rig orchestration

`scripts/rig/loka-rig.py` is the public host-side entry point for release-preflight
rigs. It resolves one requested ref to an exact commit, delegates platform
mechanisms to one registered adapter, and applies the shared result and cleanup
contract.

```sh
python3 scripts/rig/loka-rig.py run mavericks-10.9 \
  --ref <commit-sha> --mode flow --local-config <macos-local.ini>

python3 scripts/rig/loka-rig.py run toolbox-maciix \
  --ref <commit-sha> --mode flow --local-config <toolbox-local.ini>
```

The public command accepts only facts shared by every adapter: rig ID, ref,
mode, and an optional local mapping. VM names, SSH transport, MAME paths,
scenario internals, and test-only controls remain adapter-owned.

The Mavericks adapter builds all six scenario vehicles and publishes their source
stamp in `build/macos/Debug`; its rsync copy has no git metadata, so the stamp
uses a content digest (including untracked regular files, excluding `build/` and
`.git`). For direct Tahoe runs, build with `cmake --build --preset macos-scenarios`
before invoking `LOKA_MACOS_RIG=tahoe tests/macos/run-scenario.sh <example> <scenario>`;
both routes refuse a missing or changed stamp and target-only builds withdraw it.

The Toolbox runner uses a per-vehicle `<X>.source-stamp.txt` beside `<X>.bin`,
withdrawn before compilation and published after `cmake --build --preset
retro68-68k-release --target <X>_APPL`. Toolbox workflows build one vehicle at a
time, so an aggregate stamp would refuse normal builds. Missing, unanswerable,
or mismatched source identity refuses the run before MAME starts.

## Updating desktop startup-identity pairs

For a macOS or Win32 bake that changes both members of a startup-identity
pair, collect finalized captures for the intended cells from the same build
and rig before updating either golden. In a fresh directory under `build/`,
write `cells.txt` with one `example scenario` per line, and copy each capture
to `<example>/<scenario>.png`. For example:

```text
cells.txt:
  scrapbook startup
  scrapbook flip-forward-back

scrapbook/startup.png
scrapbook/flip-forward-back.png
```

Set `LOKA_GOLDEN_UPDATE_ROOT` to that directory while running each listed
cell with `--update-golden` (PowerShell: `$env:LOKA_GOLDEN_UPDATE_ROOT`). Use
an absolute native path; the Win32 runner translates it for WSL Python.
Keep the candidate set fixed until all listed updates succeed, then unset
the variable. Each runner still captures and validates its own cell; that
capture must match its prepared candidate byte-for-byte.

The shared `scripts/rig/golden_identity_guard.py` selects listed partners
from this candidate set, independent of update order. An unlisted partner
uses its live golden. Missing or symlinked listed candidates refuse the
update rather than falling back. With no candidate set, single-cell updates
retain their live-golden checks. Updates publish one cell at a time; this
input does not make desktop bundle publication atomic. Prior work-directory
captures are never discovered automatically. Toolbox keeps its existing
staged-bundle protocol.

## Registered adapters

| Adapter | Tracked descriptor | Local mapping example | Runtime mechanism |
| --- | --- | --- | --- |
| macOS | `scripts/rig/macos/rigs/mavericks-10.9.ini` | `scripts/rig/macos/rigs/local.example.ini` | Parallels VM, target-local build and scenario |
| Toolbox | `scripts/rig/toolbox/rigs/toolbox-maciix.ini` | `scripts/rig/toolbox/rigs/local.example.ini` | detached checkout, Retro68 build, MAME seven-scenario rail |

The dispatcher identifies a rig only when exactly one adapter directory contains
its descriptor. An unknown or duplicate registration fails closed. Each adapter
parses its own strict descriptor and local-mapping schema; the common layer does
not turn platform build or launch commands into configuration data.

## Shared lifecycle

The common executor owns this sequence:

1. prepare an isolated exact-commit run;
2. build through the adapter;
3. complete the adapter's machine-verdict procedure;
4. collect finalized presentation and diagnostic artifacts;
5. finalize the common manifest;
6. clean up only after successful collection and manifest finalization;
7. rewrite the manifest with the observed post-cleanup retention state.

Failure before the cleanup commit point retains the adapter work area whenever
possible. Artifact manifests refuse symlinks and duplicate field names. Adapter
fields can extend the manifest but cannot redefine common facts.

## Result vocabulary

`run-manifest.txt` separates the overall run from the two evidence lanes:

- `result`: whether the whole requested run, including collection and cleanup,
  succeeded;
- `build_verification`: whether the requested commit built;
- `runtime_verification`: whether the deterministic adapter procedure passed;
- `machine_verdict`: `passed`, `failed-or-not-reached`, or `refused` when a
  machine ran successfully but the reference identity was not eligible to
  authorize a pixel comparison;
- `presentation_status`: whether finalized human evidence was collected;
- `recording_status`: `not-requested`, `manual`, `collected`, or `failed`;
- `target_retained`, `target_workdir`, and `next_diagnostic_command`: observed
  diagnostic retention facts.

A presentation, manifest, or cleanup failure after runtime completion reports
`result=failed` while preserving `machine_verdict=passed`. The later failure
must not rewrite an already-completed machine fact.
Failures before the machine launches, including missing build provenance or a
missing emulator executable, keep both runtime and machine verdicts at
`failed-or-not-reached`; only post-runtime reference verification emits
`refused`.

The Toolbox adapter stages one strict, atomic golden bundle. Its sole manifest
binds the scenario registry, every PNG digest and producing application-binary
digest, clean-build compiler/interface
and Retro68 provenance, and the emulator, verified ROM inventory, RAM, machine,
capture adapter, and pre-launch Boot.hd digest. The bundle identity is eligible
only when it matches `reference_identity_sha256` in the tracked rig descriptor;
`--update-golden` cannot edit or otherwise self-authorize that authority.

Every other manifest field is either immutable run identity, adapter-specific
description, or a SHA-256 entry for a finalized regular file in the archive.

## Stage the last scenario capture

After a normal run, use `tests/toolbox/run-scenario.sh <example> <scenario>
--stage-last` to stage its normalized capture without launching MAME again.
The receipt in `build/mame-scenario/<example>/<scenario>/scenario-run-provenance.txt`
must match the current APPL digest, source-tree identity (the existing helper's
HEAD plus porcelain-status digest), registry digest, and capture adapter. Missing
or malformed receipts, unattestable source identity, missing capture/audit, an
unmatched or changed audit, a capture that differs from the digest recorded after
normalization, and receipts from probe/structural runs are refused.
A normal run records its receipt before launch; receipt write failures fail the
`provenance` stage. Flags are exclusive: `--stage-last` cannot accompany
`--update-golden`, `--probe`, or `--structural-audit`.
Staging uses the same bundle gate as `--update-golden`: publication waits for all
cells, and a fresh bake cannot authorize its own reference identity.

## Run every registered cell

`tests/toolbox/run-all-cells.sh [--stage-last | --update-golden]` visits the
scenario registry in order, one foreground child at a time, and continues after
failures. Its final table lists each cell's outcome and work directory; child
output remains under `build/mame-scenario/batch/`. In staging modes, `pass`
means that cell staged successfully (the bundle may still be incomplete).
Only `pass` and `differs <px>` leave `bake: possible`; audit differences,
refusals, and failed stages appear in the complete blocker list. The command
exits nonzero for any non-pass outcome, including pixel differences. Unknown or
combined flags are refused. This summary does not grant reference approval.
