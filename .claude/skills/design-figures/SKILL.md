---
name: design-figures
description: Draw the standard set of design figures (today's path, proposed path, cast table, lifecycle state machines, PR split, alternatives) as an Artifact with inline SVG, at design-rally freeze time and again when a REFUTE lands. Use whenever a ruling is about to be frozen, a rally page is drafted, or a refutation changes the shape.
---

# Design figures at ruling time

A ruling is frozen on pictures, not prose. The rally page holds the argument;
the figure page holds the shape. Both are produced before asking for a
ruling, and the figure page is republished (same URL) when a REFUTE changes
the shape. First instance: #518 rally 1 v2 (2026-09-06).

## When

- **Design complete, before asking for a ruling** — the same moment as shape
  review gate 1 (docs/ReviewPolicy.md "Shape Review Gates"). The figure
  supplements the gate's written checklist; it never replaces it. The wall-tier,
  test-observability, and primitive-member items of that list are still
  written out. What the figure adds is the shape test: a dotted arrow in the
  picture is a weak joint the written list must then name.
- **When a REFUTE lands** — annotate the broken claims in red on the existing
  figures, add the alternative as its own figure. Never start a second page;
  the reader compares versions of one page.
- Skip it only for a change that has no new owner, path, or lifetime (a
  one-file bug fix). A "small" ruling that touches ownership still gets one.

## The standard set (one page, in this order)

1. **Today's path** — one concrete operation in one named example, traced
   from its origin to its visible or externally observable effect. For a
   rendering ruling that is a State write traced to the pixels (e.g.
   "FloppyBird: `scoreText_.set(...)`"); for an ownership or lifetime ruling
   it is the allocation, request, handle, or file path traced from where it
   is created to where it is released. Every box that holds a copy of the
   same fact is marked as a copy.
2. **Proposed path** — the same operation through the proposed design, same
   lanes, same example, so the eye can diff the two.
3. **Cast table** — roles as rows (what changed / where to paint / when /
   what is on screen …), columns "today" and "after"; every struct, field,
   enum, or function that holds the fact, marked copy / retire / new. End
   with a tally: +N new surfaces, −N retired, missing roles.
4. **Lifecycle state machines** — one per lifecycle the ruling touches: every
   new owned thing (a row, a mark, a batch) and every existing resident (Node,
   Boundary, platform context) whose detach, parking, cancellation, or
   reclaim behavior changes even when no new owner is added. Each shows birth
   doors, death doors, read doors, and what asserts at reclaim.
5. **PR split** — boxes per PR with the rail, the red→green test, and what
   each PR alone guarantees; arrows show which are independent.
6. **Alternatives** — when a second opinion (REFUTE, Codex, user) proposes
   another shape, draw it in the same lanes so the choice is a visible diff.

An independent second drawing is cheap and worth it: brief Codex with this
skill, the design summary, and the REFUTE findings — not your own page — and
publish its version as a separate artifact. Where the two pages disagree
(the PR split, a missing door) is where the design is still soft.

## Conventions (keep them identical across pages)

- **Lanes = owners**, left to right in the direction of the data. For the
  rendering pipeline: `STATE / TRACKER` → `BOUNDARY` → `SCENE / TRANSACTION`
  → `PLATFORM` → `CONTEXT`. For any other ruling, name the owners the fact
  actually passes through (e.g. `APP` → `WINDOW` → `SCENE` → `ARENA`). Time
  runs top to bottom.
- **Box = owner of one fact.** A fact that appears in two boxes is a copy
  and gets the copy colour. If you cannot name the fact a box owns, the box
  is decoration — delete it.
- **Colours**: plain = existing owner; amber = copy of a fact; green = new
  seat the design adds; red = retiring, or a claim a REFUTE broke.
  Dotted = a path that bypasses an owner (native thunk, side channel).
- **Tick boundaries** = a horizontal dashed line across all lanes for each
  clock the traced operation actually crosses, labelled with exactly what
  crosses it: none for a fully synchronous operation, several when a
  lifetime spans more than one owner clock (logical retirement, then
  platform-handle disposal on the native safe point). Never invent a
  boundary to fit the template and never omit a real one; if something with
  a pointer crosses, say so in the label — that sentence is usually the
  ruling.
- **Label every arrow** with the operation (`markViewDirty(PROPS)`,
  `queryPaintDamage`), not "flows to".
- One concrete example per figure; no abstract "a State" boxes. Use the
  **real identifiers** from the source, not a shorthand: `scoreText_.set(...)`
  at `GameModel.hpp:163` read by the Text at `MainNode.hpp:81`, never
  `score.set(n+1)`. A shorthand hides which owner writes and which resident
  reads, which is usually the point of the figure (learned from the Codex
  independent version of the #518 page).
- **Every lifecycle state machine names the park, cancel, and reclaim
  doors**, not only birth and death: retained-detach (parking) bypasses the
  terminal DETACH path, a cancelled or forgotten entry must show where its
  pending work goes, and reclaim states what it asserts. A lifecycle figure
  without these three is the picture the REFUTE will draw for you.
- Figure captions state the claim of the figure in one or two sentences and
  name the file:line the boxes come from. The page's last line names the
  sources (issue, rally page, REFUTE findings file).

## Mechanics

- Load `artifact-design` and `artifact-diagramming` first, then author one
  HTML file in the session scratchpad with hand-written inline `<svg>`
  (`viewBox`, `currentColor`, per-SVG `<defs>` markers with unique ids,
  theme tokens for both light and dark). No libraries, no external images.
- Publish with the Artifact tool (private). Republish the **same file path**
  to keep the URL; pass a `label` naming the revision ("v2 draft",
  "REFUTE applied + candidate C").
- Link the artifact URL from the Notion rally page's leading callout and
  from the session handoff. Do not commit the figure page or its URL to the
  repository: it carries Japanese and personal links (AGENTS.md language
  rule; "no personal URLs in the public repo").
- Title the page as a name (`#518 Paint Ledger Figures`), never a caption.
  The page body may be in the reader's language; this skill and the title
  example stay in English because the file is tracked code-facing
  documentation (AGENTS.md language rule).

## What the figure must be able to say

If the proposed path cannot be drawn with boxes and solid lines inside one
lane frame — if it needs a dotted arrow, a box no lane owns, or a label like
"usually still alive" on the tick boundary — the design goes back before the
brief is written. That is the existing rule (a dotted arrow sends the design
back) applied with a pen instead of a paragraph.
