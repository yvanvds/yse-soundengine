# Bench/

Performance benchmark suite for libYSE. Gated behind `YSE_BUILD_BENCHMARKS`
(default OFF). See `C:\Users\yvan\.claude\plans\benchmark-suite.md` for the
design plan.

## Running

```sh
python yse.py bench                             # configure + build + run
python yse.py bench --filter Buffer             # subset by regex
python yse.py bench --json > out.json           # machine-readable
```

Or directly:

```sh
cmake --preset bench
cmake --build --preset bench
./build-bench/bin/yse_benchmarks [--benchmark_filter=...]
```

## Two tiers, by environment requirement

| Tier | Files | Calls `engineInit()`? | Runs on shared CI? |
|---|---|---|---|
| 1 — DSP micros | `dsp/bench_buffer.cpp`, `bench_filters.cpp`, `bench_oscillators.cpp`, `bench_delay.cpp`, `bench_math.cpp` | No | Yes |
| 2 — Engine + reverb | `dsp/bench_reverb.cpp`, `patcher/bench_patcher.cpp`, `integration/bench_mixing.cpp` | Yes | **No — local only** |

Tier 2 benchmarks call `YSE::System().init()`, which tries to open a PortAudio
default device. GitHub Actions runners have no audio hardware: the ALSA
probe cascade dumps ~30 lines of stderr, the JACK fallback also fails
(CAP\_IPC\_LOCK isn't available to non-Docker GHA jobs — see
`memory/project_ci_headless_audio.md` for the history), and the binary
hangs long enough that the runner times the job out.

`tools/ci-linux/Dockerfile.audio` provides the workaround locally: it starts
`jackd -d dummy` before running the build, so PortAudio finds a device and
the engine initialises normally. CI uses the filter
`--benchmark_filter='-^BM_(Reverb_|Patcher_|Engine_)'` to skip Tier 2
entirely.

## Adding a benchmark

If your benchmark **does not** touch the engine (no `engineInit()`, no
`YSE::System()`, no `YSE::sound::create`), it goes in Tier 1 — name it
anything. CI will run it.

If it **does** touch the engine, name it so it matches one of the gated
prefixes (`BM_Engine_*`, `BM_Reverb_*`, `BM_Patcher_*`) — the CI filter
will skip it, and local runs (including the audio Docker) will pick it up.

Always call `BenchHelpers::engineInit()` (from `support/bench_helpers.hpp`)
rather than `YSE::System().init()` directly — it's idempotent and pauses
the audio stream so the benchmark thread is the sole driver of
`Manager().update()`.

## Local CI reproduction

To confirm a workflow change before pushing:

```pwsh
docker run --rm -v "${PWD}:/workspace" yse-ci bash -c '
  cmake -B build-bench-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DYSE_BUILD_BENCHMARKS=ON
  cmake --build build-bench-linux
  ./build-bench-linux/bin/yse_benchmarks \
    --benchmark_filter="-^BM_(Reverb_|Patcher_|Engine_)" \
    --benchmark_format=json \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true \
    > bench-results.json
'
```

`build-bench-linux/` is intentionally separate from `build-bench/` so the
Docker (gcc) and Windows (MSYS2 Clang64) builds don't clobber each other's
CMakeCache.
