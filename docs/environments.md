# Environments

This document separates the project environment into three roles:

- `Environment for Development`: where you edit, inspect, debug, and profile the project
- `Environment for Build`: where you actually compile and package targets
- `Environment for Target`: where the resulting binaries are expected to run

## Ultimate Loka Development Env.

<img src="../assets/LokaLaptops.jpg" alt="Ultimate Loka development environment" width="480">

The most capable all-around setup today is a modern macOS machine running VS Code, with Parallels Desktop used for Windows and optional Linux workflows, plus a 68k simulator for Classic validation.

This gives Loka a practical three-OS development loop today: modern macOS, Windows, and Classic Mac OS. Over time, the same host-centered workflow is expected to extend toward Linux, iOS / iPadOS, and Windows Mobile-class targets as support expands.

For Snow Leopard-oriented workflows, either a real machine or a virtualized Snow Leopard Server setup can be used. In both cases, file sharing from the main development machine makes build, debugging, and profiling workflows much easier.

## Environment for Development

This is the environment used for day-to-day development work.

- VS Code is the primary editor environment.
- Recommended VS Code extensions:
  - Microsoft's C/C++ extension
  - Microsoft's CMake Tools extension
  - LLVM's clangd extension for formatting and language intelligence
  - LLDB DAP to absorb ARM64 / x86_64 differences on macOS as automatically as possible
- CMake and Ninja can be installed through package managers such as `winget` and `Homebrew`.
- Those package-manager paths commonly provide CMake 3.x. If you specifically need the latest CMake 4.x series, use the official GUI installer rather than the CLI package-manager route, on both Windows and macOS.
- Debugging and profiling are typically done on modern host systems.
- On macOS, profiling generally requires the full `Xcode.app` installation in addition to Xcode Command Line Tools.
- Even on older environments where VS Code is not practical, macOS 10.8 and later can still use CMake's Xcode project generation path, making debugging and profiling possible through Xcode. The legacy Leopard/Snow Leopard bridge generators apply from 10.8 up to hosts that can run an Xcode 9-series toolchain; newer hosts (through Monterey 12) can still generate bridge projects by running them with `DEPLOYMENT_TARGET=10.9`. See [scripts/macos/README.md](../scripts/macos/README.md) for the verified routes and details.
- Once an Xcode project has been generated, older Apple IDEs can still be useful for debugging and profiling if the generated project remains compatible with that IDE version.
- For efficient cooperation with older Macs, it is often practical to enable file sharing on the main development machine and access the same source tree over LAN from another Mac. This works well with Mac OS X 10.6 and later, making it possible to build targets such as Universal Binary 1 directly against source managed on the main development side.
- Mac OS X 10.5 and earlier are not reliably compatible with that file-sharing path and may fail with protocol or connection errors. In those cases, use an intermediate Mac in roughly the 10.6 to 10.14 range, or copy sources by USB media.
- Modern macOS, Windows, and WSL are the most practical development hosts.
- This environment is mainly for editing, investigation, debugging, and iteration speed.

Typical examples:

- modern macOS on Apple Silicon or Intel
- modern Windows
- WSL2

## Environment for Build

This is the environment where binaries are actually built.

- Loka uses CMake + Ninja as the main build path.
- The project already builds with older toolchains such as GCC 4.0.
- Universal Binary 1 builds are possible on Snow Leopard systems.
- On older macOS systems such as Snow Leopard, CMake and Ninja can be installed through MacPorts.
- On Windows, run release commands from an appropriate Visual Studio Developer Command Prompt so that MSVC environment variables match the intended target architecture. VS Code may inherit the same environment, but is not required.
- On Windows on ARM, use the ARM64 Native Tools Command Prompt for native ARM64 builds, or ARM64_x86 / ARM64_x64 Cross Tools prompts for x86-family builds.
- The ordinary macOS, Win32, and Retro68 Configure Presets do not register
  Standalone executable targets, so those applications do not clutter CMake
  Tools' normal launch/debug target picker. For interactive Standalone Flow or
  Standalone Loop debugging, select `macos-standalone-debug`,
  `win32-standalone-debug`, or `retro68-68k-standalone-dwarf` as the Configure
  Preset. Then select the matching Flow or Loop Build Preset and debug the
  generated executable target directly. The Flow aggregate builds the five
  finite applications; Loop builds the five autonomous applications plus the
  ordinary interactive SimpleViewer. The Retro68 DWARF targets also emit their
  `.gdb` images; attaching an image to MAME remains a separate debugging step.
  Each Standalone Configure Preset owns a separate build directory, so selecting
  it cannot leave Standalone targets in the ordinary configuration's cache.
  Target selection is the mode boundary: do not add standalone defines to an
  ordinary shipping example target. Standalone vehicles compile their complete
  core/platform source graph uniformly with `TEST_BUILD`; applying only a mode
  define to the shipping target would mix incompatible test and shipping
  layouts. A Flow target exits after its finite audit rail, while a Loop target
  keeps its App and native window and repeatedly re-arms the rail until closed.
- For a host-native macOS standalone presentation, run
  `scripts/macos-standalone-flow.sh Verify` from the repository root. It
  configures and builds Release without pinning `CMAKE_OSX_ARCHITECTURES`, stages five
  independent application bundles under
  `build/presentation/macos-<host>-release`, and launches them sequentially.
  ScrapbookUI, HelloWorld, Tutorial, MineSweeper, and FloppyBird must each
  publish their exact tracked success audit before the verifier stops that
  application's final-scene hold and starts the next bundle. SimpleViewer is
  intentionally omitted because its release scenario requires an interactive
  file chooser. `scripts/macos-standalone-flow.sh Stage` prepares the same
  portable directory without launching it; expected and actual audits are kept
  per application, while ScrapbookUI's `ASSETS.LRP` remains owned by its bundle
  at `Contents/Resources`. **Standalone: macOS Build / Stage / Verify / Release** is the VS Code
  shortcut; choose Build, Stage, or Verify from its action prompt.
- To stage the standalone macOS application payload rather than the finite audit
  rail, run `scripts/macos-standalone-flow.sh Release`. The result under
  `build/release/macos-<architecture>` contains five autonomous loop bundles
  plus the ordinary interactive `LokaSimpleViewerMacOS.app` bundle. A loop
  keeps its Config, App, and native Window; after its current scenario has
  published a successful terminal audit, it replaces the rail and re-arms the
  Scene. Closing its window stops it. SimpleViewer is built but is never
  launched by the automated verifier. Release builds default to the host
  architecture. A dedicated legacy build host may select a Release
  architecture explicitly, for example
  `LOKA_STANDALONE_MACOS_ARCH=i386 scripts/macos-standalone-flow.sh Release`.
  In **Standalone: macOS Build / Stage / Verify / Release**, choose Release for the equivalent
  VS Code action.
  macOS bundles now explicitly declare high-resolution capability in the
  project-owned `cmake/macos/MacOSXBundleInfo.plist.in`. The Tiger UB1
  release-candidate build evidence below covers this template
  ([#1253](https://github.com/SimplySmirkySoftware/Loka/pull/1253),
  [#1262](https://github.com/SimplySmirkySoftware/Loka/pull/1262)).
  Retina runtime verification remains separate.
  The same template opts the bundles into the dark appearance
  (`NSRequiresAquaSystemAppearance` false, #421); on macOS 10.14 and 10.15 a
  legacy build linked against an older SDK may still render light, while the
  UB2 release, whose x86_64 slice serves those versions with the current SDK,
  was runtime-verified in the dark appearance on 10.14 (10.15 dark appearance not verified).
  See `scripts/macos/README.md`.
  In an Xcode 3.2.6 build environment, use
  `scripts/macos-standalone-release-ub1.sh tiger` for a `ppc;i386` set or
  `scripts/macos-standalone-release-ub1.sh leopard` for a
  `ppc;i386;x86_64` set. These use the existing split-build and `lipo` merge
  paths and publish under `build/release/macos-<profile>-ub1` only after every
  application contains every expected slice. The 0.0.6 release candidate's
  Tiger set (`ppc;i386`) is build-verified on Mavericks with Xcode 3.2.6,
  GCC 4.0 and the 10.4u SDK
  ([#1253](https://github.com/SimplySmirkySoftware/Loka/pull/1253),
  [#1262](https://github.com/SimplySmirkySoftware/Loka/pull/1262)).
  This includes SimpleViewer's bundle packaging.
  The earlier Leopard set evidence predates that packaging change.
  Its updated packaging still needs build verification on that route.
  GCC 4.2 records the Leopard PPC slice as `ppc7400`.
  On Big Sur or newer with an Apple Silicon-capable Xcode, run
  `scripts/macos-standalone-release-ub2.sh` for the corresponding
  `arm64;x86_64` autonomous set. Like UB1, it stages five loops plus interactive
  SimpleViewer and SimpleText and the experimental SmirkyCard (its card scripts
  inside the bundle's Resources) under `build/release/macos-ub2` only after every
  executable contains both slices. The release's x86_64 slice targets macOS
  10.13, the current SDK's floor. It serves 10.15, which runs no 32-bit code
  and so cannot open the Tiger UB1 set (`ppc;i386`) the release ships, and
  gives 10.13 and 10.14, which run both, a 64-bit set; the arm64 slice targets
  macOS 11.0 ([#1255](https://github.com/SimplySmirkySoftware/Loka/pull/1255),
  [#1257](https://github.com/SimplySmirkySoftware/Loka/pull/1257)).
  The seven applications other than SmirkyCard
  were runtime-verified on macOS 10.14 in the dark appearance
  (2026-10-12, owner's Intel Mac,
  [#1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259)).
  On 2026-10-12, the UB2 archive (x86_64 slice) ran on macOS 10.15.7
  in an Intel VM: runtime-verified for the five loops; the three interactive
  apps startup only. See the [archive results below](#verification-status-by-target).
  On the owner's Intel Mac running macOS 10.13, UB2 SimpleText launches
  and edits, but typing Japanese through an input method crashes it
  ([#1264](https://github.com/SimplySmirkySoftware/Loka/issues/1264)).
  UB1 FloppyBird's loop runs its tour on that machine.
  These are scoped 10.13 observations, not general runtime verification.
  See [#1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259) and the
  [target evidence below](#verification-status-by-target).
- For a presentation that keeps moving without a host controller, use the
  TEST-only HelloWorld and MineSweeper loop reels. Each application shows every
  registered cell for its example, wraps to the first, and continues until the
  user closes it. It reads no `LokaTest.cfg` and publishes no audit or capture
  marker.

  On Classic, run **Build & Start Loop in MAME via SCSI** and pick
  `HelloWorldScenarioLoop` or `MineSweeperScenarioLoop` from the prompt. The same
  build emits self-contained `.dsk` images for real hardware; see
  [Classic scenario loop reels](MAME_DEVELOPMENT.md#classic-scenario-loop-reels)
  for the exact build, MAME, and SD-SCSI routes.

  On macOS, run **Build: macOS loop target** in VS Code and pick
  `HelloWorldScenarioLoop` or `MineSweeperScenarioLoop`, then open the generated
  application. The equivalent Terminal commands from the repository root are:

  ```sh
  cmake --preset macos-debug
  cmake --build build/macos/Debug --target LokaHelloWorldScenarioLoopMacOS LokaMineSweeperScenarioLoopMacOS
  open build/macos/Debug/apple/macos/LokaHelloWorldScenarioLoopMacOS.app
  open build/macos/Debug/apple/macos/LokaMineSweeperScenarioLoopMacOS.app
  ```

  The macOS apps stop when the user chooses Quit or presses Command-Q. On
  Win32, run **Build: Win32 loop target** and pick `HelloWorldScenarioLoop` or
  `MineSweeperScenarioLoop` to build the Debug reels; start the generated
  executable directly. For a portable Release reel, use the
  architecture-specific presentation preset; for example, from an ARM64 Native
  Tools prompt:

  ```bat
  cmake --preset win32-arm64-release
  cmake --build --preset win32-scenario-loop-arm64-release
  start "" build\win32\presentation\arm64\Release\example\MineSweeper\scenario-loop\LokaMineSweeperScenarioLoopWin32.exe
  ```

  Substitute `x64` or `x86` in both preset names and the output path when using
  that compiler environment. To make a verification build stop after two
  complete passes, set `LOKA_SCENARIO_LOOP_CYCLES=2` in a separate build
  directory or cache so the ordinary desk build remains unbounded. On macOS:

  ```sh
  cmake -S . -B build/macos/Loop2 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLOKA_WARNINGS_AS_ERRORS=ON -DLOKA_SCENARIO_LOOP_CYCLES=2
  cmake --build build/macos/Loop2 --target LokaHelloWorldScenarioLoopMacOS LokaMineSweeperScenarioLoopMacOS
  open -W build/macos/Loop2/apple/macos/LokaHelloWorldScenarioLoopMacOS.app
  open -W build/macos/Loop2/apple/macos/LokaMineSweeperScenarioLoopMacOS.app
  ```

  On Win32, pass `-DLOKA_SCENARIO_LOOP_CYCLES=2` to the architecture-specific
  configure command and build from that separate cache.
- For a portable standalone presentation Release, open the Visual Studio
  Command Line Tools session for the desired target and run, for example:

  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\win32-standalone-flow.ps1 -Action Stage -Architecture x64
  ```

  Substitute `x86` or `arm64` while using the matching compiler environment.
  The script uses a separate CMake cache per architecture, verifies every PE
  header, and stages five application directories named by the catalog keys.
  Each executable owns its directory and `LOG.TXT`; only `scrapbook` carries
  `ASSETS.LRP`. The generated application catalog, verifier, and tracked expected
  audits stay at the package root under
  `build/presentation/win32-<architecture>-release`. ScrapbookUI, HelloWorld,
  Tutorial, MineSweeper, and FloppyBird are included; SimpleViewer is explicitly
  omitted because it requires an interactive file chooser. Use `-Action Verify`
  to launch all five sequentially on a compatible build host. Copy the staged
  directory to the target machine and run
  `powershell -ExecutionPolicy Bypass -File .\Verify-StandaloneFlow.ps1`
  there for a hardware check. The staged verifier derives the architecture
  from the application PEs, starts each presentation in its own directory,
  waits for its exact tracked
  audit, stops the final-scene hold, and stores the five target-local verdicts
  under `actual`. For the XP target, use the named profile below.
  **Standalone: Win32 Build / Stage / Verify / Release** is the VS Code shortcut;
  choose Build, Stage, or Verify from its action prompt.

  For the standalone application payload, use `-Action Release` instead of
  `-Action Stage`. It writes `build/release/win32-<architecture>` with five
  autonomous loop directories (`scrapbook`, `helloworld`, `tutorial`,
  `minesweeper`, `floppybird`), a `simpleviewer` directory containing
  `LokaSimpleViewerWin32.exe`, a `simpletext` directory containing
  `LokaSimpleTextWin32.exe`, a `smirkycard` directory containing the
  experimental `LokaSmirkyCardWin32.exe` with the card scripts listed in
  `example/SmirkyCard/disk-scripts.txt` and PNG pictures listed by stem in
  `example/SmirkyCard/disk-pictures.txt` beside it, and a shared README. Keep each
  executable in its directory; ScrapbookUI's `ASSETS.LRP`, SmirkyCard's scripts
  and each app's `LOG.TXT` belong there.
  The CMake Standalone Flow/Loop build outputs use the same catalog-key
  directories, so direct launches also keep sidecars separate. Each loop keeps its App and native Window;
  a completed pass receives a fresh scenario rail and a re-armed Scene.
  Closing it manually does not relaunch it.
  SimpleViewer, SimpleText and SmirkyCard remain interactive and are not started by
  either audit verification or Release staging.
  In **Standalone: Win32 Build / Stage / Verify / Release**, choose Release for the equivalent
  VS Code action;
  launch VS Code from the matching Visual Studio Developer Command Prompt so
  the Task inherits the intended compiler architecture.

  The named x86 XP release profile is `-TargetProfile xp` (the default is `modern`).
  Use the Windows 8.1 build VM with Visual Studio 2017 Build Tools 15.9 and
  its **Windows XP support for C++** component, CMake 4.2.x (CMake 4.3+
  requires Windows 10), and Ninja. Initialize the v141 x86 compiler environment
  with `vcvarsamd64_x86.bat`, then run from the repository root:

  ```bat
  call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\win32-standalone-flow.ps1 -Action Release -Architecture x86 -TargetProfile xp
  ```

  This selects `win32-standalone-x86-xp-release` and its Loop build preset.
  The profile has its own cache at
  `build/win32/standalone/presentation/x86-xp/Release` and stages to
  `build/release/win32-x86-xp`. The script refuses non-x86 targets or a
  compiler environment outside v141, checks every executable for PE subsystem
  version 5.01, and records `Profile: xp` in the README. Start with a fresh
  profile cache: the XP flags seed CMake's initial flags, rather than replacing
  an existing cache's completed flags.

  For the five-app audit package, use the same command with `-Action Stage`;
  it selects the Flow build preset and stages to
  `build/presentation/win32-x86-xp-release`. Run its packaged verifier with
  `-TargetProfile xp` to retain the XP header checks; it needs no compiler:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File build/presentation/win32-x86-xp-release/Verify-StandaloneFlow.ps1 -TargetProfile xp
  ```

  The earlier Windows 8.1 probe remains evidence for its hand-configured
  payload and five packaged audits, not for the named XP profile.
  The 0.0.6 release candidate's x86 XP archive is build-verified with
  `-TargetProfile xp` on Windows 8.1 using VS2017 Build Tools 15.9
  (v141_xp, MSVC 14.16). It uses the static CRT and subsystem 5.01
  ([#1251](https://github.com/SimplySmirkySoftware/Loka/pull/1251),
  [#1252](https://github.com/SimplySmirkySoftware/Loka/pull/1252)).
  On 2026-10-12, the owner runtime-verified that archive on Windows XP
  on a Sony VAIO P (real hardware). All applications were OK in rehearsal 3.
  The owner exercised the interactive apps by hand.
  The same archive ran on a Windows 8.1 VM: runtime-verified for the five
  loops; the three interactive apps startup only
  ([#1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259)).
  The x64 archive is build-verified with VS 2022-class MSVC 14.44 and
  subsystem 6.00 ([#1251](https://github.com/SimplySmirkySoftware/Loka/pull/1251),
  [#1252](https://github.com/SimplySmirkySoftware/Loka/pull/1252)).
  It ran on a Windows 11 VM on 2026-10-12: runtime-verified for the five
  loops; the three interactive apps startup only
  ([#1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259)).
  See the [archive results below](#verification-status-by-target).

  To verify application-sidecar isolation in an interactive Windows session,
  use a freshly staged Release package and a new evidence directory:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File tests/scripts/Win32StandaloneIsolationTest.ps1 -StageRoot build/release/win32-x64 -EvidenceDirectory build/isolation-check
  ```

  This opens Scrapbook and HelloWorld concurrently from the same working
  directory, requires both application-local audits to succeed, retains them
  in the evidence directory, and closes only the processes it started. It
  refuses existing logs. The same check accepts the CMake `standalone-loop`
  output directory for direct-build launch coverage. For Visual Studio or
  Ninja Multi-Config outputs, also pass `-Configuration Release` (or the
  configuration you built); executable and audit paths then use
  `<root>/<app>/<configuration>/`. Omit this argument for staged packages
  and single-config outputs.

  To measure a Standalone Flow, configure a separate cache with
  `LOKA_STANDALONE_PERFORMANCE_RUNS` set from 3 through 10 and build its
  ordinary Standalone Flow target. The application runs the complete
  Config/App/Flow lifetime that many times, after each pass has published its
  successful terminal audit, then writes `PERF.TXT` beside the application and
  exits. A failed pass leaves the report empty and returns a failing exit code.
  The per-run `elapsed_ticks` and summary use the platform profiler's native
  tick unit, so compare reports only on the same target and rig. Run the
  ordinary audit verification first; performance mode preserves the final
  pass's `LOG.TXT`, but `PERF.TXT` is timing data rather than a correctness
  verdict.
- Some endpoint scanners flag freshly linked unsigned test executables,
  particularly 32-bit ones.
  This workflow deliberately does not add an antivirus exclusion; use the
  machine's normal review/allow procedure or an approved target environment
  when local policy blocks the launch.
- Classic Toolbox targets are currently built through Retro68.
- Retro68 keeps the core portable while allowing modern host-side tooling for Classic builds.
- Retro68 workflows are not limited to Parallels Desktop. Docker, colima, WSL, and other Linux-oriented environments are also recommended.
- Retro68 is intentionally treated as a Linux-oriented build environment rather than something that must be installed directly on the host OS. This keeps the workflow easier to support and works well with VS Code, where separate windows can target different build environments and CMake configurations against the same source tree.
- When Retro68 builds are run inside Linux containers, Microsoft's Remote - SSH / WSL extensions are recommended for working with that environment from VS Code.
- Support for older toolchains such as Visual Studio 2005 and CodeWarrior Pro is planned.

Typical examples:

- modern macOS with current Clang / Xcode tools
- Snow Leopard for Universal Binary 1 workflows
- Linux or WSL2 for Retro68 builds
- macOS with Linux containers for Retro68 builds
- modern Windows with MSVC

## Environment for Target

This is the environment where the resulting application is meant to run.

- Classic Mac OS on 68k and PowerPC systems
- Mac OS X Tiger through Snow Leopard
- modern macOS
- Windows XP through current Windows
- Windows on ARM
- additional future targets such as iOS, Linux, and other platform ports

Typical examples:

- Classic Mac OS systems running on 68k or PPC hardware
- PowerBook G4 / older Mac OS X machines
- netbooks and low-end PCs
- modern MacBook systems
- ARM-based Windows devices

## Verification Status by Target

The sections above describe where Loka is meant to run. This one records how
far each target has actually been checked, so a claim can be traced to
evidence rather than to an intention. Use the terms from
[MAME_DEVELOPMENT.md](MAME_DEVELOPMENT.md): **build-verified** means it
compiled and linked, **runtime-verified** means it was launched and
exercised.

Emulation and hardware do different jobs. An emulator that exposes the
machine — MAME does, on 68K — is where an application can be inspected from
the inside; that is where the gdb workflow in
[MAME_DEVELOPMENT.md](MAME_DEVELOPMENT.md) applies, and being scriptable it
carries the automated scenarios. Hardware answers whether the result behaves
on a real machine. Neither substitutes for the other.

The 0.0.6 release-candidate results below are dated 2026-10-12.
Their source is [release PR #1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259).
The candidate is main `39de508f`; release branch `release/0.0.6` at
`9699c15c` adds only archive allowlists.
An archive run starts from an expanded release rehearsal zip.
For archive smoke, the five loops are runtime-verified: FloppyBird,
HelloWorld, MineSweeper, ScrapbookUI and Tutorial completed their tours
with 0 failed steps.
The three interactive apps, SimpleText, SimpleViewer and SmirkyCard,
launched and stayed up (startup only).
For owner hardware reports marked “OK”, tours were not counted.
The owner exercised SimpleText, SimpleViewer and SmirkyCard by hand.

| Target | Status |
| --- | --- |
| Classic Mac OS, 68030 | Runtime-verified, under emulation and on hardware. The primary leg, and the only one that can be debugged at source level. |
| Classic Mac OS, 68020 | Emulation only. No hardware, so timing observations are not measurements. |
| Classic Mac OS, 68040 | Runtime-verified: the owner ran the 0.0.6 candidate's Classic 68K set on a PowerBook 550c (full 68040, Mac OS 7.6.1 Japanese, real hardware). Result: OK. The interactive apps were exercised by hand. |
| Classic Mac OS, PowerPC | Runtime-verified: the owner ran the 0.0.6 candidate's Classic PPC set on a PowerBook 1400c (PowerPC 603e, Mac OS 8.6) and a PowerBook G3 "PDQ" (Mac OS 8.6), both real hardware. Result: OK, and loops placed in separate folders kept presenting while in the background ([#1239](https://github.com/SimplySmirkySoftware/Loka/pull/1239)). The interactive apps were exercised by hand. |
| Mac OS X, Tiger 10.4 | Runtime-verified for generated Universal Binary 1 samples. The owner ran the 0.0.6 candidate's UB1 archive (ppc slice) on a PowerBook G3 “Pismo” (Mac OS X 10.4.11, real hardware) on 2026-10-12. Result: OK. The interactive apps were exercised by hand. |
| Mac OS X, Leopard 10.5 | Build-verified through Xcode 3.x, with native debugging verified on that host. The 0.0.6 candidate's UB1 archive (ppc slice) ran on 10.5.8 on an emulated PowerPC G4 (QEMU): runtime-verified for the five loops; the three interactive apps startup only. |
| Mac OS X, Snow Leopard 10.6 | Build-verified, including UB1 (`ppc/i386` and `ppc7400/i386/x86_64`). The bridge host for legacy builds. |
| Mac OS X, Mavericks 10.9.5 | The 0.0.6 candidate's UB1 archive (i386 slice) ran in an Intel VM: runtime-verified for the five loops; the three interactive apps startup only. |
| Mac OS X on PowerPC hardware | Runtime-verified on an iBook, from bridge output that was also four-architecture `lipo`-verified. The 0.0.6 Tiger hardware run is recorded above. |
| macOS, High Sierra 10.13 | Scoped observations only: UB2 SimpleText launches and edits on the owner's Intel Mac. Japanese input through an input method crashes it ([#1264](https://github.com/SimplySmirkySoftware/Loka/issues/1264)). UB1 FloppyBird's loop runs its tour. |
| macOS, Mojave 10.14 | Runtime-verified in dark appearance: the seven UB2 applications other than SmirkyCard on the owner's Intel Mac. |
| macOS, Catalina 10.15.7 | The 0.0.6 candidate's UB2 archive (x86_64 slice) ran in an Intel VM: runtime-verified for the five loops; the three interactive apps startup only. |
| Modern macOS | Runtime-verified: the full Loka macOS golden matrix on macOS 26.7.1 (Intel). The owner ran the 0.0.6 candidate's UB2 archive (arm64 slice) on macOS 27.0.1 on a MacBook Neo (real hardware). Result: OK. The interactive apps were exercised by hand. |
| Windows, current | Runtime-verified on Windows 11 for ARM64 with ACP=932. The 0.0.6 candidate's x64 archive ran on a Windows 11 VM: runtime-verified for the five loops; the three interactive apps startup only. |
| Windows 8.1 | The 0.0.6 candidate's x86 XP archive ran in a VM: runtime-verified for the five loops; the three interactive apps startup only. |
| Windows XP class | Runtime-verified on 2026-10-12: the owner ran the 0.0.6 release candidate's x86 XP archive on a Sony VAIO P (real hardware, rehearsal 3). All applications OK. The interactive apps were exercised by hand. |
| Linux / WSL | Runtime-verified for the headless test suites. |

Hardware coverage is uneven. The 0.0.6 candidate adds a 68040
PowerBook 550c run to the earlier 68030 hardware evidence.
These owner observations do not establish performance measurements.
The MAME scenario rail passed 29/29 cells at the candidate
([#1259](https://github.com/SimplySmirkySoftware/Loka/pull/1259)).

Classic PowerPC now has hardware runtime evidence from the PowerBook 1400c.
The automated runtime scenarios remain 68K only.
The 68K source-level debugging does not transfer to PPC;
see “Why this does not carry over to PPC” in
[MAME_DEVELOPMENT.md](MAME_DEVELOPMENT.md).

The legacy macOS toolchain matrix is recorded in more detail in
[../scripts/macos/README.md](../scripts/macos/README.md) — which host OS can
generate projects, debug, and drive Xcode 3.2.6 for Universal Binary builds.
That file is the detailed record for those rows; this table is the summary, so
add a leg there first and reflect it here rather than the other way round.

Emulator machine names elsewhere in this repository are examples of a working
configuration, not a statement about hardware.
