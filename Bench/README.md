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

## Three coverage tiers

| Tier | Files | What it measures |
|---|---|---|
| **1 — DSP micros** | `dsp/bench_buffer.cpp`, `bench_filters.cpp`, `bench_oscillators.cpp`, `bench_delay.cpp`, `bench_math.cpp` | Per-sample loops in isolation — every operator, filter, oscillator, math kernel. Regressions here scale linearly with every sound. |
| **2 — Engine config** | `dsp/bench_reverb.cpp`, `patcher/bench_patcher.cpp` | Control-plane primitives — reverb parameter changes, patcher graph construction, JSON round-trip, message dispatch. |
| **3 — Audio-thread macro** | `integration/bench_mixing.cpp` | The real audio callback body, driven offline via `System().renderOffline(blocks)`. 100 sounds + (optional) global reverb through the master-channel mix path. Reports real-time factor. |

All three tiers run in CI. Tiers 2 and 3 use `engineInitOffline()` so no
PortAudio stream is opened *and* `Pa_Initialize()` is skipped — the
ALSA / JACK backend probe never runs, so the runner-without-an-audio-
device path is fully bypassed. (Earlier versions called `Pa_Initialize()`
unconditionally, which took down bare GHA Ubuntu runners ~20 s after the
probe even though no stream was opened.)

## How offline rendering works

`YSE::system::renderOffline(blocks)` runs the same callback body the audio
thread executes in production — `master->dsp()` + `master->buffersToParent()`
per `STANDARD_BUFFERSIZE` block — synchronously on the calling thread.
Production paCallback and the bench share the extracted
`DEVICE::deviceManager::renderOneBlock()` so the measured code path is
the same one PortAudio drives.

Pair it with `YSE::system::initOffline()` (skip `addCallback()`) so no
real audio thread runs alongside the bench's synchronous render. Mixing
`init()` and `renderOffline()` would race the manager-update path.

## Adding a benchmark

If your benchmark **does not** touch the engine, name it anything and
construct YSE objects directly — see `bench_buffer.cpp` for the pattern.

If it **does** touch the engine, name it so it matches one of the gated
prefixes (`BM_Engine_*`, `BM_Reverb_*`, `BM_Patcher_*`) and call
`BenchHelpers::engineInitOffline()` at the top — this initialises the
engine without opening an audio device. Drive playback via
`YSE::System().renderOffline(blocks)` rather than the audio callback.

## Local CI reproduction

To confirm a workflow change before pushing:

```pwsh
docker run --rm -v "${PWD}:/workspace" yse-ci bash -c '
  cmake -B build-bench-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DYSE_BUILD_BENCHMARKS=ON
  cmake --build build-bench-linux
  ./build-bench-linux/bin/yse_benchmarks \
    --benchmark_format=json \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true \
    > bench-results.json
'
```

`build-bench-linux/` is intentionally separate from `build-bench/` so the
Docker (gcc) and Windows (MSYS2 Clang64) builds don't clobber each other's
CMakeCache.
