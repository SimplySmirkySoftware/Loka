# Loka Design Map

> **Status:** Normative router
>
> **Owns:** Where design information lives and which source is authoritative
>
> **Does not own:** Exact API signatures, implementation details, target
> availability, or subsystem behavior

This document is the shortest path into Loka's design. It is deliberately a
map, not a second architecture specification. Start with the question you need
to answer, follow only the relevant links, and confirm exact facts in code and
tests.

## Sources Of Truth

One fact should have one maintained home. Other documents should point to that
home instead of restating it.

| Kind of information | Authoritative source |
|---|---|
| Design values and reasons for choosing between plausible shapes | [PHILOSOPHY.md](PHILOSOPHY.md) |
| Repository-wide implementation and review constraints | [AGENTS.md](AGENTS.md) |
| App-facing naming and authoring conventions | [docs/API_STYLE.md](docs/API_STYLE.md) |
| Exact types, signatures, defaults, and compile-time constraints | Public headers under [`common/`](common/) |
| Current runtime behavior and failure boundaries | Implementation plus discriminating tests under [`tests/`](tests/) |
| Supported build configurations and compiler flags | [`CMakePresets.json`](CMakePresets.json), [`cmake/`](cmake/), and [`.github/workflows/`](.github/workflows/) |
| Platform support claims and verification terminology | [docs/RELEASE_MATRIX.md](docs/RELEASE_MATRIX.md) and [CONTRIBUTING.md](CONTRIBUTING.md) |
| Open work and intentionally deferred design | [docs/TODO.md](docs/TODO.md), tracked issues, and documents explicitly marked Draft |

A normative design document owns intended semantics; code and tests show the
current implementation. A disagreement is a defect or documentation drift to
resolve explicitly, not a reason to silently choose whichever source is more
convenient.

## Architecture At A Glance

```text
application intent
  Props · State handles · Flow · definition helpers · class nodes
                         |
                         v
scene definitions and composition
  Boundary / Section ownership · identity · update routing
                         |
                         v
runtime Node tree
  attach · apply · detach · retire · reclaim
                         |
                         v
platform projection
  logical nodes and live state -> native contexts and handles
```

The arrows are boundaries, not permission to skip layers. Application code
should state intent and ownership in Loka terms. Scene code turns definitions
into lifecycle-managed runtime nodes. Platform code projects those logical
facts into native behavior.

## Route By Question

| Question | Read first | Then inspect |
|---|---|---|
| Why does Loka prefer this design shape? | [PHILOSOPHY.md](PHILOSOPHY.md) | The narrow subsystem document, if one exists |
| How should ordinary app DSL read? | [docs/API_STYLE.md](docs/API_STYLE.md) | Shipping examples under [`example/`](example/) |
| Should this be a helper function, Component, or Boundary? | [docs/API_STYLE.md](docs/API_STYLE.md#choose-the-smallest-composition-form) | [`ComponentNode.hpp`](common/app/scene/node/ComponentNode.hpp), [`StdComposition.hpp`](common/app/nodes/boundary/StdComposition.hpp), and [`ComponentNodeTests.cpp`](tests/ComponentNodeTests.cpp) |
| Where should state live and who may mutate it? | [docs/ProgrammingGuide.en.md](docs/ProgrammingGuide.en.md) and [PHILOSOPHY.md](PHILOSOPHY.md) | [`common/app/scene/state/`](common/app/scene/state/) and State/Flow tests |
| How do composition and update routing work? | [docs/update_pipeline_overview.md](docs/update_pipeline_overview.md) | [`common/app/scene/composition/`](common/app/scene/composition/) and [`common/app/scene/boundary/`](common/app/scene/boundary/) |
| When do shared native resources retire and who drains them? | [docs/NativeResourceRetirementDesign.md](docs/NativeResourceRetirementDesign.md) | [`NativeResourceRetirement.hpp`](common/app/NativeResourceRetirement.hpp), [`App.cpp`](common/app/core/App.cpp), and [`NativeResourceRetirementTests.cpp`](tests/NativeResourceRetirementTests.cpp) |
| How do request delivery / queue / command / seat / settle work? | [docs/RequestDeliveryDesign.md](docs/RequestDeliveryDesign.md) | [`RequestSettlement.hpp`](common/app/scene/state/RequestSettlement.hpp) and rail request tests |
| How does a ScrollView derive its range, show its bars, and commit scrolling? | [docs/ScrollViewDesign.md](docs/ScrollViewDesign.md) | [`ScrollView.hpp`](common/app/nodes/nestable/ScrollView.hpp), [`LayoutHeuristics.hpp`](common/app/layout/LayoutHeuristics.hpp), and the rail ScrollView tests |
| Which input has keyboard focus, and who publishes it? | [docs/FocusDesign.md](docs/FocusDesign.md) | [`SceneFocus.hpp`](common/app/scene/SceneFocus.hpp), [`FocusPublisher.cpp`](common/app/FocusPublisher.cpp), and [focus tests](tests/FocusPublisherTests.cpp) |
| What are the lifetime and ownership rules? | The ownership and lifetime sections of [PHILOSOPHY.md](PHILOSOPHY.md) | Boundary, lifecycle, allocation, and ownership tests |
| How are shared bytes built, sealed, and refused? | [docs/BlobStorageDesign.md](docs/BlobStorageDesign.md) | Blob, file-read, and allocation pins |
| What applies to Classic Mac and Retro68? | [docs/retro68.md](docs/retro68.md) | [`apple/toolbox/`](apple/toolbox/), Retro68 presets, and Toolbox CI |
| What applies to macOS native code? | [AGENTS.md](AGENTS.md) and [docs/environments.md](docs/environments.md) | [`apple/macos/`](apple/macos/) and macOS CI |
| What applies to Win32 text, paths, and native windows? | The Win32 rules in [AGENTS.md](AGENTS.md) | [`win32/`](win32/) and the Win32 bridge/path tests |
| What must a PR claim and verify? | [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/ReviewPolicy.md](docs/ReviewPolicy.md) | Presets, CI workflows, and the PR's exact diff |

The [Programming Guide](docs/ProgrammingGuide.en.md) is the broad tutorial
reference. It does not replace the headers for exact API facts.

## Document Status And Scope

New design documents should begin with a compact ownership block:

```text
Status: Normative | Guide | Draft | Historical
Owns: The semantic decisions maintained here
Does not own: Facts that remain authoritative elsewhere
Code truth: The implementation entry points
Verification: The tests or checks that discriminate the contract
```

- **Normative** documents describe an intended current contract.
- **Guide** documents teach a supported path but are not exhaustive contracts.
- **Draft** documents are proposals or unfinished reshaping work.
- **Historical** documents explain why a superseded shape existed.

A filename ending in `Draft.md` should be treated as Draft unless its own
status block says otherwise. Before implementing from a Draft, compare it with
current headers, tests, and tracked work.

## Maintenance Rules

- Update an owning document in the same change when its semantic contract
  changes.
- Do not copy exact signatures, default values, target matrices, test counts,
  or compiler flags into design prose. Link to their maintained source.
- Prefer links to compiled examples over large Markdown code samples. If a
  sample must be normative, make it part of a build or test.
- Generate mechanical inventories when they become useful; do not maintain a
  second handwritten list beside code.
- Link to files and stable symbol names rather than line numbers.
- Keep this map small. Add a route when a recurring question has a clear
  authoritative destination; do not summarize the destination here.
