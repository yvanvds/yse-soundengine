# Send/return bus architecture specification

Status: **design gate, pre-implementation**. Tracking issue:
[#164][gh-164]. Parent epic: [#146 — Effects & routing: channel inserts,
send/return buses, DAW-baseline modules][gh-146].

This document is the settled decision for issue [#164][gh-164]. Per the
epic, the send/return architecture must be agreed *before* implementation
code lands, because getting the threading story wrong would put a lock on
the audio path. It fixes the record for the sub-issues that implement it —
[#165][gh-165] (ENGINE: send/return buses) and [#166][gh-166] (C-API:
channel DSP, effect modules, and send/return surface) — so they treat the
routing and threading model here as a fixed contract.

This follows the design-issue-first pattern proven by
[docs/design/synth_core.md][doc-synth] ([#151][gh-151]) and
[docs/design/live_coding_dsl.md][doc-dsl] ([#120][gh-120]).

> **Amended 2026-07-07, pre-merge design review.** Three decisions were
> revised: **return→return sends are allowed** (the routing is an
> acyclic DAG validated at wiring time; the flat "nesting limit = 0" was
> justified mainly by the epic's earlier non-goal, and delay-throw-into-
> reverb is bread-and-butter routing — see
> [§10](#cycle-prevention-at-wiring-time)); the **returns phase
> parallelizes each return's insert chain on the fast pool** (returns
> host the engine's heaviest DSP; processing them serially on the
> callback thread while the pool idles wasted the engine's own machinery
> — see [§7](#the-block-cycle-with-sends-and-returns)); and the
> **send-slot count is a per-channel setup choice** (default 4) rather
> than a compile-time constant. Two forward notes were added: returns
> are the intended successor home for *spatial* reverb (zone-bound
> returns with proximity-modulated sends), and send levels are expected
> **modulation targets** written at control rate — see
> [§12b](#forward-notes-spatial-reverb-and-modulated-sends) and
> [docs/project_vision.md](../project_vision.md).

The specification builds on the **final** channel semantics as merged in
[#159][gh-159] (channel insert DSP chain) and [#158][gh-158] (the
N-channel `dspObject::process` contract). Everything below is grounded in
the current engine — the `CHANNEL::implementationObject` block cycle
(`dsp()` + `buffersToParent()`), the fast/slow thread pools, the
`lfQueue` message inbox, and the `OBJECT_*` lifecycle — not a hypothetical
mixer. Concrete anchors (files and line references as of writing) are
listed in [§13](#current-engine-anchors).

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [Why the engine has no precedent](#why-the-engine-has-no-precedent)
3. [The current block cycle (what we build on)](#the-current-block-cycle-what-we-build-on)
4. [Decision summary](#decision-summary)
5. [Return-bus representation](#return-bus-representation)
6. [Send slots and tap points](#send-slots-and-tap-points)
7. [The block cycle with sends and returns](#the-block-cycle-with-sends-and-returns)
8. [Threading model and RT-safety argument](#threading-model-and-rt-safety-argument)
9. [Lifecycle: creating and destroying returns and sends](#lifecycle-creating-and-destroying-returns-and-sends)
10. [Cycle prevention at wiring time](#cycle-prevention-at-wiring-time)
11. [Public API surface (proposed)](#public-api-surface-proposed)
12. [Out of scope (explicit)](#out-of-scope-explicit)
12b. [Forward notes: spatial reverb and modulated sends](#forward-notes-spatial-reverb-and-modulated-sends)
13. [Current-engine anchors](#current-engine-anchors)
14. [Acceptance checklist mapping](#acceptance-checklist-mapping)
15. [Cross-references](#cross-references)

---

## Goals and non-goals

### Goals

- **A DAW-baseline aux send/return.** Any channel can send a scaled copy
  of its signal to a **return bus**; the return applies its own insert
  chain (and optionally reverb), then folds back into the master mix.
  This is the classic "reverb aux" / "delay throw" topology.
- **Zero locks, zero allocation, zero blocking on the audio path.** All
  cross-channel accumulation happens at a point in the block cycle that is
  already single-threaded on the audio callback thread. No new
  synchronization primitive is introduced on the render path.
- **Reuse, don't reinvent.** A return is a `YSE::channel` with a flag, so
  it inherits mixing buffers, the ramped volume fader, the insert chain
  ([#159][gh-159]), reverb attachment, per-block peak metering, the
  manager lifecycle, and device channel-count resize — for free.
- **Click-free send changes.** Per-channel send levels ramp exactly like
  `adjustVolume()` (linear over one block) so live send-level moves never
  zipper.
- **Wiring is validated where it is cheap.** Illegal routings (a send to a
  non-return, a self-send, a cycle) are rejected on the setup/control
  thread at wiring time, never discovered on the audio thread.

### Non-goals

Deferred deliberately; see [§12](#out-of-scope-explicit) for the full list
with rationale. In short: no sidechain taps, no per-sound sends, no
cyclic routing (the routing is an acyclic DAG — return→return sends are
**allowed**, cycles are rejected at wiring time), no PDC/latency
compensation, and no send-automation *engine* (send levels ramp linearly
per block; they are, however, designed to be *written* at control rate by
external drivers — [§12b](#forward-notes-spatial-reverb-and-modulated-sends)).

## Why the engine has no precedent

The engine's only existing "send" is the reverb, and it is **not** a mixer
send. `REVERB::Manager().process(channel)` blends spatial reverb zones by
listener proximity and writes the result *in place* into the one channel
the global reverb is currently attached to (`reverbManager.cpp:281`). It
is one instance, one target, computed from 3D geometry — nothing about it
generalizes to "N channels each contribute a scaled copy into a shared
bus." So the send/return routing is genuinely new surface, which is why it
earns its own design gate rather than a decision baked into
[#146][gh-146].

## The current block cycle (what we build on)

One audio block is rendered by `deviceManager::renderOneBlock()`
(`deviceManager.cpp:66`):

```cpp
void deviceManager::renderOneBlock() {
  master->dsp();             // (A) parallel render of the source tree
  master->buffersToParent(); // (B) serial bottom-up summation into master
}
```

**(A) `dsp()` — parallel.** `implementationObject::dsp()`
(`channelImplementation.cpp:93`) clears the channel's `out` buffer,
**dispatches each child channel to the fast pool** via
`Global().addFastJob(child)`, renders the channel's own sounds, runs the
pre-fader insert chain (`processInsertDSP()`, [#159][gh-159]), then reverb
and the underwater FX. Child channels therefore render **concurrently** on
fast-pool workers. Each worker writes only into *its own* channel's `out`
— strict single-writer-per-buffer.

**(B) `buffersToParent()` — serial.** `buffersToParent()`
(`channelImplementation.cpp:149`) runs on the audio callback thread. For
the channel it:

1. `join()`s the channel's own dispatched `dsp()` job (help-running wait,
   `threadPool.cpp:48` — never sleeps, never locks: if the worker hasn't
   finished it runs sibling render jobs inline);
2. **recurses** `buffersToParent()` on each child (an ordinary function
   call on the same audio thread — *not* a fast job);
3. applies the channel fader in place via `adjustVolume()` (a per-sample
   linear ramp from `lastVolume` to `newVolume`,
   `channelImplementation.cpp:369`);
4. publishes the post-fader peak;
5. if not the master, does `parent->out[i] += out[i]` — the many-to-one
   accumulation into the parent buffer.

The load-bearing fact for this design: **step 5 — every many-to-one buffer
accumulation in the engine — already happens serially on the audio thread,
inside this single depth-first post-order walk, after all `dsp()` workers
for the subtree have been joined.** The parallel phase never accumulates
across channels; it only fills per-channel buffers. Returns slot into
exactly this discipline.

## Decision summary

| Question ([#164][gh-164]) | Decision |
|---|---|
| Tap point: pre- vs post-fader | **Per-send choice.** Each send slot carries a `preFader` flag. Default **post-fader** (taps after `adjustVolume()` in the summation walk). Pre-fader taps the channel's `out` before `adjustVolume()` in the same walk. |
| Return representation | **A `YSE::channel` flagged `isReturn`.** Reuses buffers, fader, inserts, reverb, metering, lifecycle. No new bus object type. |
| Send granularity | **Per-channel only.** Per-sound sends are out of scope (per-*voice* sends from synths are a named forward direction, [§12b](#forward-notes-spatial-reverb-and-modulated-sends)). |
| Sends per channel | **Per-channel count chosen at channel setup** (default `4`), slots allocated once on the slow pool — wiring stays allocation-free. |
| Send level | Ramped per-block linear, identical to `adjustVolume()`. Expected to be *written* at control rate by modulation drivers ([§12b](#forward-notes-spatial-reverb-and-modulated-sends)). |
| Cross-channel hand-off | Sends **accumulate into return buffers during the serial `buffersToParent()` walk**, at the channel's own tap point. One writer thread → no locks, no atomics on the accumulation. |
| Join / ordering point | Returns are processed **inside master's `buffersToParent()`, after the source-tree child recursion completes and before master's own fader** — in ascending **generation** order; within a generation, each return's insert chain is **dispatched to the fast pool** and help-joined. |
| Cycle prevention | Enforced on the **setup/control thread at wiring time**. Return→return sends are allowed; a bounded DFS over the return-send graph rejects cycles, and each return carries a **generation index** (longest-path layer) that fixes the audio-thread processing order. |
| Return lifecycle | Return = channel = existing manager lifecycle. Send-target safety on return teardown uses an intrusive **back-reference registry** (the many-to-one generalization of the insert chain's `calledfrom` guard). |

## Return-bus representation

A return bus is a `CHANNEL::implementationObject` with a new
`Bool isReturn` flag (default `false`). Choosing "flagged channel" over a
dedicated bus object is the low-surface choice the epic already leans
toward ("reuses mixing, volume, inserts for free"). A return inherits,
unchanged:

- the `std::vector<DSP::buffer> out` mix buffer (device-shaped, resized on
  layout change by `resize()`);
- the ramped `adjustVolume()` fader;
- the pre-fader insert chain from [#159][gh-159] (`insert_dsp`,
  `processInsertDSP()`) — this is how a return gets its reverb/delay
  effect;
- optional `attachReverb()`;
- pre/post peak metering;
- the full `OBJECT_*` manager lifecycle.

A return differs from an ordinary channel in exactly three ways:

1. **It is excluded from the source `dsp()` dispatch tree.** A return is
   *not* a child in any channel's `children` list, so `dsp()` never
   dispatches it to the fast pool and `buffersToParent()`'s child
   recursion never visits it. It is owned by a separate, master-level
   **returns list** on the channel manager.
2. **Its `out` buffer is the send accumulation target.** Sends write into
   it during the summation walk (see [§7](#the-block-cycle-with-sends-and-returns)).
   Because the return is not dispatched, nothing clears `out` on the
   parallel path; the serial phase zeroes it once at block start, before
   any send writes.
3. **It is processed in an explicit returns phase**, after all source
   sends have accumulated and before the master fader.

The routing shape is **sources → return DAG → master**. A return's final
output always folds into master, and a return may itself hold sends
targeting other returns (a delay return throwing into a reverb return —
bread-and-butter mixing). The only structural rule is **acyclicity**,
validated at wiring time ([§10](#cycle-prevention-at-wiring-time)). The
epic's original "buses are one level" non-goal was reviewed and lifted:
it protected nothing in the threading model (the extension is a
control-thread check plus a fixed processing order), and it blocked a
standard technique.

## Send slots and tap points

Each channel impl gains an array of send slots sized **once at channel
setup on the slow pool** (never on the audio thread):

```
struct sendSlot {
  CHANNEL::implementationObject* target;  // a return, or nullptr = empty
  Flt newLevel;                           // control-thread target level
  Flt lastLevel;                          // audio-thread ramp state
  Bool preFader;                          // tap point for this slot
  // intrusive back-reference link (see §9), no heap node
};
std::vector<sendSlot> sends;  // sized in setup(); default 4, per-channel choice
```

The count defaults to `4` and is a per-channel creation parameter, not a
compile-time constant — a channel that fans out to many returns (the
zone-reverb direction, [§12b](#forward-notes-spatial-reverb-and-modulated-sends))
asks for more at create time. What matters for RT-safety is only that the
array is sized before the channel reaches the render path and never
resized on it; the per-block send loop stays bounded by the chosen count.
A send is "active" when `target != nullptr`.

**Tap point semantics** (both evaluated in the serial summation walk, so
they read a *finalized* `out`):

- **Post-fader (default).** Tap after `adjustVolume()` has scaled `out` by
  the channel fader. The send follows the channel fader — pulling the
  channel fader down also pulls its reverb/delay throw down. This is the
  standard aux-send behavior and the default.
- **Pre-fader.** Tap the channel's `out` *before* `adjustVolume()` scales
  it. Independent of the channel fader (headphone/cue-style sends, or a
  fully-wet return where the dry channel is faded out). Because
  `adjustVolume()` mutates `out` in place, a pre-fader send must read
  `out` earlier in the same walk — see [§7](#the-block-cycle-with-sends-and-returns).

**Send level ramp.** Applied identically to `adjustVolume()`: a per-sample
linear ramp from `lastLevel` to `newLevel` across `STANDARD_BUFFERSIZE`,
fused into the multiply-accumulate:

```
step = (newLevel - lastLevel) / STANDARD_BUFFERSIZE
for each output i, each sample j:
    target->out[i][j] += src[i][j] * (lastLevel + step * j)
lastLevel = newLevel
```

where `src` is `out` (post-fader) or the pre-fader snapshot. When
`lastLevel == newLevel` the ramp collapses to a plain scaled MAC. This
guarantees click-free live send-level moves without any extra state.

## The block cycle with sends and returns

No change to `renderOneBlock()` is required. The returns phase is folded
into master's `buffersToParent()`, between the source-tree recursion and
master's own fader. The revised master path:

```
master->buffersToParent():
  join();                       // master's own dsp job (immediate: master isn't dispatched)
  zeroReturnBuffers();          // (0) clear every return's `out` — once, before any send
  for child in master.children: // (1) source tree: sums into master->out,
      child->buffersToParent(); //     and taps sends into return buffers (below)
  processReturns();             // (2) generation-ordered, fast-pool-parallel (below)
  adjustVolume();               // (3) master fader over sources + returns
  publish master peak;          // (4)
  // parent == nullptr → done

processReturns():               // audio thread; returns grouped by generation (§10)
  for g in 0 .. maxGeneration:
    for r in returns where r.generation == g:
        Global().addFastJob(r->returnDspJob);   // insert chain + reverb over r->out
    for r in returns where r.generation == g:
        r->join();              // help-running wait, as in the source tree
        r->adjustVolume();      // the return's own fader
        publish r peak;
        run r's send taps;      // pre/post-fader, targets are generation > g only
        master->out[i] += r->out[i];
```

**The returns phase is parallel where it is expensive and serial where
it must be ordered.** Each return's insert chain — the reverbs and
delays that are the whole point of a return, and the heaviest DSP in the
engine — runs as a fast-pool job, exactly like a source channel's
`dsp()`. Each job writes only *its own* return's `out` (single-writer,
unchanged discipline). The accumulation steps — a return's send taps
into later-generation returns and its fold into `master->out` — stay on
the audio thread, serial, after the join, exactly like the source tree's
`parent += out`. By the time `processReturns()` runs, the source-tree
fast jobs are all joined, so the pool is otherwise idle — three reverb
returns cost roughly one reverb of wall-clock instead of three. Within a
generation, returns are mutually independent by construction (a
generation-`g` return's sends may only target generations `> g`,
enforced at wiring time, [§10](#cycle-prevention-at-wiring-time)), so
dispatch order inside a generation is irrelevant; the iteration order is
fixed only for determinism of the `+=` sequence.

And every non-master channel's `buffersToParent()` gains the send tap:

```
buffersToParent():             // non-master channel
  join();
  for child in children: child->buffersToParent();
  // --- pre-fader sends tap here, before the fader mutates `out` ---
  for slot in sends where slot.active && slot.preFader:
      accumulateRamped(slot.target->out, /*src=*/out, slot);
  adjustVolume();              // channel fader, in place on `out`
  publish post-fader peak;
  // --- post-fader sends tap here ---
  for slot in sends where slot.active && !slot.preFader:
      accumulateRamped(slot.target->out, /*src=*/out, slot);
  if parent == nullptr: return;
  parent->out[i] += out[i];    // existing tree summation
```

**Why this ordering is correct:**

- `zeroReturnBuffers()` (0) runs after phase (A) `dsp()` — which never
  touches returns — so zeroing races nothing, and it runs before any send
  writes.
- Master's child recursion (1) visits the *entire* source subtree
  depth-first before control returns to master's body. Returns are not in
  that subtree. Therefore, by the time (1) completes, **every source
  channel has already run its send taps** and every return `out` holds the
  complete accumulated send signal. No send can arrive late.
- `processReturns()` (2) runs each return's insert chain / reverb over
  its accumulated `out` (fast-pool job), then — after the join — applies
  the return's own fader, runs the return's own send taps (targets are
  strictly later generations), and does
  `master->out[i] += return->out[i]`. Because all of this happens
  **before** master's `adjustVolume()` (3), returns are correctly covered
  by the master fader (DAW-standard: the master fader is last).
- A generation-`g` return's `out` is complete before any generation-`g`
  job is dispatched: source sends accumulated during (1), and
  earlier-generation returns' sends accumulated during their own serial
  post-join step — which finishes before generation `g` dispatches.
- A pre-fader send reads `out` before `adjustVolume()`; a post-fader send
  reads it after. Both taps and the `parent += out` summation write
  disjoint buffers, so their relative order within the channel body is
  irrelevant.

## Threading model and RT-safety argument

This is the crux of the design gate. The claim: **send/return adds no lock,
no atomic, and no allocation to the render path, and introduces no data
race.**

1. **The parallel phases never accumulate across channels.** Fast-pool
   workers run `dsp()`, which writes only the worker's own channel `out`
   (and dispatches grandchildren); a return's fast-pool job runs only the
   return's insert chain, in place, over the return's own `out`. Sends
   are *not* applied in `dsp()` or in return jobs. So every concurrent
   phase keeps strict single-writer-per-buffer, exactly as today.

2. **All cross-channel writes happen in the serial walk.** Send
   accumulation and the returns phase run inside `buffersToParent()` /
   `processReturns()`, which execute on the **audio callback thread only**,
   after the relevant `dsp()` jobs are joined. A return buffer receives
   from many senders, but they execute **one at a time on one thread** —
   the identical mechanism by which a parent buffer already receives from
   many children (`parent->out += out`). "Many-to-one" here is
   many-writes-in-sequence, not many-threads-at-once, so it needs neither a
   lock nor an atomic. This is the same single-writer *discipline* the
   engine already relies on for tree summation; returns do not weaken it.

3. **Reads are of finalized buffers.** A send taps a source channel's `out`
   only after that channel's `buffersToParent()` has `join()`ed its
   `dsp()` job, so `out` is complete. `processReturns()` reads a return's
   `out` only after the source recursion has fully accumulated into it.

4. **Wiring mutations are audio-thread-only, allocation-free.** Adding,
   removing, or re-leveling a send, and flagging a channel as a return, all
   arrive as `lfQueue` messages (like `ATTACH_DSP`,
   `channelImplementation.cpp:323`) and are applied in `sync()` on the
   audio thread. The send slots and back-reference nodes are members
   allocated with the impl, so applying a wiring message is a pointer/scalar
   write — no heap, no lock. The control thread only ever *posts* messages
   and does wiring-time validation; it never touches render state.

5. **Teardown is ordered on the audio thread** (see [§9](#lifecycle-creating-and-destroying-returns-and-sends))
   so no send slot can dereference a freed return.

6. **Help-running wait is reused, not stretched.** The returns phase
   dispatches return-insert jobs to the fast pool and `join()`s them with
   the identical help-running wait (`threadPool.cpp:48`) the source tree
   already uses — if a worker hasn't finished, the audio thread runs
   return jobs inline. No new synchronization primitive, no sleep, no
   lock. And because the source-tree jobs are all joined before
   `processReturns()` runs, the pool is idle at that point — the returns
   phase *reclaims* otherwise-wasted parallelism for the heaviest DSP in
   the engine (a design-review requirement: N reverb returns must not
   serialize N reverbs onto the callback thread).

The net new render-path cost is deterministic and bounded: one zeroing of
each return buffer per block, up to `sendCount` ramped MACs per
active-send channel, and one insert/fader/summation per return — with the
insert chains running concurrently on the fast pool, and none of it
synchronizing beyond the existing help-running join.

## Lifecycle: creating and destroying returns and sends

### Creating a return

A return is created through the normal channel path
(`channel::create` → `Manager().addImplementation()` under
`implementationsMutex` on the control thread → slow-pool `setup()` →
`lfQueue` hand-off to the audio thread →
`OBJECT_CREATED → SETUP → READY`), with one extra step: an `isReturn`
message (or a `createReturn` entry point) flags the impl and links it into
the manager's **returns list** instead of a parent's `children` list. The
flag and list-linking are applied on the audio thread in `sync()`. All
buffer allocation is already done off-thread in `setup()`.

### Wiring / re-leveling / removing a send

The interface posts a message:

- `ADD_SEND{ slot, targetReturn, preFader }` — audio thread writes the
  slot's `target`, `preFader`, resets `lastLevel`, and links the slot's
  back-reference node into the target return's registry (below).
- `SEND_LEVEL{ slot, level }` — audio thread sets `newLevel`; the ramp
  does the rest next block.
- `REMOVE_SEND{ slot }` — audio thread unlinks the back-reference node and
  clears `target` (setting a level of 0 is a soft mute; `REMOVE_SEND` fully
  detaches).
- `SET_GENERATION{ value }` — posted to a return when a wiring change
  alters its generation index ([§10](#cycle-prevention-at-wiring-time));
  a single scalar write.

All are pointer/scalar writes on the audio thread; no allocation.

### Destroying a return (the safety-critical path)

The danger: channel A holds a send whose `target` points at return R; R is
destroyed; A's next-block send tap writes into freed memory. This is the
many-to-one generalization of the insert-chain use-after-free the engine
already guards with `insert_dsp->calledfrom` (the back-pointer nulled on
detach/destruct, `channelImplementation.cpp:46`, `#298`).

**Back-reference registry.** Each return impl owns an intrusive list of the
send slots currently targeting it (the link lives *inside* the `sendSlot`,
so no heap node is allocated). `ADD_SEND` links a slot in; `REMOVE_SEND`
and channel teardown unlink it. The registry is mutated only on the audio
thread.

On return release, the manager's audio-thread `update()` — which already
transitions `OBJECT_RELEASE → OBJECT_DELETE` and reparents children before
the slow pool can free the impl (`channelManager.cpp:108`) — additionally
walks the return's back-reference registry and, for each referencing slot,
clears `target` (disabling the send). Only after every referencing slot is
severed does the return transition to `OBJECT_DELETE` and become eligible
for the slow-pool `deleteJob`. This mirrors the existing
`childrenToParent()` + `disconnect()` + `connectedToParent` release
handshake and guarantees that when the impl is finally freed, no live send
slot points at it. Because it runs on the audio thread at *release* time
(not per block), it costs nothing on a steady-state render.

The symmetric case — a *sending channel* is destroyed while its send to R
is live — is simpler: the channel's destructor/teardown unlinks its own
slots from every target's registry (audio-thread release path), so R's
registry never dangles either.

## Cycle prevention at wiring time

The routing must never let a return feed itself or an ancestor, which on
the audio thread would be an infinite accumulation or a read of a
half-summed buffer.

**v1 allows the full acyclic case.** Return→return sends are legal
routing (a delay return throwing into a reverb return); the only
structural rule is that the send graph over returns is a **DAG**. The
first draft's "nesting limit = 0" was justified mainly by the epic's
earlier non-goal, and review rejected precedent-as-argument: the
extension costs a control-thread graph check and a fixed processing
order — nothing on the render path changes its nature.

Validation runs on the **setup/control thread at wiring time**, before
the `ADD_SEND` message is posted:

1. reject if `target` is not a valid, `isReturn` channel;
2. reject if `target == source` (self-send);
3. if `source` is itself a return: run a **bounded DFS** over the
   control-thread wiring graph (the control thread mirrors all accepted
   sends) from `target` along return→return edges; reject if it reaches
   `source` (cycle). Returns are few; the walk is trivial and never
   touches audio-thread state.

A rejected wiring is logged (via `LogImpl().emit`) and the message is
never posted, so the audio thread only ever sees legal routings.

**Generation indexing fixes the processing order.** After accepting a
wiring change that adds or removes a return→return edge, the control
thread recomputes each return's **generation** — its longest-path depth
over incoming return→return edges (a return fed only by source channels
is generation 0; a return fed by a generation-0 return is generation 1;
and so on). Any return whose generation changed gets a
`SET_GENERATION{ value }` message: a single scalar write applied in
`sync()` on the audio thread, no allocation. The audio-thread returns
phase simply iterates generations in ascending order
([§7](#the-block-cycle-with-sends-and-returns)); it never inspects the
graph. The invariant the wiring rules guarantee: **a return's sends only
target returns of a strictly higher generation**, so within a generation
returns are mutually independent and safe to process concurrently.

## Public API surface (proposed)

Shapes are proposed here for [#165][gh-165]/[#166][gh-166]; final names are
the C-API issue's call. The point is that every entry maps onto the message
+ lifecycle model above.

```cpp
// Mark a channel as a return bus (folds into master; excluded from the
// source dsp tree). Reuses create() then flags via message.
channel& channel::makeReturn();          // or a createReturn(name) factory
bool     channel::isReturn() const;

// Wire / adjust a send from `this` channel to a return. `slot` in [0, N).
channel& channel::send(int slot, channel& returnBus, float level,
                       bool preFader = false);
channel& channel::setSendLevel(int slot, float level);   // ramped
channel& channel::clearSend(int slot);                   // detach
float    channel::getSendLevel(int slot) const;
```

A return, being a channel, keeps `setDSP()` (its insert chain — the actual
effect), `attachReverb()`, `setVolume()` (return fader), and the peak
metering getters unchanged — and, being a channel, a return may itself
call `send()` (targeting another return; the wiring check enforces
acyclicity, [§10](#cycle-prevention-at-wiring-time)). The send-slot count
is a creation-time parameter (default 4). Typical use:

```cpp
YSE::channel verb;   verb.makeReturn().setDSP(&plateReverb);   // a reverb return
YSE::channel slap;   slap.makeReturn().setDSP(&pingPongDelay); // a delay return
music.send(0, verb, 0.3f);          // 30% post-fader throw from the music channel
voice.send(0, slap, 0.2f);          // delay throw from the voice channel
slap.send(0, verb, 0.25f);          // the delay tail washes into the reverb (DAG)
```

**Send levels are modulation targets.** `setSendLevel()` maps to a
bounded inbox message and a one-block linear ramp, which means it is
safe and click-free to call **every control tick**. This is deliberate:
the anticipated writers are not only user code but control-rate drivers
— a patcher outlet, a live-coded expression, or a listener/zone
proximity rule ([§12b](#forward-notes-spatial-reverb-and-modulated-sends)).
Implementations of [#165][gh-165]/[#166][gh-166] must not assume send
levels are set-and-forget.

## Out of scope (explicit)

These are deliberately excluded from v1 and will not be added under these
names without a new design pass:

- **Sidechain taps.** A send feeding another module's *detector/control*
  input (e.g. a compressor keyed off another channel) is a different
  routing with different timing; explicitly out ([#146][gh-146] non-goal).
- **Per-sound sends.** Sends are per-channel only. Per-sound sends would
  multiply the buffer/scratch count and the surface; a sound that needs
  its own send balance can live on its own channel. Per-*voice* sends
  from a synth's positioned notes are a named forward direction
  ([§12b](#forward-notes-spatial-reverb-and-modulated-sends)), not v1.
- **Cyclic or feedback routing.** The topology is sources → return DAG →
  master; cycles are rejected at wiring time
  ([§10](#cycle-prevention-at-wiring-time)). Deliberate feedback routing
  (a return feeding a *source* channel) is a different feature with a
  different stability story.
- **Latency compensation / PDC.** A return with a long insert latency is
  not delay-compensated against the dry path ([#146][gh-146] non-goal).
- **A send automation engine.** Levels ramp linearly over one block, as
  `adjustVolume()` does; no curve/segment automation engine. (External
  control-rate *writers* are expected and supported —
  [§11](#public-api-surface-proposed).)

## Forward notes: spatial reverb and modulated sends

Not implemented by [#165][gh-165]/[#166][gh-166]; recorded so this
architecture is built with its successors in mind (context:
[docs/project_vision.md](../project_vision.md)).

1. **Returns are the intended successor home for spatial reverb.** The
   engine's localized reverb (`REVERB::Manager`, the in-place
   zone-parameter blend at `reverbManager.cpp:281`) predates this
   architecture. The vision-aligned re-expression is: a reverb **zone**
   becomes a *return bus bound to a region of space*, and
   listener/source proximity **modulates the send levels** into it —
   ordinary routing, spatially driven. Distinct spaces crossfade as real
   wet signals instead of morphing one parameter set. The morphing
   reverb itself survives as a module — a reverb whose preset
   interpolation is a control input, with proximity as one possible
   driver among many. Consequences now: the in-place `reverbManager`
   path is **legacy-bound and must not grow new features**, and the
   returns phase was designed parallel ([§7](#the-block-cycle-with-sends-and-returns))
   precisely because several simultaneous zone reverbs are the expected
   load, not an edge case.
2. **Per-voice sends** — a positioned synth note contributing to zone
   returns by *its own* position (per
   [docs/design/per_note_positioning.md](per_note_positioning.md), which
   keeps reverb per-aggregate for v1) — would need per-voice send gains
   estimated inside the synth and accumulated into the channel's send
   taps. The send-slot architecture here doesn't preclude it; it is a
   named future design pass, not scope creep for [#165][gh-165].

## Current-engine anchors

Paths and lines as of writing; the design builds directly on these:

- Channel block cycle — `YseEngine/channel/channelImplementation.cpp`:
  `dsp()` (`:93`), `buffersToParent()` (`:149`), `adjustVolume()` (`:369`),
  `processInsertDSP()` (`:203`), `addDSP()` back-reference guard (`:185`).
- Insert chain contract ([#159][gh-159]) and `insert_dsp` /
  `calledfrom` UAF guard — `channelImplementation.h:203`, `.cpp:46`.
- Block driver — `deviceManager::renderOneBlock()`
  (`YseEngine/device/deviceManager.cpp:66`).
- Manager lifecycle, `toLoadInbox` hand-off, `OBJECT_RELEASE →
  OBJECT_DELETE` release handshake, `runDelete`/slow-pool `deleteJob` —
  `YseEngine/channel/channelManager.cpp:49`, `:108`.
- Message inbox pattern (`ATTACH_DSP`, `sync()`, `parseMessage`) —
  `YseEngine/channel/channelImplementation.cpp:306`,
  `YseEngine/channel/channel.hpp` (`MESSAGE` enum),
  `YseEngine/channel/channelMessage.h`.
- Fast/slow pools and the help-running `join()` —
  `YseEngine/internal/threadPool.cpp:48`,
  `YseEngine/internal/global.cpp` (`addFastJob`/`addSlowJob`).
- Intrusive-list discipline ([#194][gh-194]) —
  `YseEngine/utils/intrusiveForwardList.hpp` (model for the back-reference
  registry and returns list).
- N-channel `dspObject::process` contract ([#158][gh-158]) —
  `YseEngine/dsp/dspObject.hpp`.
- Reverb "send" (the non-precedent) —
  `YseEngine/reverb/reverbManager.cpp:281`.

## Acceptance checklist mapping

Issue [#164][gh-164] acceptance items and where each is settled:

- **Tap point (pre- vs post-fader).** [§6](#send-slots-and-tap-points):
  per-send choice, post-fader default, both tapped in the serial walk.
- **Return-bus representation.** [§5](#return-bus-representation): a
  `YSE::channel` flagged `isReturn`, excluded from the dsp tree, summed via
  the serial walk (no single-writer violation — argued in
  [§8](#threading-model-and-rt-safety-argument)).
- **Send levels (per-channel? per-sound? count? ramped?).**
  [§6](#send-slots-and-tap-points): per-channel only, slot count chosen
  at channel setup (default 4), ramped like `adjustVolume()`; per-sound
  out of scope ([§12](#out-of-scope-explicit)).
- **RT-safe cross-channel hand-off (where in the block cycle, ordering, no
  locks).** [§7](#the-block-cycle-with-sends-and-returns) +
  [§8](#threading-model-and-rt-safety-argument): accumulation in the serial
  `buffersToParent()` walk; returns processed in master's body after the
  source recursion and before the master fader, generation-ordered, with
  insert chains parallel on the fast pool; single writer per buffer → no
  locks/atomics.
- **Cycle prevention at wiring time (setup thread).**
  [§10](#cycle-prevention-at-wiring-time): return→return allowed; bounded
  control-thread DFS rejects cycles; generation indices fix the
  audio-thread order.
- **Out of scope (sidechain, nested return-to-return beyond a stated
  limit).** [§12](#out-of-scope-explicit): sidechain out; the stated
  nesting limit is **acyclicity** (any depth, no cycles) — revised from
  the first draft's zero, see the amendment note.
- **`docs/design/send_return_buses.md` merged; implementation issue
  references it as its contract.** This document; [#165][gh-165] and
  [#166][gh-166] cite it.

## Cross-references

Sub-issues that implement against this contract:

- [#165][gh-165] — ENGINE: send/return buses (implements
  [§5](#return-bus-representation)–[§10](#cycle-prevention-at-wiring-time)).
- [#166][gh-166] — C-API: channel DSP, effect modules, and send/return
  surface (mirrors [§11](#public-api-surface-proposed)).

Design precedents this document follows:

- [docs/design/synth_core.md][doc-synth] ([#151][gh-151]).
- [docs/design/live_coding_dsl.md][doc-dsl] ([#120][gh-120]).
- [docs/design/patcher_graphstate.md][doc-graphstate] ([#226][gh-226]) —
  same "settle the audio-thread ownership model before code" stance.

Depends on (merged): [#158][gh-158] (N-channel contract),
[#159][gh-159] (channel insert chain). Parent epic: [#146][gh-146].

[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-146]: https://github.com/yvanvds/yse-soundengine/issues/146
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-158]: https://github.com/yvanvds/yse-soundengine/issues/158
[gh-159]: https://github.com/yvanvds/yse-soundengine/issues/159
[gh-164]: https://github.com/yvanvds/yse-soundengine/issues/164
[gh-165]: https://github.com/yvanvds/yse-soundengine/issues/165
[gh-166]: https://github.com/yvanvds/yse-soundengine/issues/166
[gh-194]: https://github.com/yvanvds/yse-soundengine/issues/194
[gh-226]: https://github.com/yvanvds/yse-soundengine/issues/226
[doc-synth]: synth_core.md
[doc-dsl]: live_coding_dsl.md
[doc-graphstate]: patcher_graphstate.md
