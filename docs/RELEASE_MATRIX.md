# Release verification matrix

This document is the release-PR checklist for the verification design frozen in
[#311](https://github.com/cubenoy22/Loka/issues/311). Replace each applicable
empty evidence slot with a link from the release PR; do not mark a cell complete
without that evidence. `n/a` means that the repository has no application target
for that leg, not that verification was skipped.

For capture-bearing grades (L1 and L2), ✅ means that both layers passed: the
durable scenario audit matched its tracked expected audit byte-for-byte, and the
settled pixels matched that rig's untracked golden. The expected audit is the
cross-platform structural authority. Its verdict body is serialized by
[`common/testing/snap/SnapFormat.hpp`](../common/testing/snap/SnapFormat.hpp),
but runners treat the complete audit as opaque bytes: launch, collect, `cmp`,
then compare pixels. Pixel goldens remain local to each rig because system fonts
and chrome vary. A capture is settled only after the scenario completion marker
and consecutive identical frames.

The release gate is:

- L0 + L1: every applicable cell is automated and passes.
- L2: one representative scenario per example passes on every applicable OS.
- L3: one example cell is sampled on each applicable OS for every release.

Before applying the matrix, complete the release-provenance check:

- [ ] Every attached archive and adjacent content manifest was produced by
  `scripts/release/assemble.py` at the release tag from an explicit allowlist;
  each listed file's hash and provenance was reviewed, and each listed
  `.LRP`/`.LRPK` passed the tracked-package-file gate plus the manual
  tracked-input review documented in `scripts/release/README.md`.
- [ ] The top-level CMake source version, tag name, current-release
  documentation, and GitHub release metadata agree; `git cat-file -t <tag>`
  reports `tag` (an annotated tag, not `commit`); the annotation contains the
  release notes; and every cited issue or pull request was verified as shipped.
  The annotated-tag requirement applies to releases after v0.0.2; the published
  v0.0.2 lightweight tag is grandfathered and stays as it is.
  The enumerated sites are below; "current-release documentation" is not one
  file.

### Version-bearing sites

Every place in the tracked tree that names a Loka release. Walk this list at
each release; `grep -rn "0\.0\.[0-9]"` over the tree is the check that nothing
new joined it without being listed here.

| Site | Names | When it changes |
| --- | --- | --- |
| `CMakeLists.txt` `project(Loka VERSION ...)` | the release **under development** | bumped to the next version **immediately after tagging**, never at tag time |
| `README.md` current-release callout | the newest **published** tag | at release, to the tag just cut |
| `README.md` platform table's "not part of `X`" note | the release under development | with the CMake bump |
| `ROADMAP.md` release-provenance paragraph | the release under development | with the CMake bump |
| `ROADMAP.md` per-release sections | published history | a new section at release; the previous one loses `(current)` |

Deliberately **not** on this list, and not to be "fixed" by a sweep:

- `ROADMAP.md` history, milestone plans, and `docs/TODO.md` backlog notes name
  the release something was targeted at. They are records, not claims about the
  present.
- `scripts/ios/legacy/README.md` records the version SpringBoard actually
  enumerated during a past device run. Bumping it would falsify evidence.
- `docs/ProgrammingGuide.en.md` body passages labelled with the release whose
  semantics they describe. If such a pattern still holds, drop the label rather
  than advancing it; if it no longer holds, the passage needs rewriting, not a
  version bump.
- `docs/archives/` and the `*Draft.md` design notes.
- The staged standalone payload `README.txt` labels
  (`scripts/macos-standalone-flow.sh`, `scripts/macos/standalone-release-stage.sh`,
  `scripts/win32-standalone-flow.ps1`) derive the version from the top-level
  CMake `project()` call at run time, so they follow the bump without being
  walked. They were hardcoded once and went stale at the 0.0.4 -> 0.0.5 bump.
- `scripts/release/README.md` takes the tag from `$TAG` so its example never
  goes stale.
- `docs/ProgrammingGuide.en.md` names no release at all. The guide tracks
  the development source, and a reader who wants the guide as it stood for a
  published release reads the file at that release's tag. Do not reintroduce a
  version line: it was one, it went stale, and the tag already carries the fact.
- `apple/ios/legacy/Info.plist` `CFBundleShortVersionString` stays at the
  version the legacy iPhone OS 3.1 profile was actually verified at. It is
  frozen work, not a shipping target, so advancing it would claim a
  verification that never happened. The modern iOS bundle takes
  `@PROJECT_VERSION@` and needs no edit either way.

The OS columns come from `CMakePresets.json` and the platform workflows under
`.github/workflows/` (`linux.yml`, `macos.yml`, `windows.yml`, and
`toolbox.yml`). The separate `windows-standalone-release.yml` builds and stages
the Win32 x64 standalone Release set (five autonomous loops plus interactive
SimpleViewer and SimpleText) on `release/**` pushes and manual dispatch.
It is advisory, not a merge gate; the PR/main path no longer runs that stage
([#896](https://github.com/cubenoy22/Loka/issues/896)).
Linux is a headless host-validation leg and has no GUI example targets. The
Classic Mac column is one OS leg: L0 covers both configured Retro68 architectures
(68K and PPC), while L1-L3 use the available 68K MAME or hardware rig. The current
tree gives `ScrapbookUI` Win32 and macOS target arms;
[#281](https://github.com/cubenoy22/Loka/issues/281) remains relevant to its full
cross-platform verification, and the missing native ScrollBar arms tracked by
[#224](https://github.com/cubenoy22/Loka/issues/224) do not make the example
inapplicable because its shared UI uses Previous/Next buttons.

`SmirkyCard` is experimental — no pixel golden cells. It is opt-in at configure
time, so its L0 and L1 cells below carry their own reproduction commands: every
listed L0 configure must add `-DLOKA_BUILD_SMIRKYCARD=ON`, and its L1 cell is a
manual launch check (no scenario runner) rather than a golden.

## Grades

| Grade | Meaning | Automation |
| --- | --- | --- |
| L0 build | Every example compiles for every applicable OS/architecture. | Win32, macOS, and hosted Retro68 CI application builds. Linux CI validates host code but contributes no example cells. |
| L1 startup smoke | Launch the example's scene and capture its settled initial screen. Classic, macOS, and Win32 cells run the example's `MainNode` inside a TEST scenario vehicle (shipping binaries carry no audit door), while that vehicle forwards the example-owned production window chrome and menus. Host parity pins compose the same platform-neutral presentation classes used by every rail so those declarations cannot drift unnoticed. | Classic MAME, Win32, and macOS startup paths exist for `ScrapbookUI`, `HelloWorld`, `Tutorial`, `MineSweeper`, and `FloppyBird`; the remaining runners are tracked by [#312](https://github.com/cubenoy22/Loka/issues/312). |
| L2 scenario completion | Drive a representative Flow/State operation sequence to completion and capture its checkpoints. | Direct Flow/State emission, shared across OS runners. Classic, Win32, and macOS runners cover `ScrapbookUI`, `HelloWorld`, `Tutorial`, `MineSweeper`, and `FloppyBird`; expansion is tracked by [#312](https://github.com/cubenoy22/Loka/issues/312). |
| L3 real hardware / manual | Exercise hands-on behavior and input feel on a real or manually operated target. | Deliberately manual; real input synthesis belongs here and in input-path PR acceptance, not in the standing L2 release gate. |

## L0 — build

| Example | Linux host | Win32 | macOS | Classic Mac (Retro68) |
| --- | --- | --- | --- | --- |
| `FloppyBird` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `HelloWorld` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `MineSweeper` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `ScrapbookUI` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `SimpleViewer` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `Tutorial` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ | - [ ] Evidence: _add link_ | 68K: - [ ] Evidence: _add link_<br>PPC: - [ ] Evidence: _add link_ |
| `SmirkyCard` (experimental, opt-in) | headless test only: `cmake --preset testing -DLOKA_BUILD_SMIRKYCARD=ON && ctest --preset testing -R smirkyCard` — - [ ] Evidence: _add link_ | `cmake --preset win32-debug -DLOKA_BUILD_SMIRKYCARD=ON` — - [ ] Evidence: _add link_ | `cmake --preset macos-debug -DLOKA_BUILD_SMIRKYCARD=ON` — - [ ] Evidence: _add link_ | 68K: `scripts/retro68-cmake.sh --preset retro68-68k-release -DLOKA_BUILD_SMIRKYCARD=ON` then `--target LokaSmirkyCard68K_APPL` — - [ ] Evidence: _add link_<br>PPC: n/a (not built or claimed) |

## L1 — startup smoke

Both pixel rails refuse until the rig is named. Set `$env:LOKA_WIN32_RIG = '<rig>'`
and `LOKA_MACOS_RIG=<rig>` for the machine you are on before running the commands
in the cells below; each names a descriptor in that machine's own rig directory,
and the goldens are stored per rig. See *How each rail is run* for the full form.

| Example | Linux host | Win32 | macOS | Classic Mac (68K rig) |
| --- | --- | --- | --- | --- |
| `FloppyBird` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ (`tests/win32/run-scenario.ps1 floppybird startup`) | - [ ] Evidence: _add link_ (`tests/macos/run-scenario.sh floppybird startup`) | - [ ] Evidence: _add link_ (`tests/toolbox/run-scenario.sh floppybird startup`) |
| `HelloWorld` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ (`tests/win32/run-scenario.ps1 helloworld startup`) | - [ ] Evidence: _add link_ (`tests/macos/run-scenario.sh helloworld startup`) | - [ ] Evidence: _add link_ (`tests/toolbox/run-scenario.sh helloworld startup`) |
| `MineSweeper` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ (`tests/win32/run-scenario.ps1 minesweeper startup`) | - [ ] Evidence: _add link_ (`tests/macos/run-scenario.sh minesweeper startup`) | - [ ] Evidence: _add link_ (`tests/toolbox/run-scenario.sh minesweeper startup`) |
| `ScrapbookUI` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ (`tests/win32/run-scenario.ps1 scrapbook startup`) | - [ ] Evidence: _add link_ (`tests/macos/run-scenario.sh scrapbook startup`) | - [ ] Evidence: _add link_ (`tests/toolbox/run-scenario.sh scrapbook startup`) |
| `SimpleViewer` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** | - [ ] Evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** | - [ ] Evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** |
| `SmirkyCard` (experimental, manual) | n/a | - [ ] Evidence: _add link_ (launch `LokaSmirkyCardWin32.exe` with `MAIN.JS` beside it: Card One shows, Run evaluates `1+1`, Reload MAIN.JS re-reads an edited file) | - [ ] Evidence: _add link_ (launch `LokaSmirkyCardMacOS.app`, same three checks; `MAIN.JS` is in `Contents/Resources`) | - [ ] Evidence: _add link_ (stage with `scripts/mame-dev-disk.sh <bin> example/SmirkyCard/MAIN.JS`, same three checks) |
| `Tutorial` | n/a — no Linux GUI application target | - [ ] Evidence: _add link_ (`tests/win32/run-scenario.ps1 tutorial startup`) | - [ ] Evidence: _add link_ (`tests/macos/run-scenario.sh tutorial startup`) | - [ ] Evidence: _add link_ (`tests/toolbox/run-scenario.sh tutorial startup`) |

## L2 — scenario completion

Both pixel rails refuse until the rig is named. Set `$env:LOKA_WIN32_RIG = '<rig>'`
and `LOKA_MACOS_RIG=<rig>` for the machine you are on before running the commands
in the cells below; each names a descriptor in that machine's own rig directory,
and the goldens are stored per rig. See *How each rail is run* for the full form.

Record the chosen scenario name as well as its evidence. A release needs one
representative scenario for each example on every applicable OS.

| Example | Linux host | Win32 | macOS | Classic Mac (68K rig) |
| --- | --- | --- | --- | --- |
| `FloppyBird` | n/a — no Linux GUI application target | - [ ] Scenario: `fixed-step-flaps`; evidence: _add link_ (`tests/win32/run-scenario.ps1 floppybird fixed-step-flaps`) | - [ ] Scenario: `fixed-step-flaps`; evidence: _add link_ (`tests/macos/run-scenario.sh floppybird fixed-step-flaps`) | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** |
| `HelloWorld` | n/a — no Linux GUI application target | - [ ] Scenario: `bmi-roundtrip`; evidence: _add link_ (`tests/win32/run-scenario.ps1 helloworld bmi-roundtrip`) | - [ ] Scenario: `bmi-roundtrip`; evidence: _add link_ (`tests/macos/run-scenario.sh helloworld bmi-roundtrip`) | - [ ] Scenario: `bmi-roundtrip`; evidence: _add link_ (`tests/toolbox/run-scenario.sh helloworld bmi-roundtrip`) |
| `MineSweeper` | n/a — no Linux GUI application target | - [ ] Scenario: `new-game-twice`; evidence: _add link_ (`tests/win32/run-scenario.ps1 minesweeper new-game-twice`) | - [ ] Scenario: `new-game-twice`; evidence: _add link_ (`tests/macos/run-scenario.sh minesweeper new-game-twice`) | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** |
| `ScrapbookUI` | n/a — no Linux GUI application target | - [ ] Scenario: `flip-forward-back`; evidence: _add link_ (`tests/win32/run-scenario.ps1 scrapbook flip-forward-back`) | - [ ] Scenario: `flip-forward-back`; evidence: _add link_ (`tests/macos/run-scenario.sh scrapbook flip-forward-back`) | - [ ] Scenario: _add name_; evidence: _add link_ |
| `SimpleViewer` | n/a — no Linux GUI application target | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** |
| `Tutorial` | n/a — no Linux GUI application target | - [ ] Scenario: `increment-summary-toggle`; evidence: _add link_ (`tests/win32/run-scenario.ps1 tutorial increment-summary-toggle`) | - [ ] Scenario: `increment-summary-toggle`; evidence: _add link_ (`tests/macos/run-scenario.sh tutorial increment-summary-toggle`) | - [ ] Scenario: _add name_; evidence: _add link_; runner **TBD ([#312](https://github.com/cubenoy22/Loka/issues/312))** |

## L3 — real hardware / manual

Select and complete one example cell per applicable OS for this release. Leave
the other cells unchecked so the sample remains visible.

| Example | Linux host | Win32 | macOS | Classic Mac |
| --- | --- | --- | --- | --- |
| `FloppyBird` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |
| `HelloWorld` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |
| `MineSweeper` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |
| `ScrapbookUI` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |
| `SimpleViewer` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |
| `Tutorial` | n/a — no Linux GUI application target | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ | - [ ] Sample evidence: _add link_ |

## How to re-run

Run commands from the repository root. The configure-and-build lines below are
single shell invocations that reproduce each build leg available today; the
host must satisfy that preset's toolchain requirements.

| Grade / leg | One-command invocation available today |
| --- | --- |
| L0 Linux host | `cmake --preset testing && cmake --build --preset testing` (host validation only; there are no Linux GUI example cells) |
| L0 Win32 | `cmake --preset win32-debug && cmake --build --preset win32-debug` |
| L0 macOS | `cmake --preset macos-debug && cmake --build --preset macos-debug` |
| L0 Classic Mac 68K | `cmake --preset retro68-68k-release && cmake --build --preset retro68-68k-release` |
| L0 Classic Mac PPC | `cmake --preset retro68-ppc-release && cmake --build --preset retro68-ppc-release` |
| L1 Classic `ScrapbookUI` | `tests/toolbox/run-scenario.sh scrapbook startup` — tracked expected audit plus settled rig-local pixel golden |
| L1 Classic `HelloWorld` | `tests/toolbox/run-scenario.sh helloworld startup` — settled initial title observation, tracked expected audit, and settled rig-local pixel golden |
| L1 Classic `Tutorial` | `tests/toolbox/run-scenario.sh tutorial startup` — settled initial tutorial title observation, tracked expected audit, and settled rig-local pixel golden |
| L1 Classic `MineSweeper` | `tests/toolbox/run-scenario.sh minesweeper startup` — settled initial New Game control observation, tracked expected audit, and settled rig-local pixel golden |
| L1 Classic `FloppyBird` | `tests/toolbox/run-scenario.sh floppybird startup` — fixed-step initial surface observation, tracked expected audit, and settled rig-local pixel golden |
| L2 Classic `ScrapbookUI` | `tests/toolbox/run-scenario.sh scrapbook flip-forward-back` — tracked expected audit plus settled rig-local pixel golden |
| L2 Classic `HelloWorld` | `tests/toolbox/run-scenario.sh helloworld bmi-roundtrip` — typed TEST_ID actions enter valid, invalid, and restored BMI values through EditText; tracked expected audit plus settled rig-local pixel golden |
| L2 Classic `Tutorial` | `tests/toolbox/run-scenario.sh tutorial increment-summary-toggle` — typed TEST_ID actions increment Step 4 twice, hide and restore its derived summary, and pin the full audit plus settled rig-local pixel golden; Tutorial intentionally leaves EditText verification to HelloWorld's BMI controls. |
| L2 Classic `MineSweeper` | `tests/toolbox/run-scenario.sh minesweeper new-game-twice` — fixed caller-owned seed pins the initial board and both MainNode-owned New Game commands; tracked expected audit plus settled rig-local pixel golden |
| L2 Classic `FloppyBird` | `tests/toolbox/run-scenario.sh floppybird fixed-step-flaps` — fixed caller-owned seed and exact 1/60-step advancement pin five flaps and surface checkpoints; tracked expected audit plus settled rig-local pixel golden |
| Standalone Classic `Tutorial` | `LokaTutorialStandaloneFlow68K_APPL` presents the same typed scenario without host config; after target execution, `tests/toolbox/verify-standalone-audit.sh tutorial increment-summary-toggle <LOG.TXT>` byte-compares the complete durable audit |
| Standalone Classic `MineSweeper` | `LokaMineStandaloneFlow68K_APPL` presents the fixed-seed two-New-Game tour without host config; after target execution, `tests/toolbox/verify-standalone-audit.sh minesweeper new-game-twice <LOG.TXT>` byte-compares the complete durable audit |
| Standalone Classic `FloppyBird` | `LokaFloppyStandaloneFlow68K_APPL` presents the fixed-seed fixed-step flap tour without host config; after target execution, `tests/toolbox/verify-standalone-audit.sh floppybird fixed-step-flaps <LOG.TXT>` byte-compares the complete durable audit |
| L1 Win32 `ScrapbookUI` | `$env:LOKA_WIN32_RIG = '<rig>'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/win32/run-scenario.ps1 scrapbook startup` — shared State-driven scenario, byte-identical tracked audit, two-hash settled `PrintWindow` capture, and rig-local profiled golden |
| L2 Win32 `ScrapbookUI` | `$env:LOKA_WIN32_RIG = '<rig>'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/win32/run-scenario.ps1 scrapbook flip-forward-back` — shared State-driven scenario, byte-identical tracked audit, two-hash settled `PrintWindow` capture, and rig-local profiled golden |
| L1/L2 Win32 other scenario examples | `$env:LOKA_WIN32_RIG = '<rig>'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/win32/run-scenario.ps1 <example> <scenario>` — shared scene driver, byte-identical tracked audit, two-hash settled `PrintWindow` capture, and rig-local profiled golden; `<rig>` names a descriptor in the local rig directory (`~/.config/loka/rigs/win32/`, override with `LOKA_WIN32_RIG_HOME`) — rig descriptors are not tracked, for the same reason the goldens they pin are not; copy `scripts/rig/win32/rigs/local.example.ini` to start one. The capture environment it declares must match what the run reports |
| L1/L2 macOS | `LOKA_MACOS_RIG=<rig> tests/macos/run-scenario.sh <example> <scenario>` — tracked expected audit plus settled rig-local pixel golden; `<rig>` names a descriptor in `scripts/rig/macos/rigs/`, and the capture environment it declares must match what the run reports |
| L1 Classic `SimpleViewer` | **TBD — [#312](https://github.com/cubenoy22/Loka/issues/312)** |
| L2 Classic examples other than `ScrapbookUI`, `HelloWorld`, `Tutorial`, `MineSweeper`, and `FloppyBird` | **TBD — [#312](https://github.com/cubenoy22/Loka/issues/312)** |
| L3 all OSes | n/a — manual by definition; record the rig/hardware and evidence in the selected matrix cell |

The EditText verification path belongs to HelloWorld rather than Tutorial. For
the automated application-facing path, run the `bmi-roundtrip` cell with each
platform's scenario command above. It enters valid height and weight values,
checks the calculated BMI, enters an invalid height, and restores the valid
value through the two BMI EditText nodes. `EnterText` writes through the Scene
tracker like native text-change actuation, but it does not synthesize keyboard
input. For L3 or input-path acceptance, launch HelloWorld, type into the
`Height (cm)` and `Weight (kg)` controls, and confirm that the result follows
the entered values.

The Classic scenario command requires an already configured local MAME rig,
the example's Toolbox test application, and the host `lrpc` tool; if an
artifact is absent, the runner prints its exact build command.
`tests/scenarios/scenarios.txt` is the shared `<example> <scenario>` registry.
`--update-golden` stages the scenario capture for one failure-atomic rig-local
bundle and publishes only after every registry entry is present under one
identity and every capture records the SHA-256 of its producing application.
An incomplete bake resumes only from the Git revision and porcelain-status
digest that created it; an unattestable source must bake in one run. This
staging-only source guard is not part of the reference identity future
candidates match. The bake does not create release evidence, edit a tracked
repository file, or authorize itself; the bundle identity must separately
match the digest in the tracked Toolbox rig descriptor. `--structural-audit` validates the
tracked audit and capture structure while explicitly reporting “Pixel verdict:
not evaluated.”
