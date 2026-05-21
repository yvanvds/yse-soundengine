---
name: cleanup
description: Use after an issue's PR has been merged to wrap up the issue branch — close the issue, switch back to dev, fast-forward, and delete the local and remote feature branches. Triggers on phrases like "cleanup", "cleanup issue N", "wrap up <branch>", "post-merge cleanup", "I just merged the PR, clean up". Do NOT trigger before the PR is merged, on the `dev` or `master` branches themselves, or for cleanup of branches that were never linked to an issue (release-prep, sync PRs, etc.).
---

# Post-merge cleanup for an issue branch

CLAUDE.md establishes the project's branch flow: every change starts as a
GitHub issue, gets a branch named `<issue-number>-<short-slug>` from `dev`,
and PRs back to `dev`. Once the PR is merged, four loose ends remain:

1. The GitHub issue is still open (PRs merged to `dev` don't trigger
   GitHub's `Closes #N` auto-close — that only fires on the default branch
   `master`).
2. The local feature branch still exists.
3. The remote feature branch still exists (`deleteBranchOnMerge` is `false`
   on this repo).
4. Local `dev` is behind the merged PR.

This skill handles all four in one pass and leaves the workspace on a
fresh `dev` ready for the next issue.

## When to invoke this skill

Yes:
- The user says "cleanup", "cleanup issue N", "wrap up", or similar after
  confirming a PR merged.
- The user is on a feature branch whose PR is already merged.

No:
- The PR has not merged yet — the skill refuses by design (step 3 below).
- The current branch is `dev`, `master`, or a release branch — pass the
  feature branch name as an argument instead.
- The branch is a release-prep, dev→master sync, or other non-issue
  branch. The skill keys on the `<issue-number>-<slug>` naming pattern;
  branches that don't match need manual cleanup.

## Invocation

Two forms:

```
/cleanup            # infer the target from the current branch
/cleanup 110        # by issue number
/cleanup 110-sphinx-cdecl-warnings  # by branch name
```

When no argument is given, `git branch --show-current` is the target. The
issue number is the leading integer of the branch name, matched as
`^(\d+)-`. If the current branch doesn't match the pattern, bail with a
message telling the user to pass an explicit argument.

## Procedure

### 1. Resolve target

- If an argument was given:
  - All digits → treat as issue number; resolve the branch via
    `git branch --list "<n>-*"` (local) or
    `gh pr list --search "<n> in:title" --json headRefName` (remote, if
    local already gone).
  - Otherwise → treat as the branch name; extract issue number with the
    `^(\d+)-` regex.
- If no argument: use `git branch --show-current`; extract issue number
  the same way.
- Refuse if current branch is `dev` or `master` *and* no argument was
  given — there's nothing to infer.

### 2. Pre-flight checks

All four must hold or the skill bails with a clear error:

- `git status --porcelain` is empty (clean working tree). Otherwise
  switching branches would silently carry changes across or refuse.
- `gh pr list --head <branch> --state merged --json number,mergedAt`
  returns at least one merged PR. Capture the PR number for the close
  comment. If zero results, surface: *"No merged PR found for `<branch>`;
  did you mean to wait until after merge?"*
- `gh issue view <n> --json state` succeeds. If the issue is already
  closed, skip step 4 silently and continue.
- The feature branch is not currently `dev` or `master`.

### 3. Verify branch is no longer needed

`git log dev..<branch> --oneline` — if non-empty, the local branch carries
commits that aren't on dev. That usually means the PR was squash- or
rebase-merged and the local branch's commit SHAs differ from what landed.
**This is normal and expected** for squash merges. Note it; do not refuse.
Step 6's `git branch -d` will refuse on unsafe deletes anyway.

### 4. Close the issue

If the issue is open:

```sh
gh issue close <n> --comment "Resolved by #<pr> (merged to dev)."
```

Use `--reason completed`. Skip if already closed.

### 5. Switch to dev and pull

```sh
git checkout dev
git pull --ff-only origin dev
```

`--ff-only` prevents the skill from creating a merge commit if dev has
diverged locally (which would itself be a flag that something is off —
surface and stop).

### 6. Delete the local feature branch

```sh
git branch -d <branch>
```

Use `-d`, not `-D`. If git refuses because the branch isn't fully merged
(common with squash merges), surface the actual git error and ask the
user whether to force-delete:

> Local branch `<branch>` has commits not on `dev` (likely a squash
> merge — the change is on dev under a different SHA). Force-delete with
> `git branch -D <branch>`?

Wait for the user's reply before running `-D`. Don't quietly upgrade.

### 7. Delete the remote feature branch

```sh
git push origin --delete <branch>
```

If the remote branch is already gone (e.g., someone else deleted it via
the GitHub UI), `git push` will exit non-zero with a "remote ref does not
exist" message. Treat that specific error as success and continue.

### 8. Prune stale remote-tracking refs

```sh
git fetch --prune
```

Cleans up `origin/<branch>` so `git branch -a` reflects reality.

### 9. Report

```
Cleaned up #<n>:
  Issue:           closed (Resolved by #<pr>)
  Local branch:    deleted (<branch>)
  Remote branch:   deleted (origin/<branch>)
  Current branch:  dev (up to date with origin/dev)
```

If any step was skipped (issue already closed, branch already deleted),
note it explicitly rather than silently.

## Don'ts

- **Don't** force-delete the local branch (`git branch -D`) without
  asking. Squash-merge mismatch is the common reason `-d` refuses, but
  there's also the bad reason: the user forgot to push commits. Always
  surface and confirm.
- **Don't** run on a dirty working tree. Step 2 catches this; don't
  attempt to stash, commit, or discard on the user's behalf.
- **Don't** run `git pull` without `--ff-only`. A divergent local `dev`
  means something is off — surface it, don't auto-merge.
- **Don't** close the issue with a generic "done" comment. The "Resolved
  by #<pr> (merged to dev)" format matches the manual closes already in
  the history and gives readers a one-click hop to the PR.
- **Don't** chase other issues mentioned in the PR body (`fixes #X, also
  closes #Y`). The skill targets one issue — the one named by the branch.
  Multiple referenced issues are rare here; let the user handle them
  explicitly.
- **Don't** delete a remote branch that the merged PR doesn't own.
  Step 2's `gh pr list --head <branch>` is the authorization check.
- **Don't** run this skill on `release-*`, `release/*`, or any branch not
  matching `^\d+-`. Those are managed by the `release` skill or by hand.

## Example interaction

User: "/cleanup"

You:
1. `git branch --show-current` → `110-sphinx-cdecl-warnings`. Issue
   number 110.
2. `git status --porcelain` → empty.
3. `gh pr list --head 110-sphinx-cdecl-warnings --state merged` → PR
   #111, merged.
4. `gh issue view 110 --json state` → `OPEN`.
5. `gh issue close 110 --comment "Resolved by #111 (merged to dev)."` →
   closed.
6. `git checkout dev && git pull --ff-only origin dev` → fast-forwarded.
7. `git branch -d 110-sphinx-cdecl-warnings` → deleted.
8. `git push origin --delete 110-sphinx-cdecl-warnings` → deleted.
9. `git fetch --prune` → pruned `origin/110-sphinx-cdecl-warnings`.
10. Report.

## File / branch invariants this skill assumes

- Branches matching the issue convention are named `<n>-<slug>` where
  `<n>` is the GitHub issue number. CLAUDE.md §1.
- The `gh` CLI is authenticated.
- `dev` is the integration branch; merged PRs target `dev`.
- `deleteBranchOnMerge` is `false` on this repo (verified via
  `gh repo view --json deleteBranchOnMerge`). Remote-branch deletion is
  this skill's responsibility, not GitHub's. If that setting is ever
  flipped to `true`, step 7's "already gone" handling becomes the common
  path.
