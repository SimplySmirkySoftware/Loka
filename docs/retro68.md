# Retro68 setup

This project supports building Classic Mac OS targets with Retro68.

## Requirements

- Retro68 toolchain installed.
- `retro68.toolchain.cmake` available in a toolchain directory.

## Configuration

This repo includes shared Retro68 Release presets in `CMakePresets.json`:

- `retro68-68k-release`
- `retro68-ppc-release`
- `retro68-68k-standalone-release`
- `retro68-ppc-standalone-release`

These presets use `cmake/toolchains/Retro68.cmake`, which resolves the toolchain
from host-local configuration, environment variables, or common default paths.

By default, the Retro68 toolchain wrapper checks:

- `~/Retro68-build`
- `~/Retro68`

If your Retro68 build is in one of those locations and `ninja` is on `PATH`, the
shared Release presets can be used directly. Otherwise, copy the checked-in
example and describe this host once:

```sh
cp .env-retro68.example .env-retro68
```

`.env-retro68` is ignored by Git. VS Code Retro68 tasks and
`scripts/retro68-cmake.sh` load it automatically on macOS, Linux, and WSL. Use
paths as seen by the current host; a WSL file therefore contains Linux or
`/mnt/...` paths, not Windows path syntax.

For Retro68, use the Release presets directly. Dedicated `Debug` or
`MinSizeRel` presets are not maintained here because the practical workflow is
already Release-oriented and the Release flags are the ones we want to keep
consistent across Classic targets.

### Redistributable interfaces in hosted CI

The `Toolbox Build CI` workflow builds every Toolbox target for both 68K and
PowerPC in Retro68's official container. It selects the redistributable
Multiversal Interfaces explicitly and enables Loka's internal compatibility
boundary with `LOKA_TOOLBOX_MULTIVERSAL_INTERFACES=ON`. The container image is
pinned by digest in `.github/workflows/toolbox.yml` so the compiler and header
set cannot change silently.

This workflow is **build-verified** only: it proves that the applications
compile and link without redistributing Apple's Universal Interfaces. Runtime
and golden verification remain on the named Toolbox rig described in
`docs/LOKA_RIG.md`.

Universal Interfaces remain the default for existing local Retro68 setups. To
reproduce the hosted interface choice inside a Retro68 container launched with
`INTERFACES=multiversal`, run:

```sh
cmake --preset retro68-68k-release -G "Unix Makefiles" \
  -DLOKA_TOOLBOX_MULTIVERSAL_INTERFACES=ON
cmake --build --preset retro68-68k-release
cmake --preset retro68-ppc-release -G "Unix Makefiles" \
  -DLOKA_TOOLBOX_MULTIVERSAL_INTERFACES=ON
cmake --build --preset retro68-ppc-release
```

The compatibility headers under `apple/toolbox/compat/multiversal` are a
Toolbox implementation detail. They preserve the Universal-style contracts
already used by the backend and contain Multiversal's global Toolbox names;
they are not a second app-facing API.

### Host configuration

The local file accepts these settings:

- `RETRO68_BUILD_DIR` (path to Retro68 build output directory)
- `RETRO68_TOOLCHAIN_DIR` (advanced override for one CPU's toolchain CMake
  directory or file)
- `RETRO68_TOOLCHAIN_BIN` (optional host-tool directory)
- `CMAKE_MAKE_PROGRAM` (optional path to Ninja)

Prefer `RETRO68_BUILD_DIR`: the toolchain file uses it to select the matching
68K or PPC directory for each preset. `RETRO68_TOOLCHAIN_DIR` points at one
CPU's toolchain CMake directory, so setting it pins configuration to that CPU
and makes the other preset family fail. Set `CMAKE_MAKE_PROGRAM` when an editor
launched from the desktop cannot see Ninja on its inherited `PATH`. Values
already supplied by a caller or CI take precedence over the local file.

Then run:

```sh
scripts/retro68-cmake.sh --preset retro68-68k-release
scripts/retro68-cmake.sh --build --preset retro68-68k-release
```

### Host-tool failure after a Homebrew update

If a host tool such as `Rez` aborts under `dyld` with `Symbol not found` and
names a Homebrew `libboost_*.dylib`, the installed Retro68 tool was built
against an older Boost ABI. This is outside the Loka build: deleting Loka's
build directory does not repair the host tool.

Reconfigure and reinstall Retro68's host tools against the currently installed
Boost. Replace the two root paths below with the paths to the adjacent Retro68
source and build trees:

```sh
cmake -S /path/to/Retro68 -B /path/to/Retro68-build/build-host \
  -UBoost_DIR \
  -DCMAKE_PREFIX_PATH="$(brew --prefix boost)" \
  -DCMAKE_INSTALL_PREFIX=/path/to/Retro68-build/toolchain \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/to/Retro68-build/build-host --target install
```

The `-UBoost_DIR` removes a cached package directory that may point at a
Homebrew version that no longer exists. Confirm that the repaired executable
starts before retrying the Loka build:

```sh
/path/to/Retro68-build/toolchain/bin/Rez --help
```

Report the final 68K MacBinary application sizes after the build with:

```sh
python3 tools/ci/retro68_size_report.py build/retro68/68k/Release
```

The report reads each final `.bin` resource fork and prints its total file size
and CODE/DATA/RELA payload composition. The checked-in manifest owns the shipping
application inventory and growth thresholds. Growth means CODE+DATA (text+data),
excluding RELA and MacBinary overhead; the file total remains visible for attribution.
The default local command compares against the bank and fails above 200 KB of
cumulative growth per application. Here 1 KB = 1,024 bytes.

The maintainer ruling of 2026-09-14 sets these per-application PR bands:

| CODE+DATA delta from the built merge-base | Verdict |
| --- | --- |
| <50 KB | ok; no explanation required |
| 50 KB to <100 KB | Requires a PR body size note and the `size-note` label |
| >=100 KB | REGRESSION even with a note; a ruling is required (bank refresh PR with a design note) |

The workflow maps `size-note` to `LOKA_SIZE_NOTE=1` and reruns on label changes.
Local runs can use `--acknowledge-growth` or that environment variable. The gate
cannot inspect the PR body. The manifest stores the ok ceiling and note floor
(both 51,200 bytes), stop (102,400 bytes), and cumulative limit (204,800 bytes).

For PRs, Toolbox CI builds a fresh comparison worktree using the same pinned
image, Multiversal Interfaces, Release preset, generator and container mount path
as the candidate. It compares the **CI merge candidate** to its merge-base with
the event's immutable target SHA (normally that target SHA itself). Thus a stacked
PR uses its target branch, and already-landed growth is not charged again. The
report command takes `--compare-build-root <base Release directory>` together
with `--comparison-ref <base commit>`; the latter labels the evidence, while the
workflow is responsible for building that commit. This costs one extra 68K build
per non-skipped PR; PPC is built only for the candidate. No size cache is reused.

The checked-in bank remains the absolute record. Main/dispatch CI enforces the
cumulative guard: growth above 200 KB prints
`CUMULATIVE GROWTH: <app> +<bytes> since bank <sha>` and fails the job. Exactly
200 KB is allowed. Even local `--report-only` cannot bypass this guard. The PR
headroom comment remains informational and measures room to the cumulative limit.
Refresh the bank only with a fresh full Release build and an explicit inventory
and design note for accepted growth. A failed main check remains visible until
that refresh; a later per-PR ok verdict does not clear it.

Malformed or missing artifacts and unlisted candidate applications still fail,
including in report-only mode. For a PR adding a required 68K example, CI passes
`--compare-baseline <base manifest>`: if its artifact is absent from the comparison
build and neither its path nor its name is declared in that manifest, the report compares against
its own declared baseline numbers under the same PR growth bands and labels the
row `(new)`. A moved application (its name declared at base under another
path) and a required artifact declared at base (or compared without
`--compare-baseline`) still fails when missing from the comparison build; optional
artifacts retain their existing fallback and skip behavior.

The size audit is a drift detector. A bank refresh updates the absolute record;
it cannot waive the unconditional stop in a measured PR comparison. Changing
that stop requires an explicit ruling, not an acknowledgment flag.

### QuickDraw pattern alignment

QuickDraw reads `Pattern` arguments with long moves on the 68000, which faults
on an odd address (#1398). Toolbox `Pattern` constants are declared as
`ToolboxAlignedPattern` so their addresses are even despite `Pattern` itself
having byte alignment.
`tools/ci/check_toolbox_pattern_alignment.py` refuses a `Pattern` declared by value
outside `ToolboxAlignedPattern`. It reads only the token after the type name, so
declare a `Pattern` value in its own statement rather than after a pointer in one
declarator list. Bitmap bit buffers (`BitMap.baseAddr`) need word alignment too
but are not covered by this guard.

### Small-object pool

Every target linking `ToolboxOperatorNew.cpp` (Retro68 and RetroPPC)
serves allocations up to 256 bytes from process-lifetime, 2 KiB size-class
chunks backed by the Classic memory source (`ToolboxMemorySource.hpp`).
Each size class is a multiple of 8, and the Source declares the target's
maximum fundamental alignment (8 on PowerPC, 4 on 68K), so pooled slots
are aligned at least as strictly as any type they can hold.
On 68K the source itself guarantees that alignment: pre-7.5 Memory Managers
can hand `NewPtr` blocks back at 2-mod-4 addresses (System 7.0 on a Plus/SE
class machine), so the source asks for `size + 4`, advances 1..4 bytes to the
next 4-aligned address, stores that distance in the byte just before the
returned pointer, and recovers the native block from that byte at release.
Every source block — a 2 KiB chunk, or a large/direct/fallback request above
256 bytes — carries that one recovery byte; pooled slots never do. PowerPC
keeps the 8-aligned `NewPtr` pass-through with no prefix. Sizes that
would overflow the Memory Manager's signed `Size` after the +4 are refused
before allocating.
Chunk refusal retries the single request; nothrow forms return 0 on refusal,
and plain new aborts. All delete forms share the pool release door. The
LokaAlloc gate and its site census remain unchanged.

With `LOKA_RETRO68_DIAGNOSTICS`, timestamped debug-stats dumps include
`smallpool.rows`, `smallpool.chunks.<size>`, `smallpool.free.<size>`,
`smallpool.live`, `smallpool.unused_bytes`, `smallpool.refills`,
`smallpool.larges`, `smallpool.directs`, `smallpool.fallbacks`,
`smallpool.refused`, `smallpool.poison_violations`, and `smallpool.valid`.
The pool's in-memory counters follow `defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)`; the file output above and the `LokaClassicPoolReport` accessor are gated on `LOKA_RETRO68_DIAGNOSTICS` alone, so a `LOKA_DIAG`-only build keeps the counters but writes no pool dump. These are process-cumulative facts (live/free values are current snapshots),
independent of resettable scene totals. `live` counts pooled slots only;
`refused` counts a failed chunk plus failed single-request fallback, not failed
large/direct requests. Invalid lists leave live/unused bytes zero; check valid.
Platform-context teardown writes the pool report to a distinct fixed file
(`POOLTERM.TXT`) after App/config destruction, with empty scene totals, so it
cannot overwrite a same-second scene redraw dump. Static clients may remain: this is an
observation, never a zero-live or zero-chunk assertion. Snapshot collection is
allocation-free; file formatting uses the existing diagnostic writer.
Pool quantities do not enter scenario audit expectations.

Measured on headless MAME, 8 MB, System 7, Release, one run per cell, comparing
`main` immediately before the pool landed with `main` after it. Times are
emulated `TickCount`/60. Provenance differs by row group and hardware class
materially affects these timings, so each group names its own machine: the
**timing** rows ran on **macii (Mac II, 68020)**, the machine the whole #642
probe series used, so their before/after pairs are comparable with the earlier
probes; the **churn-replace** rows ran on the pinned **maciix (68030)** scenario
rig, which owns the tracked audits. MineSweeper and HelloWorld run at their
384 KiB minimum partition, LazyList at its shipped 1 MiB, SimpleViewer at both
its 512 KiB minimum and 1 MiB preferred.

| Application / partition | Cell | Machine | Before | After |
| --- | --- | --- | ---: | ---: |
| MineSweeper / 384 KiB | board mount | macii | 7.72 s | **0.63 s** |
| MineSweeper / 384 KiB | first paint | macii | 11.07 s | **2.23 s** |
| HelloWorld / 384 KiB | cold mount | macii | 1.15 s | **0.48 s** |
| HelloWorld / 384 KiB | first paint | macii | 2.75 s | **1.95 s** |
| LazyList / 1 MiB | cold mount | macii | 18.10 s | **1.42 s** |
| LazyList / 1 MiB | first paint | macii | 21.70 s | **3.92 s** |
| LazyList / 1 MiB | page flip | macii | 4.67 s | **1.53 s** |
| SimpleViewer / 512 KiB | churn-replace | maciix | n/a (cell added with the pool) | ten `image.load ok`, audit matches |
| SimpleViewer / 1 MiB | churn-replace | maciix | n/a (cell added with the pool) | ten `image.load ok`, audit matches |

The churn-replace cells are the retention acceptance: ten loads in one process
(eight alternating replacements, then a final replacement) still all succeed at
the 512 KiB minimum partition with chunks retained for the process lifetime, and
both partitions reproduce the tracked audit byte for byte.

Retention costs contiguity, which is reported rather than gated, because the
pool trades a contiguous zone for the per-allocation heap walk. Largest free
block at first idle: MineSweeper 1,822 -> 3,138 bytes (the 12-byte Memory
Manager block headers that 1,500-odd small allocations no longer carry outweigh
the chunk rounding), HelloWorld 6,970 -> 2,570 bytes, LazyList after twelve page
flips 17,926 -> 14,930 bytes. Every cell still completes its operation, which is
the frozen acceptance condition; a smaller chunk does not help (it fragments the
zone further) and returning empty chunks is the shelved fallback if a future
workload fails a cell.

The preset name describes the target and build policy, not the host. There is
no separate `local` preset: every host uses the same repository Release preset,
so the Classic size and linker policy cannot drift between local and shared
builds. Direct `cmake` invocations do not load dotenv files; use the wrapper, or
export the same variables explicitly in an already-managed CI environment.

## Building

Use the repository Release preset through the wrapper:

```sh
scripts/retro68-cmake.sh --preset retro68-68k-release
scripts/retro68-cmake.sh --build --preset retro68-68k-release --target LokaHello68K
```

Output files:
- `retro68-68k-release`:
  - `build/retro68/68k/Release/example/HelloWorld/LokaHello68K.dsk`
  - `build/retro68/68k/Release/example/HelloWorld/LokaHello68K.bin`
- `retro68-ppc-release`:
  - `build/retro68/ppc/Release/example/HelloWorld/LokaHelloPPC.dsk`
  - `build/retro68/ppc/Release/example/HelloWorld/LokaHelloPPC.bin`

For transportable autonomous Standalone releases, run the corresponding VS
Code task **Standalone: Toolbox 68K Build / Stage / Release** or **Standalone: Toolbox
PPC Build / Stage / Release**. Release artifacts are staged under
`build/release/toolbox-68k` or `build/release/toolbox-ppc` respectively.

## Environment variables

The CMake toolchain also accepts these variables from an explicitly managed
process environment:

- `RETRO68_BUILD_DIR`: path to the Retro68 build output directory.
- `RETRO68_TOOLCHAIN_DIR`: advanced override for one CPU's Retro68 toolchain
  CMake directory or file. It pins configuration to that CPU and makes the
  other preset family fail; prefer `RETRO68_BUILD_DIR` for shared 68K and PPC
  use.
- `RETRO68_CPU`: target CPU (`m68k` or `ppc`). Defaults to `m68k`.
- `RETRO68_PPC_FLAVOR`: `retroppc` (when `RETRO68_CPU=ppc`). Configure refuses
  any other value. RetroCarbon is not supported: the Toolbox rail reads
  QuickDraw and Dialog Manager fields that Carbon makes opaque, and no preset
  or CI job builds it (#1191).

The host-local file intentionally accepts only the location variables listed
under Host configuration. CPU and flavor remain owned by the selected preset.

Preset mapping used in this repo:

- `retro68-68k-release`: `RETRO68_CPU=m68k`
- `retro68-ppc-release`: `RETRO68_CPU=ppc`, `RETRO68_PPC_FLAVOR=retroppc`

### Compact and diagnostic 68K profiles

`retro68-68k-release` is the compact shipping profile. It strips Classic
MacsBug names from the application resources, compiles application targets
without RTTI, and leaves function-tick profiling and diagnostic file I/O out of
the binary. A separate DWARF-bearing `.gdb` link artifact remains available.

`retro68-68k-diag` is the diagnostic profile. It retains MacsBug names, enables
the function-tick profiler and diagnostic dumps, and does not define `NDEBUG`.
It is **build-verified only**; no runtime verification is claimed for that
profile. The compact 68K profile is runtime-verified separately on the named
Classic rigs; each runtime claim must identify the exact artifact and evidence
path rather than inheriting the diagnostic profile's build result.

## Notes

- Retro68 builds use the `apple/toolbox` target.
- Release builds use `-Os` and section garbage collection for smaller binaries.
- The `LOKA_RETRO68` preprocessor define is set for Classic Mac builds.
- If a `.dsk` is mounted, unmount it before rebuilding. Mounted images can leave stale artifacts or misleading runtime results.
- 68k support remains important for compatibility and experimentation, and simple samples can still boot on 68000-class environments.
- In practical terms, startup cost and responsiveness become much more constrained on low-end 68k (68000 / 68020) as applications grow in size and complexity.
- For modern-style application development on Classic Mac OS, 68030-class systems and later (and PPC601 / 603e-class PowerPC Macs) should be treated as the practical mainstream baseline; all bundled examples are runtime-verified on a 68030 PowerBook 180c with 4 MB of RAM.

## Header Boundary Notes

Classic Mac OS headers expose many old Toolbox names in the global namespace.
For example, both Universal Interfaces and Retro68's Multiversal headers can
declare a global `Button()` function. If those headers become visible in a
translation unit that also uses `using namespace loka::app`, an unqualified Loka
DSL expression such as `Button("OK")` can become ambiguous.

The important fix is not to make application authors qualify every Loka UI
control. App-facing composition should stay written in Loka concepts. Keep
Classic headers inside Toolbox implementation files, platform-specific bootstrap
files, or other private backend seams.

One subtle source of leakage is standard C/C++ compatibility headers in the
Retro68 toolchain. In at least one Retro68 setup, including `<cstring>` from a
public Loka header pulled in `string.h`, then Classic string headers, then
`Events.h`, which made the global Toolbox `Button()` visible to ordinary app
composition code. Prefer typed operations or small internal helpers over
public-header includes when the only need is a tiny standard-library operation.

## Unicode Flavor

| Flavor | Requirement | Build evidence | Runtime verification | Editor support |
|---|---|---|---|---|
| Unicode flavor: OS 9.0+, PPC | `loka_toolbox_unicode(target)` selects an exclusive core; startup refuses a failed system query or a version below 9.0, with a dialog attempt followed by exit. | `LokaSimpleTextUnicodePPC_APPL` and `LokaToolboxUnicodeImportProbe_APPL` are build-verified with Universal and Multiversal imports. The existing PPC CI job checks exact strong PEF imports and startup symbol wiring. | Not runtime-verified. The PPC startup-refusal verdict fixture is pending; pmac6100/8.1 is a [probe image](MAME_DEVELOPMENT.md), not an approved verdict fixture. | Production editors retain TextEdit; MLTE session and resource lifetime are exercised separately. |

The source contracts live in `apple/toolbox/CMakeLists.txt` and
`apple/toolbox/src/ToolboxUnicodeStartup.cpp`; host tests pin the requirement
predicate and refusal sequence. `tests/toolbox/cmake-pins` pins core exclusivity
and 68k selection refusal. The Unicode SimpleText application imports `InterfaceLib` and `Textension`; TE
SimpleText imports only `InterfaceLib`; the separate link-only
probe imports exactly `InterfaceLib`, `Textension`, `TextEncodingConverter`, and
`UnicodeConverter`, all strong. These are build checks, not loader or native
startup verdicts. No release allowlist includes the Unicode acceptance target.

`UnicodeText::Session` initializes MLTE before the OOM reserve and terminates it
only after normal window teardown, outside the Operation Regime. Sudden termination
remains a no-op for MLTE. The ledger owns committed objects; its stack `Prepared`
owner lends a wrapper to install and releases it only into allocation-free `link`.
The standalone `tests/toolbox/unicode-lifecycle/` probe exercises two fixed seats,
draw, retirement, and the two OOM experiments. It records `LIFE.TXT`; its imports
are `InterfaceLib` and `Textension`. Native lifecycle observations remain pending;
the host fake does not establish MLTE/TSM behavior on Classic hardware.

### MLTE declarations

The engine's declarations come from the repository's own header,
`apple/toolbox/src/unicode/MacTextEditorSubset.h`, not from an SDK header.
It declares only the routines the flavor calls, with the types and values
those declarations need, two-byte packed, and it refuses a translation unit
that already included the SDK's `MacTextEditor.h`. Multiversal has no MLTE
header and lacks `OSStatus`, `FourCharCode`, `ByteCount`, `ItemCount` and
`TextEncoding`; the subset declares those five identically to Universal
Interfaces so one header serves both interface sets.

Evidence has two legs. The compile pin `LokaToolboxMlteSubsetStandalone`
(`tests/toolbox/unicode-abi/`) builds the header alone in every PPC build,
so the Toolbox PPC CI job proves it under Multiversal and a local build
proves it under Universal; the link-only import probe takes
`TXNVersionInformation` through the header. The SDK comparison
`tests/toolbox/unicode-abi/compare_subset.py` runs only on a machine whose
Retro68 build has Universal Interfaces: it compiles, never runs, one layout
and one signature translation unit against each header with the same flags,
reads the exported layout array back from the XCOFF object, requires the
undefined symbols of the signature object to be exactly the manifest's
unmangled routine names, and runs three negative controls each time. Every
file it derives from the SDK stays under `build/toolbox-unicode-abi/`; it
accepts no output path. The claims it checks are `manifest.py`.

Last comparison, 2026-10-31, Universal Interfaces shipped with the local
Retro68 build: types 22 / constants 45 / functions 40, differences 0.

To add a routine later: declare it in the header, add its line to
`manifest.py` (and its types or constants, if new), run the comparison, and
record the new counts here.

## Classic Toolbox Profiles

Classic Mac localization should not assume that Unicode is the only normal
answer. For pre-8.5 Toolbox-era targets, region-specific builds and resources
are often more natural and stable than forcing one Unicode-first runtime path.

The likely long-term shape is:

- `ToolboxEN`: English/Roman-oriented Classic Toolbox resources and text path.
- `ToolboxJP`: Japanese Classic Toolbox resources and text path, similar in
  spirit to traditional `1.0J` builds.
- `iToolbox`: a Mac OS 8.5+ "internet-era" Toolbox profile that may use
  Unicode/ATSUI-style capabilities and newer Classic-era text/resource
  behavior where available.

The names are not final. The important rule is that Loka's app-facing
`String`, `AssetPool`, and resource APIs should stay neutral while Classic
backend profiles choose the appropriate build-time resource set and text
backend. Japanese localization for Toolbox should wait until the String and
asset ownership model is solid enough that the profile split is an implementation
choice rather than an app-facing API fork.

## Debug Workflow

Use different tools for different failure classes.

### 1. Modern-host profiling first

For performance problems, first reproduce on a modern host build and profile there.

- macOS: use `Time Profiler`
- Look for the top hot path before touching code
- Typical order:
  1. reproduce
  2. capture top symbols
  3. isolate by toggling sample features or commenting out components
  4. optimize only the top hotspot
  5. re-measure

This was effective for distinguishing:

- reconciler/local-diff cost
- state propagation fanout
- backend redraw cost

### 2. Classic crash debugging

For Classic-only crashes, use native Mac OS with MacsBug.

Recommended setup:

- OS 9 native on PPC hardware is preferred over Classic mode
- `MacsBug` installed
- Retro68-built `.dsk` / `.APPL`

Why:

- close/teardown crashes are often Toolbox/TE/Control Manager specific
- Classic mode adds another compatibility layer and makes root-cause analysis noisier

### 3. When to switch to MacsBug

Switch to MacsBug when the problem is:

- `illegal instruction`
- `CHK`
- `bad F-Line`
- bus error / address error
- close/quit-only crash
- crash that does not reproduce on modern macOS/Win32/Linux

### 4. Minimal MacsBug workflow

For a crash, record at least:

- crashing symbol / offset
- `sc6`
- `es` if it is still valid

In this project, that was enough to isolate a Classic close crash to:

- `ToolboxScenePlatformController::clearTextBindings()`

### 5. Practical rule

Do not guess for long on Classic teardown bugs.

- modern host + `Time Profiler` for performance
- OS 9 + `MacsBug` for Classic crash points
- if needed, ask someone with native hardware to reproduce and capture the crash symbol/stack

That hybrid workflow is faster than trying to solve all Classic issues from Linux/macOS alone.

## Classic animation / redraw notes

These notes came from profiling and tightening the `FloppyBird` sample on
Toolbox/68k.

### 1. Measure update routing before dirty-region tricks

For animated UI on Classic, broad repaint is not always the main cost.
In `FloppyBird`, the first large wins came from reducing:

- redundant `MutableState::set(..., true)` usage
- unused state writes
- extra `Boundary` / compose layers
- idle ticks that did not produce a visible output change

Only after those were removed did redraw cost become the top hotspot.

Practical rule:

- profile first
- remove redundant state/compose work before adding redraw complexity

### 2. Quantized-output gating is effective

When output is rendered at integer coordinates, game logic can advance without
producing a visible change every tick.

In `FloppyBird`, a cheap "render snapshot" of quantized sprite positions was
enough to skip model rebuild / `set()` / compose work for frames where the
visible output had not changed yet.

Practical rule:

- if a Classic animation ultimately snaps to integer positions, compare the
  quantized output first
- if nothing visible changed, skip rebuilding props/models and avoid notifying
  state

### 3. Keep surface-level optimizations in `NodeContext`

The stable path was:

- keep `RectSurfaceNode` as a normal Node
- keep shared app/model data simple
- store previous rendered state inside the Toolbox `NodeContext`
- perform Classic-specific erase/paint decisions there

This kept platform-specific optimization out of the shared DSL/app layer and
made future backends (for example CALayer-backed macOS paths) easier to reason
about.

### 4. Prefer `erase old minus new` before aggressive paint diffing

For moving rectangles, the most reliable first optimization was:

- erase only the old pixels no longer covered by the new rect
- then paint the current rect normally

This removed flicker and reduced erase area without the instability of more
aggressive "paint new minus old" matching.

Practical rule:

- first try `erase old minus new`
- only add paint-side diffing after profiling proves it helps

### 5. `Rgn`/region clipping can lose to simpler paths

`Rgn`-based clipping improved visual synchronization in some experiments, but
once redundant update/compose work was removed, it became slower than a simpler
rect-based path for this workload.

Practical rule:

- do not assume `Rgn` is faster
- measure it after higher-level update work has already been reduced
- keep it as a backend-local policy, not a general DSL feature, unless repeated
  measurements justify exposing it

## Classic investigation notes

These notes come from stabilizing repeated `PopupMenu` crashes in the
`StaticVsDynamicClassic` sample on System 7.1 / 9.1.

### 1. Do not over-attribute crashes to "old OS instability"

Classic Mac OS is fragile, but repeated crashes under the same interaction
pattern usually still have a concrete cause.

In this project, repeated `Toggle Details` / `Freeze` stress initially looked
like generic Classic instability. It was not. The root cause was a persistent
popup context design problem in the Toolbox backend.

Practical rule:

- if the crash is reproducible within tens or hundreds of actions, assume there
  is a real bug until proven otherwise
- use the old environment as a stress amplifier, not as an excuse to stop
  debugging

### 2. Cut the problem with sample variants first

Before changing core code, create narrow diagnostic variants of the sample and
 compare stability:

- no native controls
- no `EditText`
- no `PopupMenu`

This was decisive:

- `ClassicLight`: stable
- `NoPopupMenu`: stable
- `NoEditText`: unstable

That isolated the problem to `PopupMenu`, not to the reconciler or dynamic
boundary logic.

Practical rule:

- when a Classic crash looks "random", first remove whole control classes and
  compare runtime behavior
- this is often faster than stepping through low-level crashes immediately

### 3. Prefer frame-local snapshots over persistent UI-context registries

The key Classic `PopupMenu` bug was caused by keeping persistent
`ToolboxPopupMenuContext*` objects in a controller-managed vector and reusing
them across redraw/input cycles.

The stable fix was:

- stop registering popup contexts into a persistent controller loop
- replace them with frame-local popup hit snapshots
  - rect
  - line height
  - items pointer
  - selected-index state
  - enabled state
  - onChange emitter
  - boundary
  - menu id

This significantly improved stability on:

- mini vMac / System 7.1
- native OS 9 PPC hardware

Design rule:

- on Classic Toolbox paths, do not retain more live native UI context than you
  need
- if a control can be described by a frame-local snapshot for hit-testing and
  redraw, prefer that over a long-lived controller-side registry

### 4. Persistent backend context is especially suspicious when

Watch for backend-specific registries that hold:

- raw node/context pointers
- state pointers
- emitters

## Classic redraw investigation notes

These notes come from profiling `StaticVsDynamicClassic` redraw behavior on
mini vMac/System 7.1 and native PPC OS 9.

### 1. Split startup cost from interaction cost

Do not mix these into one conclusion.

- startup can show repeated full draws even before any user action
- interaction cost can be much lower after core fixes, but still look bad if
  startup/event-loop noise is included in the same measurement window

Use separate captures:

- startup: launch, then dump immediately
- interaction: reset, perform one fixed action, then dump

### 2. Root dynamic host boundary materially changed the result

For `StaticVsDynamic`, switching the root host to a dynamic boundary removed the
main `fullRebuild -> full invalidate` path for `Toggle Details`.

Observed result after that change:

- `full_rebuild=0`
- `request_full=0`
- `request_rect=1`

That was the largest meaningful performance win in this phase.

Practical rule:

- if a sample mixes static host composition with dynamic child boundaries and
  repeatedly toggles child structure, test a dynamic host root before spending
  too long on backend redraw policy

### 3. Dynamic pane and startup/event-loop noise are separate problems

When the dynamic pane was temporarily removed from `StaticVsDynamicClassic`:

- the second `Profile Toggle x2` dump became much cleaner
- rect-path redraw was still visible
- `flush_full` could drop to `0` for the second pass

This means:

- dynamic-pane interaction noise is real
- but startup/update-event full draws are a different issue

Do not treat them as one bug.

### 4. Historical dump chaining and completion semantics

`Profile Toggle x2` was only reliable after dump completion was chained through
an explicit deferred path. That unused completion door was removed under #678.

However, "dump finished" and "UI/event-loop is stable enough for the next
action" are not the same thing on Classic Toolbox.

The historical chained-dump mechanism is no longer available. A deferred
callback should not be taken as evidence that redraw/update events have fully
drained.

### 5. Prefer simple sample subtraction before deeper backend work

When redraw profiling gets muddy, remove whole sample slices first:

- dynamic pane
- popup/edit controls
- status/count text updates

If removing a slice materially changes the redraw trace, that is more useful
than adding another round of backend speculation.
- layout rects

If they survive longer than one render pass, they are prime suspects for:

- illegal instruction
- type 3 / type 10
- bad F-Line
- silent exits

Practical review question:

- can this be rebuilt every frame from current scene state instead of being
  retained in a controller vector?

If yes, that design is usually safer on Classic.

### 5. Safe-side hardening is useful, but not always sufficient

The following changes were still worthwhile:

- invalidating popup raw pointers on teardown
- avoiding fixed menu IDs when a stable control tag exists
- removing per-draw `PolyHandle` churn
- snapshotting popup state around `PopUpMenuSelect()`

However, those did not fully solve the problem until the persistent popup
context registry itself was removed.

Practical rule:

- hardening measures help
- but if a crash remains after several safe-side fixes, question the ownership
  model, not only the cleanup details

## VS Code (clangd)

To enable Classic Mac OS headers in clangd, select a Retro68 preset in CMake
Tools and configure it. The Retro68 presets generate a compilation database,
and the tracked VS Code settings copy the active database to
`compile_commands.json` at the repository root for clangd.

The workspace permits clangd to query only the Retro68 68K and PPC C++ compiler
drivers named by that database. This supplies the toolchain's actual system
include paths without hard-coding the location of a local Retro68 build.

`.vscode/c_cpp_properties.json` remains as a starter configuration for anyone
who explicitly re-enables the Microsoft C/C++ IntelliSense engine.

### Toolbox paint eligibility

`ToolboxPresentObservation` carries a window's `CLEAN`/`REFUSED` paint eligibility
between renders. A completed whole-window erase and paint with no reported
refusal establishes CLEAN; unfinished or noncovering renders and refused screen
obligations establish REFUSED. Eligibility does not schedule work. The blur
capture gate reads only refusals already reported in the current render and
preserves existing snapshot pixels when acquisition is blocked. PaintDamage's
`present_observe` and `paint_result` lines distinguish the selected snapshot's
incoming eligibility from its finalized result; see
[the diagnostic format](../tests/toolbox/PaintDamageStandalone.md).

Placement validity records whether a leaf completed layout. Each admitted leaf
compares its full spatial descriptor before overwriting its own placement;
this is separate from content-input and paint-history checks. The viewport
ledger distinguishes **placed** layout facts from the **presented** certificate
written at CLEAN render settlement. Its `copyDomain` is the content rectangle
intersected with the port; missing port evidence stays absent. At the next move,
`baseline=none|stale|valid` describes that certificate against the new layout.
Copy admission requires both CLEAN eligibility and a valid baseline; this
observation still always selects erase. `present_observe` also reports
`not_translated` and `enclosing_clip` from shipping observation values.
