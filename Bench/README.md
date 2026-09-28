# Bench/

Performance benchmark suite for libYSE, built on Google Benchmark (fetched
on first configure). Gated behind `YSE_BUILD_BENCHMARKS` (default OFF); the
`bench` CMake preset turns it on in a Release build under `build-bench/`.

## Running

```sh
python yse.py bench                             # configure + build + run everything
python yse.py bench --filter Buffer             # subset by regex (--benchmark_filter)
python yse.py bench --filter RenderHeavy -- --benchmark_repetitions=5
```

Anything after `--` goes to the binary unchanged. The binary runs with
`build-bench/bin/` as its working directory, so a relative output path lands
there. For machine-readable results write them to a file rather than
redirecting stdout — `yse.py` also prints the CMake configure/build output
and the commands it runs, so `python yse.py bench --json > out.json` does not
produce valid JSON:

```sh
python yse.py bench -- --benchmark_out=out.json --benchmark_out_format=json
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
| **1 — DSP micros** | `dsp/bench_buffer.cpp`, `bench_filters.cpp`, `bench_oscillators.cpp`, `bench_delay.cpp`, `bench_math.cpp` | Per-sample loops in isolation — buffer operators, filters (one-pole and biquad), oscillators, math kernels. Regressions here scale linearly with every sound. |
| | `dsp/bench_va_voice.cpp`, `bench_fm_voice.cpp`, `bench_sampler_voice.cpp` | One synth voice per block: the ladder filter and the VA voice (`BM_LadderFilter`, `BM_VaVoice_*`), the DX7-class FM voice (`BM_FmVoice_*`), the SFZ sampler voice (`BM_SamplerVoice_*`), each next to a `sineVoice` reference (`BM_SineVoice_*`) so the per-voice cost reads relative to the cheapest voice. |
| | `dsp/bench_plate_reverb.cpp` | The Dattorro plate reverb's `process()`, stereo and mono (`BM_PlateReverb_*`). |
| | `internal/bench_mpmcqueue.cpp` | `YSE::mpmcQueue` under 1/4/8 producer-consumer contention (`BM_MpmcQueue_*`) — the cell false-sharing workload of #288. |
| **2 — Engine config** | `dsp/bench_reverb.cpp`, `patcher/bench_patcher.cpp` | Control-plane primitives — reverb parameter changes, patcher graph construction, JSON round-trip, message dispatch. |
| **3 — Audio-thread macro** | `integration/bench_mixing.cpp` | The real audio callback body, driven offline via `System().renderOffline(blocks)`: 100 sounds with and without a global reverb, the control tick, the real-time factor, and the same scene at real-time cadence (`BM_Engine_RenderPaced_100Sounds`). |
| | `integration/bench_render_heavy.cpp` | Heavy-per-voice scenes for the render scheduler: 8 channels × 56 VA-style voices (`BM_Engine_RenderHeavy_Channels`) and the same 448 voices on one channel (`BM_Engine_RenderHeavy_Swarm`), swept over the render worker count. See [render-worker sweeps](#render-worker-sweeps). |
| | `integration/bench_synth_effects.cpp` | How voice costs compose through the engine: voice-count scaling, a channel insert chain (EQ → compressor → chorus), send fan-in to one return, and positioned notes (`BM_Engine_SynthVoiceScaling/N`, `BM_Engine_ChannelInsertChain_*`, `BM_Engine_SendFanIn/N`, `BM_Engine_PositionedNotes/N`). |
| | `integration/bench_yse_dsl.cpp` | Live-coding DSL round trip, script-thread `yse.send` to a C++ bus subscriber (`BM_Dsl_SendRoundTrip`). Compiled only when `YSE_ENABLE_PYTHON` is ON; the `bench` preset and CI leave it off, so this file is normally empty. |

All of it runs in CI (`.github/workflows/benchmark.yml`: pushes to `master`
and `dev`, PRs into `master`), 3 repetitions with aggregates only, compared
against the `bench-history` branch with a 110% alert threshold that comments
but does not fail. The engine-touching benchmarks use `engineInitOffline()`,
so no PortAudio stream is opened *and* `Pa_Initialize()` is skipped — the
ALSA / JACK backend probe never runs, so a runner without an audio device is
fully bypassed. (Earlier versions called `Pa_Initialize()` unconditionally,
which took down bare GHA Ubuntu runners ~20 s after the probe even though no
stream was opened.)

## How offline rendering works

`YSE::system::renderOffline(blocks)` runs the same callback body the audio
thread executes in production, synchronously on the calling thread, one
`STANDARD_BUFFERSIZE` block at a time. Production paCallback and the bench
share the extracted `DEVICE::deviceManager::renderOneBlock()` so the measured
code path is the same one PortAudio drives — including the render
scheduler's worker fan-out. Work done on render workers is invisible to
the calling thread's CPU clock, so a benchmark that measures parallel
rendering must time wall-clock: the heavy scenes use `UseRealTime()`, the
paced scene times its blocks by hand (`UseManualTime()`). The older
100-sound benches report the default CPU time.

Pair it with `YSE::system::initOffline()` (skip `addCallback()`) so no
real audio thread runs alongside the bench's synchronous render. Mixing
`init()` and `renderOffline()` would race the manager-update path.

## Render-worker sweeps

The render benchmarks are how the task-graph render scheduler (epic #856)
was judged, and the protocol it was judged by lives in
[docs/design/render_scheduler.md](../docs/design/render_scheduler.md)
together with every step's recorded baseline and A/B.

- **The sweep.** `BM_Engine_RenderHeavy_Channels/workers:W` and
  `BM_Engine_RenderHeavy_Swarm/workers:W` run W ∈ {0, 1, 2, 4, 8, 24};
  `BM_Engine_RenderPaced_100Sounds/workers:W` runs W = 0 and W = -1. W goes
  through the internal hook `INTERNAL::Global().setRenderWorkerCount(W)` for
  the duration of the run and is restored to the auto-sized default (-1)
  afterwards: 0 renders everything on the calling thread (the serial
  reference), -1 is the auto count (physical cores − 1, capped at 8).
- **Counters.** `per_block` and `per_channel_job` (wall time per block, and
  per block divided by the scene's channel count — at W = 0 the cost of one
  channel job); `serial` = 1 when the scheduler's serial gate kept the last
  block on the calling thread.
- **Order independence.** Every render benchmark reaches its steady state
  itself — `BenchHelpers::pumpUntil()` until every voice sounds, then
  `BenchHelpers::settleControlPlane()` to drop the update flags the
  UpdateTick benchmarks bank — so a filtered run and a full run time the same
  thing. A new render benchmark must do the same (#857).
- **Comparing two builds.** Interleave them (A, B, A, B), take several
  repetitions per round and compare medians; on a hybrid CPU pin the process
  to one core type (the recorded rounds used `ProcessorAffinity = 0xFF`, the
  four Zen 5 cores of the bench machine) or an unpinned run turns bimodal and
  looks like order dependence. Re-measure with the same mask, or compare
  configurations within one run. Include a control (`BM_VaVoice_SingleSaw`,
  `BM_Engine_RenderOffline_100Sounds`) to tell a machine-wide shift from a
  real one.
- **Correctness first.** Any change to how blocks are rendered must keep the
  bit-exact golden test green at every worker count:
  `Tests/channel/test_render_golden.cpp`, suite `rendergolden`
  (`ctest --preset tests-debug -R rendergolden`).

## Adding a benchmark

If your benchmark **does not** touch the engine, name it anything and
construct YSE objects directly — see `bench_buffer.cpp` for the pattern.
Add the file to `yse_benchmarks` in `Bench/CMakeLists.txt`.

If it **does** touch the engine, follow the naming of its neighbours
(`BM_Engine_*`, `BM_Reverb_*`, `BM_Patcher_*`) and call
`BenchHelpers::engineInitOffline()` at the top — this initialises the
engine without opening an audio device. Drive playback via
`YSE::System().renderOffline(blocks)` rather than the audio callback. The
engine is one process-global instance, so tear your scene down before
returning (see `bench_synth_effects.cpp`), and cap any benchmark that
enqueues control messages in a tight loop with
`->Iterations(BenchHelpers::kLeakyBenchIterations)`: offline, nothing drains
those queues between iterations.

## Local CI reproduction

To confirm a workflow change before pushing:

```pwsh
docker run --rm -v "${PWD}:/workspace" yse-ci bash -c '
  cmake -B build-bench-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DYSE_BUILD_BENCHMARKS=ON
  cmake --build build-bench-linux
  ./build-bench-linux/bin/yse_benchmarks \
    --benchmark_repetitions=3 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=bench-results.json \
    --benchmark_out_format=json
'
```

`build-bench-linux/` is intentionally separate from `build-bench/` so the
Docker (gcc) and Windows (MSYS2 Clang64) builds don't clobber each other's
CMakeCache. The `yse-ci` image is built from `tools/ci-linux/`.
