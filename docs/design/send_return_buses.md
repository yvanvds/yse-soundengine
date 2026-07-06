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
return→return nesting (the stated nesting limit is **zero** levels for
v1), no arbitrary routing graph, no PDC/latency compensation, and no send
automation beyond the per-block linear ramp.

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
| Send granularity | **Per-channel only.** Per-sound sends are out of scope. |
| Sends per channel | **Fixed `N = 4`** slots per channel, allocated once at channel setup — allocation-free wiring. |
| Send level | Ramped per-block linear, identical to `adjustVolume()`. |
| Cross-channel hand-off | Sends **accumulate into return buffers during the serial `buffersToParent()` walk**, at the channel's own tap point. One writer thread → no locks, no atomics on the accumulation. |
| Join / ordering point | Returns are processed **inside master's `buffersToParent()`, after the source-tree child recursion completes and before master's own fader** — so every source send has already been accumulated, and returns are still covered by the master fader. |
| Cycle prevention | Enforced on the **setup/control thread at wiring time**. With return→return disallowed, sources→returns→master is a DAG by construction; the check is a local type test, no graph traversal. |
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

A return still lives at "one level" in the routing sense: **sources →
returns → master** ([#146][gh-146] non-goal: "buses are one level"). A
return's final output always folds into master; a return may not itself be
a send *target* of another return (v1 nesting limit = 0, see
[§10](#cycle-prevention-at-wiring-time) and [§12](#out-of-scope-explicit)).

## Send slots and tap points

Each channel impl gains a fixed array of `N = 4` send slots, allocated
once with the impl (never on the audio thread):

```
struct sendSlot {
  CHANNEL::implementationObject* target;  // a return, or nullptr = empty
  Flt newLevel;                           // control-thread target level
  Flt lastLevel;                          // audio-thread ramp state
  Bool preFader;                          // tap point for this slot
  // intrusive back-reference link (see §9), no heap node
};
sendSlot sends[YSE_MAX_SENDS_PER_CHANNEL /* = 4 */];
```

`N = 4` is a fixed small bound chosen so wiring stays allocation-free and
the per-channel send loop is trivially bounded; it matches the reverb's
own 4-tap early-reflection precedent and is comfortably DAW-typical. A
send is "active" when `target != nullptr`.

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
  processReturns();             // (2) each return: inserts/reverb, fader, += into master->out
  adjustVolume();               // (3) master fader over sources + returns
  publish master peak;          // (4)
  // parent == nullptr → done
```

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
- `processReturns()` (2) runs each return's insert chain / reverb over its
  accumulated `out`, applies the return's own fader, and does
  `master->out[i] += return->out[i]`. Because this happens **before**
  master's `adjustVolume()` (3), returns are correctly covered by the
  master fader (DAW-standard: the master fader is last).
- A pre-fader send reads `out` before `adjustVolume()`; a post-fader send
  reads it after. Both taps and the `parent += out` summation write
  disjoint buffers, so their relative order within the channel body is
  irrelevant.

The returns phase (2) is itself serial and iterates the returns list in a
fixed order. With return→return disallowed, returns are mutually
independent (none reads another's `out`), so any iteration order is
correct; the order is fixed only for determinism.

## Threading model and RT-safety argument

This is the crux of the design gate. The claim: **send/return adds no lock,
no atomic, and no allocation to the render path, and introduces no data
race.**

1. **The parallel phase never accumulates across channels.** Fast-pool
   workers run `dsp()`, which writes only the worker's own channel `out`
   (and dispatches grandchildren). Sends are *not* applied in `dsp()`.
   Returns are never dispatched. So the concurrent phase keeps strict
   single-writer-per-buffer, exactly as today.

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

6. **Help-running wait is unaffected.** The returns phase runs after the
   source recursion's `join()`s; it adds serial audio-thread work but
   dispatches nothing new and waits on nothing, so the `threadPool.cpp:48`
   help-running contract is untouched.

The net new render-path cost is deterministic and bounded: one zeroing of
each return buffer per block, up to `N` ramped MACs per active-send
channel, and one insert/fader/summation per return — all on the audio
thread, none of it synchronizing.

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

**v1 makes cycles structurally impossible** by the one-level rule:

- A **channel** may send only to a channel flagged `isReturn`.
- A **return** may *not* be a send target of another return (nesting limit
  = 0). A return's signal reaches master only through the returns phase,
  never through a send.

Under these two rules the routing is a DAG by construction —
sources → returns → master — with no back edges possible, so **no graph
traversal is needed**. Validation reduces to a local type check performed
on the **setup/control thread at wiring time**, before the `ADD_SEND`
message is posted:

1. reject if `target` is not a valid, `isReturn` channel;
2. reject if `target == source` (self-send);
3. reject if `source` is itself a return (return→return, out of scope for
   v1).

A rejected wiring is logged (via `LogImpl().emit`) and the message is never
posted, so the audio thread only ever sees legal routings. Because the
check is on the control thread and is a plain field test, it costs nothing
on the render path and cannot fail late.

**Forward note (not implemented):** if a future version lifts the nesting
limit to depth 1 (e.g. a reverb return feeding a delay return), cycle
prevention would need a bounded DFS ancestor-walk over the return→return
edges at wiring time (still control-thread), and the returns phase would
need to iterate returns in dependency (topologically sorted) order so an
upstream return is fully summed before a downstream one reads it. The
accumulate-into-return discipline itself is unchanged by that extension —
only the wiring-time check and the returns-phase ordering grow. v1 does
neither.

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
metering getters unchanged. Typical use:

```cpp
YSE::channel verb;  verb.makeReturn().setDSP(&plateReverb);  // a reverb return
music.send(0, verb, 0.3f);          // 30% post-fader throw from the music channel
voice.send(0, verb, 0.15f, true);   // 15% pre-fader throw from the voice channel
```

## Out of scope (explicit)

These are deliberately excluded from v1 and will not be added under these
names without a new design pass:

- **Sidechain taps.** A send feeding another module's *detector/control*
  input (e.g. a compressor keyed off another channel) is a different
  routing with different timing; explicitly out ([#146][gh-146] non-goal).
- **Return→return nesting.** The stated nesting limit is **zero**: a return
  may not be the target of another send. Even one level is out of scope for
  v1 (rationale and the extension shape are in
  [§10](#cycle-prevention-at-wiring-time)). This honors the epic's
  "buses are one level: channels → returns → master."
- **Per-sound sends.** Sends are per-channel only. Per-sound sends would
  multiply the buffer/scratch count and the surface; a sound that needs its
  own send balance can live on its own channel.
- **Arbitrary routing graphs.** No general node graph; the topology is
  fixed at sources → returns → master ([#146][gh-146] non-goal).
- **Latency compensation / PDC.** A return with a long insert latency is
  not delay-compensated against the dry path ([#146][gh-146] non-goal).
- **Send automation beyond the per-block linear ramp.** Levels ramp
  linearly over one block, as `adjustVolume()` does; no curve/segment
  automation engine.
- **More than `N = 4` sends per channel.** Fixed small bound to stay
  allocation-free; raising it is a constant change, not a design change.

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
  [§6](#send-slots-and-tap-points): per-channel only, `N = 4` fixed slots,
  ramped like `adjustVolume()`; per-sound out of scope
  ([§12](#out-of-scope-explicit)).
- **RT-safe cross-channel hand-off (where in the block cycle, ordering, no
  locks).** [§7](#the-block-cycle-with-sends-and-returns) +
  [§8](#threading-model-and-rt-safety-argument): accumulation in the serial
  `buffersToParent()` walk; returns processed in master's body after the
  source recursion and before the master fader; single writer thread → no
  locks/atomics.
- **Cycle prevention at wiring time (setup thread).**
  [§10](#cycle-prevention-at-wiring-time): one-level rule makes routing a
  DAG; local type check on the control thread.
- **Out of scope (sidechain, nested return-to-return beyond a stated
  limit).** [§12](#out-of-scope-explicit): both listed; nesting limit
  stated as zero.
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
