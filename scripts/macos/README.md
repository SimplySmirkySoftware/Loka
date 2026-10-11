# macOS Build Scripts

`scripts/macos` provides entry points by target OS generation.
In normal use, call one of the wrapper scripts below instead of `build.sh` directly.

## Verification Matrix

Legend: `:white_check_mark:` verified, `△` verified with caveats (see Notes),
`-` unchecked / not part of that route, `:x:` checked and not working.

| Environment | Project generation | Native Xcode debug | Xcode 3.x UB1 build | Xcode 3.x debug | CLI build | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Tiger 10.4 / Xcode 2.4 | - | :x: | - | :x: | - | Runtime-verified only for generated 10.4 UB1 samples; Xcode 2.4 debugging is not pursued. Tiger-hosted CLI builds are unchecked, including tigerbrew. |
| Leopard 10.5 / Xcode 3.1 | - | :white_check_mark: | :white_check_mark: | :white_check_mark: | :x: | Requires a copied project saved with Xcode 3.1 compatibility; CMake could not be installed via MacPorts in testing. |
| Snow Leopard 10.6 / Xcode 3.2.6 | - | :white_check_mark: | :white_check_mark: | :white_check_mark: | :white_check_mark: | Main UB1 bridge; copied projects and `build-10_4.sh` / `build-10_5.sh` are build-verified (`ppc/i386` and `ppc7400/i386/x86_64`). |
| Lion 10.7 + final Xcode | - | :white_check_mark: | :white_check_mark: | - | :x: | Final Xcode can debug native builds; cross-partition Xcode 3.2.6 can build UB1 through the UI, but CLI wrappers cannot select it with `xcode-select`. |
| Mountain Lion 10.8 + final Xcode | :white_check_mark: | :white_check_mark: | :white_check_mark: | - | :x: | Can generate projects from source, debug with the final Xcode, and run cross-partition Xcode 3.2.6 for UB1 UI builds. CLI wrappers cannot select that Xcode through `xcode-select`. |
| Mavericks 10.9 / Xcode 6.2 | :white_check_mark: | - | :white_check_mark: | - | :white_check_mark: | Legacy project generation works. An installed Xcode 3.2.6 provides the CLI compilers, SDKs, and `lipo`; the autonomous Tiger and Leopard UB1 release sets are build-verified there with the explicit `CC`/`CXX` recipe (see Which Script To Run). |
| Yosemite 10.10 | :white_check_mark: | :white_check_mark: | :x: | :x: | - | Project generation and native debugging work, but Xcode 3.2.6 cannot launch there. |
| El Capitan 10.11 through Sierra 10.12 / Xcode 9.2 | :white_check_mark: | :white_check_mark: | - | - | - | Leopard-facing Xcode project generation has been verified on Sierra with Xcode 9.2; generated projects have been copied back to Snow Leopard and build-verified there. |
| High Sierra 10.13 | △ | :white_check_mark: | - | - | - | Depends on the selected Xcode version; Xcode 9.4.1-era setups are expected to work, while Xcode 10.1 fails legacy UB1 generation. |
| Mojave 10.14 / Catalina 10.15 | △ | - | - | - | - | Bridge to a separate Snow Leopard machine: the legacy UB1 generator run with `DEPLOYMENT_TARGET=10.9` produces projects that build in Xcode 3.2.6 with the usual manual steps and no `-fno-objc-arc` cleanup. |
| Catalina 10.15 / Big Sur 11 | △ | - | - | - | - | Bridge to a separate Snow Leopard machine: plain `gen-xcodeproj.sh` projects open in Xcode 3.2.6 and build with the usual manual steps after removing `-fno-objc-arc` per target. |
| Monterey 12 / Xcode 14.2 | △ | :white_check_mark: | - | - | - | Bridge to a separate Snow Leopard machine: after removing unsupported flags such as `-fno-objc-arc`, Monterey-generated bridge output has been four-architecture `lipo`-verified and runtime-verified on an iBook. Xcode 3.x cannot launch on Monterey; the UB1 build happens on Snow Leopard. |
| Ventura 13 / Xcode 15.2 and newer | :x: | :white_check_mark: | - | - | - | Modern native debugging works, but Xcode 15.2 only offered Xcode 12.0+ project formats in testing. |

## OS Workflow Notes

There are two separate legacy routes to keep in mind. Building UB1 outputs
directly requires a host that can launch Xcode 3.2.6; this has been verified on
Snow Leopard 10.6 through Mavericks 10.9 and is not available on Yosemite or
newer. CLI wrapper builds through Xcode 3.2.6 auto-resolve tools on Snow
Leopard only: Xcode 3.2.6 is an old `/Developer`-style install that
`xcode-select` on Lion and newer cannot select. On those newer hosts the CLI
path still works by naming the legacy tools explicitly with `CC`/`CXX` and
keeping `DEVELOPER_DIR` unset — verified on Mavericks; recipe under Which
Script To Run.

Generating bridge projects has two paths. The legacy UB1 generator scripts need
an Xcode 9-series or earlier host; Sierra with Xcode 9.2 is verified and Xcode
9.4.1-era High Sierra setups are expected to work. On Xcode 10 and newer hosts,
the legacy generators also work when run with `DEPLOYMENT_TARGET=10.9`, because
deployment targets of 10.9 and newer switch the default C++ standard library
from the removed `libstdc++` to `libc++`; the generated project then needs no
`-fno-objc-arc` cleanup, and the final Base SDK, deployment target, and
architectures are set in Xcode 3.2.6 as usual; this route has been
build-verified from Mojave 10.14 and Catalina 10.15 generator hosts through to
Xcode 3.2.6 builds. Treat that override as a fallback for when the work must
happen on a modern host (generation verified through Monterey; build-verified
from 10.14/10.15 hosts). The most comfortable setup is a virtualized Mountain
Lion 10.8 or Mavericks 10.9 system with a working Xcode 3.2.6 install: there
the legacy generators run with their default deployment targets and the
generated project builds in Xcode 3.2.6 on the same system, with no flag
cleanup or cross-machine copying. The plain `gen-xcodeproj.sh`
path has been verified for bridge projects on Catalina 10.15, Big Sur 11, and
Monterey 12, with `-fno-objc-arc` removed per target when opening the generated
project in Xcode 3.2.6.

Snow Leopard is the main Xcode 3.2.6 bridge. Projects generated on tested newer
systems (10.8-10.12, plus 10.13 with a 9.4.1-era Xcode, and newer generator
hosts through the plain `gen-xcodeproj.sh` route) can be copied with the
repository tree and opened directly in Xcode 3.2.6. Generated Xcode projects are
absolute-path-sensitive: keep the same user/path layout used during generation,
or Xcode file references may break. With a copied project, Snow Leopard does
not need CMake or Ninja installed. Direct script builds such as
`build-10_4.sh` and `build-10_5.sh` do require CMake/Ninja on Snow Leopard and
are useful for standalone command-line build verification, but they do not
provide Xcode debugging.

Xcode 3.2.6 can build 32/64-bit Leopard-style UB1 outputs (`ppc7400`, `ppc64`,
`i386`, `x86_64`) and Tiger-oriented 32-bit UB1 outputs. For Tiger, set the
deployment target to Mac OS X 10.4 and use 32-bit Intel plus PPC; verified
outputs use `ppc i386` slices. Tiger runtime compatibility does not imply
Tiger/Xcode 2.4 project debugging: the Cocoa backend uses Objective-C
`@property` / `@synthesize` in a few internal delegate classes, so use Snow
Leopard/Xcode 3.2.6 for debugging Tiger-targeted builds.

Lion through Mavericks can also run a correctly installed Xcode 3.2.6 from a
Snow Leopard partition for Xcode-hosted UB1 work. With that setup,
Tiger-oriented 32-bit UB1 builds using GCC 4.0 have been build-verified from
the Xcode UI on Lion/Mountain Lion; Mavericks has been verified for
Leopard-facing UB1. The command-line wrappers are a secondary path there: they
prefer tools resolved through `xcrun` / the selected developer directory before
falling back to `PATH`, but Xcode 3.2.6 is an old `/Developer`-style install
rather than a modern `Xcode.app/Contents/Developer` bundle and cannot be
selected with `xcode-select` from those hosts. Auto-resolution therefore picks
the modern PPC-less compiler; bypass it with the explicit `CC`/`CXX` recipe
(verified on Mavericks) or use the Xcode UI.

Leopard can open copied projects when a newer Xcode has saved them with Xcode
3.1 compatibility. If the copied project reports the original Base SDK as
missing, select the current Mac OS X 10.5 SDK in Xcode and build again. This has
been build-verified on both Intel and G4 Leopard systems. Do not expect the
`build-10_4.sh` / `build-10_5.sh` command-line scripts to work on Leopard:
CMake could not be installed via MacPorts in testing there, so those scripts
should be treated as Snow Leopard CLI verification paths.

Mountain Lion through Monterey can generate Xcode projects with
`gen-xcodeproj.sh`, so native debugging is available on the generating machine
with that host's final supported Xcode. The default path is 64-bit-only for
older Intel Macs, and Big Sur and later can use the UB2 path for
`arm64;x86_64` builds. Projects generated in that range can be prepared for
Xcode 3.2.6, and if the project is switched to Xcode 3.1 compatibility and
saved, the same project can be copied to Leopard for Xcode-hosted debugging
there. This is a bridge-project workflow through the default `gen-xcodeproj.sh`
path, not evidence that Monterey can directly generate a Leopard-deployment
project with the legacy UB1 generator.

On newer generator hosts, the deployment target used while generating an Xcode
project may need to be newer than the final target built on Snow Leopard. For
example, Monterey/Xcode 14 can run the default `gen-xcodeproj.sh` path and
generate a usable bridge project, then Snow Leopard/Xcode 3.2.6 (or the
copied-project Leopard/Xcode 3.1 route) can open that project and perform the
final UB1 build after removing unsupported flags such as `-fno-objc-arc` and
selecting the intended Base SDK, deployment target, and architectures. That
workflow has produced four-architecture UB1 output verified with `lipo` and
launched on an iBook.

For project-format down-conversion, Xcode 14.2 on Monterey has been observed to
offer Xcode 3.1 compatibility for a new project. Xcode 15.2 on Ventura only
offered Xcode 12.0 and newer formats in testing, so use an older generator host
when preparing projects for Leopard/Xcode 3.1.

High Sierra needs extra care because the selected Xcode version changes the
legacy generator behavior. Sierra with Xcode 9.2 has been checked and can
generate Leopard-facing Xcode projects. High Sierra with an Xcode 9.4.1-era
setup is expected to behave like earlier verified hosts. High Sierra with Xcode
10.1 and the MacOSX10.14.sdk has been checked and fails the Leopard and Lion UB1
generation paths during CMake's initial C++ compiler check because the toolchain
still selects `libstdc++` for the old deployment target, but the library is no
longer available. Running the legacy generators with `DEPLOYMENT_TARGET=10.9`
avoids this failure. Treat Xcode 10 and newer primarily as modern macOS
generator hosts: Xcode 10/11 are useful for modern Intel-oriented projects,
while UB2 (`arm64;x86_64`) starts with Apple Silicon-capable Xcode releases.

## Which Script To Run

- `scripts/macos/build-10_4.sh`
  - Legacy build path for Tiger/Leopard compatibility work.
  - Defaults: `DEPLOYMENT_TARGET=10.4`, `ARCHS=ppc;i386`, `MAC_OS_10_4=1`
  - Intended for Snow Leopard environments with CMake/Ninja and the 10.4u SDK installed.
  - The previous five-target default set is build-verified on Snow Leopard with Xcode 3.2.6; expected merged slices are `ppc i386`. The newly enumerated ScrapbookUI bundle has not yet been build-verified on that toolchain.
  - SimpleText desktop build and runtime verification are pending on the owning rigs.
  - By default builds all nine shipping app targets, including the bundled `ScrapbookUIMacOS`. Plain executables are merged directly under `universal`; ScrapbookUI is merged as `universal/ScrapbookUIMacOS.app` so its `Resources/ASSETS.LRP` remains with the executable.
  - Xcode UI builds using a Snow Leopard-partition Xcode 3.2.6 install have also been build-verified from Lion/Mountain Lion hosts.
  - CLI builds through an Xcode 3.2.6 install on a newer host (`xcode-select` cannot select it there) work by bypassing `xcode-select`/`xcrun` rather than fighting them — verified end to end on Mavericks 10.9.5 (all five example targets, `ppc i386` fat binaries):

    ```sh
    export PATH=/opt/local/bin:$PATH        # cmake and ninja live here, not on the default PATH
    unset DEVELOPER_DIR                     # must stay unset: Xcode 3.2.6 has no xcrun (the script refuses otherwise;
                                            # ALLOW_DEVELOPER_DIR_WITHOUT_XCRUN=1 overrides on a host known to tolerate it)
    export MAC_OS_10_4_SYSROOT=/Developer/SDKs/MacOSX10.4u.sdk
    export CC=/Developer/usr/bin/gcc-4.0
    export CXX=/Developer/usr/bin/g++-4.0
    ./scripts/macos/build-10_4.sh
    ```

    Use the universal `gcc-4.0` driver, not the `powerpc-` prefixed one: CMake passes `-arch ppc -arch i386` to a single driver. Without the `CC`/`CXX` override, tool resolution falls through `xcrun` to `PATH` and picks the modern PPC-less `/usr/bin/g++`.
  - `MAC_OS_10_4_SYSROOT` is auto-resolved from `DEVELOPER_DIR` / `xcode-select` when unset.
  - Prefers `gcc-4.0` / `g++-4.0` resolved through `xcrun` from the selected Xcode, then falls back to `gcc-4.2` / `g++-4.2` and `PATH`.
  - `ppc` builds require a working PPC-capable compiler/toolchain (validated by a compile check).
  - If PPC tools are missing, the build fails by default; set `ALLOW_PPC_FALLBACK=1` to continue with non-ppc archs.
  - Outputs are written under `build/macos-10.4-ub1`.

- `scripts/macos/build-10_5.sh`
  - Leopard multi-arch path excluding `ppc64`.
  - Defaults: `DEPLOYMENT_TARGET=10.5`, `ARCHS=ppc;i386;x86_64`, `MAC_OS_10_4=0`
  - `OSX_SYSROOT` is auto-resolved from `DEVELOPER_DIR` / `xcode-select` when unset, then falls back to `/Developer/SDKs/MacOSX10.5.sdk`.
  - Prefers `gcc-4.2` / `g++-4.2` resolved through `xcrun` from the selected Xcode, then falls back to `PATH`.
  - The previous five-target default set is build-verified on Snow Leopard with Xcode 3.2.6; expected merged slices are `ppc7400 i386 x86_64`. The newly enumerated ScrapbookUI bundle has not yet been build-verified on that toolchain.
  - Treat this as a Snow Leopard CLI verification path, not a Leopard-hosted script path.
  - SimpleText desktop build and runtime verification are pending on the owning rigs.
  - By default builds all nine shipping app targets, including the bundled `ScrapbookUIMacOS`, and creates merged outputs in `build/macos-10.5-ub1/universal`. Plain executables are emitted directly there; ScrapbookUI remains a bundle so its Resources stay intact.

macOS bundle targets use the project-owned
`cmake/macos/MacOSXBundleInfo.plist.in` through CMake's default-template lookup.
It declares `NSHighResolutionCapable` explicitly for legacy and modern SDKs.
The legacy build evidence below predates this template change; the updated
metadata still needs legacy build verification and Retina runtime verification.

The template also declares `NSRequiresAquaSystemAppearance` as false (#421).
AppKit keeps applications linked against SDKs older than 10.14 in the light
appearance, and macOS 10.14 and 10.15 are served by the legacy builds, which
link older SDKs. The key declares the opt-in; whether AppKit honors it for
those binaries is not runtime-verified, so on 10.14 and 10.15 a Loka
application may still render light while the system is dark. That is a known
limitation, not a drawing defect. The key is unknown to 10.13 and earlier,
and it is already the default for the modern builds that serve 11 and later.

- `scripts/macos-standalone-release-ub1.sh tiger|leopard`
  - Builds the five autonomous Standalone Loop bundles plus the interactive
    SimpleViewer through the same per-architecture build and failure-atomic
    `lipo` merge used by `build-10_4.sh` and `build-10_5.sh`.
  - `tiger` selects `ppc;i386` and stages the completed set under
    `build/release/macos-tiger-ub1`.
  - `leopard` selects `ppc;i386;x86_64` and stages the completed set under
    `build/release/macos-leopard-ub1`. GCC 4.2 records its requested `ppc`
    input as the `ppc7400` Mach-O subtype, so the staged slices are
    `ppc7400 i386 x86_64`.
  - Run this with the Xcode 3.2.6 build environment after selecting the same
    SDK and compiler variables required by the underlying build script. The
    stage is published only after all six applications contain every expected
    architecture; Scrapbook's `ASSETS.LRP` remains inside its bundle.
  - Before SimpleViewer was converted to an `.app` bundle, the complete
    autonomous sets were build-verified through the Mavericks 10.9.5 + Xcode
    3.2.6 CLI route: Tiger as `ppc i386`, Leopard as `ppc7400 i386 x86_64`.
    SimpleViewer's new bundle packaging remains pending build verification
    on that route for both profiles. This is not a runtime-verification claim
    for either target OS or PowerPC hardware.

- `scripts/macos-standalone-release-ub2.sh`
  - Builds the five autonomous Standalone Loop bundles plus the interactive
    SimpleViewer as `arm64;x86_64`, then stages the complete set under
    `build/release/macos-ub2`.
  - Uses the modern UB2 build path with deployment target 11.0 and publishes
    only after every executable contains both required slices. Scrapbook's
    `ASSETS.LRP` remains inside its bundle.

- `scripts/macos/build-10_7.sh`
  - Standard build path for Lion and newer.
  - Defaults: `DEPLOYMENT_TARGET=10.7`, `MAC_OS_10_4=0`
  - On Snow Leopard with Xcode 3.2.6, CMake may warn that the `MacOSX10.6.sdk/Library/Frameworks` path is not set up correctly. The x86_64-only build has still been build-verified in that environment.
  - Treat this as a direct CLI build path for Intel macOS hosts whose installed Xcode still supports the requested deployment target. It is not the preferred path for preparing old Xcode bridge projects.

- `scripts/macos/build-ub2.sh`
  - Universal Binary 2 path for modern macOS.
  - Defaults: `DEPLOYMENT_TARGET=11.0`, `ARCHS=arm64;x86_64`, `MAC_OS_10_4=0`
  - SimpleText desktop build and runtime verification are pending on the owning rigs.
  - By default builds all nine shipping app targets: `LokaFloppyBirdMacOS`, `LokaHelloMacOS`, `LokaMineMacOS`, `LokaSimpleViewerMacOS`, `LokaSimpleTextMacOS`, `LokaSmirkBenchMacOS`, `LokaLazyListMacOS`, `ScrapbookUIMacOS`, and `LokaTutorialMacOS`.
  - Treat this as a direct CLI build path for the UB2 generation, not as a fallback for legacy UB1 project generation.

- `scripts/macos/build.sh`
  - Shared implementation used by the two wrapper scripts.
  - Use directly only when you need fully custom environment variables.
  - On legacy gcc-4.x toolchains, use single-arch only (`ARCHS` must not contain `;`).

- `scripts/macos/gen-xcodeproj.sh`
  - Generates an Xcode project (`-G Xcode`) for debugging.
  - Arch, SDK, and deployment target are controlled by env vars (default `ARCHS=xcode-standard`, `OSX_SYSROOT=macosx`, `DEPLOYMENT_TARGET=11`).
  - The default uses Xcode's `$(ARCHS_STANDARD)` preset so modern Xcode versions choose the appropriate standard architecture set.
  - `ARCHS=xcode-standard` omits `CMAKE_OSX_ARCHITECTURES` and writes Xcode's `$(ARCHS_STANDARD)` preset into the generated project.
  - The legacy spelling `ARCHS=archs-standard` is accepted as an alias for `ARCHS=xcode-standard`.
  - Use an explicit CMake architecture list only when needed, for example `ARCHS="arm64;x86_64"` or `ARCHS=x86_64`.
  - Requires a full `Xcode.app` installation; Command Line Tools alone are not enough.
  - In Xcode > Settings > Locations, set Command Line Tools to the Xcode version you want to use for generation. Equivalently, run `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
  - Requires CMake with Xcode generator support. Ninja is not required for Xcode project generation.
  - Intended for macOS Big Sur (11.0) and later by default.
  - For OS X Mountain Lion (10.8) and later, override `ARCHS` and `DEPLOYMENT_TARGET` explicitly.
  - Projects generated on 10.8+ are generally Xcode 3.2-compatible enough to be opened on Snow Leopard with Xcode 3.2.6.
  - Some newer flags may still need manual cleanup for Xcode 3.2.6, for example removing unsupported options such as `-fno-objc-arc`.
  - The generated project remains path-sensitive: source references are anchored to the path used when `gen-xcodeproj.sh` ran.
  - In practice, older Xcode versions may fail to resolve sources if the repository root folder name changes or if the project is reopened from a different absolute path after generation.
  - Treat generated `build/macos-xcodeproj/...` projects as machine-local artifacts; regenerate them on the target machine/path instead of copying them as reusable nightly-build artifacts.
  - Legacy `MAC_OS_10_4*` flags are intentionally not used here.

- `scripts/macos/gen-xcodeproj-leopard-ub1.sh`
  - Experimental Leopard/Snow Leopard Universal Binary 1 Xcode project generation path.
  - Defaults: `OSX_SYSROOT=macosx`, `DEPLOYMENT_TARGET=10.5`, `ARCHS=xcode-standard-32-64`.
  - Generates under `build/macos-xcodeproj-leopard-ub1/<build-type>-<arch-mode>`.
  - Requires a CMake/Xcode generator environment with Xcode 5.0 or newer. With the default `DEPLOYMENT_TARGET=10.5`, Xcode 10 and newer fail during CMake's initial compiler check because deployment targets below 10.9 select the removed `libstdc++`; on those hosts run this script with `DEPLOYMENT_TARGET=10.9` (clang then defaults to `libc++`) — generation succeeds and no `-fno-objc-arc` cleanup is needed, since this path already suppresses those flags. Set the final Base SDK, deployment target, and architectures in Xcode 3.2.6 as usual.
  - Make sure Xcode > Settings > Locations > Command Line Tools points at that full Xcode.app before running the script.
  - `ARCHS=xcode-standard-32-64` omits `CMAKE_OSX_ARCHITECTURES` and writes Xcode's `$(ARCHS_STANDARD_32_64_BIT)` preset into the generated project for Snow Leopard/Xcode 3.2.6 testing.
  - Use an explicit CMake architecture list only when needed, for example `ARCHS="i386;x86_64;ppc;ppc64"` on generator environments that still accept PPC architecture names.
  - Set `XCODE_ARCHS='$(ARCHS_STANDARD_32_64_BIT)'` only when overriding the default Xcode-native architecture preset; `ONLY_ACTIVE_ARCH=NO` is set by default.
  - Set `XCODE_VALID_ARCHS="i386 x86_64 ppc ppc64"` if the destination Xcode needs an explicit valid-architecture list.
  - Set `OSX_SYSROOT=macosx10.6`, `OSX_SYSROOT=macosx10.5`, or an SDK path when you need to force a specific SDK.
  - Disables Loka's explicit `-fno-objc-arc` example-target flags so the generated project is easier to open in older Xcode versions.
  - Suppresses generated `libarclite_macosx.a` link entries that are not available in Xcode 3.2.6.
  - Suppresses CMake's regeneration target so copied Snow Leopard projects do not require CMake just to build.
  - Sets `CLANG_ENABLE_OBJC_ARC=NO` for Xcode generators that understand it; Loka macOS code remains non-ARC by policy.
  - When opening in Xcode 3.2.6 for Leopard UB1 builds, manually add `ppc` and `ppc64` to Architectures if needed, set Base SDK to Mac OS X 10.5, and switch the compiler from GCC 4.2 to GCC 4.0.
  - For Tiger 32-bit UB1 builds in Xcode 3.2.6, set the deployment target to Mac OS X 10.4, use only 32-bit architectures, and choose 32-bit Intel plus manually added `ppc` when using the Xcode architecture picker. Verify the final slices with `lipo`; expected slices are `ppc i386`.
  - This path is experimental but has been build-verified after copying generated projects to Snow Leopard/Xcode 3.2.6 and Leopard/Xcode 3.1-compatible setups.

- `scripts/macos/gen-xcodeproj-lion-ub1.sh`
  - Experimental Lion-era Universal Binary 1 Xcode project generation path.
  - Defaults: `OSX_SYSROOT=macosx`, `DEPLOYMENT_TARGET=10.7`, `ARCHS=xcode-standard-32-64`.
  - Generates under `build/macos-xcodeproj-lion-ub1/<build-type>-<arch-mode>`.
  - Requires a CMake/Xcode generator environment with Xcode 5.0 or newer. With the default `DEPLOYMENT_TARGET=10.7`, Xcode 10 and newer fail during CMake's initial compiler check because deployment targets below 10.9 select the removed `libstdc++`; on those hosts this script is expected to behave the same when run with `DEPLOYMENT_TARGET=10.9` (clang then defaults to `libc++`), though that path has not been separately verified. Set the final Base SDK, deployment target, and architectures in Xcode 3.2.6 as usual.
  - Make sure Xcode > Settings > Locations > Command Line Tools points at that full Xcode.app before running the script.
  - `ARCHS=xcode-standard-32-64` omits `CMAKE_OSX_ARCHITECTURES` and writes Xcode's `$(ARCHS_STANDARD_32_64_BIT)` preset into the generated project.
  - Keeps Loka's explicit `-fno-objc-arc` example-target flags enabled, unlike the Leopard/Xcode 3.2 compatibility path.
  - Sets `CLANG_ENABLE_OBJC_ARC=NO`, `ONLY_ACTIVE_ARCH=NO`, and suppresses CMake's regeneration target in the generated project.
  - The Snow Leopard/Xcode 3.2.6 manual steps are otherwise similar to the Leopard path: select the intended Base SDK and deployment target, add/choose the needed PPC and Intel architectures in Xcode, and remove the kept `-fno-objc-arc` flags per target.

## Examples

```bash
# 10.7+ (default path)
./scripts/macos/build-10_7.sh

# 10.4 target (ppc + i386)
./scripts/macos/build-10_4.sh

# 10.4 target for one app target only
TARGET=LokaSimpleViewerMacOS ./scripts/macos/build-10_4.sh
TARGET=LokaSimpleTextMacOS ./scripts/macos/build-10_4.sh

# 10.5 target (ppc + i386 + x86_64, no ppc64)
./scripts/macos/build-10_5.sh

# UB2 target (arm64 + x86_64, macOS 11+)
./scripts/macos/build-ub2.sh

# UB2 autonomous Standalone Loop release set
./scripts/macos-standalone-release-ub2.sh

# Generate Xcode project with Xcode's standard architecture preset
./scripts/macos/gen-xcodeproj.sh

# Generate Xcode project for x86_64-only older macOS debugging
ARCHS="x86_64" DEPLOYMENT_TARGET=10.8 ./scripts/macos/gen-xcodeproj.sh

# Generate experimental Leopard/Snow Leopard UB1 Xcode project
./scripts/macos/gen-xcodeproj-leopard-ub1.sh

# Generate experimental Lion-era UB1 Xcode project
./scripts/macos/gen-xcodeproj-lion-ub1.sh
```

## Main Environment Variables

- `BUILD_TYPE` (default: `Release`)
- `TARGET` (if omitted, builds default/all configured targets)
- `ARCHS` (`;` separated, for example `ppc;i386` or `x86_64`)
- `GENERATOR` (`Ninja` / `Unix Makefiles`)
- `MAC_OS_10_4_SYSROOT` (example: `/Developer/SDKs/MacOSX10.4u.sdk`)
- `OSX_SYSROOT` (example: `/Developer/SDKs/MacOSX10.5.sdk`)
- `CC`, `CXX` (for example `gcc-4.0`, `g++-4.0` when needed)
  - For `build-10_4.sh`, prefer `gcc-4.0` / `g++-4.0`; for `build-10_5.sh`, prefer `gcc-4.2` / `g++-4.2`.

## Troubleshooting

If Xcode.app is installed but CMake reports
`No CMAKE_CXX_COMPILER could be found`, launch Xcode once, accept the license,
and install the required components. Then verify the selected developer tools:

```bash
xcode-select -p
xcodebuild -version
xcrun -find clang++
```

If needed, select the full Xcode app and complete first-launch setup:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license
sudo xcodebuild -runFirstLaunch
```

When keeping multiple Xcode.app versions side by side, avoid spaces in the app
bundle name used for legacy project generation. For example,
`/Applications/Xcode9.4.1.app/Contents/Developer` works more reliably than
`/Applications/Xcode 9.4.1.app/Contents/Developer`; the latter can make CMake's
Xcode generator fail to find `CMAKE_CXX_COMPILER` even when `xcode-select`,
`xcodebuild`, and `xcrun` appear to resolve the selected Xcode correctly. This
appears to be a generator/toolchain path-handling issue, not simply a wrapper
script argument-splitting problem.
