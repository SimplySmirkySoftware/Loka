---
name: codex-delegate
description: Delegate routine implementation or GitHub work to the Codex CLI without the known traps — stdin, orphan processes, connector write limits. Use whenever launching codex exec from a session.
---

# Delegating to Codex CLI

Fleet roles: Fable = design/review/commit, Codex = routine implementation and
GitHub operations, Sonnet = domain research, Haiku = cross-tree sweeps. Codex
works in the fleet clone `~/loka` (WSL ext4), never in the user's `/mnt/c`
checkout; sync goes through origin only.

## Launch rules (each one paid for in lost time)

- **Background `codex exec` MUST redirect stdin**: append `< /dev/null`.
  With stdin open it prints "Reading additional input from stdin..." and
  waits forever (32 minutes lost once).
- **Full output to a file, never `| tail`** — piping hides progress and
  truncates the execution evidence.
- **Before re-issuing a request, check for orphans**: `pgrep -af codex`.
  A previous run may still be alive; non-idempotent operations (pushes, issue
  comments, file moves) risk double execution.
- **Outside a git repository, pass `--skip-git-repo-check`.** Running from
  `/tmp` otherwise dies with "Not inside a trusted directory". This comes up
  whenever the point of the run is to keep Codex *away* from the repository —
  e.g. re-implementing something from its spec alone as an independence check.
- `--full-auto` is deprecated; use `--sandbox workspace-write`.
- **Do not wait on `pgrep -f "codex exec"`** — the pattern matches the waiting
  shell's own command line, so the loop reports "still running" forever (two
  finished runs were watched for over an hour, 2026-08-30). Wait on a
  process-completion sentinel in the log instead. The `===== REPORT =====`
  marker alone is not one — a run that dies before printing it leaves the
  loop waiting forever — so write the exit status from the same subshell
  that ran the exec:

  ```sh
  ( codex exec --sandbox workspace-write -C "$WT" "$(cat brief.md)" \
      < /dev/null > run.log 2>&1; echo "codex exit=$?" >> run.log ) &
  ```

  `codex exec ... & echo "codex exit=$?"` is wrong: it records the launch
  status immediately, not Codex's. Then wait for `grep -q '^codex exit='
  run.log` — it fires on success and on abnormal exit alike. `pgrep -af
  codex` is still right for the orphan check *before* a launch, where a false
  positive only costs a look.
- **`--sandbox workspace-write` blocks the network, so `git push` and `gh pr
  create` always fail** — the sandbox only allows `[workdir, /tmp, $TMPDIR]`
  and no outbound traffic. Left to its own devices Codex clones into `/tmp`,
  re-points `origin` at the GitHub URL, runs `gh auth status`, and reports a
  `gh could not authenticate` / "run `gh auth refresh`" error. **That error is
  the sandbox, not a real auth problem.** Do not chase it. **Every
  implementation brief must say: commit and STOP; do not push, do not `gh pr
  create` — the delegator pushes and opens the PR.** Then recover the commit
  (it is on the branch Codex made, and/or as uncommitted changes on the
  working clone's HEAD — check both) and push it yourself from a worktree.
  Seen on #488, #491.
- **In a linked worktree Codex usually cannot commit at all.** A worktree's
  `.git` is a file pointing at the parent clone's `.git/worktrees/<name>/`,
  which lies outside the sandbox's write root, so `git commit` dies with
  `index.lock: Read-only file system`. An individual run occasionally succeeds
  (environment variation), so brief for both outcomes: **"commit if you can; if
  the commit fails, stop and leave the work dirty, with the results in
  `FINDINGS.md`"** — the delegator commits at harvest either way. The split is
  not only a workaround: nothing lands without having been read. Measured
  2026-08-28.
- **Nor can it `git cherry-pick` or `rebase` there** — the sequencer directory
  lives under the same read-only `.git/worktrees/<name>/`, so a brief that
  opens with "cherry-pick the prerequisite branch first" dies at step one
  (`could not create sequencer directory ... Read-only file system`,
  2026-09-07, one run lost; a second run worked around it with `git apply`
  and fused four prerequisite commits with its own work into one 45-file
  commit that had to be re-split by hand). Stack the prerequisite commits on
  the worktree yourself before launching, and say in the brief that HEAD
  already carries them.
- **MAME does not launch from the sandbox** (`UtilBindVsockAnyPort: socket
  failed`, WSL vsock). A brief that asks Codex to run scenario cells gets
  "blocked at emulator launch" back; the MAME leg is always the delegator's.
  Brief Codex to stop at the Retro68 build and list the cells to run.
- **A worktree has no `build/host/lrpc/lrpc`**, and the six scrapbook cells
  refuse without it. Build it before every candidate bake, not once per
  worktree (`cmake -S tools/lrpc -B build/host/lrpc && cmake --build
  build/host/lrpc`): the runners only check that the binary exists, so a
  worktree that moved to another commit or touched `tools/lrpc` would stage
  assets with a stale tool while the bake reports the current candidate.
  Forgetting the build cost a partial bake twice (2026-09-06, 2026-09-07).

## Resuming instead of re-briefing

A run that came back incomplete — a leg skipped, the smell list missing, a
question it should have asked — is cheaper to continue than to re-issue.
`codex exec resume <SESSION_ID> "<follow-up>"` replays that session's context,
so the follow-up does not re-pay for the repository walk the first run already
did; `--last` picks the most recent session for this cwd (`--all` disables the
cwd filter). `codex exec fork <SESSION_ID>` branches a session instead, for
trying a second approach without spending the first. Both subcommands are
verified against `codex-cli 0.149.1`; `fork` did not exist under `exec` in
older 0.144 alphas, so check `codex exec --help` before writing a brief around
it on an older CLI.

Every launch rule above still applies to a resume — `< /dev/null`, full output
to a file, and orphan check first. **Global exec flags go BEFORE the
subcommand**: `codex exec --sandbox workspace-write -C <dir> resume
<SESSION_ID> "<follow-up>"`. Putting `--sandbox`/`-C` after `resume` dies
instantly with `error: unexpected argument` (one launch lost, 2026-08-31);
the resume parser accepts many exec options of its own (`-m/--model`,
`-i/--image`, `--enable/--disable`, `--skip-git-repo-check`, …) but NOT
`--sandbox` or `-C` — check `codex exec resume --help` for the exact list on
your CLI version rather than assuming, and a cheap syntax probe before the
real launch is
`codex exec <flags> resume --help > /dev/null; echo $?`. The orphan check matters more here, not
less: a resume that races the original run's leftovers double-executes whatever
the follow-up asks for. Session ids are in the transcripts under
`~/.codex/sessions/<JST date>/rollout-*.jsonl`.

## Connector limits

- Codex **reads** Notion fine, but **cannot write**: the app-connector write
  approval is auto-cancelled in non-interactive runs, and `--full-auto` does
  not override it. For a Notion write, an interactive codex session with one
  manual approval is the shortest path.

## Briefing a change that adds tests

- **A new test header must be included in both test mains**:
  `tests/ContractTestMain.cpp` *and* `tests/FlowDslTestMain.cpp`. The
  macOS and Win32 test targets compile only the second one, so a header
  included only in the first passes Linux CI and fails macOS/Win32 CI (PR
  #532, one extra round trip). Put the sentence in the brief; Codex does not
  find the second main on its own.
- In a test that projects nodes through `NullScenePlatformController`, declare
  the controller before the nodes so it outlives their terminal fact delivery.
- Run `python3 tools/ci/check_test_asserts.py` after changing test assertions.
  `LOKA_VERIFY(x.call())` registers the call name as load-bearing, so the audit
  will reject plain `assert(y.call())` occurrences with the same name. Convert
  genuinely load-bearing assertions; when the same name also has
  non-load-bearing uses, bind the result to a local and verify the local. Never
  replace `LOKA_VERIFY` with `assert` merely to silence the audit: `assert`
  removes the call under `NDEBUG`.

## Always ask for the smell list (required deliverable)

Every implementation brief must require, **as a section of the PR body**, a
ranked list naming the places the delivered shape reads as bolted-on.
docs/ReviewPolicy.md "Shape Review Gates" gate 2 item 5 puts the findings in
the PR body; a scratch
file while working is fine but **must not be committed** — say so in the brief.
Asking for a `.<slice>-smells.md` deliverable without that caveat put four
stray markdown files on the ScrapbookUI stack and cost a rebuild to remove
them. Word the requirement so the walk cannot be skipped by default (there is no numeric quota; the quota produced padding on small diffs, #729):

> Walk the review lenses below and, for each, either name the finding (ranked
> by how much it would cost to fix later) or say what you considered and why
> it does not apply. The lenses: a door or field that duplicates the shape of
> an existing one; a check whose absence in some build configuration is worse
> than "undetected"; a step added to one reclamation or teardown path that
> other paths reaching the same point still lack; a field a type never reads
> itself; an API shape chosen for test observability. Separately, enumerate
> every primitive member (`bool`, counter, index) the change adds to an
> existing type, each with its single writer, every reader outside the owning
> type, and the existing phase, type, or return value it could not be derived
> from — an added flag or counter is returned for reshaping unless that line
> exists, and a foreign reader fails it even when the writer is unique. For
> every door or call path the change adds, one cost line: who calls it, how
> many times per update cycle, and whose rows it walks — a walk over rows
> another owner holds is a return-for-reshaping finding, not a medium smell.
> "No applicable structural change" is an accepted answer only with the lens
> walk written out; "looks good" without it is not. For a class E or C change
> (docs/ReviewPolicy.md "Review Paths") the lens walk is replaced by that
> class's required statement.

Reason for the forced output: a passive escape hatch does not fire. The S3
brief said "stop and write a questions file if the rulings underdetermine
something"; the file came back absent while the review found three real holes,
one of them exactly an underdetermined ruling. This mirrors
docs/ReviewPolicy.md "Shape Review Gates" gate 2, which the delegator runs on
the same diff.

## Pre-PR review fixed point

The goal is one independent review of a complete change, not repeated review
as a substitute for finishing its design.

1. **Apply the repository complexity gate before delegation.** A change that
   crosses its split or hard-stop thresholds returns to design; extra review
   rounds do not make an over-broad shape safer. For deferred, queued, cached,
   or ledgered work, put one validity invariant and the concrete events that
   invalidate it in the implementation brief, then pin those failure modes.
   Put the anticipated flags and their required review lenses and evidence in
   the brief. Reconcile them against the candidate revision and publish the
   result in the PR body as `Review risk profile`; explicitly record a zero-flag
   result. Use the catalog and thresholds in docs/ReviewPolicy.md instead of
   restating them here.
2. **Produce one candidate revision.** First run the affected checks that can
   inspect the working tree. Then commit, confirm clean status, and record the
   candidate SHA, merge base, and changed-file list. Run exact-ref platform
   rigs and one fresh-context adversarial pass against that same SHA. Use the
   owning workflow or platform skill rather than copying command lists here;
   if local and CI execution must stay identical, extract a shared script.
   State unavailable rig coverage in the PR; hosted CI remains the verdict for
   its runner identity. A candidate exists only when all claimed evidence names
   that revision.
3. **Make the adversarial pass independent and explicit.** Use `codex review
   --base <base>` from the clean candidate worktree, or a read-only REFUTE
   `codex exec` brief. A same-session self-review is not independent. Procedure
   and design-document changes receive the same cross-source check against the
   documents, workflows, and scripts they cite. A run that exits without an
   explicit verdict is not a completed pass.
4. **Any edit invalidates the candidate.** Collect and classify the whole pass
   before editing, make one coherent correction, and produce a new candidate
   from step 2. If two candidate revisions are rejected for new defects on the
   same design, ownership, lifecycle, update-flow, or verification axis, stop
   adding case rules: return the change to design and split or reshape it.
5. **Give the opened or readied PR one bot-review trigger.** Count an automatic
   review started by opening or leaving Draft; use `@codex review` only if no
   review started. Treat reports as claims, batch all confirmed findings into
   one correction, verify the result, and request at most one final full
   review. Do not request a new review after each individual fix. Any confirmed
   finding from that final review returns the PR to Draft: reshape it when the
   finding belongs to this change, or route a pre-existing defect to its owning
   issue. Do not start a third full review.

## Reviewing what comes back

- Treat Codex reports as claims: independently re-verify key results
  (test counts, exit codes) rather than quoting them.
- Read the smell list before reading the diff, then review the diff without it
  — the list is a hypothesis set, not a substitute for looking.
- The Codex PR-review bot's comments on GitHub PRs must always be read and
  answered before merge.

- **`--sandbox read-only` also blocks the deliverable file.** A REFUTE/audit
  run briefed to write `FINDINGS.md` under `~/codex-briefs` finished its whole
  audit and then failed the single write (2026-08-30, 277k tokens). For
  read-only audits either use `--sandbox workspace-write -C ~/codex-briefs`
  with an explicit "do not modify the repository" instruction, or brief the
  run to print the document to stdout (the log captures it). Recovery is
  `codex exec resume <SESSION_ID> "print the complete findings to stdout"` —
  the session id is the first line of the exec log (`session id: ...`).
