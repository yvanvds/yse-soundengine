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

- `python yse.py release patch|minor|major --bump-only` — bumps `VERSION`
  in `system.hpp` and commits `Release vX.Y.Z`. No tag, no push. Requires
  a clean tree, a tag that doesn't exist yet, and the current branch to be
  exactly `release/vX.Y.Z-prep` for the computed version. Used in step 5.
- `python yse.py release --tag <commit>` — tags `<commit>` with
  `v<VERSION at that commit>` and pushes only the tag. Refuses an existing
  tag. Used in step 8 with `origin/master`. Both modes take `--dry-run`.
  The bare `yse.py release <level>` (bump + tag + push from master/dev) is
  still there, but it can't push to the protected `master`. Don't use it
  for releases.
- `.github/workflows/release.yml` — fires on `v*` tag push. Builds Linux
  x64, Windows x64, and Android multi-ABI archives and publishes them as
  GitHub release assets, with `--generate-notes` as the body (step 9
  replaces it).
- `.github/workflows/build.yml`, `benchmark.yml`, `documentation.yml`,
  `format.yml` — the prep PR runs them in full. The dev→master PR skips
  its PR-event runs (`head_ref == 'dev'`), because the push to `dev`
  already ran the same SHA. Its required "Build and analyze" check is the
  green result from that `dev` push.

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

### 5. Release-prep PR on dev: docs refresh + version bump

Branch: `release/vX.Y.Z-prep` from `dev`. The name has to match the new
version exactly, because `yse.py release --bump-only` refuses any other
branch.

```sh
git checkout -b release/vX.Y.Z-prep dev
git merge --no-edit origin/master   # usually a no-op, see below
```

**Why merge master first.** Each release's dev→master merge commit lands
on `master` only, and `dev` never gets it. Branch protection on `master`
runs in strict mode ("branches must be up to date"), so the *next*
dev→master PR would be reported as behind `master` until `dev` contains
that merge commit. The merge is safe and changes no files:

- At promotion time the previous `master` tip is an ancestor of `dev`. The
  merge base of the two is that old tip, which has no changes of its own
  on the master side, so GitHub's `--merge` commit `M` gets exactly the
  tree of the `dev` tip `D` it merged (`git diff D M` is empty; it was for
  #995).
- Later `dev` work `D'` descends from `D`, so `merge-base(D', M) = D`, and
  `M` brings no changes relative to `D`. Merging `M` into the prep branch
  therefore keeps the prep branch's tree unchanged (`git diff HEAD^1 HEAD`
  is empty). It only makes `M` an ancestor of `dev` again, which restores
  the precondition for the next release.

If `master` is already an ancestor of `dev`, git prints "Already up to
date" and nothing happens. If `git diff HEAD^1 HEAD` after the merge
shows changes, something reached `master` without going through `dev`.
Stop and ask the user.

Then the docs:

- `README.md` — only update the `# libYSE X.Y` header if the major or
  minor bumped (patch releases don't touch the README header). Skip on
  patch.
- `PROJECT_OVERVIEW.md` — delegate to the `project-overview-update`
  skill workflow (read meta header SHA, diff against HEAD, decide
  partial vs full). On a patch release this is usually a no-op or a
  small section touch; on a minor/major it is more often a partial
  rewrite of a few sections.
- `docs/release-notes/vX.Y.md` — the release-notes file for a minor or
  major release. Its body (everything below the first `---` rule) becomes
  the GitHub release body in step 9. A patch release reuses the
  `vX.Y.md` file only if the user wants hand-written notes. Otherwise
  `release.yml`'s generated notes stand.
- Sphinx `conf.py` reads VERSION from `system.hpp` automatically (per
  PR #89) — **do not** edit `documentation/` for the version. Only
  touch `documentation/source/` to close API-reference coverage gaps
  found in step 3b (missing `doxygenfile` directives or pages), or if a
  code change in this release also changed user-facing behaviour that
  the intro/tutorials describe.

Commit the docs with message:

```
release-prep vX.Y.Z: refresh README and overview
```

Then bump the version **on the same branch**, as its last commit:

```sh
python yse.py release patch|minor|major --bump-only
```

This writes `VERSION` in `system.hpp` and commits `Release vX.Y.Z`. It
doesn't tag and doesn't push. Push the branch and open the PR with
`gh pr create --base dev --title "Release vX.Y.Z prep"`.

### 6. Wait for prep-PR CI, merge to dev

Wait for the prep PR's checks to finish. Poll
`gh pr view <pr> --json statusCheckRollup` or `gh run view --json jobs`.
Don't trust `gh pr checks --watch`, which can exit 0 before the run has
even registered. When green, merge:

```sh
gh pr merge --merge <pr-number>
```

`--merge` matches the existing style. This PR is the only full CI matrix
run of the release. The bump can't break anything this run doesn't
already cover, and `release.yml` rebuilds every platform from the tag
anyway.

### 7. Promote dev → master

Wait until the `push` run on `dev` for the merge commit from step 6
finishes green. The dev→master PR skips its own PR-event runs and
borrows that result for the required "Build and analyze" check. Then:

```sh
gh pr create --base master --head dev \
  --title "Release vX.Y.Z" \
  --body "<short summary of what's in this release>"
```

Branch protection on `master` requires only "Build and analyze", runs in
strict mode, and has `enforce_admins` on, so there is no admin bypass. If
the PR reports "behind master", step 5's merge of `origin/master` was
skipped. `gh pr update-branch <pr>` adds the same tree-identical merge to
`dev`. Then wait for the new `dev` push run. When the check is green, merge with `--merge` (release commits should stay
individually identifiable on master — do **not** squash):

```sh
gh pr merge --merge <pr-number>
```

`master` now carries the bump, because it came in with `dev`. No direct
push to `master` and no separate bump PR is needed.

### 8. Tag the promotion merge commit

```sh
git fetch origin
python yse.py release --tag origin/master --dry-run   # expect: Git tag vX.Y.Z
python yse.py release --tag origin/master
```

This tags the dev→master merge commit (not the `Release vX.Y.Z` bump
commit beneath it) and pushes only the tag. `yse.py` derives the tag
name from `VERSION` at that commit. If the dry run shows anything other
than `vX.Y.Z`, `origin/master` isn't the merge from step 7. Stop.
The manual equivalent is:

```sh
git tag vX.Y.Z $(git rev-parse origin/master)
git push origin vX.Y.Z
```

The `release.yml` workflow fires on the `v*` tag push and builds +
publishes the GitHub release.

There is no sync-back PR. `dev` already has the bump from step 6. The
only commit on `master` that `dev` lacks is the tree-identical merge
commit, and the next release's prep branch picks it up (step 5).

### 9. Publish the release notes

`release.yml` publishes the release with `--generate-notes`. For a
release with a `docs/release-notes/vX.Y.md` file, replace that body once
the release exists (`gh release view vX.Y.Z` succeeds). The body is
everything below the first `---` rule:

```sh
sed '1,/^---$/d' docs/release-notes/vX.Y.md > "$SCRATCH/notes-vX.Y.Z.md"
gh release edit vX.Y.Z --notes-file "$SCRATCH/notes-vX.Y.Z.md"
```

Check the result with `gh release view vX.Y.Z`. Skip this step when the
release has no notes file.

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

  Branch:   release/vX.Y.Z-prep → dev → master (two PRs)
  Tag:      vX.Y.Z on the dev → master PR's merge commit on master
  Workflow: release.yml will publish Linux x64, Windows x64, Android multi-ABI archives
  Notes:    <docs/release-notes/vX.Y.md body | generated notes>

  C API drift check: <none | <list of headers worth reviewing>>
  Docs coverage:     <complete | <headers missing from c_api.rst / classes without a page>>

  Steps that will run unattended after you confirm:
    1. release/vX.Y.Z-prep from dev, merge origin/master in (tree-identical, usually a no-op)
    2. README/PROJECT_OVERVIEW.md/release-notes refresh, then yse.py release <kind> --bump-only
    3. prep PR → dev, wait for CI, merge
    4. dev → master release PR (carries everything since last release, bump included)
    5. wait for "Build and analyze", merge with --merge
    6. yse.py release --tag origin/master: tag the merge commit vX.Y.Z, push the tag
    7. release.yml fires on the tag — builds + publishes the GitHub release
    8. gh release edit --notes-file with the release-notes body (if there is one)
```

After "yes", run end-to-end without further pauses. Surface CI failures
immediately if they happen; do not retry destructive steps on your own.

## Don't

- Don't run the bare `yse.py release <level>` (no `--bump-only`). It
  tags and pushes the branch, which branch protection rejects on
  `master`. On `dev` it would put the tag on the wrong commit. The bump
  goes through the prep PR (step 5) and the tag through `--tag` (step 8).
- Don't push anything directly to `master`. Everything reaches it
  through the dev→master PR, and `enforce_admins` blocks a bypass.
- Don't open a separate bump PR to `master` or a master → dev sync PR.
  Both belonged to the old four-PR flow. The bump rides in with `dev`,
  and the next prep branch picks up the leftover merge commit.
- Don't tag the bump commit. Tag the **merge commit** of the dev→master
  PR (`origin/master` right after the merge). `release.yml` doesn't
  care which commit the tag points at, but with the tag on the merge
  commit, `git show vX.Y.Z` lands on the merge that brought the release
  to `master`, which is what readers expect. Earlier releases did the
  same (v3.0.0 points at #996's merge commit).
- Don't squash-merge the release PR — release commits should be
  individually identifiable in `master`'s history.
- Don't bump the version yourself in `system.hpp` — `yse.py release
  --bump-only` is the only sanctioned mutator. Direct edits skip the
  prep-branch-name check, the tag-exists check, and the clean-tree check.
- Don't edit `documentation/source/conf.py` for the version. It reads
  from `system.hpp` (PR #89).
- Don't run the skill if there's an open PR that should land before the
  release. Check `gh pr list --base dev --state open` first; flag any
  outstanding ones and let the user decide whether to wait.
- Don't merge other PRs into `dev` between steps 6 and 7. The dev→master
  PR would then carry unreleased, un-bumped work under the release's name.
- Don't run on a dirty working tree. `yse.py release` enforces this
  but step 5's prep work would already have failed earlier.
- Don't publish a release for a downgrade or skip-version. `yse.py
  release` doesn't support it; if the user asks for a custom version,
  bail out and tell them they need to do it manually.

## Example interaction

User: "Cut a patch release"

You:
1. Ask: "Patch / Minor / Major?" → user says Patch.
2. Read `YseEngine/system.hpp`: current is `3.0.0`. New is `3.0.1`.
3. `gh api repos/yvanvds/yse-soundengine/releases/tags/v3.0.1` → 404, OK.
4. `git log v3.0.0..HEAD -- YseEngine ':!YseEngine/c_api'`: 8 commits, no public-header touches.
5. `python yse.py build --release` → success, enum check passes.
6. Show the plan + confirmation gate. User confirms.
7. `git checkout -b release/v3.0.1-prep dev && git merge --no-edit origin/master` ("Already up to date": #999 synced master back after v3.0.0). Refresh PROJECT_OVERVIEW.md (incremental update). README header stays at "libYSE 3.0". Commit.
8. `python yse.py release patch --bump-only` → `Release v3.0.1` commit, no tag. Push + PR → dev + wait CI + merge.
9. dev → master PR "Release v3.0.1", wait for "Build and analyze", merge with --merge.
10. `git fetch origin && python yse.py release --tag origin/master`. release.yml fires.
11. No notes file for a patch → keep the generated notes.
12. Report URLs.

## File / branch invariants this skill assumes

- `YseEngine/system.hpp` is the single source of truth for VERSION.
- `dev` is the integration branch; `master` is the release branch.
- Every commit on `master` reaches it through a dev→master PR. So
  `master` differs from `dev` only by those PRs' merge commits, and each
  of them has the same tree as the `dev` commit it merged. Merging
  `origin/master` into the next prep branch is therefore always a tree
  no-op.
- Branch protection on `master`: required check "Build and analyze"
  only, strict mode on, `enforce_admins` on. `dev` is not protected.
- The `release.yml` workflow triggers on `v*` tag push on any branch but
  the project convention puts release tags on `master`.
- `release.yml` already builds Linux + Windows + Android multi-ABI and
  uploads to the GitHub release — we don't need to invoke `yse.py
  package` ourselves.
- `documentation.yml` rebuilds Sphinx + Doxygen and deploys to GitHub
  Pages on every push to `master`, so the docs site updates
  automatically when the release PR merges.
