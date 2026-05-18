---
name: fix-issues
description: Use when fixing compiler warnings, static analyzer findings (clang-tidy, etc.), or runtime errors/crashes in the libYSE codebase. Enforces the project's performance-first stance: never trade audio-thread speed for stylistic cleanliness, never modify vendored dependencies, never broaden scope beyond the reported issues.
---

# Fixing warnings, analyzer findings, and runtime errors in libYSE

libYSE is a real-time C++ sound engine. Audio-callback code runs at buffer rate
on a dedicated thread and must remain lock-free and allocation-free. "Cleaning
up" code here is not free: a well-meaning fix can introduce a glitch, a click,
or a crash that is far worse than the original warning.

This skill governs how to triage and fix reported issues. Read it fully before
making any change.

## Issue tracking — GitHub Issues only

All bugs, follow-ups, and known-issue notes live in GitHub Issues on
`yvanvds/yse-soundengine`. Do **not** add new entries to a local
`KNOWN_ISSUES.md` or any other in-repo issue list — that file has been
retired and removed.

- **Read context:** before starting a fix, check the relevant issue with
  `gh issue view <n>`. Look for prior repro steps, fix sketches, and
  workarounds-in-tests already documented.
- **File new findings:** if a fix surfaces a separate bug, file it with
  `gh issue create --title "..." --label bug --body-file ...` rather than
  inlining a TODO comment that no one will find later. Workarounds in test
  code should reference the issue number (e.g. `// see #29`) instead of a
  file path.
- **Close on landing:** when a fix is merged, close the issue with
  `gh issue close <n> --reason completed`. If the fix is partial, leave
  the issue open and edit the body to describe what is still outstanding.
- **Search before filing:** `gh issue list --search "<keyword>"` —
  duplicates are easy to make when descriptions live across multiple
  subsystems.

The `gh` CLI is authenticated in the project environment; no MCP server or
extra setup is required.

## Core principles

1. **Correctness bugs first, cosmetic warnings second, never both in one pass.**
   A dangling pointer and an unused-parameter warning are not the same problem.
   Fix the bug properly. Silence the cosmetic warning with the smallest
   possible change.

2. **Performance is non-negotiable on the audio thread.** Anything inside or
   reachable from `process()` methods on `dspObject` / `dspSourceObject`,
   the PortAudio/OpenSL ES callback path, or the lock-free message queue
   consumer must not get slower. If a "fix" adds branches, allocations,
   virtual dispatch, atomic ops, or mutex acquisitions on this path,
   it is the wrong fix.

3. **Stay in scope.** Fix only what was reported. Do not refactor, modernize,
   reformat, rename, add `[[nodiscard]]` everywhere, swap containers, sprinkle
   `noexcept`, or "improve" anything adjacent. Each of those is a separate
   decision the user has not made.

4. **Never modify vendored dependencies.** Anything under `dependencies/`
   (doctest, portaudio headers, rtmidi, libsndfile) is off-limits. Suppress
   warnings from these at the CMake target level or with a localized pragma
   around the include site, not by editing the header.

## Triage: classify every reported issue before touching code

For each warning, analyzer finding, or error, decide which bucket it falls into:

### A. Real bug
Examples: returning a pointer into a destroyed temporary, use-after-free,
uninitialized read, missing `override` that hides a base method, data race,
mismatched `new`/`delete`, signed/unsigned comparison that actually overflows.

These get fixed properly, even if the fix is bigger than a one-liner. A real
bug in audio-thread code is the highest priority issue in this codebase
because it manifests as glitches/clicks/crashes that are very hard to debug.

### B. Cosmetic / pedantic warning
Examples: `__COUNTER__` is a C2y extension, `const` qualifier on a return
value type, unused parameters in virtual base methods with empty default
implementations, unused-but-set variables in debug-only paths.

These get silenced with the minimum-impact mechanism (see below). Do not
restructure code to satisfy a stylistic warning.

### C. Maybe-bug-maybe-not
Examples: an overloaded virtual that hides the base, a signed/unsigned
comparison in a loop bound, a "may be uninitialized" the compiler isn't sure
about.

Investigate. If it is a bug → bucket A. If the compiler is being conservative
and the code is correct → silence with `[[maybe_unused]]`, `static_cast`,
explicit initialization, `using Base::method;`, etc. Do not silence by
disabling the warning globally.

### D. Vendored / third-party
The reported file lives under `dependencies/`. Never edit. Suppress at the
build-system level (target-scoped `-Wno-...`) or with a `#pragma clang
diagnostic push`/`ignored`/`pop` block around the `#include` of the vendored
header in our own code.

## Performance rules

Before applying any fix, identify whether the affected code is on the audio
thread. The audio thread is reached through:

- Any `process()` override on a `dspObject` or `dspSourceObject` subclass
- The PortAudio callback (`device/portaudioDeviceManager.cpp`) and OpenSL ES
  equivalent
- The consumer side of the lock-free message queue (`utils/lfQueue.hpp`)
- Anything called from the above transitively

On the audio thread, these are forbidden as part of a warning fix:

- Adding heap allocation (`new`, `malloc`, `std::vector::push_back` on a
  vector that wasn't pre-sized, `std::string` construction, etc.)
- Adding mutex/lock acquisition of any kind
- Adding `try`/`catch` or anything that could throw
- Replacing a plain field access with a `std::atomic` operation that wasn't
  already atomic (the project has `aBool`/`aInt`/`aFlt` wrappers — use those
  if atomicity is genuinely needed, but warning fixes rarely require it)
- Adding virtual dispatch where there was none
- Adding work inside hot loops (per-sample, per-frame) — even a single extra
  branch matters at 48000 × N-channel rates

Off the audio thread (application thread, manager singletons running in
`update()`, file loading, demo code, tests), normal C++ rules apply, but
still: minimum change to silence the warning.

## Preferred silencing mechanisms (when the issue is genuinely cosmetic)

Ranked from least invasive to most:

1. **Local annotation in our own code:** `[[maybe_unused]]`, `(void)param;`,
   `/*paramName*/` comment, `using Base::method;`, explicit initialization,
   `static_cast` to the right type.
2. **Localized pragma around a vendored `#include`:** `#pragma clang
   diagnostic push` / `ignored "-Wfoo"` / `pop` in the file that includes
   the third-party header.
3. **Target-scoped CMake suppression:** `target_compile_options(yse_tests
   PRIVATE -Wno-foo)` for warnings that only matter in one target (tests are
   the typical case). Never apply globally to the engine target.
4. **Disabling the warning globally:** essentially never. Reserve for warnings
   that have no signal value at all in this codebase.


## Platform coordination

If the warning is platform-specific (e.g. only appears on MSVC, only on
MSYS2/Clang64, only on Android NDK), check that the fix doesn't break the
other platforms. The CMake build covers Windows and Linux; Android is built
separately. Suppression flags must be guarded by compiler/platform conditions
when the warning only exists on one toolchain.

## Workflow

1. **Inventory.** List every distinct warning/error from the build log.
   Group identical ones (a doctest `__COUNTER__` warning hitting 38 times is
   one issue, not 38). Note the file path of each.
2. **Classify.** Assign each group to bucket A/B/C/D above.
3. **Audio-thread check.** For each bucket-A and bucket-C item, determine
   whether the affected code is on the audio thread.
4. **Plan.** Write down the intended fix per group before editing. If a fix
   would change anything on the audio thread other than removing a real bug,
   stop and surface it for review rather than applying it.
5. **Apply.** Make the smallest change that resolves each issue. Do not
   touch unrelated code in the same file.
6. **Verify.** Rebuild on the platforms in scope. Run `ctest --preset
   tests-debug`. Confirm warning count drops as expected and no new warnings
   appear. Spot-check at least one demo runs (audio-thread regressions
   typically show up at runtime, not at compile time).
7. **Report.** Summarize: groups fixed, mechanism used per group, any items
   deferred and why, any audio-thread-adjacent code touched.

## Running clang-tidy locally

A baseline check set lives at [.clang-tidy](../../../.clang-tidy). Invoke
analysis through the project wrapper rather than calling `clang-tidy`
directly — the wrapper picks the right `compile_commands.json` and avoids
the "doctest/doctest.h not found" failure mode on test files:

```powershell
python yse.py analyze YseEngine/dsp/lfo.cpp        # one file
python yse.py analyze YseEngine/device/            # one directory
python yse.py analyze                              # whole project (slow)
```

Per CLAUDE.md item 6, run `python yse.py analyze <changed-files>` before
committing and clear any **new** findings on the modified code. The
baseline (~50 pre-existing findings across `YseEngine/`) is tracked as
backlog — don't fix in passing during unrelated work.

## Things that are out of scope for this skill

- Adding new compiler warnings to the build (enabling `-Wfoo` that wasn't
  on before) without an accompanying issue
- Migrating to a newer C++ standard
- Replacing project utilities (`aFlt`, `MULTICHANNELBUFFER`, the message
  queue, `dspObject::link()`) with standard-library equivalents
- Reformatting code that happens to be near a warning

If any of those would actually help, raise them as a separate proposal,
not as part of a warning-fix pass.

## Runtime errors and crashes

For runtime errors (assertions, segfaults, audio glitches, hangs) the same
priorities apply but with different emphasis:

- Reproduce first. A crash you can't reproduce is not a crash you can fix
  safely.
- Distinguish audio-thread crashes from application-thread crashes. The
  former are usually races, lock-free queue misuse, allocations on the
  audio path, or DSP buffers being read past their length.
- Do not "fix" by adding locks on the audio thread. The lock-free contract
  is structural; a mutex on the callback is a regression even if it
  eliminates the immediate symptom.
- Audio glitches/clicks without a hard crash are real bugs, not cosmetic
  issues. Treat them as bucket A.
