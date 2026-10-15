# Contributing to Loka

Thank you for contributing.

## Scope and Compatibility

- Library/core compatibility targets macOS Tiger through Snow Leopard.
- Consumer apps are expected to run on Big Sur and newer.
- Any feature PR must clearly state the supported OS range.

## Required PR Information

Every PR must include:

- Change summary.
- Supported OS range for the change (for example: `10.4-10.6`, `10.7+`, `11+`).
- Verification type for each claimed platform: `build-verified` (compile/link passed) or `runtime-verified` (app launched and behavior checked).
- A `Review risk profile` produced from the complexity gate in
  [docs/ReviewPolicy.md](docs/ReviewPolicy.md#complexity-gate), including an
  explicit zero when no flag is triggered.

## Testing Requirements

Every change follows the
[verification policy](docs/ReviewPolicy.md#verification-policy-for-bug-fixes-and-refactoring)
in `docs/ReviewPolicy.md` and records its evidence in the PR. That section is the one
statement of what the tests must show; no workflow measures or gates a coverage
percentage.

For logic-heavy changes:

- Add or update unit tests.

For UI changes:

- Implement an autopilot/test-driving path for the changed flow (timer/state/event-driven is acceptable).
- Attach visual evidence (capture or recording) showing the expected result.

## Compiler Warning Policy

Every repository-owned compiled target uses the warning floor defined by
`cmake/LokaWarnings.cmake`:

- GCC, Clang, AppleClang, and both Retro68 GCC toolchains: `-Wall -Wextra`.
- MSVC: `/W4`.

Warnings stay target-local so platform SDK and future third-party targets do
not inherit Loka's policy. Repository configure presets additionally enable
`LOKA_WARNINGS_AS_ERRORS`, producing `-Werror` or `/WX`; an ad hoc CMake
configuration defaults to warning-only unless that option is enabled. The
presets are the entry points of the required status checks (the default
branch's `Main Protection` ruleset), so those checks inherit the same warning
floor and cannot pass with new warnings.

One narrow waiver remains for GCC 11 and newer:
`-Wmismatched-new-delete` is disabled on Loka targets because
`Node::operator delete` intentionally serves arena and plain-new storage while
the allocation-gate path uses `DestroyHeapNode`. The compiler cannot prove the
current call-site discipline. Issue #175 owns the provenance fix and removal of
this waiver; no other GCC/Clang warning category is disabled.

MSVC C4458 is disabled on Loka targets. Its current sites are the repeated
`Node::context`, `BoundaryCompositionState::diff`, and
`WindowProps::width`/`height` vocabulary used by parameters or short-lived
locals. Those names make the cross-layer contracts consistent, member access
is already explicit, and renaming them would not strengthen correctness.

MSVC C4996 is also disabled for the portable `fopen`/C stdio sites in
`BlobLoader`, `SimpleViewerFlowAdapters`, and their tests. Replacing them with
MSVC-only secure CRT calls would break the shared C++98/Classic implementation;
Classic paths intentionally avoid iostreams for binary size.

Retro68 68K and PPC accept `-Wall`, `-Wextra`, and `-Werror`; no baseline flag
is dropped for either Classic compiler.

## Objective-C Rules (Library/Core)

- Library/core Objective-C(++) code is non-ARC.
- `@property` / `@synthesize` are allowed.
- Explicit ownership semantics are required (`retain`, `assign`, or `copy`).
- Direct ivar access (`obj->ivar`) is forbidden.
- Internal state access must go through private getter/setter methods.

## Build Entry Points (macOS)

- `scripts/macos/build-10_4.sh`: Tiger/Leopard compatibility path.
- `scripts/macos/build-10_7.sh`: Lion and newer path.
- `scripts/macos/build.sh`: shared internal driver.

## Retro68 Toolchain Location

If your Retro68 toolchain is not auto-detected (under `~/Retro68-build` or
`~/Retro68`), follow `docs/retro68.md`: copy `.env-retro68.example` to the
ignored `.env-retro68` and set `RETRO68_BUILD_DIR`. VS Code tasks and
`scripts/retro68-cmake.sh` still use the repository presets, which keep the
Classic size/flag policy (`-Os`, `-fno-exceptions`, `--gc-sections`, and the
compact/diagnostic split — see #135/#136/#137) identical on every host.

## Commit and Review Hygiene

- Keep commits small and scoped.
- Do not amend commits except immediate tiny follow-up fixes.
- Prefer compile-time guarantees over runtime checks where practical.
