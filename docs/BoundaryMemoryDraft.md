# Boundary Memory Draft

> **Status:** The owner-slot `Held<T>`, `BoundarySection`, nearest-owner state
> resolution, and ownership-dump portions were implemented by
> [#36](https://github.com/SimplySmirkySoftware/Loka/issues/36). This file preserves the
> broader design history and still-open directions; it is not the current API
> contract. See [PHILOSOPHY.md](../PHILOSOPHY.md) and the
> [English Programming Guide](ProgrammingGuide.en.md) for the canonical
> ownership model.

This draft captures the intended direction for Loka's more complete memory and
resource ownership model. It is not a stable API contract yet.

## Goal

Loka already treats `Boundary` as the owner for state, Flow, composition, and
local resources. That ownership must become strong enough that applications can
build larger document and media models without scattering `new` / `delete`,
hidden reference counts, or app-local lifetime flags across the codebase.

The target model is:

- `Boundary` owns state, Flow, and boundary-local resources.
- Memory ownership should be readable from the declarative structure. App,
  Window, Scene, Boundary, BoundarySection, and Node form the visible ownership
  tree rather than hiding owners behind unrelated runtime objects.
- Domain root objects such as `Movie`, `Document`, or `Project` can be held by a
  clear owner such as a `Boundary`, Repository, or ApplicationScope.
- Internal allocations inside a domain object remain normal C++ ownership unless
  there is a concrete reason to make them Loka-managed.
- Loka provides enough owner-aware handles that applications do not have to
  invent ad hoc reference counting or delayed deletion patterns.

Ordinary `Node` objects express meaning, composition, layout, and projection.
They should not become hidden state/resource owners by default. If a subtree
needs a smaller owner than its Boundary, that scope should be explicit in the
DSL.

## Boundary Lifetime

Boundary-owned resources are valid only while their owner Boundary is in an
alive phase. Teardown must be deterministic and child-first:

1. Child Boundaries detach and release before their parent Boundary.
2. Parent Boundary resources release only after child resources have had a
   chance to detach.
3. Scene root Boundary releases last.
4. Scene replacement through `SceneManager` must preserve the same ordering for
   the old Scene before the new Scene becomes the active owner of UI resources.

During teardown, creating new boundary-owned state, Flow, or resource holds
should be treated as a contract violation in debug/test builds.

A Boundary is not necessarily a Window lifetime. It may be a window root, but it
may also be a panel, tab, dialog, conditional subtree, loop item, or future
view-scoped owner. Any API that crosses a Boundary must therefore treat the
target as borrowed unless a longer-lived owner such as a Repository or
ApplicationScope is explicit.

`NodeState<T>` should remain a Boundary-local handle. Cross-boundary reads
should use read-only state views such as `State<T>*` or a future
`BorrowedState<T>`, and cross-boundary writes should go through an event,
facade, or owner method rather than transporting `NodeState<T>` or
`MutableState<T>*`.

## BoundarySection

If Boundary-level ownership is too broad, prefer an explicit DSL-visible
`BoundarySection`-style scope over making every Node an `IStateOwner`.

A BoundarySection is a smaller owner scope inside a Boundary. It can own state,
Flow, callback bindings, event listener bindings, managed holds, and future
section-local resource hooks for the subtree it contains. The default owner
resolution should be:

1. nearest active BoundarySection;
2. otherwise the current Boundary.

This keeps ordinary code simple: `this->state(...)`, Flow slots, listener
bindings, and managed holds naturally belong to the smallest visible owner
scope in the DSL tree.

BoundarySection should be structural, not a hidden named registry. A label such
as `BoundarySection("edit-draft")` may be useful for diagnostics, but it should
not imply lookup or reuse by name. The tree should remain the source of truth
for lifetime:

```cpp
Show(open)
  << (BoundarySection("edit-draft") << EditDialog());
```

In this shape, the `edit-draft` section cannot outlive the `Show` branch that
contains it. Parent lifecycle dominates child owner scopes. Conversely:

```cpp
BoundarySection("details")
  << (Show(expanded) << DetailsPanel());
```

Here the section can retain state while only the child details subtree is
attached or detached. The choice is visible in the DSL structure.

Future loop/list APIs should use the same principle. Item-local memory should
belong to an explicit item subtree or section, not to a hidden global section
table.

## State, Flow, And Binding Scope

State, Flow, event listener bindings, and callbacks should usually live in the
same owner scope. Flow without the state it reads or writes is not meaningful;
temporary state without the Flow/bindings that update it is usually only a
stored fact. When those lifetimes intentionally differ, the owner difference
should be visible in Props, a facade, Repository/ApplicationScope, or another
explicit API.

Default rule:

- attach-time bindings are released on detach;
- owner-scope release frees state, Flow, bindings, callbacks, and managed holds
  owned by that scope;
- retained detach may keep the owner scope alive, but must still run
  attach/detach lifecycle so callbacks and platform attachment can pause or
  rebind correctly.

`MutableState<T>` should be treated as the low-level state primitive, not the
normal app-facing ownership unit. Ordinary app and DSL code should create state
through lifecycle-aware handles such as `state()`, `declareStates()`,
`NodeState<T>`, future `BoundState<T>`, or owner-scope APIs. Those handles bind
the state to a Boundary, BoundarySection, or another explicit OwnerScope so
tracker ownership, release timing, and cleanup all move together.

Directly allocating or passing around `MutableState<T>` should remain a
low-level/test/special-purpose path. If code needs a raw `MutableState<T>`, the
owner should be explicit and the call site should be reviewed like other
`dangerously*` state APIs. In the long term, checked APIs such as `trySet`
should probably live on the owner-aware handle surface first, because many
failure reasons depend on owner lifetime, tracker state, and boundary scope.

The intended user model is simple:

```text
state() / NodeState / BoundState -> owned by the nearest explicit scope
MutableState<T>                  -> implementation primitive
State<T>* / BorrowedState<T>     -> borrowed read-only/live view
EmitterState                     -> owner-aware event channel
```

This makes edit-dialog style workflows straightforward:

1. parent Boundary or Repository owns the committed value;
2. dialog BoundarySection owns a mutable draft, validation Flow, and temporary
   bindings;
3. cancel releases the section and its draft;
4. save builds an immutable result and asks the parent owner to apply it.

The draft does not leak into the parent, and the parent only receives an
intentional completed value.

## Conditional Lifetime Policies

`Show`/conditional UI should not be treated as delayed declaration. Once a
branch becomes active, its subtree participates in attach/compose lifecycle.
The important contract is not only whether nodes are retained, but whether
ATTACH and DETACH phases are delivered correctly.

The likely direction is:

- `Show` is the simple retained attach/detach baseline for disclosure, tabs,
  tutorial cards, and HyperCard-like navigation where returning to a previous
  view should be cheap and stateful.
- a separate `MountIf`/destroy-on-detach style primitive or section policy
  should be used for temporary drafts, dialogs, import previews, and other
  views whose memory should be released when hidden.
- a future `VisibleIf` may mean projection visibility only, keeping logical
  ownership and bindings attached.

Names are not final. The principle is that retain/destroy/visibility semantics
should be deterministic and visible in the DSL, not hidden behind local
`isInitialized_` flags or ad hoc detach behavior.

Section-level policies should also use explicit names such as
`RetainDetached`, `ReleaseOnDetachNextTick`, or `DestroyOnDetach` rather than a
vague `Default`. A policy should say what happens; it should not require the
reader to know the current project default.

## Release Timing

Immediate release is not always safe. Native platform callbacks, deferred Flow
work, and same-tick composition/apply code may still be unwinding when a
Boundary starts to detach.

The default release policy should be deferred unless a resource explicitly opts
into immediate release:

- `ReleaseNextTick` or equivalent should be the safe default for Boundary-owned
  state, Flow, and managed resources.
- `ReleaseImmediately` should be explicit and reserved for resources whose
  teardown is known to be safe in the current phase.
- A Scene-level or App/SceneManager-level dispose queue must guarantee that
  deferred releases still run when an entire Scene is replaced.

The final names are not decided. Candidate policy markers include:

- `ReleaseNextTick`
- `ReleaseImmediately`
- `UntilUnowned`
- `UntilUnownedNextTick`

The name must not imply that a resource is disposed at a specific time if
another Boundary still holds it.

The same principle can support an autorelease-style adoption model without
making ownership invisible. Newly created resources can initially belong to a
short-lived current tick/pool owner. If they are adopted by a Boundary,
BoundarySection, App/Window owner, or AssetPool before the tick ends, ownership
transfers to that visible owner. If no owner adopts them, they are released on
the next tick.

Possible vocabulary:

- internal pool: `AutoPool` or tick pool;
- public timing: `releaseOnNextTick`;
- persistent owner action: `hold`, `adopt`, or `keep` depending on API context;
- borrowed access: no ownership transfer.

This should feel automatic for normal code while remaining inspectable in debug
tools. A resource should be able to report whether it is pending next-tick
release, held by a Boundary/Section, held by an AssetPool, or only borrowed.

## Logical And Native Resources

Boundary memory has at least two layers that should stay separate.

Logical Loka resources are values and handles that describe application meaning:
`NodeState<T>`, props, definitions, Flow slots, composition snapshots, image
handles, and document/movie root handles. These may often live for the Boundary
lifetime because they are small or because their ownership is part of the
logical model.

Native or heavy resources are platform projections or decoded payloads: native
views and controls, HWND/NSView/ControlRef-style handles, decoded bitmaps,
QuickTime/WIC/NSImage resources, graphics buffers, textures, platform callback
registrations, and similar objects. These should live behind platform,
multimedia, cache, or projection scopes with explicit release and cache policy.

For example, a logical `Image` handle can remain in state while decoded pixels
or native image objects are lazily held and released by a resource/platform
scope. Boundary ownership should describe the logical lifetime; heavyweight
native storage should not leak into ordinary DSL state unless the API makes that
cost explicit.

## Owner-Aware Handles (Implemented As `Held<T>`)

This section records the design precursor to the shipped `Held<T>` name and
contract. `Managed<T>` remains internal value plumbing; it is not the
app-facing owner model described here.

The implemented shape is:

- `Held<T>` points to a small control block with four inline owner slots.
- The slots record which Boundaries or Sections currently hold the payload.
- Overflow is nullable; the teaching `hold(held)` form asserts in audit builds.
- Copying a `Held<T>` handle never changes ownership.
- No general `StrongRef<T>` / `WeakRef<T>` surface is part of this contract.

For debugging, the important question should be "which owner is still holding
this?" rather than "why is the count nonzero?"

The broader direction is one ownership model with multiple concrete scopes, not
many unrelated lifetime mechanisms:

```text
OwnerScope owns resources.
Boundary is the normal UI OwnerScope.
BoundarySection is the normal smaller subtree OwnerScope.
AssetPool is the normal shared immutable resource OwnerScope.
AutoPool/tick pool is the short-lived temporary OwnerScope.
Borrowed references never own.
```

`AssetPool` should be considered for localized strings, images, sounds, fonts,
sprites, decoded blobs, and other reusable immutable or controlled-cache assets.
It should not become a place for arbitrary mutable UI state. State and Flow
belong to Boundary-style owner scopes; shared resources belong to AssetPool or
Repository-style owners.

## Boundary Holds

A boundary hold API should make the owner relationship visible without exposing
raw Boundary internals to ordinary app code.

Implemented shape:

```cpp
loka::core::Held<Movie> movie =
    c.hold(new Movie(), &ReleaseMovie);
```

Important semantics:

- A hold belongs to a Boundary or Section owner scope.
- Detaching one scope does not dispose the payload if another owner
  still holds it.
- The last slot drop queues the payload releaser on the Boundary retire pool;
  it never runs inline from the detach stack.
- Owner pointers are ledger identity and ancestry facts only. `Held<T>` does not
  expose Boundary traversal through them.

## Actions And Event Bindings

Callbacks, thunks, functors, and platform event hooks have lifetime hazards
similar to resources. A raw function pointer plus context pointer can outlive
the Boundary or Node it calls into.

The preferred cross-boundary pattern is not to store raw thunks into a
shorter-lived Boundary. Shared Boundaries or Repository-like owners should own
state, event sources, or facades whose own lifetime matches the shared owner.
Shorter-lived Boundaries should bind listeners while alive and unbind on detach.

`EmitterState` already fits this model better than raw functors because it can
represent a one-to-many event channel and can participate in bind/unbind
lifecycle management. Future action wrappers should follow the same principle:
owner-aware, unbindable, and debug-visible when invoked outside the owner
lifetime.

If direct callable objects are introduced later, distinguish local actions from
cross-boundary actions. Boundary-local actions may be optimized more
aggressively; cross-boundary actions must carry owner identity and fail safely
or report a debug/test error after the owner is no longer alive.

Sibling Boundaries should not communicate by storing direct Node/Boundary
pointers or raw thunks to each other. Route sibling interaction through the
parent Boundary, Repository/ApplicationScope, Props, event channels, or
owner-aware facades. `findBoundary()` should remain a direct-parent borrowed
path, not a general traversal or lifetime extension mechanism.

## Domain Objects

Loka should not force every internal allocation inside a domain model to become
Boundary-managed. A `Movie` may own frames, effects, buffers, and decoded data
with normal C++ ownership internally.

Loka should manage the root object and integration points:

- which Boundary/Repository/ApplicationScope owns the root object;
- which UI reads it;
- which facade or Flow mutates it;
- which state/event reports that it changed;
- when the root object and related resources are released.

This keeps domain code portable and testable without making it unnecessarily
dependent on Loka internals.

## App, Window, And Dynamic Declarations

App, Window, and Scene are also part of Loka's declarative ownership structure,
even though their platform responsibilities differ from ordinary Nodes.

Dynamic Window support should reuse the same visible ownership and
attach/detach ideas as Scene, Boundary, and BoundarySection. However, a Window
should not be forced into the ordinary Node hierarchy just to share lifecycle
machinery. Window has platform-specific responsibilities such as native window
handles, focus, close/minimize behavior, menus, and OS event integration.

The open design direction is a higher-level declaration/owner abstraction shared
by App, Window, Scene, Boundary, and future BoundarySection where appropriate:

- declarative definition;
- attach/detach lifecycle;
- owner scope;
- diff/apply or projection;
- dynamic add/remove;
- cleanup and deferred release.

This should allow future APIs such as dynamically declared windows without
turning Window into a visual Node or hiding ownership outside the declarative
structure.

## Open Questions

- Should owner slots store `BoundaryNode *`, a generic owner token, or both?
- What is the default owner slot count, and what happens on overflow?
- Should deferred disposal be owned by `Scene`, `SceneManager`, or `App`?
- When a weak reference observes an object queued for next-tick disposal, should
  it return null immediately or remain readable until the dispose queue flushes?
- How should Repository/ApplicationScope integrate with the same hold model?
- Should Boundary-inner memory scopes be introduced for conditional subtrees,
  loop items, or reusable nodes instead of making every Node an `IStateOwner`?
- What is the exact contract for `EmitterState` listener cleanup during
  Boundary detach, Scene replacement, and emit-while-unbinding?
- What is the first stable API shape for structural `BoundarySection`?
- Should `Show` remain retained attach/detach by default, with a separate
  destroy-on-detach primitive for temporary subtrees?
- How should dynamic Window declarations share lifecycle semantics with Scene
  and Boundary without becoming ordinary Nodes?
- Which resources are safe to release immediately in Classic Toolbox, Win32,
  and macOS platform paths?
