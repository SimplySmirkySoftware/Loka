# Loka Philosophy

Loka is not only a GUI toolkit. It is intended to grow into a small, coherent
engine for applications, animation, video, and game production while keeping the
same core concepts meaningful across those domains.

## Software Longevity

Loka's retro-to-modern reach is not nostalgia for its own sake. The deeper goal
is to let people build software whose meaning can outlive a particular operating
system version, UI framework, compiler trend, device class, or distribution
channel.

Application logic, state ownership, composition, resources, timelines, and
interaction models should be described in Loka concepts as much as practical.
Platform layers should project those concepts into native behavior without
making the application itself depend on the platform's incidental shape.

Software should be able to age well. A Loka application should have a path to
keep running, be ported, be inspected, and be adapted without rewriting its core
meaning every time the surrounding environment changes.

## Software As Specification

Loka should help separate what software is from where it happens to run.

Modern frameworks often make application behavior tightly coupled to a specific
OS feature, runtime convention, UI toolkit, build system, or distribution model.
Loka should reduce that stress. Its APIs should let application code read more
like a durable specification of state, composition, resources, events, timelines,
and intent, while platform layers translate that specification into native
behavior.

The goal is not to avoid native integration. The goal is to keep native
integration from becoming the application's identity.

Application-facing headers and composition code should not inherit native
toolkit namespace pollution as an ambient cost of portability. Platform headers,
native type names, and compatibility shims belong behind platform projection
boundaries or narrow bootstrap seams. If an application author is writing a
logical `Button`, `Text`, `Window`, or future control, that expression should
mean the Loka concept by default, not whichever native SDK happened to be
included first.

## Meaningful Code

Application-facing code should express intent, ownership, state flow, and
composition. It should not force users to think in platform IDs, encoding
buffers, native handles, or other incidental mechanics unless they are explicitly
working at that layer.

When an API name can reasonably be read in more than one way, tighten the name
or split the concept. Framework code should not depend on vibes.

Names should also carry their regime. A wrapper called `OwnedDef<T>` states
that it holds a Definition-regime value — exclusively owned, moved by explicit
transfer, intended to enter and leave only via clone. The type alone cannot
prove that intent (it adopts any pointer, and `take()` releases one); the name
assigns the contract, and review enforces it — which is exactly what a generic
name cannot do. `ScopedPtr` is the contrast, not a failure: it serves the
bootstrap seam well precisely because "owns and deletes" is all that seam
needs, but at the definition sites the missing information was *which regime's
rules apply* — nullable clone, failure-atomic transfer, no use on chain
residents — and no generic name can say that. When another regime needs a
similar shape, mint a new name rather than generalizing.

## Structure Over Vigilance

Correctness should be carried by structure, not by contributor care. When a
comment must warn that several loose fields only stay consistent if every code
path remembers to update or reset them together, that is not documentation; it
is a fragile invariant waiting for a refactor to break it. Group those fields
into a small type whose operations maintain the invariant, give resources a
dedicated owner that releases them by construction/destruction pairing, and let
the surrounding code shrink to policy that can be read as a short sequence of
decisions.

The test for such an internal boundary is what it deletes. A good extraction
removes duplicated bookkeeping, per-item copies of aggregate data, defensive
comments, and cleanup interleaved with unrelated work. The remaining types hold
only data that is meaningful at their own level. A wrapper that merely adds a
name and a level of indirection has not found a real boundary and should not be
kept.

The same discipline applies to mirrored logic. Reserve and allocate, create
and destroy, attach and detach, estimate and consume: when two code sites must
agree for the system to stay correct, that agreement is an invariant even
though no shared field connects them. Writing the second side by hand, from
memory, in a different file is how such pairs drift. Give the shared fact one
named home that both sides use, or keep the pair adjacent with an explicit
cross-reference. Parallel implementations across platform seams may stay
separate deliberately, but each unmarked twin is a porting hazard; divergence
should be a choice, not an accident.

This matters during bugfix and PR-comment work too. The first locally correct
patch is often the one that preserves the fragile shape: add one more reset,
one more paired call, one more "remember to do both" rule. When a fix depends
on contributors repeating a sequence in the right order, first ask whether a
small structural move can carry that burden instead: RAII instead of manual
pairing, one named entry point instead of several equivalent ones, private
primitives behind a safer wrapper, or a helper type that owns the invariant.
`assert` is still useful, but mainly to guard contracts after the shape has
already made ordinary misuse difficult.

This applies inside the framework as much as at app-facing surfaces. Retro
targets make hidden fragility expensive to debug, so the same pressure that
keeps application code explicit should keep internal mechanics encapsulated,
owned, and inspectable.

## Safe By Shape

A sound component should be drawable: boxes for owners, solid lines for the
ports that are actually declared. If the drawing comes out as clean rectangles
and labeled arrows, a reviewer can say "this is safe" without mentally
executing every line. A bug is what water looks like poured into a plastic bag
with holes — the shape itself is distorted. Code whose diagram is misshapen
remains risky even when all of its tests pass, because the next change may leak
through a hole nobody drew.

Four properties make a shape clean:

- one owner per box;
- data enters and leaves only through drawn ports;
- phases advance in order and cannot be skipped;
- while a phase is executing, nothing outside the box can reach inside it.

The review-stopper is the dashed arrow: a coupling that exists at runtime but
appears nowhere in the types. A numeric stamp standing in for a structural
boundary, a phase flag that silently changes what a write means, a container
being fed while an iterator is walking it — each is a hole in the bag. A fix
that patches behavior by adding another dashed arrow makes the shape worse
even when it is provably correct; prefer reshaping the structure first, then
re-fixing the bug deliberately on the clean shape, with the failure-mode tests
carried over as the acceptance criteria.

This is the geometric face of Structure Over Vigilance, and it doubles as a
communication contract: a design that cannot be explained with one page of
boxes and solid lines is not finished, no matter how correct the
implementation is. Correct but illegible is not finished.

## Rails, Not Bricks

Most programs are bricks with control flashing somewhere inside. Loka lays
rails: every change travels a named path with a small number of named stops.
A State publish, a tracker commit, and a settle are places where a reader can
stand and watch what entered, what was accepted, and what changed.

A path must be inspectable at those stops. A callback fired from a helper's
tail, or a value that changes without a publish, leaves the reader outside a
brick. Return that shape for a rail: give the change an owned path and an
explicit completion point instead of asking the reader to reconstruct control
flow from incidental calls.

The delivery-site enumeration in [#873](https://github.com/SimplySmirkySoftware/Loka/issues/873)
and the [#882 settle contract](docs/RequestDeliveryDesign.md#settle-882) are the
worked example. Named entry operations meet one common completion sequence,
and its records expose takes and fact changes. The payoff is that differences
between OS versions and platform rails become a diff of records instead of a
stack trace.

## Joyful, Unambiguous Authoring

Loka should be beautiful to design and pleasant to use. A person writing Loka
code should be able to stay close to the software they want to make, choose the
next expression without hesitation, and enjoy the feeling that the code is saying
what they mean.

This is not about hiding structure or making everything look short. It is about
removing accidental decisions so the remaining choices are meaningful. Good Loka
code should be readable after the fact as a record of intent, ownership, and
composition rather than a pile of framework ceremony.

## Small Concept Set

Loka should prefer a small set of reusable concepts over a new mechanism for
every feature:

- `Node` for meaningful units in a composition tree
- `Boundary` for ownership, lifecycle, composition, and dirty/update routing
- `State` and `StateTracker` for explicit reactivity
- `Props` for stable inputs
- `Flow` for bounded work, event pipelines, and future media/game operations
- platform projection for native integration without leaking platform details

This consistency matters more than short-term convenience.

## Boundaries Protect Complexity

Advanced behavior should be isolated behind clear boundaries: module targets,
platform handlers, repositories, plugin-like seams, or `Boundary` scopes.

The point of a plugin or handler seam is not extensibility for its own sake. It
is to prevent platform, media, game, or experimental behavior from weakening the
core model. A seam is good when it keeps ownership, lifecycle, and dirty/apply
contracts inspectable.

A `Boundary` is both a compartment and a diagnostic boundary. It localizes
ownership, lifecycle, update routing, composition policy, and platform
projection so complexity has a place to live and a place to be inspected.

A `Boundary` should also make independently owned application regions
composable. Different composition, update, or projection strategies may live
inside different boundaries, but they should meet the outside world through the
same state, tracker, dirty-result, and lifecycle contracts. This lets nested or
sibling regions cooperate without forcing a parent to inspect a child's
internals.

A `Boundary` protects complexity; it should not become the complexity. This is
not only a Boundary rule. Any long-lived object can become an accidental
controller if it collects every property, operation, platform detail, and
business rule that passes nearby. A Boundary is an owner, lifecycle scope,
tracker, composition boundary, and diagnostic surface; other framework objects
have their own narrower roles. When any object begins to grow a broad set of
mutable fields plus general-purpose methods, split facts from operations:
completed state belongs in small value objects or explicit owner state, while
behavior belongs in Flow, actions, policies, repositories, domain objects, or
platform projection seams.

## No Black Boxes

Loka should avoid black boxes. Powerful features may hide routine mechanics, but
they must not hide ownership, lifecycle, update flow, or the path to customize
behavior.

When a feature becomes complex, prefer a clear compartment with named extension
points over a sealed subsystem. Users and maintainers should be able to inspect
what owns the work, replace the platform projection, override the policy, or
move the behavior behind a different `Boundary` or module without breaking the
rest of the model.

Abstractions are welcome when they reduce accidental decisions. They are not
welcome when they make the framework impossible to reason about.

## Describe Generically, Fetch Specifically

Wanting to inspect a whole family of facts is not the same as wanting to read
them through one generic call. Inspection wants every member of the family, in
order, rendered as text. Retrieval wants one member, as its own type, for a
purpose the other members do not share: a resource picker needs density and
appearance to choose a representation, a compositor needs the refresh rate to
pace frames. The types differ because the uses differ, so no useful loop exists
over "each fact, fetched and then handled the same way".

Designing for the loop that does not exist is expensive. A uniform getter must
either erase the type and re-discover it at runtime, which trades a compile-time
check for a silent one, or recover it through a type map, which introduces a
second table that must agree with the first. Both pay for freedom the call sites
never asked for.

So give the family a generic surface for enumerating and describing it, and keep
retrieval specific and typed. The generic surface should be derived from the
specific answers rather than implemented alongside them, so that adding a member
cannot leave the two out of step. This keeps the family inspectable, which is
what the impulse toward a generic getter is usually reaching for.

## Escape Hatches Must Look Like Escape Hatches

Loka should provide ways to cross boundaries, integrate unusual platform
behavior, and take manual control when a real application needs it. Those paths
must not look like the normal path.

If an API bypasses ordinary ownership, lifecycle, state creation, dirty routing,
or platform projection rules, its name and type shape should make that visible.
The `dangerously*` naming style is intentional: it allows power without
normalizing risk.

The best API is one people can usually use by feel because the concept set is
small and the names are precise. Documentation should explain and confirm the
model, not compensate for vague names or hidden contracts.

## Test What Can Be Known

Loka should prefer unit tests wherever behavior can be verified without a
specific operating system or machine. Core ownership, state routing, composition,
diffing, formatting, resource policy, and Flow behavior should be covered by
small tests that run often.

Platform and hardware behavior still matters. When behavior depends on an OS,
native toolkit, emulator, or real machine, Loka should move toward Flow-based
capture and automated scenario tests that can record, compare, and detect
regressions across environments.

The testing goal is not only correctness at one moment. It is to make future
porting, optimization, and platform-specific work safer by preserving evidence
of how the system behaves.

## Explicit Ownership

A good Loka abstraction should make it possible to answer:

- Who owns this state?
- Which lifecycle cleans it up?
- Which boundary sees changes?
- What becomes dirty when it changes?
- Which platform layer is allowed to project it?

If those answers are not visible in the type or API shape, the abstraction is
not ready to become a framework-facing surface.

## Prefer Immutable Facts

Completed facts should prefer immutable shapes. A snapshot, props object, apply
plan, analysis result, or other completed value should normally become read-only
once it has been built. Mutation belongs in a named owner, builder, transaction,
or local construction phase.

This is not immutability for fashion. It limits the number of places where a
value can change, makes Flow and update routing easier to debug, and prevents a
framework object from slowly becoming a shared mutable bag. When performance on
68030-era or other supported targets makes copying too expensive, Loka may use
explicit mutable storage, reusable buffers, or owner-local caches. The mutable
section should still be narrow and named, and immutable subsections should be
split into separate value types when that keeps the change surface smaller.

A growing set of properties plus general methods is often a sign that a class is
becoming an accidental owner. Prefer stack-local construction, explicit input
arguments, diff/result objects, builders, and small state machines over
permanently adding fields to a long-lived object. Paying a small cost to keep
facts immutable-like and lifecycle-aware is usually better than making cleanup,
ownership, and update routing implicit.

## Absent Is Not A Value

Some facts cannot be answered on every target. Classic Mac has no notion of a
dark appearance; on modern Windows the colour depth of a monitor does not
meaningfully vary. When an API answers such a question with a substitute value,
it merges two different statements into one: "the answer is X" and "there is no
answer here". A caller cannot tell them apart afterwards, and the wrong branch
is taken silently.

Prefer an answer that carries its own availability. A query that reports
presence and value together cannot disagree with itself, and a target that
cannot answer simply does not answer. An accessor taking a fallback
(`asInt(defaultValue)`) is convenient for genuinely optional data, but it is the
wrong shape for a capability, because the fallback is indistinguishable from a
real reading. Reporting availability through a second, independent query is the
same mistake one step removed: the two can then drift. Derive that view from the
answer instead of maintaining it beside the answer.

This deserves more care than corruption detection. A stale or damaged value
usually announces itself loudly and rarely. A substituted value passes every
type check and every validation, and the only symptom is that the screen is
quietly wrong.

## State Ownership Is Explicit, Not Ambient

Loka should not assume that every `Node` is a state owner. A `Node` may express
meaning, receive props, compose children, draw, handle events, or declare local
state handles, but those capabilities do not automatically make it a lifecycle
owner.

Making all nodes own state would be convenient at first, but it would scatter
lifetime, tracking, cleanup, and update roots across the whole tree. That makes
applications harder to optimize and harder to inspect, especially on retro
targets where every hidden tracker, list, and allocation matters.

The default path should be that ordinary node-local state is backed by the
attached `Boundary` owner. Shorter or more specific lifetimes should be explicit:
a scope, logic/model node, resource owner, or boundary-inner owner should say in
its type shape that it owns state. Loka should make it easy to create such
owners when they are meaningful, but it should not create them everywhere by
default.

## Read And Write State Are Different

State access should communicate authority. If a prop or API only needs to read
a live value, it should accept `State<T>*`. Requiring `MutableState<T>*` for a
read-only input gives the callee apparent write authority it does not need.

Use `MutableState<T>*` when the component, platform bridge, or framework feature
is expected to write a result back to the caller, such as text input, selection
changes, file dialog results, or window state. Use `State<T>*` for borrowed live
inputs such as labels, enabled flags, layout alignment, padding, font size, or
other values that affect rendering or layout but are not owned or mutated by
the receiving node.

This distinction keeps application code meaningful: a state binding should show
whether data flows into a node, out of a node, or both. It also keeps ownership
and debugging clearer on targets without ARC, tracing GC, or pervasive smart
pointers.

## Boundary-Centered Memory

Memory and resource ownership should be organized around `Boundary` whenever
possible. A `Boundary` is not only an update scope; it is the place where state,
trackers, flows, composition snapshots, and local resources become accountable.

When data is shared across boundaries, the owner should still be obvious. A
pointer, handle, `NodeState<T>`, `Held<T>`, repository, or resource facade
should make it inspectable which boundary or higher-level owner keeps the
payload alive. That makes leaks and premature cleanup easier to debug: the
question becomes "which owner is still holding this?" rather than "which hidden
runtime retained this?"

Loka cannot assume ARC, tracing GC, exceptions, or modern smart-pointer-heavy
code on every target. That is not a weakness. It is a reason to make ownership
visible, centralized, and lifecycle-aware instead of accidental.

## Ownership Is Named, Not Counted

An anonymous reference count answers how many owning aliases remain. It does
not answer which application scope gives the value its lifetime meaning, why
that scope still needs it, or which clock may release it. Loka should prefer
the stronger answer whenever ownership participates in application lifecycle.

`Held<T>` is the app-facing form of that rule for passive payloads. A
`Boundary` or `Section` takes an owner slot in the payload's ledger. Copying the
handle creates only an inert, non-retaining alias; the owner slot is the
lifetime edge. `Held<T>` governs lifetime, not payload mutation authority: it
does not make `T` immutable, and mutable operations still need an explicit
owner, facade, or State update path. A copied view must not be used after the
holding scope or creator's storage landlord is gone. A scope outside the
creator's ownership subtree may not acquire a slot, because allowing it would
turn the creator's storage lifetime into a dangling foreign hold. Sharing
across unrelated branches belongs in a meaningful common owner, repository,
or immutable global cache instead.

The last owner-slot drop crosses the detach line synchronously, but it does not
run the payload releaser there. Release is queued on the owning clock after the
drop-site callback returns, and the control-block bytes remain with their arena
landlord until reclamation. Handle destruction therefore cannot introduce an
observable callback or reclamation into an arbitrary rebuild stack frame.

The diagnostic should preserve this meaning. `use_count() == 1` says only that
one anonymous retain remains; `held-by [section(4103)]` names the declarative
scope responsible for the lifetime. Inspection is not a separate model added
for tests. It is a rendering of ownership facts the runtime already carries.

Small internal value plumbing may still use hidden reference counts when
release is truly unobservable and no application lifecycle meaning is lost.
That mechanism must not escape upward and become the default answer for state,
chain residents, or app-facing resources.

## Lifetime Has Two Lines

Retirement is two events, not one. The detach line fires: synchronously,
inside the tick, it unbinds observers, deregisters back-pointers, cancels
pending work, and hands native resources to their platform queue. The reclaim
line is silent: at the boundary of its owning clock it returns memory and
fires nothing — no callbacks, no state writes, nothing observable. Everything
observable must finish on the detach line; that silence is what makes
reclamation timing invisible, and invisible timing is what keeps pooling,
deferral, and future offload possible without changing application meaning.

A clock may only live in a box that cannot itself be retired mid-tick. A
Boundary can be retired by its parent's recompose while its own handler is
still on the stack; a Scene can be swapped out; a Window can be closed from
inside its own button handler. The App is the fixed point — it dies only
after the run loop exits, when no user code remains on the stack. Native
resources follow a second clock at each platform's safe point, because the
OS — not Loka — decides when a control may be disposed. The wall between the
two clocks: by the time logical memory is reclaimed, native handles must
already be stripped or queued on the platform clock.

Mixing the lines is a bug shape. Reclaiming synchronously on the detach line
(a `delete` inside a rebuild pass) and firing observable events during
reclamation (a state write in a destructor) are the same hole viewed from
opposite sides.

## Three Lifetime Regimes

Every independent lifetime participant belongs to exactly one regime; code
that fits none of them is a design smell, not a fourth category waiting to be
invented. Exclusively-owned values such as Definition trees are deliberately
not a regime of their own: they are property of a container (a composition
arena, snapshot, or template slot) owned by a chain resident, share that
container's lifetime, and move between containers only through failure-atomic
clone. They participate in lifetime through their owner, never independently.

- **Chain residents** (App, Window, Scene, nodes, boundaries, state, flows,
  trackers): a single owner on the containment chain, retirement through the
  two lines. Reference counting is forbidden here — deleting a chain resident
  is observable, so its timing has meaning and must follow the clock.
  Borrowing points upward only: a resident may borrow from its own scope or
  from an ancestor, because ancestors outlive it by containment. A resident
  must not *retain* a borrowed reference to a descendant-owned or
  sibling-owned value — the borrower must not outlive the target's owner.
  (Owning edges and bounded downward traversal within a pass are not borrows
  and are unaffected.) A value that must outlive its scope is declared in a
  higher scope, not smuggled upward.
- **Passive shared values and resources** (strings, blobs, plain memory
  payloads): small internal value plumbing may use reference counting when
  release is unobservable and carries no application identity. App-facing
  payload lifetime follows explicit owner scopes through `Held<T>` when the
  holding scope matters. When a value's releaser has a native side effect, it
  is not passive; its release belongs behind the platform clock.
- **Edge services** (sockets, decoders, future realtime I/O): own their own
  concurrency and lifetime, and meet the chain only as sealed values handed
  over at a tick boundary. Threads never enter the chain.

## Failure Is An Input

On a 4 MB machine, allocation failure is Tuesday, not an edge case. Loka
treats clone/create failure as a first-class input: structural operations
must be failure-atomic — build the replacement fully, cloning into
temporaries, and commit only when everything has succeeded. Destroying the
old value before the new one exists is a bug shape even when the failure path
"cannot happen" on the machine in front of you.

What failure-atomicity protects is *live truth* — the value the application
is currently showing or acting on, like the installed menu bar. Derived and
applied-state caches obey the opposite rule: a reconciliation baseline that
no longer matches what was applied is more dangerous than an empty one, so a
failed capture clears the cache and the next pass falls back to a full
rebuild. Keeping "the old value" there would be preserving a lie. Decide
which rule applies by asking what the value *is*: live truth is preserved,
stale caches are invalidated.

The transaction is wider than the allocation. Anything consumed on the way to
a fallible commit — a dirty flag, a one-shot token, a queued event — is part
of the same transaction and must be restored or requeued when the commit
fails, or consumed only at the commit point. A lost dirty flag is the same
bug as a destroyed menu bar: state that existed before the attempt is gone
after a failure that was supposed to change nothing.

Retries obey structure too. A retry scheduled from inside a scheduler's own
drain loop can spin that loop; schedule it after the current run completes so
persistent failure degrades to one bounded attempt per drain run instead of a
busy loop full of recomposition. (The bound is per run/flush — the scheduler
has no notion of tick identity, and a second flush in the same event-loop
turn may legitimately retry again.)

## Composition Strategy

`StdComposition` is the default scene composition model because Loka has already
validated that `State` + `StateTracker` can provide strong reactive behavior
with much less machinery than full structural reconciliation.

Small implementation should not mean small expressive power. The standard
composition path intentionally avoids implicit whole-tree reconciliation as the
default model. It favors explicit ownership, visible lifecycle phases, and
localized update paths that can be understood without reverse-engineering a
hidden matching algorithm.

`attachNode`, `composeNode`, and `detachNode` exist so composition can be
recomposable without collapsing setup, structural declaration, and teardown into
one ambiguous operation. A node should be able to say when it enters a
composition scope, what children it declares, and when its local resources or
bindings leave that scope.

More dynamic composition strategies are not forbidden. They can exist as
separate concrete strategies behind a `Boundary` when their identity, lifetime,
state preservation, and platform-context rules are explicit. The default path
should remain simple, inspectable, and friendly to retro targets.

Menu composition and future media/game composition may use different strategies
when their lifecycle and platform constraints justify it. Those differences
should be explicit, not accidental.

Loka should not add another scene composition algorithm only because it is
theoretically possible. New strategies should be justified by a domain that
needs different timing, redraw, reuse, or resource rules, such as real-time
surfaces, animation, games, or video tools. Until such pressure is concrete,
bugs and missing behavior should be fixed in the standard path rather than
splitting the model prematurely.

Performance pressure alone should usually be handled as a target profile before
it becomes a new application model. The standard composition path should remain
modern, practical, and expressive. Extremely constrained targets may justify a
separate 68k-focused Boundary/profile that sacrifices convenience, diagnostics,
or generality for smaller memory, code size, and runtime overhead, but it should
still honor the same ownership, state/tracker, dirty-result, and lifecycle
contracts so it can coexist with ordinary Boundaries.

## Platform Native, Core Neutral

Platform layers should follow native conventions. Core and app-facing DSL should
stay neutral. Use names like `controlTag` rather than `toolboxControl` unless an
API is intentionally platform-specific.

Platform conventions such as macOS application menus or Win32 window-attached
menus should become explicit policies instead of implicit behavior scattered
through platform code.

## Retro Constraints As Design Pressure

C++98 support, no exceptions, classic hardware, and disabled RTTI are not only
compatibility constraints. They are useful pressure to keep abstractions small,
predictable, and cheap to inspect.

When modern convenience conflicts with those properties, prefer the design that
keeps ownership and update flow understandable.

## Modern Type Safety Without Modern Assumptions

Loka should pursue modern type safety as far as each target allows. The fact
that the portable core must remain C++98-friendly should not excuse ambiguous
runtime contracts, untyped channels, or framework APIs that accept almost
anything and fail later.

Prefer compile-time structure over runtime discovery: typed props, explicit
owner handles, narrow result types, template constraints, TypeTag checks,
`asXxx()` accessors instead of RTTI, and small value/result objects that make
valid usage visible from code shape. When a modern compiler offers stronger
checks such as `static_assert`, Loka should benefit automatically. When a
classic compiler cannot provide the same check, the API should still be shaped
so misuse is difficult, reviewable, and easy to test.

The baseline constrains what Loka ships, not what may examine it. Anything that
only inspects the code — an extra warning pass, a sanitizer, a static analyzer,
a modern diagnostic no supported target's compiler can even produce — should be
reached for eagerly and used at full strength. Such a tool cannot cost a retro
target anything, because nothing it produces is shipped there; declining to run
it buys nothing and leaves real defects standing. What it finds still has to be
fixed within the baseline, which is where the design work actually is.

The goal is not to imitate modern language features for their own sake. The
goal is to give application authors the same confidence: if code compiles, its
ownership, state direction, boundary crossing, and platform capability
assumptions should already be much narrower than an untyped runtime framework
would allow.

## Modern Code Is Welcome At The Edges

Loka's repository code keeps a C++98-friendly baseline so the same framework
ideas can survive on retro targets. That baseline should not be mistaken for a
restriction on every application built with Loka.

Applications targeting modern systems may integrate C++11 and later, Swift,
Rust, Unity, platform SDKs, or other tools when that is the right way to deliver
the product. Those integrations should live at clear app, repository, platform,
module, or plugin boundaries so they add capability without making the portable
Loka core depend on modern-only assumptions.

## Do Not Surrender To The Environment

Loka should not treat language, compiler, platform, or hardware limits as a
reason to give up on good software design. Constraints are real, but they should
be converted into better structure whenever possible.

If C++98 lacks a modern feature, look for a static, explicit, inspectable
alternative before accepting a weaker runtime contract. If a platform API is
awkward, isolate it behind a meaningful projection instead of leaking its
awkwardness into application code. If a retro target is slow, use that pressure
to make ownership, allocation, and update flow simpler rather than abandoning
safety.

This does not mean cleverness for its own sake. It means being unwilling to
lower the design bar just because the environment is old, small, or inconvenient.
