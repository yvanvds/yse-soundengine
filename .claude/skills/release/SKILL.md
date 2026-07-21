---
name: release
description: Use when the user asks to cut a new release of libYSE — phrases like "release a new version", "cut a patch/minor/major release", "publish a new release", "ship X.Y.Z". Orchestrates version bump, doc refresh, C API drift audit, dev→master promotion, and tag-driven publication via the existing release.yml workflow. Do NOT use for branch-management tasks unrelated to a release, or for republishing an already-tagged version (that's a workflow_dispatch on release.yml, not this skill).
---

# Cutting a new libYSE release

A release is irreversible (the tag, the GitHub release artefact, and the
downstream binding regenerations all assume the version went out the door
and stayed there). This skill drives the workflow end-to-end with a single
confirmation gate at the start.

Authoritative version source: [`YseEngine/system.hpp`](YseEngine/system.hpp)
line `const std::string VERSION = "X.Y.Z";`. Everything else — Sphinx
`conf.py`, `dist/` archive names, the GitHub release title — derives from
that string.

## Tools the skill drives

- `python yse.py release patch|minor|major` — bumps `VERSION` in
  `system.hpp`, commits `Release vX.Y.Z`, tags `vX.Y.Z`, pushes both.
  Requires clean tree and current branch in `{master, dev}`. **Only runs
  the bump on `master`** — see step 5.
- `.github/workflows/release.yml` — fires on `v*` tag push. Builds Linux
  x64, Windows x64, and Android multi-ABI archives; publishes them as
  GitHub release assets.
- `.github/workflows/build.yml`, `benchmark.yml`, `documentation.yml` —
  the dev→master PR triggers these; we wait for green before merging.

## Procedure

### 1. Ask the user which bump kind

The trigger phrase rarely specifies. Prompt:

> Patch (`X.Y.Z+1`) / Minor (`X.Y+1.0`) / Major (`X+1.0.0`) — which?

Use `AskUserQuestion` with the three options and one "Other" for raw
input (e.g. they want to skip a number, which `yse.py release` does not
support and would have to be done manually).

### 2. Pre-flight on `dev`

```sh
git checkout dev
git pull --ff-only origin dev
git status --porcelain   # must be empty
```

Read `YseEngine/system.hpp` to get the current version. Compute the new
version. Confirm the tag does not already exist (`git tag -l vX.Y.Z` and
`gh api repos/:owner/:repo/releases/tags/vX.Y.Z` — the second catches
remote-only tags).

### 3. C API drift audit

The Dart bindings (and any other FFI consumer) are generated from
`YseEngine/c_api/include/yse_c/*.h` in a separate repository. If new
public C++ surface landed since the last release but wasn't mirrored
into the C bridge, downstream bindings will silently miss it. To catch
this:

1. Find the last release commit: `git log v$LAST..HEAD -- YseEngine/ ':!YseEngine/c_api'`
   — public headers (`*.hpp` not in `internal/` / `implementations/` /
   `synth/`) and `yse.hpp` that changed.
2. Inspect each changed header. Look for:
   - New public methods on classes already exposed via `yse_c/yse_*.h`
   - New public free functions / accessors
   - New enum values in `YseEngine/headers/enums.hpp` (compile-time
     `yse_enums_check.cpp` will catch missing mirrors, but only if a
     build runs — see step 4)
3. For each suspected drift, hand off to the `c-api-extend` skill
   (`/c-api-extend`) — it knows the conventions. Do NOT inline a wrapper
   yourself unless it is a single trivial accessor.
4. If drift exists and the user wants to ship the release before fixing
   it, document the gap in the release notes ("known to be missing from
   C API in this version").

### 3b. API-reference coverage audit (docs drift)

The Sphinx API reference enumerates C headers **explicitly** —
`documentation/source/api/c_api.rst` carries one `doxygenfile` directive per
`yse_c/*.h` header — so a brand-new header never appears on the docs site by
itself. (This bit the v2.3.x cycle: `yse_clip.h`, `yse_python.h`, and
`yse_bus.h` were all missing until the pre-v2.3.2 audit.) Check:

```sh
for h in YseEngine/c_api/include/yse_c/*.h; do
  grep -q "$(basename "$h")" documentation/source/api/c_api.rst \
    || echo "MISSING from c_api.rst: $(basename "$h")"
done
```

Every hit is a doc gap: add a matching `doxygenfile` block (under a fitting
section heading) in the release-prep PR (step 5).

The C++ pages have the same failure mode at class granularity: for each new
public class step 3 surfaced, confirm it renders somewhere under
`documentation/source/api/*.rst` (a genuinely new subsystem needs a new page
plus an `index.rst` toctree entry). There is no mechanical check for this
side — use step 3's changed-header list as the worklist.

### 4. Build sanity + enum mirror check

```sh
python yse.py build --release
```

A successful release-config build with `YSE_BUILD_C_API=ON` (the default)
implies `yse_enums_check.cpp` passed at compile time — enum mirrors in
`yse_c/yse_enums.h` are consistent with `YseEngine/headers/enums.hpp`. If
the build fails on `yse_enums_check.cpp`, the enums drifted; fix them
before continuing.

### 5. Refresh README and PROJECT_OVERVIEW.md (on dev, in a release-prep PR)

Branch: `release/vX.Y.Z-prep` from `dev`.

- `README.md` — only update the `# libYSE X.Y` header if the major or
  minor bumped (patch releases don't touch the README header). Skip on
  patch.
- `PROJECT_OVERVIEW.md` — delegate to the `project-overview-update`
  skill workflow (read meta header SHA, diff against HEAD, decide
  partial vs full). On a patch release this is usually a no-op or a
  small section touch; on a minor/major it is more often a partial
  rewrite of a few sections.
- Sphinx `conf.py` reads VERSION from `system.hpp` automatically (per
  PR #89) — **do not** edit `documentation/` for the version. Only
  touch `documentation/source/` to close API-reference coverage gaps
  found in step 3b (missing `doxygenfile` directives or pages), or if a
  code change in this release also changed user-facing behaviour that
  the intro/tutorials describe.

Commit the prep PR with message:

```
release-prep vX.Y.Z: refresh README and overview
```

Push and open the PR with `gh pr create --base dev`.

### 6. Wait for prep-PR CI, merge to dev

Use `gh pr checks --watch` on the prep PR. When green, merge:

```sh
gh pr merge --merge --auto <pr-number>
```

(Or `--squash` if the project convention is squash; check existing
release-prep PRs for the pattern. Looking at recent history,
`--merge` matches the existing style.)

### 7. Promote dev → master

```sh
gh pr create --base master --head dev \
  --title "Release vX.Y.Z" \
  --body "<short summary of what's in this release>"
```

Wait for CI on this PR too (`gh pr checks --watch`). When green, merge
with `--merge` (release commits should stay individually identifiable on
master — do **not** squash).

### 8. Run the bump and route it through a release PR

`master` is a protected branch — direct pushes are rejected, including
the one `yse.py release` would attempt. The bump has to land via PR.
The local-side prep mirrors what `yse.py release` does but stops short
of pushing to `master`:

```sh
git checkout master
git pull --ff-only origin master
python yse.py release patch|minor|major    # creates the bump commit + local tag,
                                            # then fails at `git push` due to branch protection
```

The push failure is expected — `yse.py release` does not yet know about
branch protection. Recover by moving the bump commit onto a release
branch and discarding the local tag (the tag has to land on the merge
commit, not the bump commit; matches the existing `v2.1.0`/`v2.0.2`
pattern):

```sh
git checkout -b release-vX.Y.Z      # carries the new "Release vX.Y.Z" bump commit
git branch -f master origin/master  # rewind local master to the unbumped origin tip
git tag -d vX.Y.Z                   # the tag goes on the merge commit later, not the bump
git push -u origin release-vX.Y.Z
```

Open the bump PR (this is the second PR in the release path; the first
was step 7's `dev → master` promotion). Title is the **same** as the
step-7 PR for searchability — it shows up as e.g. "Release v2.1.1" in
both PR lists.

```sh
gh pr create --base master --head release-vX.Y.Z \
  --title "Release vX.Y.Z" \
  --body "Version bump for the vX.Y.Z release. Routed through a PR because branch protection on master requires it (same path as release-v2.1.0 \\u2192 PR #87)."
```

Wait for CI (`gh pr checks --watch`). When green, merge with `--merge`.

Pull master, then **tag the merge commit** (not the bump commit) — this
matches how `v2.1.0` was tagged (`git show v2.1.0` → points at PR #87's
merge commit, not at `54d1d96 Release v2.1.0`):

```sh
git checkout master
git pull --ff-only origin master
git tag vX.Y.Z $(git rev-parse origin/master)
git push origin vX.Y.Z
```

The `release.yml` workflow fires on the `v*` tag push and builds +
publishes the GitHub release.

> **Skill follow-up:** `yse.py release` is mid-way: it bumps and tags
> locally but cannot push to a protected master. Either teach it about
> branch protection (open the release PR itself, wait, merge, tag the
> merge commit) or split it into two subcommands (`release-bump` that
> just bumps + commits on the current branch, and `release-tag` that
> tags an arbitrary commit and pushes the tag). Until that lands, the
> dance above is the manual fallback.

### 9. Sync the release commit back to dev

```sh
git checkout dev
git pull --ff-only origin dev
gh pr create --base dev --head master \
  --title "Sync Release vX.Y.Z bump back to dev" \
  --body "Brings the version bump commit and tag back to dev so the two branches stay in sync."
```

Merge **immediately** with `--merge` — no CI wait. `build.yml`,
`benchmark.yml`, `release.yml`, and `documentation.yml` all skip the
master → dev sync PR (either by trigger filter or by the
`head_ref == 'master'` guard in `build.yml`'s job condition). Every
commit on master already passed CI on the PR that put it there. After
merge, fast-forward local dev and confirm it's at the same tip as
master.

### 10. Report status to the user

```
Released vX.Y.Z.
- Tag pushed:           https://github.com/yvanvds/yse-soundengine/releases/tag/vX.Y.Z
- Release workflow:     https://github.com/yvanvds/yse-soundengine/actions
- Wait ~5–10 minutes for the multi-ABI build matrix to finish; artefacts
  are uploaded as release assets automatically.
```

Optionally poll `gh run watch` on the release-workflow run if the user
wants confirmation that the artefacts uploaded.

## Confirmation gate

Before doing anything destructive, show the plan and stop **once**:

```
About to release vX.Y.Z:

  Current:  v<cur>
  New:      v<new>  (<bump-kind>)

  Branch:   dev → master → release-vX.Y.Z bump → master (three PRs)
  Tag:      vX.Y.Z on the bump PR's merge commit on master
  Workflow: release.yml will publish Linux x64, Windows x64, Android multi-ABI archives

  C API drift check: <none | <list of headers worth reviewing>>
  Docs coverage:     <complete | <headers missing from c_api.rst / classes without a page>>

  Steps that will run unattended after you confirm:
    1. release-prep PR on dev (skill housekeeping + README/PROJECT_OVERVIEW.md refresh)
    2. wait for CI, merge to dev
    3. dev → master release PR (carries everything since last release)
    4. wait for CI, merge to master
    5. yse.py release <kind> locally → push fails on protected master (expected)
    6. move bump commit to release-vX.Y.Z branch, open PR → master
    7. wait for CI, merge with --merge
    8. tag the merge commit with vX.Y.Z, push tag
    9. release.yml fires on the tag — builds + publishes the GitHub release
   10. master → dev sync PR (no CI wait — sync PRs are skipped by every workflow)
```

After "yes", run end-to-end without further pauses. Surface CI failures
immediately if they happen; do not retry destructive steps on your own.

## Don't

- Don't run `yse.py release` on `dev`. Even though the script allows
  it, the project convention is that release tags live on `master`.
- Don't try to push the bump commit directly to `master` after a
  rebound `yse.py release` push fails. Master is protected; the bump
  has to ride through its own PR (step 8). The expected end state of
  `yse.py release` on a protected master is: local bump commit, local
  tag, push rejected, you take it from there.
- Don't tag the bump commit. Tag the **merge commit** of the bump PR —
  matches how `v2.1.0` (and earlier) were tagged. `release.yml` doesn't
  care which commit the tag points at, but having it on the merge
  commit means `git show vX.Y.Z` lands on the merge that introduced
  the bump to `master`, which is what readers expect.
- Don't squash-merge the release PR — release commits should be
  individually identifiable in `master`'s history.
- Don't bump the version yourself in `system.hpp` — `yse.py release`
  is the only sanctioned mutator. Direct edits skip the tag push, the
  branch-name check, and the clean-tree check.
- Don't edit `documentation/source/conf.py` for the version. It reads
  from `system.hpp` (PR #89).
- Don't run the skill if there's an open PR that should land before the
  release. Check `gh pr list --base dev --state open` first; flag any
  outstanding ones and let the user decide whether to wait.
- Don't run on a dirty working tree. `yse.py release` enforces this
  but step 5's prep work would already have failed earlier.
- Don't publish a release for a downgrade or skip-version. `yse.py
  release` doesn't support it; if the user asks for a custom version,
  bail out and tell them they need to do it manually.

## Example interaction

User: "Cut a patch release"

You:
1. Ask: "Patch / Minor / Major?" → user says Patch.
2. Read `YseEngine/system.hpp`: current is `2.1.0`. New is `2.1.1`.
3. `gh api repos/yvanvds/yse-soundengine/releases/tags/v2.1.1` → 404, OK.
4. `git log v2.1.0..HEAD -- YseEngine ':!YseEngine/c_api'`: 8 commits, no public-header touches.
5. `python yse.py build --release` → success, enum check passes.
6. Show the plan + confirmation gate. User confirms.
7. Branch `release/v2.1.1-prep` from dev, refresh PROJECT_OVERVIEW.md (incremental update). README header stays at "libYSE 2.1". Commit + push + PR + wait CI + merge.
8. dev → master PR, wait CI, merge.
9. `git checkout master && python yse.py release patch`. Push fails on protected master (expected).
10. `git checkout -b release-v2.1.1 && git branch -f master origin/master && git tag -d v2.1.1`. Push branch, open PR → master, wait CI, merge.
11. `git checkout master && git pull && git tag v2.1.1 $(git rev-parse origin/master) && git push origin v2.1.1`. release.yml fires.
12. master → dev sync PR, merge.
13. Report URLs.

## File / branch invariants this skill assumes

- `YseEngine/system.hpp` is the single source of truth for VERSION.
- `dev` is the integration branch; `master` is the release branch.
- The `release.yml` workflow triggers on `v*` tag push on any branch but
  the project convention puts release tags on `master`.
- `release.yml` already builds Linux + Windows + Android multi-ABI and
  uploads to the GitHub release — we don't need to invoke `yse.py
  package` ourselves.
- `documentation.yml` rebuilds Sphinx + Doxygen and deploys to GitHub
  Pages on every push to `master`, so the docs site updates
  automatically when the release PR merges.
