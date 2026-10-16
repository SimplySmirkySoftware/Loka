# SmirkyCard runtime experiment

This optional example has two **C++-defined Scenes**. Each card has an editable
script box; press **Run** to evaluate it with QuickJS and show its string result
or exception. **Run JavaScript** remains the navigation experiment: it evaluates
the displayed expression, returns `"first"` or `"second"`, and C++ installs
that card in the same Window. Each visit creates a fresh Scene; the old Scene is
retired through SceneManager.

## MAIN.JS cards

`MAIN.JS` beside the application replaces the built-in card definitions at
launch. On Windows it lives beside the executable; on macOS it is a bundle
Resource; on Classic it is a plain data-fork file beside the application. The
repository's `MAIN.JS` is the built-in sample, so it is a useful starting point.
Edit the file on the disk and launch again to see changed cards. While the app
is running, press **Reload MAIN.JS** on Card One to re-read it and rebuild the
current card by name. Reload releases the previous script generation after its
outgoing card is reclaimed; card fields, counters, and other state are not
carried over. A reload failure stays on the current card and appears in its
status text. A card that throws while it is constructed or composed shows an
error with its own **Reload MAIN.JS** button, so fixing the file does not require
relaunching. Stage Classic with:

```sh
scripts/mame-dev-disk-app.sh --build-and-prepare SmirkyCard
```

It builds the app and puts it on the dev disk together with every card script
listed in [disk-scripts.txt](disk-scripts.txt), plus the pictures listed in
[disk-pictures.txt](disk-pictures.txt). `scripts/mame-boot-disk.sh --all`
reads the same list. To ship a new card file, add its name to that list.

`open(name)` loads a sibling script into a fresh engine and shows its `first`
card. MAIN.JS links to `./MINES.JS` and `./VIEWER.JS`; each links back to
`./MAIN.JS`. VIEWER.JS opens a picture through the platform's open-file
dialog (on Classic, a PICT file). Names must be flat filenames: one leading `./` is stripped, but
paths (including `../`), backslashes, colons, and embedded NUL are refused.
Each open reads the disk again. `reload()` rereads the current engine's file
and keeps the current card id; built-in cards retry MAIN.JS. Both operations
limit source files to 64 KiB and preserve the live card and engine on read,
evaluation, interruption, or missing-entry failure, reporting the filename in
the status. Constructor/compose refusal after admission uses the existing
native reload button (currently labelled **Reload MAIN.JS** even for a sibling).
No card-local state survives a successful open or reload.

The AppConfig owns one runtime/context and outlives all windows. Card boundaries
borrow it. The interpreter returns a card identifier and releases its result
before navigation; there are no JS-held Node pointers or JS callbacks. A small
C seam keeps QuickJS's modern header macros out of the C++98 application.

`CardScene::replaceWith` adopts the replacement through SceneManager. The Window
root seat mounts and composes it at the next App admission, then installs it on
the existing controller. A refused preparation preserves the old card and leaves
the replacement pending for the next admission. The outgoing Scene is reclaimed
at the following admission. Evaluation/definition allocation errors likewise
leave the old card installed; native projection failures after installation do
not have a rollback protocol.

## Build

The example requires CMake 3.18 or later. It is off by default, so ordinary Loka
builds do not fetch or link QuickJS. Enabling it downloads the pinned QuickJS-ng
v0.16.2 source archive and verifies its SHA-256; only the engine is built.

From a configured Windows developer command prompt:

```sh
cmake --preset win32-debug -DLOKA_BUILD_SMIRKYCARD=ON
cmake --build --preset win32-debug --target LokaSmirkyCardWin32
```

On macOS:

```sh
cmake --preset macos-debug -DLOKA_BUILD_SMIRKYCARD=ON
cmake --build --preset macos-debug --target LokaSmirkyCardMacOS
```

For an existing dependency checkout or an offline build, also pass
`-DFETCHCONTENT_SOURCE_DIR_SMIRKYCARD_QUICKJS=/absolute/path/to/quickjs`.
Use commit `1ab8676f4b6d6d669baeb5f21790fb9734636a20` to reproduce the pinned
build; an override deliberately uses the caller's supplied source.

Linux/WSL provides a headless integration test, not a GUI target:

```sh
cmake -S . -B build/SmirkyCard-Testing -G Ninja \
  -DTEST_BUILD=ON -DLOKA_BUILD_SMIRKYCARD=ON -DLOKA_WARNINGS_AS_ERRORS=ON
cmake --build build/SmirkyCard-Testing --target LokaSmirkyCardTests
ctest --test-dir build/SmirkyCard-Testing -R '^smirkyCardSceneSwitch$' --output-on-failure
```

The test evaluates expressions, checks failure/interrupt recovery, and fires
the real Button binding for 20 alternating Scene replacements. It checks the
mounted title and deferred Scene retirement, then destroys the final Window.

## Verification of the initial experiment

- Windows x64 / MSVC: build-verified and runtime-verified. A native-button probe
  checked six alternating card transitions in the same Window, five child
  controls after each transition, and a clean application exit.
- Linux / GCC: the headless test passes with warnings as errors, and with
  AddressSanitizer, UndefinedBehaviorSanitizer, and leak detection enabled.
- macOS: a build target is provided; not yet build-verified or runtime-verified.
- XP, legacy Mac OS X, and Classic Mac: no compatibility claim from this run.

## Deliberate limits

- Synchronous global scripts only. No file watching, module loader, Promise job
  pump, or JS UI DSL.
- One runtime with an 8 MiB engine allocation limit, 256 KiB JS stack limit,
  and an evaluation-local interrupt budget. These are experiment limits, not
  measurements or a supported Classic memory profile.
- Window and Menu remain ordinary C++ definitions.
- Classic builds refuse this option until macQJS/Retro68 integration is added.
  This dependency selection does not establish XP or legacy Mac OS X support.
- QuickJS-ng is MIT-licensed; its fetched source includes the upstream LICENSE.

Upstream: [QuickJS-ng](https://github.com/quickjs-ng/quickjs),
[macQJS Classic port](https://github.com/mplsllc/macQJS).

## RetroPPC QuickJS stack allocation

RetroPPC GCC 16.1.0 can place fixed locals at frame offset 72 while returning
an ordinary `alloca` pointer at offset 80 after the dynamic stack adjustment.
When the requested allocation is a multiple of 16 bytes, its last eight bytes
overlap those locals. In the second card's compose call, the
closure-reference array overlapped the QuickJS frame; `get_var_ref` then treated the
string tag `0xfffffff9` as a pointer and raised a PPC data-access exception.
The local MAME reproduction used pmac6100, 72 MiB RAM, and J1-8.1.

The Classic source preparation routes QuickJS's five stack allocations through
`ClassicQuickjsStack.h`. On RetroPPC it reserves one additional 16-byte ABI
stack unit, and the existing stack-limit check accounts for the same padding.
The 68K path keeps its original allocation size. Ordinary `alloca` is retained
because some argument buffers outlive the block allocating them; an aligned
builtin with block lifetime would not preserve that contract.

Run the compiler-layout pin against the configured PPC build:

```sh
python3 tests/scripts/ClassicQuickjsStackTest.py --build-dir build/retro68/ppc/Release
```

The pin uses the actual QuickJS compiler command and an ordinary-alloca
control. With GCC 16.1.0 the control overlaps by eight bytes; the patched
payload ends at offset 64, before fixed locals at 72. An unfamiliar assembly
shape fails for inspection. This compile check does not replace the Classic
runtime check: evaluate `1+1`, switch to Card Two, evaluate `2+2`, switch back,
and repeat before checking Reload.

The 2026-09-20 fix was build-verified for PPC and 68K with Retro68 GCC 16.1.0.
After removing diagnostic logging, it was runtime-verified on MAME 0.289
pmac6100 (72 MiB, J1-8.1): `1+1` and `2+2` evaluation, ten Card One/Card Two
round trips (20 navigation calls), and Reload. This evidence does not cover
physical hardware.

## JS seats and clickables

`c.state.image(name)` declares an ordinary IMAGE seat with a shipped picture
already decoded as its initial value; `c.state.image()` remains deliberately
empty. The name is a stem of 1..26 ASCII letters, digits, underscores or hyphens,
without dots, separators or NUL. The host appends `.PICT` on Classic and `.PNG`
on Win32/macOS; JS never supplies the suffix. Invalid names, missing files,
read/decode refusals and a missing platform throw a `TypeError` naming the stem
and reason; an uncaught error fails the constructor, without declaring the seat.
[disk-pictures.txt](disk-pictures.txt) lists the stems staged beside the app
(in macOS bundle Resources); both binary twins live under `assets/`.


Declare `state(initial)` only in a card constructor. Initial values may be
Strings, integers, or Bools; the seat keeps its type. `Text(seat)` formats
integers and Bools, and `EditText(seat)` requires a String seat.

`Button(textOrSeat, handler)` and `Cell(textOrSeat, handler)` accept a literal
String or a live String seat. The latter renders Loka's native clickable text
Cell, as used by the C++ MineSweeper. A handler runs with the card as `this`;
seat writes update the label without composing the card again.
Only Button has `.enabled(boolSeat)`. Both support `.TEST_ID('name')`.

`Grid(rows, cols, children)` lowers to Loka's native Grid, with equal-sized
cells in row-major order. Both dimensions must be numeric integers in 1..16.
The third argument accepts a tree node or an array of nodes; like stacks,
only one array level is flattened. The child count must equal `rows * cols`
(up to 256); a mismatch refuses with `Grid requires exactly rows * cols
children (N)`. Nested arrays are refused. Grid has its own child limit;
Row/VStack remain limited to 16. See [the board example](../../docs/smirkycard/clickables.md#grids).

`ScrollView(child)` wraps exactly one tree in a native ScrollView with a private,
card-owned viewport state starting at origin (0, 0). Each view scrolls independently;
there is no JS viewport seat or viewport argument. `Card(child)` places exactly
one tree in the native Card body using the default CardStyle; it has no style
argument yet. Both refuse missing, extra, array, or non-tree children with
TypeError and each counts one level toward the depth-eight tree limit.

`ImageView(imageSeat)` retains the default image props. The optional second
argument is a plain options object: `ImageView(imageSeat, {width: 120, height: 80,
fit: 'contain'})`. Width and height must be supplied together as integers in
1..4096; fit independently accepts `'contain'`, `'cover'`, or `'stretch'`.
Unknown keys (including symbols), non-plain options, invalid dimensions, missing
paired dimensions, and unknown fits throw TypeError when the tree is built.
Validated options are copied into the frozen tree.

Each card has two independent admission budgets, declared in
[src/CardRecords.hpp](src/CardRecords.hpp): `kCardSeatBudget` (128 seats) and
`kCardClickableBudget` (128 total Buttons plus Cells). Exceeding either shows
the existing refusal card with a message naming that budget and the Reload
button. Records are allocated only for seats declared and clickables lowered;
there is no eight-slot allocation or dispatch table. The refusal UI's Reload
button is a separate host control. The existing 16-children-per-stack and
depth-eight tree limits still apply. Grid capacity does not raise the seat or
clickable budgets (a 256-cell Grid can use Text, but cannot contain 256 Cells).

The card owns stable seat handles and JS handler records. Each clickable is a
small child Component that owns its emitter and binds in its own declaration
window. Detach withdraws that binding synchronously; the card releases records
only during its destruction after its subtree is detached. Child destruction
never accesses the borrowed handler.

## Production card Flows (stage 2a)

`Flow` is installed in every engine. Declare a card-owned Flow with `c.flow(chain)`
inside the card constructor; the returned frozen handle has only `run(value)`.
The description is consumed once and its functions remain rooted until the card
is reclaimed. A card admits up to 32 Flows, each with up to 128 steps.

```js
card('first', c => {
  const input = c.state(0);
  const output = c.state('');
  const update = c.flow(Flow()
    .watch(input, value => value < 0 ? Flow.SKIP : value)
    .step(value => value * 2)
    .onSuccess(value => output.set(String(value)))
    .onFailure(message => output.set(message)));
  return { compose() { return VStack(Text(output), Button('Run', () => update.run(4))); } };
});
```

Only the declaring card's constructor-created `c.state` seats may be watched.
The optional first `watch(seat, adapter)` subscribes after state materialization,
without an initial fire. Its adapter passes its return value to the next step;
returning the frozen unique `Flow.SKIP` quietly ignores that firing. An adapter
exception fails the execution. `run(value)` bypasses the watching adapter and
passes the supplied value to the next step. Without a watch, only `run` starts
an execution. There is no polling, waiting, retry, or public status.

All steps and the terminal callback execute synchronously in one card-tracker
transaction. Derived values and projected text read mid-Flow may be stale until
settlement. Steps pass return values to the next function. Success receives the
final value; failure receives the diagnostic string once and does not recover.
A terminal callback that throws records a diagnostic without another callback.

The admission door stays held through terminal notification, value release, and
transaction unwind. Reentrant `run` returns false; own-watch firings are dropped
without calling the adapter. Another Flow's synchronous watch firing is dropped
with a diagnostic and a debug assertion. Completion states must therefore be
written outside a running Flow, including completion from modal native work.
Append steps to one Flow to express a synchronous sequence.

Navigation via `c.go` cancels the execution even in the last step: later steps
and success do not run. Each call checks the card's lifecycle before entry and
immediately on return, before formatting exceptions. Detaching withdraws watches
before `onDetach`; retained handles return false after revocation. Reclamation
releases roots silently. Waiting and nested execution belong to stage 2b.

## Card scenarios (TEST_BUILD, stage 1)

A runner calls `ScriptRuntime::enableRunner(sink, clock, seed, companionName, bakedText)`
before loading sources or creating cards. It owns the sink and clock until all
cards are gone. The optional nonempty baked text replaces companion file reads,
including reload/open; omitting it retains the file-based runner. Without this
opt-in, `scenario`, `c.run`, and `c.test` are absent. Scenario execution and
storage are excluded from non-TEST_BUILD builds; the `Flow` description builder
is shared with production. Stage-1 `c.run` does not accept a watching step.

The companion file registers `scenario('first', function(c) { c.run(chain); })`.
Both sources are evaluated in one candidate engine before commit; registration
and file errors are setup failures. `loadMain` reports them through
`mainErrorFor`, and `prepareReload`/`prepareOpen` return a null candidate with an
error. A missing main source may select the built-in source, but its companion
must still load and register successfully. An invalid main source never silently
falls back under the runner.

`Flow().step(fn).named(name).onSuccess(fn).onFailure(fn)` describes a chain.
`c.run` accepts it once, only during that card's scenario invocation, and closes
its mutation door. Each function receives the preceding return value (initially
`undefined`). Each authored action uses `RunOnce`, with an automatic `Settle`
afterward; all C++ edges remain `Scene*`. JavaScript values and functions remain
rooted in their originating engine. `waitUntil`, `options`, retry, and resume are
not part of stage 1.

After mounting, the runner advances its clock and calls
`CardScene::tickScenario()` once per tick. This typed door reaches the root card
without a tree search. The scenario is invoked on the first safe tick, after
children exist, never during attach. It runs once per candidate engine's initial
Scene: reattaching or navigating with `go` does not invoke it again. Reload/open
validate fresh sources and provide a fresh scenario invocation. Scene admission
and reclamation happen outside the tick; synchronous detach cancels work already
on the stack, whose step outcome and canceled terminal are recorded on unwind.
No JavaScript terminal callback runs after Detaching.

Each JavaScript step is due on its own scenario tick (`due_tick` = step
ordinal, starting at 1): a step's seat writes complete with the rail's entry
operation, not with the step, so a Show branch that step k reveals exists only
on tick k + 1. Toolbox routes those writes when the idle operation that ran the
tick completes; Null routes them synchronously and used to pass every step on
tick 1, which hid the dependency. Put a reveal (`click` on a Button that flips a
Show seat) in one step and the lookups of the revealed nodes in the next.

The immediate test operations are:

- `c.test.click(id)`: Button or Cell, with a flush; a disabled Button fails.
- `c.test.enabled(id)`: Button enabled state; Cells return true.
- `c.test.text(id)`: Text, Markup's concatenated text segments, Button/Cell label, or
  EditText contents. IDs must identify exactly one node of the requested kind.
- `c.test.log(text)`: one percent-escaped audit record (`log text=...`).
- `c.test.random()`: a masked 32-bit LCG, with an integer-constructed binary64
  result exactly equal to `state / 2^32`. Every card Scene starts from the runner
  seed, before its factory executes; `go` also creates a fresh random stream.

The host runner harness is in [SmirkyCardTests.cpp](tests/SmirkyCardTests.cpp);
`LokaSmirkyCardTests --scenarios` runs these mechanism checks. The generic Classic
standalone runner bakes both sources through
`smirkycard_add_scenario_runner` in [CMakeLists.txt](CMakeLists.txt). Add content
with another call specifying CARD, FLOW, SCENARIO, APP_NAME and SEED; no C++
changes are needed. In any Classic build with `LOKA_BUILD_SMIRKYCARD` enabled,
the first call registers `LokaSmirkyCardStandaloneFlow68K_APPL` or
`LokaSmirkyCardStandaloneFlowPPC_APPL` (excluded from ALL); the VS Code task
"Build: Retro68 68K SmirkyCard" builds it together with the app. It uses SmirkyCard's SIZE resource, writes
LOG.TXT beside the application, logs the seed, advances the clock once per 0.1 s
idle callback, and quits after the terminal audit record.

[MINES.FLOW.JS](MINES.FLOW.JS) plays one board: flags a cell, returns to reveal
mode and checks a zero-cell flood. MINES.JS uses `c.test.random` only under the
runner; its ordinary module-level generator and New Game behavior are unchanged.
`LokaSmirkyCardTests --mines-scenario` checks the real files and the baked app
configuration, including the [expected audit](tests/MINES.audit).
The standalone audit is not registered in `scenarios.txt`: that file also controls
the approved MAME golden cells. Until its registration policy is decided, compare
LOG.TXT directly with the expected audit rather than using
`verify-standalone-audit.sh`. After building the 68K runner, run:

```sh
tests/toolbox/run-standalone.sh build/retro68/68k/Release/example/SmirkyCard/LokaSmirkyCardStandaloneFlow68K.bin --expect example/SmirkyCard/tests/MINES.audit
```

The launcher reads `.env-mame` (or `MAME_ENV_FILE`), boots a fresh template copy,
and retrieves LOG.TXT under `build/mame-standalone/<app name>/`, wiped per run.
It allows 90 emulated seconds for the app's own quit (`LOKA_RUN_WAIT` overrides)
and compares the original audit bytes. Elapsed time alone does not prove success.
Finder Tabs default to the staged item count; `LOKA_TAB_COUNT` overrides that for
diagnosis. `LOKA_LAUNCH_WAIT` overrides the 90-second boot wait. Do not run the
same app concurrently: its work directory is shared.

## SimpleViewer.JS card

[VIEWER.JS](VIEWER.JS) composes an Open button, a conditional OpenFileDialog,
and an ImageView. Its card-owned Flow consumes the FILE result, loads the image
through `c.native.loadImage`, and writes the IMAGE seat. Cancellation keeps the
previous picture. To launch it directly, copy VIEWER.JS beside the application
as MAIN.JS. The card needs no application-specific C++ code.

Under an enabled runner, `c.test.deliverChosenFile('chosen', 'Sun.pict')`
delivers a file result into the current card's own FILE seat; passing `null`
instead of a filename delivers cancellation. A seat handle is also accepted.
The operation runs outside production CardFlow execution and forces notification
inside a tracker transaction. Classic resolves the file beside the application
and captures its FSSpec into the delivered File through the same seam used by
the native dialog. The decoded name is display-only; native reads use the
captured locator.

`c.test.imageFacts('picture')` (or an own IMAGE seat handle) returns the frozen
plain object `{ empty, width, height }`, with zero dimensions when empty. It
borrows the current image to copy facts only; it exposes no native handle and
retains no image. Both seat operations refuse foreign seats, revoked cards,
and calls made inside a production CardFlow. Named lookup rechecks admission
if a Proxy property trap navigates.

[VIEWER.FLOW.JS](VIEWER.FLOW.JS) checks the initially empty picture, delivers
Sun.pict without opening a modal dialog, checks the image after the runner's
automatic settlement, delivers cancellation, and checks unchanged picture
facts. Loading is synchronous, so the first check after settlement is the
bounded success/failure check; there is no retry loop. The facts comparison
checks dimensions and emptiness, not image identity. Existing text/control
inspection cannot observe OpenFileDialog or the `shown` seat, so the JS audit
makes no claim about `shown` after cancellation.

Build and run the host coverage with `LokaSmirkyCardTests --viewer-scenario`.
For Classic, enable SmirkyCard and build
`LokaSmirkyViewStandaloneFlow68K_APPL` or
`LokaSmirkyViewStandaloneFlowPPC_APPL`. Stage the resulting `.bin` and
`Sun.pict` beside it on the dev disk; the picture comes from the boot template's
`:Desktop Folder:Images:` folder. Launch the runner application. Both JS sources
are baked in; it needs no MAIN.JS, VIEWER.JS, VIEWER.FLOW.JS, or LokaTest.cfg
sidecar. It writes LOG.TXT beside itself and quits on the terminal record.
With `Sun.pict` extracted from the boot template to `build/fixtures/Sun.pict`, run:

```sh
LOKA_TAB_COUNT=2 tests/toolbox/run-standalone.sh build/retro68/68k/Release/example/SmirkyCard/LokaSmirkyViewStandaloneFlow68K.bin build/fixtures/Sun.pict --expect example/SmirkyCard/tests/VIEWER.audit
```

The same launcher settings described for MINES apply. With the picture beside the
application the Finder needs two Tabs to select it (observed on the maciix rig);
the runner refuses extra files without an explicit `LOKA_TAB_COUNT`.
For the Sun fixture, the result records are:

```text
log text=image.load%20ok
log text=image.width%20256
log text=image.height%20256
log text=cancel.picture.facts%20unchanged
terminal status=succeeded
```

The whole LOG.TXT for this configuration is the [expected audit](tests/VIEWER.audit)
(runtime-verified on the maciix rig, 68K runner, 2026-10-01; re-baked 2026-10-16 when each step moved to its own tick).
The host decoder double supplies a 256 by 256 image and checks the same records
through the same runner and audit writer. Runner names must keep the Classic
target name within HFS's 31-character limit; `smirkycard_add_scenario_runner`
refuses a longer one at configure time.

## LaptopTracker.JS card

[LAPTOP.JS](LAPTOP.JS) is an illustrated guide to five Apple portable Macs
(Macintosh Portable, PowerBook 100, 180c, 550c and 5300c): a toolbar, a tall
card list, a wide three-slot carousel, and a detail page, all in one card whose
seats hold the selection, the page and the presentation. Its five pictures are
`c.state.image('MACPORT')` and friends, loaded from [disk-pictures.txt](disk-pictures.txt)
when the card is constructed; the carousel shows three of seven fixed ring slots
(last, 0..4, first) so a wrap never moves a picture between seats; the
presentation is a toolbar toggle because no viewport fact reaches JavaScript
yet. Open it from the first card's `./LAPTOP.JS` button.

[LAPTOP.FLOW.JS](LAPTOP.FLOW.JS) drives it: the initial list, the five picture
facts, open details from the list, Back with the selection kept, the carousel
(selection kept, arrows wrap in both directions), open details from a carousel
slot, and the way back to the list. Host coverage runs as part of the default
`LokaSmirkyCardTests` run (or `--laptop-scenario` alone) with a 2 by 3 decoder
double, announced to the scenario through `LAPTOP_PICTURE_SIZE`. For Classic,
build `LokaLaptopStandaloneFlow68K_APPL` and stage the five `.PICT` files beside it:

```sh
LOKA_TAB_COUNT=6 tests/toolbox/run-standalone.sh build/retro68/68k/Release/example/SmirkyCard/LokaLaptopStandaloneFlow68K.bin example/SmirkyCard/assets/MACPORT.PICT example/SmirkyCard/assets/PB100.PICT example/SmirkyCard/assets/PB180C.PICT example/SmirkyCard/assets/PB550C.PICT example/SmirkyCard/assets/PB5300C.PICT --expect example/SmirkyCard/tests/LAPTOP.audit
```

The whole LOG.TXT is the [expected audit](tests/LAPTOP.audit).
