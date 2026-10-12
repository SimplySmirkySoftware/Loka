# Flow-Based UI Test Sketch

## Status

The Text scenario audit records logical values only, with the per-node input mark pinned by [focused tests](../../tests/ObservedLayoutInputsTests.cpp); the dirty-field examples below are historical and superseded by [#974](https://github.com/SimplySmirkySoftware/Loka/issues/974).

- Status: partially implemented
- This file started as a sketch; several pieces are now real and should be read as current capability, not speculation.
- Current source of truth:
  - `common/loka/dsl/testing/SceneTestFlow.hpp`
  - `common/loka/dsl/SnapFlow.hpp`
  - `tests/FlowDslTests.cpp`

Implemented now:

- `CaptureNode`
- `CaptureScene`
- `CaptureViewTarget`
- `CaptureView`
- `CaptureViewBitmap`
- `CaptureViewBitmapSnap`
- `AssertPredicate(...)`
- `timeoutPending(...)`
- `SnapWriteAdapter`

Still future / draft:

- real multi-platform screenshot pipelines beyond current platform seams
- global UI capture
- CI-level screenshot sequence diff orchestration
- broad timing regression policy and baseline management

Exploration of reusing the Loka Flow DSL for automated UI testing.

## Scope and Security Model

- Default scope is **Scene inside the app window only**.
- Global UI (menu bar, app-external UI) is not part of default capture.
- Privileged/global inspection is optional and runs only when platform capability is available.
- If global inspection is unavailable, tests must report `SKIP` (not `FAIL`).

## Concept

Flow's `Step` / `goto` / `loop` / error handling maps directly to test scenario description. On failure, `Dump()` serializes the entire flow state for debugging.

`NextTickTracker` is used as the deterministic observation boundary:
- `update request -> next tick flush -> capture/assert`
- Multiple updates in the same tick are treated as one flush unit.
- `WaitNextTick()` は sleep 置換プリミティブとして扱い、待ち時間固定 (`sleep 100ms`) を禁止する。

## Two-Layer Test Strategy

テストは2層で構成し、Loka はデータ出力に徹する。判定はすべて外部 CLI / AI が担う。

```
Layer 1: Screenshot sequence diff (視覚的デグレ検出)
Layer 2: .snap parameter assert  (数値的デグレ検出)

Loka (C++98)           外部 CLI / AI
────────────────       ─────────────────────────────
スクショ連番保存   →   sequence diff → 視覚判定
.snap 書き出し     →   diff → 数値変化検出 → PR 判定
```

### Layer 1: Screenshot Sequence

- tick ごとにスクショを連番保存
- sequence 全体を diff することで「最終状態だけでなく中間状態のデグレ」も検出できる
- 外部 AI が「デグレか仕様変更か」を判定し PR に自動フィードバック

### Layer 2: .snap Parameter Assertion

- Flow 内に `Snap()` ステップを書くと、対象ノードのパラメータ（height, x, y, width など）をファイルに書き出す
- 同時に tick 単位の時間指標（`flushTimeMs`, `recomposeTimeMs`, `layoutTimeMs` など）も書き出す
- `.snap` ファイルが存在しない場合: 新規生成して **PASS**（初回受け入れ）
- `.snap` ファイルが存在する場合: 今回の値を別ファイルに書き出し、diff は外部ツールが担う
- Loka 側は「値を書くだけ」に徹し、判定ロジックを持たない

#### Minimal `.snap` Schema (v1, TSV key/value format)

```txt
dirty	LAYOUT|PROPS
timing.flush_ms	3
format_version	1
h	64
timing.layout_ms	2
node	MainText
timing.recompose_ms	1
scenario_version	3
schema_version	1
step	after-wrap
test	TextWrapRelayout
tick	12
w	220
x	20
y	40
```

v1 ルール:
- 1 行 1 レコード（追記型 stream out 前提）。
- 1 レコードは `key\tvalue` の複数行で構成する。
- 同一レコード内のキーは **ASCII 昇順で固定**（diff 安定化のため）。
- 同名キー禁止。
- 文字列は `\t`, `\n`, `\\` のみエスケープする。
- 整数値のみ（小数なし）。
- 時間は ms 単位の tick 集計値。
- `dirty` は `|` 区切り集合（重複なし）。

バージョンキー:
- `format_version`: ランナー実装の出力形式バージョン。
- `schema_version`: snap キー意味論のバージョン。
- `scenario_version`: テスト作成者が管理するシナリオバージョン。

Node 識別子:
- 比較主キーは `node` (= `test-id`) を使用する。
- `.snap` 比較対象の重要ノードは `testId("...")` の明示指定を強く推奨。
- 未指定時は自動IDを生成して `WARN`（ローカル向け）。CI strict では未指定を `FAIL` 扱いにできる。

## API Sketch

Canonical lookup API for v0.0.1:
- `FindNodeById<T>(testId)` is the primary lookup entry point.
- `T` is the expected node type and must be checked via `NodeKind` / `asXxx()` helpers, not RTTI.
- `CaptureNode<T>()` consumes a previously resolved `T*`; it does not perform lookup.
- `CaptureScene()` is a separate scene-level API for scene/root metadata capture and must not implicitly perform node lookup.
- `CaptureViewTarget(testId)` resolves a raw node/view target from `Scene*` by `testId`.
- `CaptureView()` consumes a previously resolved `ViewCaptureTarget`; v1 records target/context metadata only.
- Bitmap/image screenshot capture should be a later adapter layered after `CaptureViewTarget`, not part of `CaptureScene()`.

Error model for v0.0.1:
- `ScenarioError`: execution failed before the assertion could be evaluated (node not found, type mismatch, timeout, invalid config, missing `testId`, duplicate `testId`).
- `TestError`: execution succeeded but the captured value violated an expectation (bounds/value/timing threshold mismatch).
- Current lightweight implementation keeps a single `FlowError` payload but splits `kind` into scenario vs assertion domains.
- Debug builds should `assert` for programmer mistakes; runtime test execution should surface structured scenario/test errors instead of aborting the whole runner.

`testId` policy:
- `Snap` / `Capture` target nodes must provide a `testId`.
- `testId` uniqueness is required within a Scene.
- `testId` is opt-in for the general UI tree, but mandatory for nodes addressed by test flows.

```cpp
// Layer 1: スクショ + Layer 2: snap を組み合わせた例
TestFlow(testState)
  | Step(WAIT_WINDOW, WaitFor(WindowVisible("Main")))
  | Step(UPDATE_TEXT, SetState(textState, "long long long..."))
  | Step(WAIT_TICK,   WaitNextTick())
  | Step(SCREENSHOT,  CaptureScreen("after-wrap"))
  | Step(CHECK_TEXT,  CheckText("MainText", "long long long..."))
  | Step(SNAP_NODE,   CaptureNode<TextNode>("TextWrapRelayout", "after-wrap", 12, 3))
  | Step(CHECK_TIME,  CheckTimingLessEqual("timing.flush_ms", 16))
  | onFailure(Dump(testState), ABORT);
```

Minimal scene/node split:

```cpp
TestFlow(testState)
  | Step(CHECK_TEXT, CheckText("MainText", "Ready"))
  | Step(CHECK_NOT_DIRTY, CheckTextDirtyEquals("MainText", NODE_DIRTY_NONE))
  | Step(UPDATE_TEXT, SetStringStateAndFlush(textState, "Updated"))
  | Step(UPDATE_FONT, SetIntStateAndFlush(fontSizeState, 20))
  | Step(ENABLE_BUTTON, SetBoolStateAndFlush(enabledState, true))
  | Step(SNAP_TEXT, SnapText("MainText", "SceneTest", "after-ready", 1, 1))
  | Step(CHECK_SNAP_TEXT, CheckSnapStringEquals("text.value", "Updated"))
  | Step(CHECK_DIRTY, CheckTextDirtyHasBits("MainText", NODE_DIRTY_PROPS))
  | Step(CHECK_TIME, CheckTimingLessEqual("timing.flush_ms", 16))
  | Step(SNAP_SCENE,  CaptureScene("after-ready"));
```

Mixed props/layout update:

```cpp
TestFlow(testState)
  | Step(UPDATE_TEXT, SetStringState(textState, "Updated"))
  | Step(UPDATE_FONT, SetIntState(fontSizeState, 20))
  | Step(FLUSH, FlushSceneInvalidation()) // deterministic flush for deferred/dynamic path
  | Step(CHECK_TEXT, CheckText("MainText", "Updated"));
// platform flags should include NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT
```

State-level dirty routing:

```cpp
TestFlow(testState)
  | Step(ENABLE_BUTTON, SetBoolStateAndFlush(enabledState, true));
// same Boundary may also contain wrapped Text / Conditional nodes
// platform flags should include NODE_DIRTY_PROPS only
```

Full rebuild contract:

```cpp
TestFlow(testState)
  | Step(UPDATE_LAYOUT, SetIntStateAndFlush(fontSizeState, 20));
// Scene should report fullRebuild=false to the platform controller

TestFlow(testState)
  | Step(SWAP_BRANCH, SetBoolStateAndFlush(showState, true));
// Scene should report fullRebuild=true to the platform controller
```

macOS native relayout probe:

```cpp
// test-only access under apple/macos/src/testing
expect(MacScenePlatformTestAccess::hasPendingRelayout(controller));
expect((MacScenePlatformTestAccess::lastChangeFlags(controller) & NODE_DIRTY_LAYOUT) != 0);
```

Conditional child swap:

```cpp
TestFlow(testState)
  | Step(CHECK_FALSE_BRANCH, CheckText("OffText", "Off"))
  | Step(SWAP_BRANCH, SetBoolStateAndFlush(showState, true))
  | Step(CHECK_TRUE_BRANCH, CheckText("OnText", "On"));
```

Privileged probe sketch:

```cpp
if (probe.canInspectGlobalUi()) {
  CaptureGlobal().expect(MenuItemExists("File/Quit"));
} else {
  Skip("global ui probe unavailable");
}
```

## Assertion Extensions (Quality / Performance)

### A. ViewDirtyFlags Assertion

見た目一致に加えて内部 dirty も検証対象にする。

```cpp
CaptureScene()
  .expect(WasDirty(DIRTY_LAYOUT))
  .expect(NotDirty(DIRTY_CHILD));
```

Capture layering guideline:
- `CaptureNode<T>`: lightweight node-level assertions (`text`, `dirty.mask`, small props).
- `CaptureScene`: scene/root metadata (`root present`, `root boundary present`, scene-wide dirty/timing later).
- `CaptureViewTarget -> CaptureViewBitmap`: platform/native screenshot path; keep it explicit and opt-in.

目的:
- 見た目は同じでも毎回 `DIRTY_CHILD` (全再構築) が走る退行を検知する。
- 同じ Boundary に wrap text や conditional が同居していても、unrelated state update で `DIRTY_LAYOUT` / `DIRTY_CHILD` が混入しないことを検知する。
- `LAYOUT` update で `fullRebuild=false` が維持され、native context identity が保たれることも検証対象に含める。

### B. Static / Dynamic Policy Assertion

- STATIC: `SetState()` の直後に反映されることを検証。
- DYNAMIC: `requestInvalidate()` / deferred path では `WaitNextTick()` or `FlushSceneInvalidation()` 前は未反映、後に反映されることを検証。

```cpp
Step(SET_STATIC,  SetState(staticState, 1))
  | Step(ASSERT_IMMEDIATE, CaptureScene().expect(ValueEquals("StaticText", "1")))
  | Step(SET_DYNAMIC, SetState(dynamicState, 1))
  | Step(ASSERT_NOT_YET, CaptureScene().expect(ValueEquals("DynamicText", "0")))
  | Step(WAIT_TICK, WaitNextTick())
  | Step(ASSERT_AFTER_TICK, CaptureScene().expect(ValueEquals("DynamicText", "1")));
```

### C. Dump Dirty Reason Trace

失敗時 `Dump()` に以下を含める:
- dirty 発生 Boundary
- dirty flags
- trigger になった State ID / Name
- tick ID

これにより「なぜ汚れたか」を追跡可能にする。

### D. Timing Regression Assertion

- 見た目/値だけでなく実行時間も回帰検知対象にする。
- 例: `FlushTimeMs`, `RecomposeTimeMs`, `LayoutTimeMs` を `.snap` に記録し、CI で前回との差分を判定する。
- v0.0.1 の最小実装は単純閾値アサート (`AssertTimingLessEqual("timing.flush_ms", 16)`) とする。
- 判定時は OS ぶれ対策として許容幅を持つ（例: `<= baseline + 2ms`）。
- 連続 N 回中 M 回超過で FAIL とする運用を推奨（例: 5 回中 3 回超過で FAIL）。
- 68k/Toolbox は jitter が小さいため、v1 では単純閾値（1 回でも超過で FAIL）でもよい。
- Retro68/Toolbox で ms 精度が取れない場合は tick (1/60s) 記録へフォールバックし、未取得値は `na` を許容する。

## Capture Output Policy

```
captures/
  golden/          ← 承認済みシーケンス・snap（CI artifact または git 管理）
  current/         ← 今回の run 出力
  diff/            ← 差分出力（外部ツールが生成）
```

- `current/` の per-run ディレクトリ名: `LokaTest-<locale>-<timestamp>/`
- File-safe timestamp format only (no `:` or `/`)
- Toolbox/retro targets should enforce limits:
  - max files
  - max total bytes
  - delete oldest first when limit is exceeded (for both limits)
  - if one record itself exceeds `max_total_bytes`, reject write
- 68k/Retro68 では `CaptureScene` の既定をテキスト snap (Node tree + bounds) にする。
- bitmap screenshot は明示 opt-in のみ（メモリ/ディスク制約対策）。
- capture layering should stay explicit:
  - `CaptureNode<T>` for lightweight node assertions
  - `CaptureScene` for scene/root metadata
  - `CaptureViewTarget -> CaptureViewBitmap` for platform/native screenshots

## Config Hook (`LokaTest.cfg`)

When present in the executable directory, load optional test settings:

- `capture_dir`
- `max_files`
- `max_total_bytes`

CI では `capture_dir` に workspace パスを明示的に渡し、artifact として収集する。設定がない場合はデフォルト値で動作する。

## CI Integration

- CI は主実行環境。PR ごとに `current/` と `golden/` を比較して自動判定。
- PASS / FAIL / SKIP を明示的に分離して報告する。
- 有料ビジュアルリグレッションサービス不要。Loka がデータを出力し、外部 CLI / AI が判定する。
- 時間系メトリクスも `.snap` diff に含め、性能退行を PR で自動検出する。
- Runtime verification for text relayout should include:
  - short -> long text
  - long -> short text
  - resize + text update

### AI Review Workflow (Suggested)

- PR で sequence diff と `.snap` diff を収集。
- 外部 AI が視覚差分の要約（退行候補/仕様変更候補）を作成。
- 人間レビューで承認した差分のみ `golden` 昇格を許可する。

### CI Failure Policy (v1)

- `FAIL`:
  - `format_version` がランナー非対応
  - `schema_version` 不一致
  - bounds/value の差分が許容外
  - dirty flags が期待に反する
  - timing が許容幅を継続超過
- `SKIP`:
  - 権限不足（global probe）
  - platform capability 不足
- `PASS`:
  - 上記に該当せず、比較対象との差分が許容内
- `scenario_version` 不一致は golden 更新要求として扱う（運用ポリシーで FAIL/保留を選択）。

## Alignment with Existing Design

### Flow Primitives Reused Directly

- `Step` for sequential actions
- `goto` / `loop` for retries and iterative scenarios
- Timeout and error handling built into the flow model
- No new control-flow concepts needed

### Dump() for Failure Diagnostics

- Serializes which Step was reached and the value of each State at the point of failure
- Flow state is already structured data, so extra instrumentation is minimal
- Reports can be assembled from a sequence of `Capture()` + `Dump()` results

### 68k Self-Testing

- Typical GUI test automation frameworks assume modern environments
- Flow-based testing runs on the same runtime as the app itself, enabling self-tests on 68k/Classic
- `.snap` format is plain text, readable by any external tool regardless of the host environment

## Status

In progress (v0.0.1 baseline partially implemented).

Implemented:
- `snap v1` serializer (flat `key\tvalue`, ASCII key sort, blank-line record separator).
- `SnapWriteAdapter` validation for required v1 keys.
- Snap flow builder (`SnapV1`) for common keys, dirty flags, timing metrics.
- Error fields (`error_code`, `error_msg`, optional `error_detail`) and `status=partial` with `na` timings.
- `SnapV1(...).snapFlowError(code)` helper for standard error snapshot fields.
- `onFailure` relay helpers (`captureSnapFlowError*`) to bridge `FlowError` into `SnapV1(...).snapFlowError(...)`.
- `SnapErrorDetailBuilder` for stable `error_detail` payload (`key=value;...`, escaped).
- `captureSnapFlowErrorWithDetail*` always prefixes `error_detail` with `error_kind` / `error_code`.
- Optional `source_step` for failure snapshots (where the error originated).
- `snapSourceStepFromId(id)` helper for stable `source_step` labels (`step#<id>`).
- Error capture contexts can set `sourceStepId` directly (preferred over raw `sourceStep` strings).
- `SnapErrorV1(...)` adapter for direct `SnapFlowErrorSnapshot -> snap v1` error record conversion.
- `SnapErrorV1(...)` also supports optional `dirty` and `timing.*` fields.
- `SnapErrorV1(...).status("partial")` can override default `error` status when needed.
- `LokaTest.cfg` loading for `capture_dir`, `max_total_bytes`, `max_files` parsing.
- `capture_dir` output path resolution.
- `max_total_bytes` enforcement on append.
- `max_total_bytes` enforcement per snap file (drop oldest first, reject only when one record is too large).
- `max_files` enforcement per snap file (keep latest N records).
- When both `max_files` and `max_total_bytes` are set, pruning continues until both constraints are satisfied.

Not yet implemented:
- Golden update policy automation.
- Strict CI defaults and thresholds.

## v0.0.1 Fixed Decisions

- `snap v1` is flat `key\tvalue` text.
- One record is a block of lines; blank line terminates a record.
- Keys in a record are output in ASCII ascending order.
- Nested data is expressed by dotted keys (e.g. `timing.flush_ms`), not indentation.
- Required keys:
  - `format_version`
  - `schema_version`
  - `scenario_version`
  - `test`
  - `step`
  - `node`
  - `tick`
  - `status`
- `node` is `test-id` and is the primary comparison key.
- Missing data handling:
  - `status` is one of `ok|partial|error`
  - unavailable values are written as `na`
  - when `status=error`, include `error_code` and `error_msg` (`error_detail` / `source_step` are optional)
- Keep serialization platform-independent (no platform-specific serializer dependency such as NSCoder).
- Toolbox/68k prefers text snapshots; bitmap capture is optional.

Deferred for v0.0.2+:
- default timing thresholds
- golden update policy
- strict CI defaults
- child-flow composition (`RunFlow(child)` / reusable check subflows)
- logical flow combinators for scenario semantics (`all`, `race`) after child-flow support is defined
