# Render scheduler: measurement protocol and baselines

Status: **record**. Epic: [#856](https://github.com/yvanvds/yse-soundengine/issues/856)
(task-graph render scheduler, sub-issues #857-#862). Moved here from
`Tests/TEST_PLAN.md` (issue #891); how to run the benchmarks is in
[Bench/README.md](../../Bench/README.md#render-worker-sweeps), and the
user-facing threading model is the manual's
[Threading model](../../documentation/source/intro/threading.rst) page.

Each step of the epic was judged against the two things #857 introduced — a
bit-exact golden test and heavy-per-voice benchmarks — and recorded its A/B
here. The sections are in landing order; the figures in each are the ones
measured when that step landed, on the machine and mask it names.

## Measurement foundation (#857)

The render-scheduler epic (#856) replaces the channel fan-out render pool in
steps (#858 park-and-wake, #859 task graph, #860 voice slices, #861 cost
tracking). Every step is judged against two things introduced here: a
correctness oracle and a set of benchmarks with work worth parallelising.

**Worker-count hook.** `INTERNAL::Global().setRenderWorkerCount(n)` (backed by
`threadPool::setWorkerCount`) re-sizes the render pool between renders: `-1`
restores the auto-sized default (`MAX_AUTO_RENDER_THREADS` = 2), `0` means no
render workers at all — the rendering thread runs every channel job itself via
`join()`'s help-running. Internal until #861 exposes a thread-count policy
(since #861: auto = physical cores − 1, capped at `renderScheduler::MAX_AUTO_WORKERS`
= 8; public as `System().renderThreads()` / `yse_system_set_render_threads()`).
Control thread only, and only while nothing renders.

**Golden test** — `Tests/channel/test_render_golden.cpp`, suite
`rendergolden`. Renders a deterministic 19-voice scene (a parent channel with
five leaf channels, one with a child of its own, all captured through a
post-fader send into a return whose insert records the mix) for 48 blocks with
0, 1, 2 and N render workers, then 0 again, and requires every captured block
to match the 0-worker render bit for bit, with every voice rendered exactly
once per block. Runs in the monolithic `yse_unit_tests` entry (so in the
dev-push TSan sweep, `tests-tsan-full`) and in its own `yse_tests_rendergolden`
entry (so in the per-PR `tests-tsan` gate). Removing the `join()` from
`buffersToParent()` makes it fail at every worker count.

**Benchmarks** — `Bench/integration/`:

- `BM_Engine_RenderOffline_100Sounds*` / `BM_Engine_RealtimeFactor_100Sounds`
  (`bench_mixing.cpp`) are now order-independent. The shared scene is pumped
  until every sound is playing, and each render benchmark drops the update
  flags the UpdateTick benchmarks bank before timing
  (`BenchHelpers::settleControlPlane`), so a filtered run and a full run time
  the same thing: the audio callback body with no control-plane work.
- `BM_Engine_RenderHeavy_Channels/workers:W` (`bench_render_heavy.cpp`) — 8
  channels x 56 voices, each voice a dspSourceObject running saw -> swept
  ladder filter -> envelope (one VA-synth voice, ~4 us per block).
- `BM_Engine_RenderHeavy_Swarm/workers:W` — the same 448 voices on one
  channel: the swarm shape a channel-granular pool cannot split.

W in {0, 1, 2, 4, 8, 24} is the render worker count. `per_channel_job` is the wall
time per block divided by the channel count; at W = 0 it is the cost of one
channel job.

**Baseline** (AMD Ryzen AI 9 HX 370, 12C/24T, Windows 11, MSYS2 Clang64,
`bench` preset, 2026-09-24; medians of 5 repetitions of a filtered run, the
process pinned to logical CPUs 0-7 — the four Zen 5 cores, `ProcessorAffinity
= 0xFF` — so every run lands on the same core type, see the note below):

| Benchmark | W = 0 | W = 1 | W = 2 | W = 4 |
|---|---|---|---|---|
| `RenderHeavy_Channels` per block | 1.87 ms | 1.02 ms | 0.75 ms | 0.51 ms |
| `RenderHeavy_Channels` per channel job | 234 us | 128 us | 94 us | 64 us |
| `RenderHeavy_Swarm` per block | 1.92 ms | 1.85 ms | 1.88 ms | 1.88 ms |

The heavy scenes read the same in a filtered and in a full `BM_Engine_` run
(within 2%). `BM_Engine_RenderOffline_100Sounds` (auto-sized pool, per 64
blocks): 96 us filtered, 112 us in the full run. Before this change the same
benchmark reported 8.8 us filtered (silence: ~930 M samples/s) and 411 us in a
full run (every timed block also ran the manager update the UpdateTick
benchmarks had banked).

Reading them: with jobs past 200 us the current channel fan-out already pays
off (2.5x at W = 2, 3.6x at W = 4 for the eight-channel scene) — #812's
"parallelism loses" was a property of 2-15 us jobs. The swarm does not move
at any W: one channel is one job.

Why pinned: the part is hybrid (4 Zen 5 + 8 Zen 5c cores), and an unpinned
run is bimodal — whenever the rendering thread lands on a compact core every
render benchmark reads ~1.6x slower (W = 0 swarm: 1.85 ms pinned to CPU 0 or
2, 2.9-3.1 ms pinned to CPU 8, 16 or 22). Unpinned, that looks like order
dependence between benchmarks and is not. Re-measure with the same mask, or
compare configurations within one run. Core placement itself is #862.

## Park-and-wake workers (#858)

**Render workers park instead of yield-spinning (#858).** W now also sweeps
8 and 24. Interleaved A/B (HEAD vs. #858, two rounds of 3 repetitions each,
medians, same 0xFF mask, per block):

| Benchmark | W = 0 | W = 1 | W = 2 | W = 4 | W = 8 | W = 24 |
|---|---|---|---|---|---|---|
| `RenderHeavy_Channels` before | 1.85 ms | 1.01 ms | 0.74 ms | 0.51 ms | 0.27 ms | 0.28 ms |
| `RenderHeavy_Channels` after | 1.85 ms | 1.00 ms | 0.74 ms | 0.50 ms | 0.27 ms | 0.32 ms |
| `RenderHeavy_Swarm` before | 1.85 ms | 1.87 ms | 1.85 ms | 1.85 ms | 1.88 ms | 1.90 ms |
| `RenderHeavy_Swarm` after | 1.85 ms | 1.85 ms | 1.85 ms | 1.85 ms | 1.86 ms | 1.85 ms |

The swarm (one job, W - 1 idle workers) no longer degrades with W: the idle
workers sleep instead of competing with the rendering thread. W = 8 and 24
run 9 and 25 threads on 8 logical CPUs; at W = 24 the eight-channel scene
pays the Windows wake latency of the workers it has to unpark (Windows cannot
wake "n" waiters, so a partial fan-out wakes one and the woken pass it on).
`BM_Engine_RenderOffline_100Sounds`: 108 us before and after.

## Task graph (#859)

**Task-graph render scheduler (#859).** The channel fan-out (`addFastJob` +
help-running `join()`) is replaced by `INTERNAL::renderScheduler`: static
per-worker leaf lists with CAS stealing, continuations for channel mixes and
returns, the audio thread as worker 0. `setRenderWorkerCount()` now re-sizes
the scheduler. Coverage: `Tests/internal/test_render_scheduler.cpp` (every task
once per block and after its dependencies at W = 0/1/2/4, stealing from
workers that never run, a four-thread barrier graph that fails on any lost
wake, park/shutdown/revive, rebuilds, and a leafless graph that must not hang),
and a second `rendergolden` case pinning the D3 mixer order — a bus insert
must see its subchannels' signal (it saw silence under the old order). The
golden test stays bit-exact at 0/1/2/N. Interleaved A/B (HEAD vs. #859, two
rounds of 3 repetitions, medians, same 0xFF mask, per block):

| Benchmark | W = 0 | W = 1 | W = 2 | W = 4 | W = 8 | W = 24 |
|---|---|---|---|---|---|---|
| `RenderHeavy_Channels` before | 1.83 ms | 0.95 ms | 0.72 ms | 0.49 ms | 0.28 ms | 0.34 ms |
| `RenderHeavy_Channels` after | 1.84 ms | 0.94 ms | 0.71 ms | 0.48 ms | 0.27 ms | 0.37 ms |
| `RenderHeavy_Swarm` before | 1.83 ms | 1.83 ms | 1.84 ms | 1.84 ms | 1.83 ms | 1.83 ms |
| `RenderHeavy_Swarm` after | 1.83 ms | 1.83 ms | 1.83 ms | 1.85 ms | 1.83 ms | 1.83 ms |

The machinery swap is throughput-neutral on the heavy scenes (the partition
is the same: channels and returns), with parallel well ahead of serial at 2
and 4 workers. W = 24 (25 threads on 8 CPUs) pays more wake latency, which
is #861's thread-count policy to avoid. Controls: `BM_VaVoice_SingleSaw` 4.10 us
before and after; `BM_Engine_RenderOffline_100Sounds` 88 -> 80 us.

## Voice slices (#860)

**Voice slices (#860).** A channel's sounds are split over up to 16 voice
slices (a new one opens when every active slice holds 32 sounds), each a
render-graph leaf accumulating into its own buffers, summed by the channel's
mix task in slice order. Coverage: `Tests/channel/test_voice_slices.cpp`
(least-loaded join, open-on-full, close-when-empty hysteresis, the soft
capacity past 16 slices, moves between channels, release to the parent); the
golden scene gains a `golden.swarm` channel with 2 x 32 + 7 voices (three
slices) and stays bit-exact at 0/1/2/N — making every slice accumulate into
the channel's `out` directly fails it at W = 1, 2 and N — and a
`rendergolden` churn case creates, moves and destroys sounds across slice
boundaries on two channels while a render thread runs two workers (the
sanitizer gate). Interleaved A/B (HEAD vs. #860, two rounds of 3 repetitions,
medians, same 0xFF mask, per block):

| Benchmark | W = 0 | W = 1 | W = 2 | W = 4 | W = 8 | W = 24 |
|---|---|---|---|---|---|---|
| `RenderHeavy_Channels` before | 1.86 ms | 0.98 ms | 0.73 ms | 0.50 ms | 0.28 ms | 0.36 ms |
| `RenderHeavy_Channels` after | 1.86 ms | 1.01 ms | 0.68 ms | 0.46 ms | 0.32 ms | 0.36 ms |
| `RenderHeavy_Swarm` before | 1.87 ms | 1.90 ms | 1.92 ms | 1.88 ms | 1.89 ms | 1.87 ms |
| `RenderHeavy_Swarm` after | 1.90 ms | 1.01 ms | 0.72 ms | 0.45 ms | 0.31 ms | 0.36 ms |

The swarm (448 voices on one channel, 14 slices) now scales like the
eight-channel scene. The eight-channel scene splits each 56-voice channel into
two slices: better balanced at W = 2 and 4, while W = 8 (9 threads on 8 CPUs,
all of them now with leaves to wake) pays more wake latency — #861's
thread-count policy. Controls: `BM_VaVoice_SingleSaw` 4.22 -> 4.18 us;
`BM_Engine_RenderOffline_100Sounds` 85 -> 95 us (per 64 blocks: its ~34 cheap
sounds per channel open a second, near-empty slice each — ~0.15 us per block
of extra clear, sum and claim, which #861's cost-based balancing and serial
gating are for).

## Cost tracking and thread-count policy (#861)

**Cost tracking, balancing, serial gating, thread-count policy (#861).** Every
16th block (and the block after a rebuild or a sound connect/disconnect) each
task is timed into an EMA of its own time (continuations it ran inline are
subtracted); leaves are dealt to the least-loaded list by that cost; voice
slices join, open, merge, split and level by measured cost against a target
derived from the measured wake cost (10–200 us); a light block (estimated below
one wake) wakes nobody and, once every worker has parked, runs on the calling
thread alone. The wake cost is the latency from the wake call to the first
woken worker joining; a block that closes before any woken worker joins is
only a lower bound, so it may raise the estimate but never lower it. The auto
worker count is physical cores − 1, capped at 8, and
`System().renderThreads()` / `yse_system_set_render_threads()` make it public.
Coverage: `test_render_scheduler.cpp` (light blocks serial at real-time
cadence with the workers left parked, light back-to-back blocks still open,
heavy blocks fan out, a task's cost excludes its continuations, the cost deal
balances two heavy leaves that round-robin would stack and rebuilds once after
a blind deal, the auto count), `test_voice_slices.cpp` (cheapest-slice join,
open past the target, merge of a count-opened second slice — the #860
regression — split above two targets, leveling of an uneven pair and no
leveling within noise, the count policy when cost balancing is off), the golden
test (count policy pinned for its known three-slice layout, gating off for the
1/2/N runs, plus a gated N-worker run that must stay bit-exact and fan out), a
`rendergolden` case for a latent #860 bug the cost policy exposes — the
master's mix task counted one dependency for its own sounds however many voice
slices it had, so a master with a second slice mixed while that slice was
still writing (a master insert checks every master voice has rendered the
block) — and C API cases in `capilowcov` (NULL safety; 0 / 3 / auto applied to
an offline session that keeps rendering) and `capilowcovlife` (the setting
survives close and init; per-process ctest entries only). Fail-without-fix,
verified: reverting the master dependency count, charging a task its
continuations, a gate that never goes serial, or a count-only slice pick each
fail their cases.

A finding that shaped the gate: rendered back to back (offline), the
100-sound scene is *faster* in parallel than serial — ~84 us per 64 blocks at
W = 2 against ~119 us at W = 0 — because the workers are still in their 50 us
post-block spin and join for a cache miss. #812's "parallel always loses"
measured the old yield-spinning pool. So a light block that starts within the
spin window of the previous one still opens; the serial gate applies at
real-time cadence, where every block starts with the workers parked.
`BM_Engine_RenderPaced_100Sounds/workers:W` measures exactly that (one block
per 300 us, only the block timed): auto-sized workers render serially
(`serial` = 1) at 1.89 / 1.95 us per block against 1.90 / 1.98 us at W = 0
(two filtered runs, medians of 5).

Interleaved A/B (#860 = 8c1adc2 vs. #861, two rounds of 3 repetitions,
medians averaged, same 0xFF mask, per block). Both builds' heavy benches send
32 control ticks before timing, as a host does every frame: the cost policy
re-shapes slices one step per tick, and without ticks the timed blocks see the
connect-time layout (slices up to 3x uneven at W = 8):

| Benchmark | W = 0 | W = 1 | W = 2 | W = 4 | W = 8 | W = 24 |
|---|---|---|---|---|---|---|
| `RenderHeavy_Channels` before | 1.86 ms | 0.972 ms | 0.686 ms | 0.452 ms | 0.290 ms | 0.371 ms |
| `RenderHeavy_Channels` after | 1.85 ms | 0.980 ms | 0.651 ms | 0.408 ms | 0.274 ms | 0.385 ms |
| `RenderHeavy_Swarm` before | 1.86 ms | 0.975 ms | 0.693 ms | 0.439 ms | 0.306 ms | 0.366 ms |
| `RenderHeavy_Swarm` after | 1.83 ms | 0.961 ms | 0.679 ms | 0.451 ms | 0.294 ms | 0.363 ms |

A focused re-run (three rounds of 3) confirms: Channels W = 4 0.445 -> 0.406,
W = 8 0.281 -> 0.274, W = 24 0.368 -> 0.385 ms; Swarm W = 4 0.430 -> 0.458,
W = 8 0.300 -> 0.287, W = 24 0.364 -> 0.362 ms. W = 8 is the auto-sized count
on this machine. Two regressions remain, both granularity: the swarm levels
into 16 equal slices, which five lists (W = 4) take 4 at a time (25% of the
work on the slowest list vs 21% for #860's 14 count slices), and W = 24
oversubscribes the 8-CPU mask. Isolated at W = 8 (env-switched build, not
committed): the cost deal beats round-robin at W = 2/4 (Channels 0.64 vs
0.66 ms) and loses ~3% at W = 8 without leveling; sampling costs nothing
measurable. The #860 build did not reproduce its recorded W = 8 regression
(0.28 ms either way). Controls: `BM_Engine_RenderOffline_100Sounds` 87.5 ->
88.3 us (auto-sized: 2 workers before, 8 after — at W = 2 the #861 build reads
80–85 us, so the slice merge fixed #860's near-empty second slices and the
remainder is six more spinning workers on a ~1.4 us block; back to back, so it
stays parallel, `serial` = 0); `BM_VaVoice_SingleSaw` 4.13 -> 4.09 us.

## Topology-aware placement (#862)

**Topology-aware render workers (#862).** `INTERNAL::cpuTopology` reads the
physical cores the process may run on — SMT siblings merged, the affinity mask
applied (Windows `GetLogicalProcessorInformationEx` + process mask; Linux and
Android sysfs `core_cpus_list`/`thread_siblings_list` + `sched_getaffinity`) —
and their performance class (Windows `EfficiencyClass`; sysfs `cpu_capacity`,
else `cpufreq/cpuinfo_max_freq`). A part is hybrid when its weakest core is
below 0.85x its strongest; the cores within 0.85x of the weakest are its
efficiency cores (favoured-core boost bins stay one class; on a
prime + big + little phone only the little cluster is "efficiency"). Worker i
is planned onto entry i mod n of performance-cores-first order (entry 0 left
to the calling thread) and applies a soft hint on its own thread before its
first block: Windows `SetThreadIdealProcessorEx`; Linux/Android on a hybrid part
an affinity mask of the whole performance cluster (a uniform machine gets no
hint — a one-core mask would be a hard pin). `global::init()` logs the count
and each worker's core at debug level, e.g. on the bench machine (Ryzen AI 9
HX 370, 4 Zen 5 + 8 Zen 5c, Windows EfficiencyClass 1/0):
`render workers: 8 (auto); 12 physical cores (hybrid: 4 performance, 8
efficiency); cores: cpu2 cpu4 cpu6 cpu8(e) cpu10(e) cpu12(e) cpu14(e) cpu16(e)`.

The auto count stays physical cores − 1 capped at 8 — now counting only cores
inside the affinity mask — rather than dropping to performance cores − 1 as
the issue first proposed. Measured unmasked (all 24 logical CPUs, first A/B
round with the performance-core count), the heavy scenes keep scaling onto
the efficiency cores: `RenderHeavy_Channels` 10.2 / 6.85 / 4.57 / 2.33 ms at
W = 2 / 4 / 8 / 24, so the 3-worker default would have cost ~75% against
#861's 8 on exactly the scenes the scheduler exists for. The same round showed
what that default would buy: `RenderOffline_100Sounds` 174 -> 80 us (eight
spinning workers on a ~2 us back-to-back block). Under the old 0xFF mask of
earlier rounds the new count is 3 (4 cores in the mask, not 12).

Coverage: `test_cpu_topology.cpp` (CPU-list parsing; uniform, favoured-core,
unknown-capacity, Zen 5/5c, three-cluster classification; performance-first
order; the count rule for hybrid, uniform, capped, single-core and unknown
topologies; a fake sysfs tree — SMT merge, offline CPU, the older sibling-file
name, cpufreq fallback, an affinity mask dropping a sibling and two cores,
arm64 `cpu_capacity` taking precedence, no capacity at all, no tree — run on
every host; the live machine), `test_render_scheduler.cpp` (each worker's
planned core, wrap-around past the core count, every hint tried and on
Windows accepted, performance cores filled first with one core per worker, a
re-size re-plans) and a `lifecycle` case in `system/test_render_placement.cpp`
(an auto-sized offline session logs the count and every worker's core at init,
the workers apply their hints, and a scene renders with gating off).
Fail-without-fix, verified: counting logical CPUs or ignoring the
performance-first order fails the count and placement cases.

Interleaved A/B (#861 = 5798499 vs. #862, three rounds of 3 repetitions,
median of the round medians, unmasked; the machine ran ~1.6x slower than in
earlier sessions — `BM_VaVoice_SingleSaw` 6.56 / 6.63 us — equally for both
builds): `RenderHeavy_Channels` W = 0/1/2/4/8/24 37.1 / 24.5 / 16.6 / 9.15 /
4.80 / 2.33 -> 37.7 / 24.8 / 16.7 / 9.07 / 4.80 / 2.33 ms;
`RenderHeavy_Swarm` 48.1 / 25.3 / 17.7 / 9.97 / 6.28 / 3.76 -> 47.5 / 25.3 /
17.6 / 10.2 / 6.32 / 3.80 ms; `RenderOffline_100Sounds` 193 -> 197 us;
`RenderPaced_100Sounds` (serial-gated) W = 0 4.61 -> 4.90 us, auto 6.70 ->
5.78 us (both noisy). No regression and no measurable difference beyond
~2%: the Windows scheduler already keeps a raised-priority worker on a
performance core, so the ideal-processor hint stays best-effort and minimal.
An earlier clean round showed W = 8 at -2% (Channels) and -4% (Swarm), which
the three-round run did not reproduce.

## As built: render path, voice slices, cost tracking, workers

The as-built description of the scheduler (issues #858–#862), moved here from PROJECT_OVERVIEW.md by
issue #890; the overview keeps a summary.

**Render path — task graph (issue [#859](https://github.com/yvanvds/yse-soundengine/issues/859),
epic #856).** `deviceManager::renderOneBlock()` calls `CHANNEL::Manager().render(master)`, which
runs one block of a task graph on
[internal/renderScheduler.h](../../YseEngine/internal/renderScheduler.h). Every channel in the tree
is one `voiceSlice` leaf per active voice slice plus a `mixTask` that runs once those leaves and
every child's `mixTask` have finished. A return is one `mixTask` that waits for every child of the
master (so every source) and every lower-generation return; the master's `mixTask` — the root —
waits for its own slices, its children and every return. The graph is rebuilt on the audio thread,
in place, whenever a `children`/`returns` change, a new generation or a change in a channel's slice
count marks it dirty (`renderScheduler::markDirty()`); a sound joining or leaving an existing slice
needs no rebuild.

**Voice slices (issue [#860](https://github.com/yvanvds/yse-soundengine/issues/860)).** A channel's
sounds live in up to `CHANNEL::MAX_SLICES` (16) `voiceSlice`s — fixed containers owned by the
channel impl, each an intrusive sound list and a render-graph leaf — so one busy channel (a swarm,
or many patchers) spreads over every render worker. A sound joins the least-loaded active slice at
connect time (`SOUND::implementationObject::slice` records which); a new slice opens only when every
active one holds `SLICE_CAPACITY` (32) sounds, and a trailing slice closes only when empty and the
rest fit with half a slice to spare, so swarm churn does not rebuild the graph. Slice 0 accumulates
into the channel's `out`; slices 1..n into private buffers (sized for all slices in
`setup()`/`resize()`, never on connect), which `toChannels(dest)` writes and the channel's mix task
adds to `out` in slice order before the children — single writer per buffer, and the sum is
bit-identical for any worker count. Once a channel's sounds have been measured (#861), the policy is
by cost instead: a sound joins the slice with the lowest measured cost, a new slice opens only when
even that one would pass `CHANNEL::sliceTargetCost()` (the scheduler's measured wake cost, clamped
to 10–200 µs), and `CHANNEL::Manager().update()` re-shapes slices on control ticks — merging the
last slice away when the channel fits in fewer at half the target, splitting the heaviest above
twice the target, otherwise leveling the heaviest and lightest slice when they differ by more than a
quarter of the heavy one. Membership changes only at connect/disconnect and control ticks, never
with the worker count. `CHANNEL::Manager().setCostBalancing(false)` pins the count policy (test
hook). The audio thread is worker 0: each worker owns an intrusive leaf list, dealt by measured cost
(least-loaded list first; unmeasured leaves round-robin in pre-order, then one rebuild once they are
measured), and claims leaves by CAS on its cursor, stealing from the other lists when its own is
empty; a continuation is never queued — whoever satisfies its last dependency runs it inline. With
zero workers the audio thread runs the whole graph itself. Workers park between blocks (the #858
futex/WaitOnAddress primitive) and every worker has left the block before `run()` returns.

**Cost tracking and serial gating (issue
[#861](https://github.com/yvanvds/yse-soundengine/issues/861)).** Every `COST_SAMPLE_PERIOD` (16th)
block — and the block after a rebuild or a sound connect/disconnect — each task is timed with one
`steady_clock` pair; `renderTask::cost()` is an EMA of its *own* time (continuations it ran inline
are subtracted), and `renderScheduler::blockCost()` the block's total. `wakeCost()` is measured on
sampled blocks that wake a parked worker (wake call → first woken worker joining; a block that
closes before any woken worker joins only ever raises it). When `blockCost() < wakeCost()` (a light
block; heavy again above 1.5×) nobody is woken for it: blocks arriving back to back (within the
workers' 50 µs post-block spin — offline rendering) still open, since spinning workers join for a
cache miss and that measured faster than serial even for the ~2 µs 100-sound scene; otherwise a
worker not yet parked may join; once every worker has parked (real-time cadence) the audio thread
renders the block alone without opening it (serial gating), so a light scene pays no wake. Gating
changes who runs a task, never what it computes. `setSerialGating(false)` is the test hook that
forces blocks open.

**Mixer bus order (behaviour change in #859).** A channel's mix task sums its voice slices 1..n and
its children *first* (fixed slice and list order), then runs its insert chain, reverb and pre-fader
meter, pre-fader send taps, the fader, the post-fader meter and post-fader taps. Before #859 a bus
insert processed only the bus's own sounds and children were added afterwards. The master sums its
children and every return before its own insert chain and fader, and has no send taps (a send from
the master would feed a return the master waits on; they were dead before, too). Sends no longer add
into the return's `out` directly: a source writes each send into its slot's private `tap` buffer and
the return's task sums its registry's tapped slots (`gatherSends`), so parallel sources never share
a buffer and the fold order is fixed. The mix is bit-identical for any worker count (`rendergolden`
suite).

**Render scheduler** — `renderScheduler` (`Global().renderer()`) owns the raised-priority render
workers and runs the channel task graph once per block with the audio thread as worker 0 (see
[§3](../../PROJECT_OVERVIEW.md#3-channel-system-hierarchical-mixer)). It replaced the channel
fan-out (`addFastJob` + help-running `join()`) in #859 (epic #856; design and bench protocol:
[docs/design/render_scheduler.md](render_scheduler.md)). Between blocks a worker spins ~50 µs, then
parks in the kernel on a 32-bit wake word (futex / `WaitOnAddress`, #858). Worker count (#861):
public `System().renderThreads(n)` / C API `yse_system_set_render_threads()` — `-1` auto = physical
cores − 1 capped at `MAX_AUTO_WORKERS` (8), counting since #862 only cores inside the process
affinity mask (SMT siblings as one; efficiency cores included — measured, the heavy scenes keep
scaling onto them), `0` = the audio thread alone, `n` = exactly n (≤ 64); applied at once with no
session or an offline one, deferred to the next `init()` during a device session, persists across
`close()`. `Global().setRenderWorkerCount()` is the internal apply-now hook used by tests and
benches. The old pool's `MAX_AUTO_RENDER_THREADS` = 2 stopgap (#650) is retired. Placement (#862):
[internal/cpuTopology.h](../../YseEngine/internal/cpuTopology.h) reads the physical cores (SMT
siblings merged, the process affinity mask applied) and their efficiency class (Windows
`GetLogicalProcessorInformationEx`; Linux/Android sysfs `cpu_capacity`, else
`cpufreq/cpuinfo_max_freq` — hybrid when the weakest core is below 0.85× the strongest), once per
process. Worker i is planned onto entry i mod n of performance-cores-first order (Intel P/E, Zen
5/5c, big.LITTLE) (entry 0 left to the audio thread) and applies a soft hint on its own thread at
start: Windows `SetThreadIdealProcessorEx`; Linux/Android on a hybrid part an affinity mask of the
whole performance cluster (a uniform machine gets none — a one-core mask would be a hard pin). All
best-effort; `global::init()` logs the count and each worker's core at debug level.
