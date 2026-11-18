# Loka App API Style

> **Status:** Normative
>
> **Owns:** App-facing naming, composition-form selection, and sample authoring
> conventions
>
> **Does not own:** Exact signatures, implementation mechanics, or platform
> support matrices
>
> **Code truth:** [`common/app/`](../common/app/)
>
> **Verification:** Shipping examples under [`example/`](../example/) and DSL,
> Component, Boundary, and scenario tests under [`tests/`](../tests/)

Loka app code should look like a direct description of application intent while
keeping ownership, lifecycle, and update flow visible. Concision is valuable
when it removes ceremony; it is not valuable when it hides which concept owns
the work.

Exact constructors, overloads, defaults, and template constraints belong to the
public headers. This document owns the choice between those APIs and the shape
expected at app call sites.

## Choose The Smallest Composition Form

Composition remains owned by a Boundary. Helpers and class Components can give
structure and behavior meaningful names, but they do not silently add another
owner scope.

| Form | Use it when | Lifecycle and ownership |
|---|---|---|
| Inline helper returning a Definition | A name improves readability, but the helper needs no independent state, identity, or lifecycle | The helper adds no runtime owner or node of its own; the returned definitions materialize in the parent's composition |
| `scene::Component(props)` with `ComponentNodeWithProps<Props>` | A class needs node-local members, state declarations, or attach/detach behavior, while its child structure remains fixed for one structural lifetime | State resolves to the nearest enclosing Section or Boundary; changing child structure requires identity replacement |
| `scene::Boundary<Node>(props)` | A subtree needs independent ownership, tracking, and dirty routing | The Boundary is the visible owner and lifecycle compartment |

Use an ordinary inline function first when it is sufficient. Tutorial helpers
such as [`TutorialTitle`](../example/Tutorial/src/TutorialShared.hpp) demonstrate
that form without inventing a component runtime.

Use a `Fragment` or a helper returning definitions when several definitions
should inline into their parent without an independent lifecycle.

Use a class Component when the class lifecycle is meaningful. The MineSweeper
cell in [`MainNode.hpp`](../example/MineSweeper/src/MainNode.hpp) is the canonical
small example. `scene::Component(props)` obtains its node type from
`Props::NodeType`; the call site must not repeat a second node type.

Framework semantic vocabulary may wrap `scene::Component` in a named function; use `doc::Heading`, `doc::Paragraph`, and `doc::CodeBlock` with the `doc::` qualifier even under `using namespace loka::app;`.

A Component's children materialize once per structural lifetime. Props may be
reapplied to the resident class without rebuilding that child structure. Give
the enclosing Section or seat a new identity when structure must be replaced;
use State props for values and Match, Show, or Keyed seats for changing structure. The exact
contract lives in
[`ComponentNode.hpp`](../common/app/scene/node/ComponentNode.hpp) and is pinned
by [`ComponentNodeTests.cpp`](../tests/ComponentNodeTests.cpp).

Do not describe the class form as automatically faster. Its fixed structure can
avoid work in the right application shape, but performance claims require the
repository's normal measurement process.

## Props And Definitions

- `Props` is the canonical complete input surface. A Definition setter is
  optional shorthand for fields used frequently in DSL call sites.
- Constructors and factories should cover the common, readable case. Less
  common configuration should use an explicit Props value instead of growing
  parallel shorthand APIs.
- A Props type's `NodeType` is its canonical runtime node type. Do not ask an
  app call site to restate the same relationship.
- Props-owned constants stay values. Use `State<T>*` only for genuinely live
  inputs, and expose `NodeState<T>` to read-only consumers through `.state()`.
- Type and ownership constraints should fail at compile time when C++98 can
  express the wall without runtime cost.

For the broader State, Props, and ownership model, route through
[`DESIGN.md`](../DESIGN.md) rather than expanding those contracts here.

## Namespace And Platform Collisions

Ordinary compose bodies may use `using namespace loka::app;` for the app DSL.
Lifecycle-bearing scene factories remain visibly qualified:

```cpp
cell << scene::Component(MineCellProps(...));
```

`Component` is also a global Classic Mac SDK type. Keep the factory in
`loka::app::scene`; do not re-export it into `loka` or `loka::app` merely to
remove one qualifier. The qualified spelling is portable and communicates that
the call selects a scene lifecycle form.

When one narrow function contains many class-component declarations, an
explicit using-declaration is acceptable:

```cpp
using loka::app::scene::Component;
```

Do not use `using namespace loka::app::scene;` to obtain the same effect; it
widens lookup and reintroduces collision risk. New app-facing names should be
checked against the global names exposed by supported platform SDKs, not only
against the default host build.

## Sample Code Should Look Authored

Examples should resemble a reasonable first implementation by someone who
understands the application, not a framework showcase optimized to remove every
line.

- Keep the main composition legible as DSL chaining.
- Introduce a helper when it gives a repeated intent one name.
- Introduce a class when its members or lifecycle are meaningful.
- Introduce a Boundary when independent ownership is
  meaningful.
- Avoid macros, type erasure, generic member bags, and convenience layers whose
  only result is fewer characters.
- Leave uncommon mechanics visible when hiding them would make the next feature
  harder to place.
- Prefer one canonical spelling across examples. A tutorial may introduce less
  machinery than a lifecycle-heavy example, but the same concept should not
  acquire a different declaration style without a reason.

The goal is not minimum source length. It is app code that can be written
casually, reviewed by shape, and extended without discovering hidden ownership
or lifecycle rules.
