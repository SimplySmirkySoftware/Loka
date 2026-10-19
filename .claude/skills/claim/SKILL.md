---
name: claim
description: Coordinate parallel sessions on this machine using local issue and resource claims. Load before the first investigation step on a GitHub issue or PR follow-up, or before touching a shared Win32 VM golden lock, guest worktree, or MAME golden bake.
---

Use `.claude/skills/claim/claim.sh` from the repository root.
At session start, run `claim.sh list` using that path.
Choose a short owner slug such as `opus-0929a` and keep it for the session's
lifetime; different sessions must use different slugs.

Before investigating an issue, take `issue-<n>`. For PR follow-up, use `pr-<n>`;
keep the issue claim too if the work continues that issue. Before touching a
shared rig resource, take `vm-<thing>` (for example `vm-golden` or
`vm-worktree-vmred`); use a shared agreed key such as `mame-golden` for a bake.

```bash
.claude/skills/claim/claim.sh take issue-877 --as opus-0929a --purpose '#877 release hang'
.claude/skills/claim/claim.sh check issue-877
.claude/skills/claim/claim.sh list
.claude/skills/claim/claim.sh release issue-877 --as opus-0929a
```

A refused take means choose other work or coordinate with the holder.
Keep the GitHub "picking this up" comment as the human-visible record.
Release when work is merged, abandoned, or handed off; include `list` output
in handoffs, and have the receiving session take the released claim.
Claims persist outside repositories in `${LOKA_CLAIMS_DIR:-$HOME/.loka-claims}`;
all sessions must use the same directory. This coordinates only one host.

The TTL defaults to 24 hours (`LOKA_CLAIM_TTL_HOURS`). `check` returns 0 for
free or stale, 1 for held; a stale result is advisory, never an acquisition.
`take` never steals. Inspect stale work before `release KEY --as OWNER --force`,
then take it. Force refuses fresh claims. Same-owner take refreshes nothing.
`INCOMPLETE` is held while fresh and becomes stale after the TTL; missing or
invalid `taken` uses directory mtime. Stale incomplete claims allow `--force`.
`list` warns about old `.gone.*` release leftovers; inspect and remove them by
hand. A release race preserves a replacement claim or reports its conflict path.
Invalid arguments exit 2. Keys and owners use `[a-z0-9][a-z0-9._-]*`.

The Win32 VM golden lock and desktop lock in
[win32-verify](../win32-verify/SKILL.md) remain the authority for the guest
itself. A `vm-golden` claim is the host-side courtesy note, not a replacement
for the golden lock; no claim stands in for the desktop lock.
