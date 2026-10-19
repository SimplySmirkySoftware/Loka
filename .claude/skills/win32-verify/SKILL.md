---
name: win32-verify
description: Build and verify Loka on a local Win32 machine from WSL — vcvarsall via bat file, worktree setup, test evidence rules, assert-dialog hang workaround, screenshot capture. Use when a change touches win32/ or needs real-machine Win32 verification.
---

# Win32 real-machine build & verify (WSL → MSVC)

Hosted CI covers x64 Debug only; the native architecture and full-width-path
behavior are verified on a real machine here.

Never hardcode the Visual Studio path or the target architecture. Both differ
per rig, the install path does not follow the product year (VS 2026 lives under
`\18\`), and a wrong guess fails at `call`. Discover both in the bat file — see
Build below.

## Standard rig: an isolated VM

Since 2026-08-27 the standard rig for golden work and pre/post comparisons is
an isolated Windows guest in a VM, not bare metal. A VM is the only kind of
Win32 machine whose environment is reproducible (image + disabled updates +
named snapshot), which the golden-lifetime ruling requires of a long-term
baseline. **The condition is hypervisor-agnostic**: the reference rig is a
Hyper-V guest, but a Parallels, VMware, or UTM Windows guest that freezes the
same way qualifies identically — the Mavericks rig is already a Parallels VM
in the same image-backed column. Goldens are per-rig either way; two
hypervisors are two rigs. Bare metal remains for release evidence and
explained version-to-version comparison only.

One caveat travels with the hypervisor: the determinism observed on the
reference rig (all ten matrix cells bake and re-verify; the #496 multistable
settle has never manifested there) was measured on Hyper-V's virtual display
adapter. A different hypervisor's virtual GPU is a different renderer — before
trusting a new guest, probe a cell several times for a single content hash
the way #496 was characterized, rather than assuming the determinism carries.

- Hyper-V + WSL plumbing specifically: WSL and the Default Switch are
  separate NATs the host does not route between, so bridge exactly one ssh
  port with `netsh portproxy` bound to the WSL-facing address — an external
  switch would put the guest on the LAN and break the isolation the VM exists
  for. Both addresses move on host reboot; re-run the bridge then. (Other
  hypervisors' shared-NAT modes are typically reachable from the host
  directly and need none of this.) The rig's name, addresses, and credentials
  live outside this repository, with the rig descriptors.
- The interactive-session requirement below is a Windows-guest fact, not a
  Hyper-V one; it applies under any hypervisor.
- Anything that opens a window (bake, verify, scenario runs) must run in the
  guest's interactive console session, not the ssh session: register a
  scheduled task with an Interactive logon principal for the autologon user,
  have the script write its own output file, and poll that file over ssh. An
  ssh-run scenario dies at the capture stage reporting zero windows found.
  Such tasks run under the desktop lock (see "Several sessions share one
  guest").
- **Everything the rail touches lives on the guest's own virtual disk** — a
  normal clone on the guest, its build tree, its goldens, and its rig
  descriptor (`~/.config/loka/rigs/win32/` in the guest, untracked like every
  rig's). The Checkout-layout section below is the bare-metal flow; do not
  apply it here. A host-side worktree or share would put the goldens outside
  the checkpoint boundary, so a revert would restore the machine but not the
  baselines it is compared against — exactly the drift the VM exists to
  prevent. The guest builds with the same build.bat below.
- The checkpoint freezes the goldens with the machine: reverting to it also
  reverts any goldens baked after it.

### Several sessions share one guest

Agent sessions run in parallel and reach the same guest. On 2026-09-28 one
session ran `git checkout` in the guest's clone while another session was
building its candidate there. The second session's single-test runs then
executed the other branch's binary and reported every new test as
`Unknown test`. These rules keep one guest usable by several sessions:

- **Unit tests and non-golden verification builds run in a per-session
  worktree of the guest's clone:**
  `git -C <guest-clone> worktree add --detach <guest-clone>-<slug> <sha>`.
  - The worktree has its own `build\` tree.
  - It lives on the guest's disk, so the host-side-worktree warning above
    does not apply.
- **Golden bake/verify runs entirely in the guest's main clone, under the
  golden lock.** This includes checking out and building the candidate it
  verifies.
  - The rail reads the goldens *and* the scenario executables from the clone
    it runs in: `tests/win32/run-scenario.ps1` derives both from
    `$ProjectDirectory`.
  - A candidate built in a worktree and verified from the main clone would
    therefore run the main clone's stale binaries against the goldens.
  - The rig descriptor and the goldens stay in the main clone because the
    checkpoint freezes them together.
  - Outside the lock, never check out or build anything in the main clone.
- **Interactive tasks run one at a time across the guest, under the desktop
  lock.** The guest has one interactive desktop and no user input, so a new
  window from any process can take the foreground from another session's
  run. A focus pin whose window loses the foreground mid-run fails or prints
  a skip. In #1378, a helper injected to take the foreground once during
  `testWin32FocusCompletion` failed it in 10 runs out of 10 before #1497.
  - Take `<setup-folder>\desktop.lock` over ssh before `schtasks /Run`, not
    inside the task: starting a task opens its console window, which was the
    foreground in all three #1378 foreground-poller logs.
  - Release it after the task's result file appears. If no result file
    appears, end the task (`schtasks /End`) before releasing. A lock left by
    a session that has ended is cleared by the human, not by another session.
  - This covers every interactive task: unit runs, scenario cells, golden
    bake/verify, standalone apps, and builds started as a task (their console
    window alone can take the foreground from another session's run). A build
    run directly over ssh is not in the interactive session and needs no lock.
  - Golden work takes the golden lock first, then the desktop lock for each
    task it starts. Desktop-only work never takes the golden lock.
- **Take either lock atomically and release only your own.** A
  check-then-write lock lets two sessions both see no lock.
  - Take it by creating a directory without `-Force`: `New-Item -ItemType
    Directory <setup-folder>\golden.lock -ErrorAction Stop` (or
    `desktop.lock`) fails when the directory already exists.
  - Then write `owner.txt` inside it with the session and the purpose.
  - When the work ends, remove the directory only if `owner.txt` still names
    your session.
  - If you cannot take the lock, wait or ask. Do not delete another session's
    lock.
- **Give scheduled tasks names that carry the session's slug**, so that
  `schtasks /Create /F` cannot silently replace another session's task.

When the work ends, delete your scheduled tasks. Delete your worktree too,
unless a follow-up on the same issue still needs it.

## Checkout layout

- **The WSL ext4 clone is the source of truth.** Clone it with WSL `git`, not
  Git for Windows: Git for Windows checks out CRLF, WSL `git` has
  `core.autocrlf` unset, and every tracked file then reads as modified.
- **Work in a worktree on `/mnt/c`** (NTFS). Windows tools cannot see the ext4
  clone; create the worktree from it with `git worktree add`.
- **Silence the `/mnt/c` filemode noise once per worktree.** drvfs reports every
  file as 0777, so `git status` shows a `100644 => 100755` mode change for the
  whole tree. `core.filemode` is shared across worktrees, so set it per
  worktree instead of turning it off in the ext4 clone:

  ```sh
  git -C <ext4-clone> config extensions.worktreeConfig true   # once per clone
  cd <mnt-c-worktree>
  git config --worktree core.filemode false
  ```

- **Never pass complex arguments to `cmd.exe` directly** — WSL quoting mangles
  them. Write a `.bat` file in the worktree and run
  `cmd.exe /c <name>.bat > log.txt 2>&1` from that directory.
- **Full output to a file, never `| tail`** — piping hides progress and
  truncates the execution evidence.
- Delete the worktree when the PR merges. The bat files and the
  `golden.lrpk` the test binary writes from the worktree root are
  gitignored, so they cannot reach a commit if you forget.

## Build

`build.bat` (the two builds are both required — neither implies the other,
see `.github/workflows/windows.yml`). The first two lines resolve the toolchain, so
the same file works on any rig:

```bat
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSPATH=%%i
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (set VCARCH=arm64) else (set VCARCH=x64)
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" %VCARCH% || exit /b 1
cmake --preset win32-debug || exit /b 1
cmake --build --preset win32-debug || exit /b 1
cmake --build --preset win32-tests || exit /b 1
```

The presets are architecture-neutral — they use whatever `cl.exe` and
`ninja.exe` vcvarsall puts on `PATH`, so nothing below the `call` is per-rig.

## Test evidence rules

- Run the suite with the exit code captured, by its generated path (the CI
  workflow's invocation; a bare `LokaTestsWin32.exe` from the worktree root
  finds nothing):
  `build\win32\Debug\example\HelloWorld\LokaTestsWin32.exe > test-log.txt 2>&1`
  then `echo TESTEXIT=%ERRORLEVEL%`.
- **Evidence for "test N exists and runs" is `--list` plus a single-test
  run**, never a grep of the output banner (banners are each test's own
  printf and prove nothing).
- Full-width path coverage: single-run all three and check `EXIT=0`. They cover
  different seams, so naming a subset understates what a change needs.
  - `testWin32OpenWriteTruncateAcceptsFullWidthPath` — the write seam. It has no
    ACP gate at all, so it is positive coverage on any rig.
  - `testWin32OpenReadAcceptsFullWidthPath` — the read seam, plus a contrast pin
    that a narrow `fopen` still cannot reach the file.
  - `testWin32FileFromWidePathSurvivesToOpen` — the producer seam
    (`win32::FileFromWidePath`), which is where #15 was actually lost. **A change
    to a `W` entry point or to the open-file dialog needs this one**; the other
    two open a path we built ourselves and can pass without exercising it.
  - The two contrast pins skip only on a UTF-8 ACP (`GetACP() != CP_UTF8`,
    `tests/Win32FilePathTests.cpp:103,169`), so **any non-UTF-8 ACP
    discriminates** — 932 is one rig's value, not the requirement. Confirm the
    codepage rather than assuming it: on a UTF-8 ACP the pins print `[skip]` and
    the run proves less than the exit code suggests.

## Assert red → dialog hang (#210)

A failing `assert` in an MSVC Debug build raises an abort dialog and hangs
forever. To capture a red run:

1. Redirect stderr to a log **on the Windows side** (inside the bat).
2. Run under WSL `timeout`, e.g. `timeout 60 cmd.exe /c run-tests.bat`.
3. Reap with `taskkill.exe /IM LokaTestsWin32.exe /F`.
4. Read the assert message (`Assertion failed: ... file:line`) from the log.

This is also the mutation-testing loop: mutate → red assert captured from the
log → restore → green (precedent: #211, `plans/199-mutation-log.md`).

## Screenshots (visual verification)

Capture with a PowerShell script (`Add-Type` + `System.Drawing`/GDI screen
copy, or the repo's `scripts/mame-run.ps1` style as a reference), inspect the
PNG by Reading it. For PR embedding, push assets to the `pr-assets` branch and
link raw URLs (example: the comment thread on #162).
