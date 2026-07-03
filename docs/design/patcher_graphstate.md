# Patcher `GraphState`: swappable topology + atomic publish

Status: **design gate, pre-implementation**. Tracking issue:
[#226][gh-226]. Parent epic: [#189 — make the patcher graph wait-free on
the audio thread][gh-189].

This document is the settled decision for issue [#226][gh-226]. Per the
epic, the mechanism must be agreed *before* implementation code lands. It
fixes the record for the sub-issues that follow — [#225][gh-225] (SPSC
value-command queue), [#227][gh-227] (safe reclamation), [#228][gh-228]
(off-thread parse/dump/clear/rename), [#229][gh-229] (TSan/ASan stress
gate) — so they treat the graph-ownership model here as a fixed contract.

This follows the design-issue-first pattern proven by
[docs/design/synth_core.md][doc-synth] ([#151][gh-151]) and
[docs/design/live_coding_dsl.md][doc-dsl] ([#120][gh-120]).

## Table of contents

1. [The bug](#the-bug)
2. [Goal](#goal)
3. [Decision summary](#decision-summary)
4. [Mechanism: stable objects + swappable topology](#mechanism-stable-objects--swappable-topology)
5. [`GraphState` contents](#graphstate-contents)
6. [The send path: pinned graph + stable id](#the-send-path-pinned-graph--stable-id)
7. [Why not the alternatives](#why-not-the-alternatives)
8. [Memory ordering](#memory-ordering)
9. [Structural edits: build and swap](#structural-edits-build-and-swap)
10. [Reclamation handoff](#reclamation-handoff)
11. [Scope of #226](#scope-of-226)
12. [Open dependencies](#open-dependencies)

## The bug

Two contradictory synchronization regimes meet in
[`patcherImplementation.cpp`][src-patcher]. `Calculate` takes `mtx` on the
audio thread every block ([`patcherImplementation.cpp:65`][src-calc]),
while every structural edit — `CreateObject`, `DeleteObject`, `Connect`,
`Disconnect`, `ParseJSON` — mutates, under the same mutex, exactly the
state the callback walks. Live-editing a playing patcher blocks the audio
callback for the whole edit. That is the RT violation.

The reason the mutex is *needed* is that the connection topology lives
**inside the objects**:

- [`outlet::connections`][src-outlet-h] — `std::vector<inlet*>`, the
  fan-out targets an outlet pushes to.
- [`inlet::dspConnection` and `inlet::connections`][src-inlet-h] — the
  single DSP input and the reverse control-edge list.

`Connect`/`Disconnect` `push_back`/`erase` on those vectors in place
([`outlet.cpp:53-66`][src-outlet-connect], [`inlet.cpp:110-138`][src-inlet-connect]),
so a reallocation can move the very array the audio thread is iterating in
[`outlet::SendBuffer`][src-sendbuffer]. The mutex is papering over a
data-ownership problem.

## Goal

`Calculate` reads the graph through a single `std::atomic<GraphState*>`
load — **no mutex**. Structural edits build the next `GraphState`
off-thread and publish it with one atomic pointer swap. The audio thread
pins one coherent snapshot for the duration of a block.

## Decision summary

| Question | Decision |
| --- | --- |
| Mechanism | **Stable objects + swappable topology** (epic option 1). Objects allocated once, never rebuilt; DSP state preserved for free. |
| Send-path resolution | **Pinned graph + stable id.** Audio thread pins the active `GraphState` once per block; outlets resolve targets from it by a construction-time dense id. |
| Traversal model | **Unchanged** — the push/readiness scheduler is kept (see rationale below). Push-vs-pull is orthogonal to RT-safety; this issue moves *where topology is stored*, not *how the graph is walked*. |
| Memory ordering | Publish `store(release)`; audio thread `load(acquire)` once per block, then pin. |
| Reclamation | Retired `GraphState`/objects handed to a retire queue; freeing is [#227][gh-227]'s job — never on the audio thread. |

### Why the traversal model is not touched

The push/readiness scheme is not a legacy accident — it carries real
semantics that a topological pull traversal does not replicate:

1. **Readiness-gated fan-in.** [`CalculateIfReady`][src-calcifready] fires
   an object only once *all* its DSP inputs have arrived
   ([`WaitingForDSP`][src-waiting]). That is a data-driven topological
   schedule for free; a mixer with N buffer inputs calculates exactly
   when the last input lands.
2. **The graph is heterogeneous, not pure-DSP.** Inlets carry buffers
   **and** events — bang/int/float/list — and events also trigger
   `CalculateIfReady` ([`inlet.cpp:48-103`][src-inlet-set]). Event outlets
   fire *during* an object's `Calculate`; they are not latched buffers a
   pull walk could read afterward. A pull model would still need the push
   mechanism for event edges — i.e. both, which is worse.
3. **Feedback.** Patcher DSP graphs routinely contain cycles. The
   push + per-block `dspReady` scheme tolerates them; a strict
   topological pull requires a DAG or hand-coded cycle-breaking.

Decoupling topology into `GraphState` does **not** foreclose a future
pull traversal — it *enables* one, because the hard part (getting
topology out of the objects) is done here. If a pull walk ever proves
worthwhile it becomes a localized change to `Calculate` alone. It is out
of scope for this issue and the epic's stated decision.

## Mechanism: stable objects + swappable topology

Objects are allocated once and never rebuilt. All DSP history (filter
state, delay lines, phase accumulators) lives inside the object and is
therefore preserved across any edit for free — this is why the epic chose
this over per-object `Clone()`.

`GraphState` owns **only** the topology — the object set plus adjacency.
It owns no DSP state and no buffers. Editing the graph means building a
new `GraphState` that references the same stable objects with different
wiring, then swapping the pointer.

The in-object `outlet::connections`, `inlet::connections`, and
`inlet::dspConnection` stop being the live topology. `GraphState` becomes
the single source of truth; `DumpJSON` reads adjacency from the active
`GraphState` rather than from the objects. (`dspReady` stays in the inlet
— it is per-block state, written and read only on the audio thread.)

## `GraphState` contents

An immutable snapshot, built off-thread, read-only once published:

- **Object list** — the objects to process this snapshot (for iteration
  and to define lifetime membership).
- **DSP start-points** — precomputed list of DSP objects with no active
  DSP input. Replaces the per-block scan of every object testing
  [`IsDSPStartPoint`][src-startpoint] in `Calculate`.
- **DAC list** — precomputed, for the output-summing loop.
- **Adjacency** — for each outlet, an immutable, resolved list of target
  `inlet*`. Pointers are stable because objects are never rebuilt.
  Indexed by the outlet's dense id (below), so lookup is O(1) with no
  hashing on the hot path.
- **Per-inlet has-DSP-input flags** — replaces the audio-thread reads of
  `dspConnection` used by start-point selection and `WaitingForDSP`.

Everything in `GraphState` is set at build time and never mutated after
publication. Its internal vectors do not reallocate during reads.

## The send path: pinned graph + stable id

At the top of `Calculate`, the audio thread performs exactly one atomic
load and pins the result for the whole block:

```cpp
void patcherImplementation::Calculate(THREAD thread) {
  currentBlockGraph = active.load(std::memory_order_acquire); // once per block
  // ... ResetDSP over currentBlockGraph->objects
  // ... for each currentBlockGraph->startPoints: obj->Calculate(thread)
  // ... sum currentBlockGraph->dacs into output
}
```

`currentBlockGraph` is a plain (non-atomic) member: written only here, at
the top of the block, and read only by the same audio thread during that
block. It never changes mid-block, so every send within the block sees
one coherent snapshot.

Each outlet resolves its targets from the pinned graph:

```cpp
void outlet::SendBuffer(DSP::buffer* value, THREAD thread) {
  for (inlet* in : owner->parentPatcher()->currentBlockGraph->targetsOf(graphId))
    in->SetBuffer(value, thread);
}
```

- **`graphId`** is a dense integer assigned to the outlet **once, at
  construction**, and never rewritten. Because it never changes on a
  swap, the audio thread never observes a torn id, and no object is
  written during publication. `GraphState::targetsOf(id)` indexes a flat
  vector — O(1), allocation-free, no hashing.
- **Reaching the patcher** is via the owning object: an outlet is created
  by its object (the `ADD_OUT_*` macro passes `this`), and `pObject`
  already carries `parent` (the patcher, via `SetParent`). Two plain
  pointer hops on the audio thread.

This confines the implementation surface to `outlet`, `inlet`,
`patcherImplementation`, and the two `ADD_OUT_*` / `ADD_IN_*` macros
(to thread the owning-object pointer). **All ~40 object `Calculate`
bodies are untouched** — they still call `outputs[i].SendBuffer(&buf,
thread)` verbatim.

## Why not the alternatives

- **Thread `GraphState&` through the render.** Add a context parameter to
  `Calculate` and `SendBuffer`. Explicit, no back-pointers — but it
  touches every DSP object's `Calculate` signature and every send call
  site (~40 objects + the macros). That is precisely the large mechanical
  surface the epic chose stable-objects to avoid.
- **GraphState-driven pull traversal.** Cleanest end-state for a
  homogeneous DAG, but it reworks the execution model and does not fit
  YSE's heterogeneous event+DSP+feedback graph (see rationale above).
  Deferred, not foreclosed.

## Memory ordering

- **Publish** (control thread, after the new `GraphState` is fully
  built): `active.store(next, std::memory_order_release)`. The release
  fence guarantees every write that constructed `next` (its adjacency
  vectors, target spans, start-point/DAC lists) is visible before the
  pointer becomes visible.
- **Read** (audio thread, once per block):
  `active.load(std::memory_order_acquire)`. The acquire pairs with the
  release, so the audio thread sees a fully constructed snapshot. Pinning
  it in `currentBlockGraph` for the block guarantees coherence: no send
  can straddle two topologies.

A single `atomic<GraphState*>` (not per-outlet atomics) is required so a
multi-edge edit — e.g. a disconnect + connect, or a create-with-wiring —
publishes atomically and the audio thread never sees a half-applied
topology.

## Structural edits: build and swap

`Connect`, `Disconnect`, `CreateObject`, `DeleteObject` stop mutating
in-object vectors under `mtx`. Each instead, on the control thread:

1. Copies the current `GraphState`'s adjacency into a mutable builder.
2. Applies the edit:
   - **Create** — allocate the object (allocation on the control thread
     is fine), add it to the object list. No wiring yet.
   - **Delete** — remove the object from the object list and drop every
     edge touching it. The object is *not* freed here (see reclamation).
   - **Connect / Disconnect** — add/remove the resolved edge; recompute
     the affected start-point/has-DSP-input entries.
3. Finalizes an immutable `GraphState` (recompute start-points, DAC list,
   flat adjacency indexed by `graphId`).
4. `active.store(next, release)`.
5. Hands the retired `GraphState` (and, for Delete, the removed object) to
   the retire queue.

`Calculate` loses its `mtx.lock()`/`unlock()` entirely.

## Reclamation handoff

A retired `GraphState` (and any object removed by `Delete`) may still be
referenced by an audio block in flight, so it **must not** be freed on the
control thread immediately, and **never** on the audio thread. #226 only
*hands off* the retired pointer to a retire queue; the epoch/hazard-style
reclamation that frees it once the audio thread has provably advanced past
it is [#227][gh-227]'s deliverable.

Until #227 lands, #226 uses a conservative interim: retired snapshots
accumulate in the retire list and are reclaimed by a simple
deferred-by-one-block rule (a snapshot retired before block *N* is freed
no earlier than the start of block *N+2*, once the audio thread has
demonstrably loaded a newer pointer). This is correct but not optimal;
#227 replaces it. Removed *objects* are held in the same list and freed
under the same rule, so a deleted object outlives any block that could
still touch it.

## Scope of #226

**In scope:**

- Introduce `GraphState` and `patcherImplementation::active`
  (`atomic<GraphState*>`).
- `Create` / `Delete` / `Connect` / `Disconnect` → build-and-swap.
- Remove `mtx` from `Calculate`; pin the snapshot once per block.
- Redirect `DumpJSON` and start-point/`WaitingForDSP` reads to the active
  `GraphState`.
- Interim deferred reclamation (placeholder for #227).
- Regression tests: an edit on a running patcher preserves audio
  continuity and the topology takes effect; a delete does not free an
  object still in use.

**Out of scope (other sub-issues):**

- Value/control message deferral via SPSC queue — [#225][gh-225].
- Real epoch/hazard reclamation — [#227][gh-227].
- Off-thread `ParseJSON` / `DumpJSON` / `Clear` / `SetName`, retiring the
  `fileHandlerActive` hack — [#228][gh-228].
- TSan/ASan concurrency stress gate — [#229][gh-229].

## Open dependencies

- **`fileHandlerActive`.** `ParseJSON` currently calls `CreateObject` /
  `Connect` in a loop under one held lock, using `fileHandlerActive` to
  skip per-call locking. Under build-and-swap this becomes one snapshot
  built from many edits then published once. #226 keeps `ParseJSON`
  functionally correct by building the full parsed graph into one builder
  and swapping once; the *threading* rework (parse fully off-thread) is
  #228. The `fileHandlerActive` flag is retired no later than #228.
- **Interim reclamation vs. #227.** The deferred-by-one-block rule above
  is the seam. If #227 lands concurrently, #226's interim is deleted in
  favour of it; the retire-queue handoff API is designed to be the same
  shape either way.
- **Value path still synchronous until #225.** `PassBang`/`PassData` and
  live param writes remain on their current path in #226; they read the
  same pinned `GraphState` for fan-out, so they are consistent, but their
  *deferral* is #225.

<!-- link references -->
[gh-189]: https://github.com/yvanvds/yse-soundengine/issues/189
[gh-225]: https://github.com/yvanvds/yse-soundengine/issues/225
[gh-226]: https://github.com/yvanvds/yse-soundengine/issues/226
[gh-227]: https://github.com/yvanvds/yse-soundengine/issues/227
[gh-228]: https://github.com/yvanvds/yse-soundengine/issues/228
[gh-229]: https://github.com/yvanvds/yse-soundengine/issues/229
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[doc-synth]: synth_core.md
[doc-dsl]: live_coding_dsl.md
[src-patcher]: ../../YseEngine/patcher/patcherImplementation.cpp
[src-calc]: ../../YseEngine/patcher/patcherImplementation.cpp#L65
[src-outlet-h]: ../../YseEngine/patcher/outlet.h#L53
[src-inlet-h]: ../../YseEngine/patcher/inlet.h#L84
[src-outlet-connect]: ../../YseEngine/patcher/outlet.cpp#L53
[src-inlet-connect]: ../../YseEngine/patcher/inlet.cpp#L110
[src-sendbuffer]: ../../YseEngine/patcher/outlet.cpp#L47
[src-calcifready]: ../../YseEngine/patcher/pObject.cpp#L34
[src-waiting]: ../../YseEngine/patcher/inlet.cpp#L105
[src-inlet-set]: ../../YseEngine/patcher/inlet.cpp#L48
[src-startpoint]: ../../YseEngine/patcher/pObject.cpp#L17
