# Loka Tutorial

This example is meant to be explained step by step on video or followed in the
editor.

## Flow

Start with `DoItYourselfNode` in `src/MyAppConfig.hpp`.

When you want to check an answer, switch the active typedef to one of:

- `Step1Node`: a minimal `Hello, Loka`
- `Step2Node`: increment button and counter state
- `Step3Node`: conditional content with `Show(condition) << ...`
- `Step4Node`: a growing list built with `Keyed` + `For`, with a declared capacity of 100

## Suggested walkthrough

1. Build and run `LokaTutorial`.
2. Edit `src/DoItYourselfNode.hpp` to recreate the steps yourself.
3. Uncomment a `StepNNode` typedef in `src/MyAppConfig.hpp` when you want to compare.

## Step 4: a list with a capacity

Each Add changes the count. `Keyed` retires the old generation first, then
`For(...).window(0, count)` builds the numbered rows for the new generation.
The reservation and Add guard share one capacity constant: 100. At capacity,
Add is disabled and the summary reads `Items: 100 (full)`. Both the summary
and enabled state derive from the count through owner-held Flow slots.
Show/Hide hides and restores the summary and list together.

This builds every row; it is not a lazy list. Rows eventually extend below the
window. Scrolling is a later step. The `increment-summary-toggle` scenario still
adds twice, hides and restores the content, and ends at `Items: 2`.

## Targets

- macOS: `LokaTutorialMacOS`
- Win32: `LokaTutorialWin32`
- Retro68 68K: `LokaTutorial68K`
- Retro68 PPC: `LokaTutorialPPC`
