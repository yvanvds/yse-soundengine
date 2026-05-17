# CLAUDE.md

Workflow rules for Claude Code sessions on the YSE Sound Engine. Read at
session start. Pair with [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md).

## How we work

1. **Issues first.** Every bug, feature, or enhancement is filed as a
   GitHub issue *before* code is written. Branch from `master` as
   `<issue-number>-<short-slug>`. PR through CI. The only exception is a
   trivial doc fix.
2. **Tests where sensible** (not dogmatically). DSP nodes, the C API
   surface, lifecycle/threading code, and reverb get tests first. Demos
   and one-off scripts don't need tests. The broader plan lives in
   [Tests/TEST_PLAN.md](Tests/TEST_PLAN.md).
3. **Real-time discipline.** Code on the audio callback path must not
   allocate, lock, or block on I/O. Never trade audio-thread speed for
   stylistic cleanliness — see the `fix-issues` skill for the project
   stance.
4. **Don't modify vendored deps.** Anything under `dependencies/` or
   `build*/_deps/` is read-only. File upstream issues where appropriate.
5. **Layered structure.** Engine internals → C API → bindings/demos.
   `YseEngine/c_api/` is the only entry point for FFI consumers. Keep
   `PROJECT_OVERVIEW.md` current after structural changes.

## Issue templates

The `.github/ISSUE_TEMPLATE/` directory defines four forms: **bug**,
**feature**, **enhancement**, and **task**. Blank issues are disabled —
every new issue picks a template. The bug form has a "Real-time /
audio-thread path?" checkbox so RT-critical bugs are easy to filter.

## Layer taxonomy

The templates ask which area of the codebase an issue touches. The
canonical terms are:

- `engine` — Core lifecycle, scene, sound objects, manager threading
- `dsp` — DSP nodes, channels, mixing, reverb
- `audio-io` — PortAudio backends, output devices, file loading
- `midi` — MIDI device I/O, MIDI file playback
- `music` — Music primitives, generative player, scales/chords
- `c-api` — C API surface in `YseEngine/c_api/`
- `tests` — doctest suite + benchmarks + demos
- `android` — NDK, Oboe, Gradle/APK
- `infra` — CMake/presets, `yse.py`, CI, docs

These are informational dropdowns, not enforced labels — maintainers
apply repository labels at triage.

## Running locally

Requires CMake ≥ 3.20 and one of: Windows (MSYS2 Clang64 or MSVC), Linux
(gcc/clang), or the Android NDK toolchain. The Python wrapper drives
every preset:

```powershell
python yse.py build              # debug preset
python yse.py build --release    # release preset
python yse.py test               # tests-debug preset + ctest
python yse.py run Demo00         # run a demo from build-debug/bin/
python yse.py analyze            # clang-tidy
python yse.py format             # clang-format -i
python yse.py coverage           # gcovr (Linux) / llvm-cov (Windows)
```

See [README.md](README.md) and [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)
for the full toolchain matrix.

## Boundaries

- **Don't** push to `master` without confirmation.
- **Don't** skip hooks (`--no-verify`) or bypass signing.
- **Don't** modify vendored sources under `dependencies/` or
  `build*/_deps/`.
- **Don't** allocate, lock, or block on audio-thread paths.
- **Don't** broaden scope on a bug fix — no opportunistic refactors, no
  surrounding cleanup, no abstractions for hypothetical use.
- **Do** prefer `yse.py` over hand-rolled `cmake` invocations so the
  presets stay the source of truth.

## Memory

There is a Claude Code memory store for this project under
`~/.claude/projects/d--yse-soundengine/memory/`. It captures durable
preferences and project facts that survive between sessions.
