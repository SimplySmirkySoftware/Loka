# Loka TODO

## Highly recommended

- **#642 tracker entry growth (2026-09-10)**: an unreserved tracker keeps its
  first registration as a single row and grows by 16-row chunks afterwards, so
  MineSweeper's 64 singleton owners no longer pay a 16-row array each (about
  8 KB) while multi-state owners stop allocating per registration. Allocation
  counts scale the ROM `_NewPtr` cost linearly; the quadratic part (one heap
  walk per `NewPtr`) is the Classic small-object allocator design tracked on
  #642, not further slices.
- **#642 inline first tracker row (2026-09-10)**: PushStateTracker owns its
  first registration row inline, so a singleton owner registers with zero heap
  allocations (MineSweeper production-root Null census: 130 fewer calls). The
  tracker is non-copyable (an embedded row would alias on copy). Scene mount on
  a 68020 is still dominated by the per-call cost of ROM `_NewPtr`; that is the
  Classic small-object allocator design on #642.

- **#567 E1 scope / review risk (4 flags)**: State/Boundary/Platform span, parent-owned live inputs passed to child props, layout dirty routing changes, and State-or-value selection. E1 deliberately adds only Stack axis and Box width State props and migrates HelloWorld/SmirkBench; notification/seat pins and mutations cover the changed routing. E2 supplies Keyed seats for MineSweeper; E3 removes the recompose base. Derived layout NodeStates remain explicit app-owned plumbing until a separately designed derived-State helper can absorb them. The Null controller retains fixture viewport input across notifications; no production scheduler or lifetime protocol changes.

These items address recurring bug patterns and structural risks identified during recent bugfixes (ConditionalDefinition dangling pointer, Mac platform context preservation, startup redraw).

- **0.0.1 release sanity pass**: Before tagging, exercise Tutorial and the main examples as user flows, verify the intended macOS/Win32/Toolbox sample paths, review README quick-start wording, and confirm `LICENSE.md` copyright holder text.
- **Node state batch API follow-up**: Revisit whether `ComposableNode::declareStates(count)` and the `NodeComposition::declareStates()` batch should share more implementation, and whether initializers larger than the inline storage bytes should be supported. Consider a compile-time inline capacity (e.g. `declareStates<N>()`) so batches known to exceed one page can avoid the overflow heap allocation entirely. Keep `this->state(...)` as the simple default for ordinary small state sets.
- **Growable StateArena for multi-batch owners**: `StateArena::reserve` is once-per-owner, so only the first batch in a boundary compose gets arena locality; later batches and per-state declarations from other nodes under the same owner silently fall back to per-state heap allocation. The batch page-chaining fix covers overflow within one batch, not multiple batches per owner. A chained-block arena keeps the one-shot reserve API while extending locality to the whole compose; deferring or scoping declarations instead would break the "state is usable right after its declaration statement" contract.
- **Definition ownership clarity**: `clone()` returns raw `new`-ed pointers across 19+ call sites with implicit ownership. Introduce a lightweight `OwnedDef<T>` wrapper (C++98-compatible) so ownership intent is visible in code. Priority: prevents the same class of bug as the ConditionalDefinition fix.
- **Module decomposition / build parallelism**: Split common/platform/example code into clearer CMake targets/modules so Ninja/CMake parallel builds scale better and old machines avoid unnecessary recompilation. Treat build-time improvement as part of the broader module-boundary cleanup; current modern hosts are fast, but G4-era builds can take minutes when dependencies are too coarse. First step: introduce a shared `LokaCommon` target instead of linking `common/*.cpp` directly into every platform/test target. Then split along namespace/module boundaries (`LokaCore`, `LokaDsl`, `LokaApp`, later `LokaMultimedia` / `LokaGameExperimental`) once dependencies are explicit enough.
- **Platform Controller layout共通化**: `layoutNode()` is near-identical across Mac/Win32/Toolbox (~2000 lines each). Extract platform-independent layout traversal and size calculation to shared code; platform-specific parts (NSView/HWND/QuickDraw context creation) become overrides. Priority: bug fixes (like removing `clearContexts()`) currently need manual porting to 3 implementations.
- **Layout helper contract polish before v0.1.0**: The shared container helpers now live under `common/app/layout`, but their return-value semantics and callback shape still need tightening before the layout seam feels stable. Clarify whether each helper returns the next vertical position or the occupied lower bound, introduce a named child-layout callback/traversal type if it improves readability, and add focused tests for `Box`/`Column`/`Row`/`Grid`/`ZStack` edge cases before promoting the layout handler seam as the normal extension path.
- **Scene trunk readability before v0.1.0**: `Boundary.hpp`, `Node.hpp`, `Scene.hpp`, and `SceneDirector.hpp` are the main scene runtime files. They can stay powerful, but their responsibility boundaries should be obvious before wider platform/control expansion. Prioritize API comments, ownership notes, and low-risk header splits around existing responsibilities such as boundary state/compose/apply details, node definition contracts, scene update orchestration, and projection transaction flow. Continue moving update planning, transaction analysis, and apply-plan decisions out of `Scene` and into `SceneDirector` where that makes `Scene` remain the owner/mount/invalidation shell rather than the policy engine. Avoid cosmetic churn and keep behavior changes covered by existing scene/flow tests.
- **Portable platform controller tests**: Mac/Win32 layout/context tests only run on their native platform. Abstract layout calculation into a testable interface so core layout logic can be verified on Linux CI.
- **Composition event tracing**: Add `LOKA_TRACE_COMPOSITION` macro to log ATTACH/UPDATE/DETACH transitions in the static boundary/scene compose path. Disable on Retro68. Priority: reduces manual printf debugging during composition bugs.
- **Scene storage policy seam**: Introduce a small C++98-friendly list/storage abstraction for retained scene internals that can use `std::vector` on modern builds and intrusive linked lists on 68k-sensitive paths. Keep the interface inline/template-based where practical so the abstraction does not add runtime dispatch to hot paths.
- **Dialog/window ownership draft**: Turn [DialogWindowDraft.md](DialogWindowDraft.md) into a concrete direction for App/Window-owned dialog presenters, Scene-side requests/results, `OpenFileDialog` migration, and future alert/message dialog support before adding more ad hoc native dialog helpers.
- **PlatformContext responsibility split**: `PlatformContext` is a historical platform facade and should not grow into a catch-all factory. Before adding dialog/window/resource creation paths to it, move toward a `RunApp(AppDefinition)` model where App/Window/Dialog/Node/Resource declarations bind to platform implementations through declaration-specific seams or registries. Missing platform support should fail through explicit missing binding/signature diagnostics instead of implicit runtime behavior. Target this cleanup early after `0.0.1` (`0.0.2`/`0.0.3` range).
- **Mutability/resource value draft**: Turn [MutabilityDraft.md](MutabilityDraft.md) into concrete rules for immutable completed values versus explicit mutable builders/buffers, especially for `Blob`, `Image`, decoded representations, owner-aware copy-on-write, and Boundary-local mutation.
- **Scene projection transaction draft**: Turn [SceneProjectionTransactionDraft.md](SceneProjectionTransactionDraft.md) into concrete tests and implementation cleanup for next-tick Boundary update coalescing, parent-structure-dominates-child updates, child-layout-affects-siblings relayout, and local paint fallback behavior. Include a `LeftRightResize`-style sample/test in the `0.0.2` timeframe.
- **Conditional lifetime policy follow-up**: `BoundarySection` now provides the explicit subtree-local owner scope and resolves state/Held ownership nearest-section-first. The remaining work is the default `Show`/conditional behavior. Current `ConditionalNode` retains inactive branches; for map/list-style UIs the likely default should be destroy-on-detach, with any retained/keep-alive policy exposed explicitly rather than hidden.
- **0.0.2 interaction-pattern validation**: Add a small converter-style sample and a minimal `Slider` control to exercise bidirectional inputs, continuous input, validation, quantized updates, and Flow/State interaction without relying on shared mutable state. Use this sample to decide the first official patterns for `ControlledState`-style bindings, read-only inputs plus change events, result-only state, and owner-side update application.
- **State/Flow update gates**: Provide official building blocks for common interaction hazards instead of leaving every app to invent mutable flags. Likely first pieces: distinct-until-changed, quantize/fixed-point value helpers, throttle/debounce gates, stale-result dropping, and explicit apply-to-owner-state adapters. Continuous input should publish comparable/quantized values by default; raw floating-point intermediates should not be fed back into UI state unless an equality policy is explicit.
- **0.0.3 checked State updates**: Turn [StateUpdateResultDraft.md](StateUpdateResultDraft.md) into explicit `trySet`/checked transaction APIs and Flow failure adapters. Keep ordinary `set()` lightweight, but provide typed success/failure results for bidirectional inputs, continuous controls, owner-side apply, stale result dropping, and StateTracker transaction failures.
- **State interaction groups and loop diagnostics**: The current internal infinite-update limit should become inspectable before `v0.1.0`. Add debug/test-only tracing for the state, tracker phase, bind/defer/Flow step, notification depth, and interaction group that caused the limit to trip. For bidirectional or mutually dependent updates, provide an explicit group/transaction API that declares which inputs may update which outputs, detects cycles in debug/test builds, and supports an `onUpdateLoopError`-style hook. Prefer declared interaction graphs and explicit phases over app-local `updating_` flags, hidden version counters, or shared mutable refs.
- **SceneManager transaction follow-up**: Revisit `SceneManager` on a separate branch after the first Scene transaction lifecycle pass lands. Align scene switching/pending transition ownership with the newer Scene/Projection transaction model instead of broadening the current refactor.

## Open

- Register `smirkbench/text-editor-plain` on the desktop scenario runners so the settle-trace audit is golden on Win32/macOS too (source: [PR #894 body](https://github.com/cubenoy22/Loka/pull/894)).

- Input-shaping facilities for bidirectional and continuous inputs (an interaction group, a Flow gate, distinct/quantize/throttle/debounce policies, stale-result rejection, and a stall/resume rule for hidden nodes) are an idea carried over from the archived Japanese guide, not an API; they stay here until a concrete input case needs one.

- **Window placement type split**: `Window` currently carries a platform
  desktop-placement `x`/`y` and logical content `width`/`height` in one
  `Frame`. Before mobile/windowing families or explicit unit overloads make
  that distinction app-visible in more places, introduce a small placement
  value (or equivalent overloads) that names desktop position separately from
  logical size without changing local UI `Frame` semantics. See #289.
- Toolbox button release semantics differ between examples (#324 close-out): ScrapbookUI's Next fires only when the mouse-up lands inside the control (release-inside tracking), while MineSweeper's New Game fired even when a scenario warped the cursor away in the release instant. Both go through the same ButtonNode path, so something downstream diverges. Benign today, but worth locating the divergence before more Toolbox controls are added.
- Define Modifier system (Text style + Window sizing) and wire through WindowProps/Layout.
- Decide default window size (macOS/Win32/Toolbox) and unify hardcoded values.
- Sample: MyTracker (editable device spec catalog).
- Audit remaining `std::string` usage and keep only intentional interop edges (UTF-8/platform bridge paths).
- String comparison fast paths: keep the current pointer-identity shortcut, and consider empty/length/hash metadata fast paths only if profiling shows `loka::core::String::compare()` or sorting as a real cost. Prefer cached platform-string metadata over making the `String` value wrapper heavier.
- Declarative OpenGL game foundation (mainline): scene/update/render flow, resource model, and platform bridge boundaries.
- Sprite path (Toolbox/Win32) as lightweight compatibility backend, not the primary game direction.
- Pseudo-view sprite path: for lightweight game/decorative nodes, prefer one retained root native view plus scene-managed pseudo-views/sprites instead of one native subview per sprite. Keep `RectSurfaceNode`/similar nodes as normal scene nodes and push backend-specific diff/draw optimization into `NodeContext`.
- `RectSurface` direction: do not force it through the same generic handler-migration path as `Button`/`Text`/`Cell`. Revisit it as a possible `RealTimeComposition` / continuously-updated surface family with its own redraw, diff, and scheduling contract.
- **Alternate composition strategy gate**: keep `StdComposition` as the only normal scene UI strategy for now. Do not reintroduce a dynamic/reconciliation-style composition path until a concrete domain such as games, animation, video tools, or real-time surfaces proves that different timing, redraw, resource reuse, or scheduling rules are needed. If such a strategy appears, isolate it behind a `Boundary` and keep State/StateTracker/dirty-result/lifecycle contracts compatible with sibling and parent Boundaries.
- **68k-focused Boundary/profile**: investigate a separate ultra-light Boundary/profile for 68k-class targets before changing the normal scene UI model for performance alone. It may sacrifice diagnostics, convenience, or generality to reduce memory, code size, retained metadata, and hot-path overhead, but it must keep the same ownership, StateTracker, dirty-result, and lifecycle contracts so it can coexist with ordinary `StdComposition` Boundaries.
- **Node definition internal payload seam**: investigate a `NodeDslPayload` / definition-internal payload layer that can be shared by `NodeDefinition` and the materialized `Node`, separate from app-facing `NodeProps`. This could reduce clone/copy/allocation overhead and provide an escape hatch for 68k profile storage policy, arena allocation, retained metadata trimming, and debug/test metadata stripping without changing the public DSL. Runtime state, native contexts, layout/dirty state, and observed bindings must remain Node/runtime-owned, not shared payload.
- **Template materialization model**: introduce the design vocabulary for templates that materialize 0..N runtime node instances from an input. Start from `Show`/conditional rather than `For`: first prove 0/1 and 1-of-2 subtree materialization, retained-vs-destroy policy, owner scope, and instance identity. Then extend the same model to named switch branches, sorted/keyed `For`, index/key slots, and per-item props/state storage. This should clarify where `NodeDefinition`/`NodeDslPayload` can be shared and where runtime Node state must be per materialized instance.
- Event loop + tick design for game-style rendering (separate from UI updates), aligned with the declarative game path.
- Window close request: delegate to Scene/Root.
- Internal `loka::core::Managed<T>` value-plumbing circular reference patterns (Group/Weak or one-way ref policy). This is not the app-facing ownership model; state/resources should keep a meaningful owner, and subtree-local passive resources use `Held<T>` owner slots.
- MutableState notification timing: micro-tick end vs immediate; document or change.
- Migrate legacy definition-clone seams that still cannot report allocation
  failure explicitly. Current examples: `NodeComposition::declare*()`,
  `Scene(const DefT &)`, and constructor-only clone paths that can still blur
  "empty" with "clone failed". Keep pointer/bool-returning seams as the normal
  nullable/OOM surface for `0.0.x`. Audit concrete clone/create implementations
  that still use plain `new` (including constructors that allocate internally)
  before claiming end-to-end OOM recovery in no-exception builds.
- DerivedNodeState follow-up: derived-to-derived chains (need dependency-ordered settlement) and a batch declaration door.
- Boundary lifecycle policy follow-up: current default is scene/tree-owned rather than native-view-owned. If a future opt-in `view-scoped` boundary lifecycle is needed, define it explicitly as a separate policy instead of overloading `detach`/visibility semantics.
- Wire Node.dirty with IPlatformController::synchronize (diff-based redraw path).
- Smart redraw scheduler at Window/Scene scope: collect dirty rects per tick and flush once via platform invalidate APIs.
- Win32 redraw policy follow-up: keep `Text`/`Cell` immediate for correctness for now, but revisit a clearer control-type policy once transparent text/custom child repaint contracts are documented.
- Win32 XP flicker follow-up: XP still shows noticeable redraw flicker even after recent state/update cleanup. Reuse the current `win32-redraw` instrumentation first and classify whether the dominant cost is root `WM_ERASEBKGND`, child HWND paint churn, or broad queued invalidation before attempting any new redraw abstraction.
- Toolbox/68k redraw policy: replace direct `drawDirty` calls with `markDirty` accumulation, then flush once in `updateEvt` (`BeginUpdate/EndUpdate`) using `InvalRect`/`InvalRgn`.
- Props in/out pattern for `State<T>*` and `EmitterState*` props (bidirectional vs one-way).
- requestDiscard protocol for save/confirm flows via EmitterState.
- Menu architecture status: treat the current Menu/App menu implementation as provisional. Clarify platform convention ownership before promoting the API: macOS/Classic Mac use an application menu bar, while Win32 attaches native menus to windows. Keep App-wide `MenuBarDefinition`/`MenuCompositionDiff` usage limited to the default/app menu snapshot for now, and avoid building future Win32 per-window menu diff/apply behavior on a single App-owned diff state.
- DSL definition lifetime safety: `ConditionalDefinition` now owns cloned trueDef/falseDef (fixed). Remaining: audit other definition types for similar raw-pointer-to-temporary patterns; see "Definition ownership clarity" in Highly recommended.
- Manual wiring pitfalls: explicit registration/findBoundary-style coordination is Loka-like but easy to miswire (for example, registering the wrong boundary instance or forgetting a required hook). Add small helper APIs, debug asserts, and example/platform smoke tests for high-risk manual paths such as freeze targets, boundary-local callbacks, and owner registration.
- Suppress clangd incomplete type warning for AttachedContext -> BoundaryNode access (include or type split).
- C++98: down-port tests/SceneTests.hpp (no lambda/auto/override) or exclude for legacy builds.
- C++98: reintroduce NodeComposition map/filter via C++98-friendly adapters.
- Toolchain matrix: validate oldest MSVC/GCC and record gaps.
- **macOS test target `ALL_BUILD` policy**: Replace the current Debug-versus-Release/multi-config proxy with an explicit legacy/retro build profile. Modern macOS builds should include `LokaTestsMacOS` in `ALL_BUILD` once the test target keeps assertions active in optimized configurations; Xcode 3.2.6 and legacy SDK builds may exclude it to avoid unnecessary rebuild cost and unsupported test-toolchain surface.
- Legacy Mac build automation idea: document or prototype developer-side remote build workflows for old macOS hosts. Keep this outside Loka's runtime/core responsibilities: a modern development machine or AI assistant could sync the repository to a Snow Leopard build node over SSH, run `scripts/macos/build-10_4.sh` / `build-10_5.sh`, collect `lipo -info` output and logs, and leave Xcode 3.2.6 UI workflows as a manual or semi-automated verification path.
- Classic Toolbox profile split: keep app-facing String/Asset APIs neutral, but plan build-time profiles such as `ToolboxEN`, `ToolboxJP`, and an `iToolbox` Mac OS 8.5+ profile rather than forcing pre-8.5 Classic localization through a single Unicode-first path.
- Docs/tests: document C++98 constraints and add checks for accidental C++11 usage.
- Cleanup staged work from earlier C++98 retrofit (split/rebase if needed).
- DSL shorthand ideas: direct props overloads (Text("...")), direct State props (EditText(State*)), optional prepare/compose merge, namespace alias, Fragment helper.
- C++98 chain-entry helper idea: add a thin wrapper/helper for DSL/Stream/Flow entry points so callers can keep chaining without spelling long intermediate types when `auto` is unavailable. Keep it narrow and purposeful rather than a broad "wrap anything" abstraction.
- Decentralize node-type dispatch: replace the single `NODE_KIND` + `asXxxNode()` + `PlatformController` switch concentration with a registration-based context factory model.
  Stage 1: split context creation from `PlatformController::layoutNode()` by introducing per-platform node-context mappers/factories (Toolbox-style) for Win32/macOS.
  Stage 2: migrate leaf nodes first (`Button`, `Text`, `ImageView`, then `EditText`/`PopupMenu`/`Cell`/`OpenFileDialog`) so context ensure/reuse and leaf-specific sizing move out of the controller without changing behavior.
  Projected layout naming/follow-up: `IProjectedLayoutNode::layoutProjected()` currently means "prepare platform/native projection, then continue layout" and is easy to misread. Revisit whether platform controllers should own the prepare+layout sequence directly, and either rename the seam (`IPlatformHandledLayoutNode`, `layoutWithPlatformProjection`, etc.) or collapse it into the node-handler path once leaf layout migration stabilizes.
  `RectSurface` is not on the same track anymore: treat it as a candidate for a dedicated `RealTimeComposition`-style family rather than another generic retained-control leaf.
  Stage 3: extract shared container traversal/layout (`Column`/`Row`/`Grid`/`Box`/`ZStack`) into common code, keeping boundary bounds updates and platform-local ownership hooks intact.
  Stage 3.5: once shared container helpers stabilize, define a separate layout-extension seam for external/custom layout providers rather than forcing container traversal into the node-context handler API.
  Note: Win32/macOS now also have an internal `PlatformLayoutHandlerRegistry` seam and built-in layout handlers for `Box`/`ZStack`/`Column`/`Row`/`Grid`; Toolbox can follow later without changing the public controller contract.
  Stage 4: make registry/factory lookup the primary path and keep `NODE_KIND`/`asXxxNode()` fallback only where 68k-sensitive builds still need the cheaper/static dispatch path.
  Long-term goal: remove enum/switch dispatch and most `asXxxNode()` paths outside 68k-sensitive builds while preserving user-defined component extensibility.
- Introduce `loka::multimedia` layer for codec/media responsibilities (ImageDecoder/Audio/Video), keeping `app` layer UI-only. Platform contexts should call multimedia abstractions instead of embedding QuickTime/AVFoundation/Win32 decode logic directly.
- ImageView rendering policy: keep platform contexts on custom drawing paths (NSView/GDI/Toolbox) and avoid tying behavior to NSImageView-specific features for cross-platform parity.
- ImageView scaling contract: `fit` mode handling aligned on Win32/macOS (`NONE/CONTAIN/COVER/STRETCH`); Toolbox now has ImageView render path (frame + loaded-state overlay), but native image blit from `Image::nativeHandle` is still pending.
- Classic Toolbox image roadmap: move JPEG/PNG decode through QuickTime-backed multimedia decoder, then feed `Image` as normalized native handle (PICT/GWorld policy decided in multimedia).
- SimpleViewer (Toolbox): add optional QuickTime decode path via `loka::multimedia` and surface explicit error codes for `QT_UNAVAILABLE` / `QT_DECODE_FAILED` when fallback PICT decode cannot render.
- Win32 image backend split plan: keep legacy GDI path for XP/2k compatibility and add capability-based modern path (WIC/high-DPI aware) without OS-name hard forks.
- Win32 compatibility note for planning: treat XP/2k as legacy baseline (build-verified target) and Vista+ as modern-capability tier; record per-feature support in the matrix.
- Flow DSL use-case validation: keep `Step(id, adapter)` focused on bounded-lifetime async/transform pipelines and verify readability/testability.
- Flow DSL composition hygiene: avoid one-step flows by default; split into 2+ steps at meaningful boundaries (transform/decision/side-effect) or use a plain function when flow orchestration value is absent.
- Flow DSL nesting/composition: design `RunFlow(child)` or equivalent child-flow invocation so parent scenarios can keep coarse-grained steps while low-level checks stay encapsulated in reusable subflows.
- Flow DSL combinators: revisit logical `all` / `race` after child-flow support lands; prioritize test/scenario semantics (grouped checks, timeout-vs-event wait) over concurrency.
- Flow DSL runtime quirk: investigate cases where adding `FlowChain::onFailure(...)` changes outcome/stability of otherwise equivalent scene tests; likely shared/clone/runtime-step state handling around failure callbacks.
- Platform apply taxonomy: revisit `PAINT_COMPOSITED` classification. A simple `ZStack`-presence heuristic was not sufficient even when the composed tree clearly contained `ZStack`; likely needs boundary-local paint/opacity metadata rather than scene-side structural guessing.
- Flow DSL use-case validation: add video encoder stub scenarios (Qt / AVFoundation / Windows API style) for `open -> frame push -> finalize` and failure-path coverage.
- Control components roadmap: add constrained `For` (static/fixed list) after `Cond`/`ShowIf`, while keeping platform `#if` isolated in capability/platform implementation layers and avoiding unnecessary 68k runtime loop costs.
- Conditional visibility follow-up: retained attach/detach is the baseline for `Show()` now. Keep loop/list reuse semantics and shared reuse pools out of scope until dedicated `For`/list DSL work lands.
- Conditional lifecycle API follow-up: split the user-facing meaning of conditional UI into explicit primitives such as `AttachIf` / `VisibleIf` or an advanced `MountIf(cond, policy)`. The key design axis is not just whether a child is visible, but whether it participates in the logical tree/projection and whether detached logical/native resources are retained or destroyed. Loka should keep the beginner path simple while still exposing this manual-control surface cleanly for multi-OS native behavior.
- Conditional policy follow-up: even if a future `DynamicComposition` restores plain `if (...)` authoring, structure diffs alone are not enough. Conditional UI still needs an explicit detached-lifecycle policy marker (`retain detached`, `destroy detached`, future reuse pool, native retention policy) so logical absence and cleanup semantics do not stay implicit.
- Cond/ShowIf remaining coverage: fill the gaps beyond the current basic branch-switch tests, specifically default/otherwise branches, nested evaluation order, cleanup/destructor behavior on branch replacement, and platform-selection stubs without DSL-side `#if`.
- Layout alignment tests: validate `VStack` horizontal alignment and `HStack` vertical alignment defaults/overrides with deterministic node bounds.
- Text overflow tests: validate `BlockStyle` wrap/truncation (`none/word/char`, `none/clip/ellipsis`) under constrained width and confirm `isClipped` does not replace text overflow policy.
- Toolbox wrapped-text relayout behavior: runtime-verify that content-change triggers relayout correctly (build/test path implemented).
- Row alignment measure pass currently does a 2-pass scan when vertical alignment is enabled; keep as-is for now, revisit for large-child optimization if profiling shows cost.
- State scheduler idea: add `NextEventTracker` (next-event-cycle flush, setTimeout(..., 0)-like batching) to coalesce rapid `State::set()` bursts on Main Thread.
- Style follow-up: newer boundary state helper headers (`BoundaryStateTypes.hpp`, `BoundaryRuntimeState.hpp`, etc.) still mix direct member access and `this->`; align them with the repository-wide `this->` preference during future cleanup passes.
- C++98 RAII follow-up: `BoundaryComposePhaseScope` / `BoundaryApplyPhaseScope` still rely on copy-transfer via `mutable` + `const_cast`; keep usage narrow and revisit if a simpler non-copying scope pattern becomes practical.
- Testing surface policy follow-up: keep production fields private where possible and expose snapshot/transaction introspection through dedicated testing access helpers instead of widening normal app-facing APIs.
- Animation scheduler: keep it separate from dirty routing. Use a lightweight frame/timeline scheduler that advances animation state, then let existing `State -> dirty` routing handle `PROPS/LAYOUT/CHILD`. Start with an active-animation list plus `NextTickTracker::request(delayMs)` rather than a generic global queue.
- Motion architecture note: keep `attr()` static and model time-based behavior in `Flow`/`State` (`TimeController`, timeline/Frame/delay chain). Backends resolve execution quality (GPU interpolation vs CPU step/invert fallback) via capabilities.
  - Sketch:
    `TimeController(ctrlState)`
    `  | Step(STEP_BLINK_ON, Frame(InvertRgn(target)).delayMs(1000))`
    `  | Step(STEP_BLINK_OFF, Frame(InvertRgn(target)).delayMs(1000))`
    `  | onSuccess(Handler::noop, STEP_BLINK_ON);`

## Completed (recent)

- Sample: FloppyBird (FlappyBird clone) implemented under `example/FloppyBird` (game logic, RectSurface-based main node, Retro68 path).
- Update/apply optimization pass (2026-03): archived in [docs/archives/update_pipeline_optimization_2026-03.md](archives/update_pipeline_optimization_2026-03.md).
  Highlights:
  - scene-level `fullRebuild` accuracy improved for child-dirty and mixed dirty paths
  - localized platform apply routing (`PlatformApplyPlan` / `BoundaryLocalApplyInfo`) is now the main redraw contract
  - Win32 interaction flicker and Toolbox follow-up redraw behavior were reduced by platform-local fixes
- Toolbox redraw profiling support: HelloWorld can dump/reset Toolbox debug stats to timestamped files for local interaction analysis.
- Support matrix in README: split compatibility claims into `build-verified` and `runtime-verified`.
- Menu bar support implemented across macOS/Win32/Toolbox app layers.
- MenuItemAttr `visible` is evaluated during menu build. A runtime visibility toggle needs a new Scene bar declaration (Keyed-type seat, menu rally page 3); the App default bar is composed once at bootstrap and never rebuilt.
- ConditionalDefinition/ConditionalNode and `NodeComposition::conditional(..., node)` default false/Empty path implemented.
- BoundaryNode owns StateTracker; useState auto-registers; Context API removed; RootBoundaryWrapper in Scene; DSL naming cleanup.
- `VStack/HStack` alignment props are wired into platform layout engines (Win32/macOS/Toolbox), including remaining-height handling for `VStack + ImageView(FILL_PARENT)`.
- `BlockStyle` overflow (`wrap`/`truncation`) is wired into native text contexts (Win32/macOS/Toolbox with low-memory-safe clip fallback).
- Boundary memory ownership tranche (#36): `BoundarySection` provides explicit subtree owner scopes, state resolves nearest-section-first, `Held<T>` records named owner slots instead of anonymous app-facing reference counts, detach queues last-drop release on the owning clock, and `OwnershipDump` renders the live owner tree.
- ProgrammingGuide boundary/state pass: the guide explains the boundary-first model explicitly, including `this->state(...)` as the normal small Node-local owner path, `declareStates()` as a batch/Boundary-scoped path, `currentBoundary()` for owner-side access, `findBoundary()` for direct-parent borrowed access, `BoundaryProps` as parent-to-child input, `Held<T>` for owner-scoped passive resources, and repositories/caches for unrelated long-lived sharing.
- Scene local diff first pass: retained native contexts now survive local replace/reorder paths across generic/macOS/Win32 tests, retired subtree cleanup has a platform seam, and boundary-local rebuild planning is separated from apply.
- Scene `fullRebuild` accuracy is no longer root-only in the old sense; child-dirty and mixed dirty cycles now use boundary/root diff results to downgrade more aggressively when structure work is not required.
- Scene transaction lifecycle first pass: projection targets are grouped in `SceneProjectionTransaction`, update request/apply facts are captured as snapshots, transaction internals are hidden from normal app-facing APIs, and test-only inspection goes through `SceneTestAccess`.
- Show/OpenFileDialog retained lifecycle baseline: `Show()` uses retained attach/detach semantics for the `0.0.1` non-loop scope, and OpenFileDialog result delivery is guarded against stale callback/lifetime hazards across the supported platform paths.
