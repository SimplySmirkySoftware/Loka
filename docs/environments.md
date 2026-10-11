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
  project-owned `cmake/macos/MacOSXBundleInfo.plist.in`. The legacy build
  evidence below predates this template change; the updated bundle metadata
  still needs legacy build verification and Retina runtime verification.
  The same template opts the bundles into the dark appearance
  (`NSRequiresAquaSystemAppearance` false, #421); on macOS 10.14 and 10.15,
  served by legacy builds linked against older SDKs, that opt-in is not
  runtime-verified, and the application may still render light. See
  `scripts/macos/README.md`.
  In an Xcode 3.2.6 build environment, use
  `scripts/macos-standalone-release-ub1.sh tiger` for a `ppc;i386` set or
  `scripts/macos-standalone-release-ub1.sh leopard` for a
  `ppc;i386;x86_64` set. These use the existing split-build and `lipo` merge
  paths and publish under `build/release/macos-<profile>-ub1` only after every
  application contains every expected slice. Before SimpleViewer was converted
  to an `.app` bundle, these complete autonomous sets were build-verified
  through the Mavericks 10.9.5 + Xcode 3.2.6 CLI route. SimpleViewer's new
  bundle packaging remains pending build verification on that route for both
  Tiger and Leopard. GCC 4.2 records the Leopard PPC slice as `ppc7400`.
  Runtime on a PowerPC Mac remains a separate verification step.
  On Big Sur or newer with an Apple Silicon-capable Xcode, run
  `scripts/macos-standalone-release-ub2.sh` for the corresponding
  `arm64;x86_64` autonomous set. It stages the same five loops plus SimpleViewer
  under `build/release/macos-ub2` only after every executable contains both
  slices. The default deployment target is macOS 11.0.
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
  under `actual`. For a VAIO P, build x86 from a VS2017 `x64_x86 Cross Tools`
  session. **Standalone: Win32 Build / Stage / Verify / Release** is the VS Code shortcut;
  choose Build, Stage, or Verify from its action prompt.

  For the standalone application payload, use `-Action Release` instead of
  `-Action Stage`. It writes `build/release/win32-<architecture>` with five
  autonomous loop directories (`scrapbook`, `helloworld`, `tutorial`,
  `minesweeper`, `floppybird`), a `simpleviewer` directory containing
  `LokaSimpleViewerWin32.exe`, and a shared README. Keep each executable in its
  directory; ScrapbookUI's `ASSETS.LRP` and each app's `LOG.TXT` belong there.
  The CMake Standalone Flow/Loop build outputs use the same catalog-key
  directories, so direct launches also keep sidecars separate. Each loop keeps its App and native Window;
  a completed pass receives a fresh scenario rail and a re-armed Scene.
  Closing it manually does not relaunch it.
  SimpleViewer remains interactive and is not started by either audit
  verification or Release staging.
  In **Standalone: Win32 Build / Stage / Verify / Release**, choose Release for the equivalent
  VS Code action;
  launch VS Code from the matching Visual Studio Developer Command Prompt so
  the Task inherits the intended compiler architecture.

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

| Target | Status |
| --- | --- |
| Classic Mac OS, 68030 | Runtime-verified, under emulation and on hardware. The primary leg, and the only one that can be debugged at source level. |
| Classic Mac OS, 68020 / 68040 | Emulation only. No hardware, so timing observations are not measurements. |
| Classic Mac OS, PowerPC | Build-verified. Hardware is available for the runtime check; it has not been done yet. |
| Mac OS X, Tiger 10.4 | Runtime-verified for generated Universal Binary 1 samples. |
| Mac OS X, Leopard 10.5 | Build-verified through Xcode 3.x, with native debugging verified on that host. |
| Mac OS X, Snow Leopard 10.6 | Build-verified, including UB1 (`ppc/i386` and `ppc7400/i386/x86_64`). The bridge host for legacy builds. |
| Mac OS X on PowerPC hardware | Runtime-verified on an iBook, from bridge output that was also four-architecture `lipo`-verified. |
| Modern macOS | Runtime-verified on Apple silicon. |
| Windows, current | Runtime-verified on Windows 11 for ARM64 with ACP=932. |
| Windows XP class | Build target. Not runtime-verified. |
| Linux / WSL | Runtime-verified for the headless test suites. |

Hardware coverage is uneven, and the gaps shape what a result can mean. The
68K side rests on a single 68030 PowerBook, so 68020 and 68040 behaviour can
only be observed under emulation — useful for "does it still work", not for
"is it fast enough".

PowerPC divides by operating system rather than by hardware. Mac OS X on
PowerPC has been reached, including a runtime check on an iBook. **Classic**
Mac OS on PowerPC has not: the automated runtime scenarios are 68K only, so
that leg has never been exercised beyond building, even though the hardware
for it exists. That gap will not
be closed by the 68K tooling — the source-level debugging does not transfer;
see "Why this does not carry over to PPC" in
[MAME_DEVELOPMENT.md](MAME_DEVELOPMENT.md).

The legacy macOS toolchain matrix is recorded in more detail in
[../scripts/macos/README.md](../scripts/macos/README.md) — which host OS can
generate projects, debug, and drive Xcode 3.2.6 for Universal Binary builds.
That file is the detailed record for those rows; this table is the summary, so
add a leg there first and reflect it here rather than the other way round.

Emulator machine names elsewhere in this repository are examples of a working
configuration, not a statement about hardware.
