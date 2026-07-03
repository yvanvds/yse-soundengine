# Patcher live `SetParams`: deferred scalar apply + structural replacement

Status: **design gate, settled**. Tracking issue:
[#234][gh-234]. Parent epic: [#189 — make the patcher graph wait-free on
the audio thread][gh-189]. Companion decision:
[patcher_graphstate.md](patcher_graphstate.md) ([#226][gh-226]).

This document settles the design gate in issue [#234][gh-234]: how a live
`pHandle::SetParams` re-parse becomes RT-safe without bypassing the
GraphState swap and without mutating a published object.

## Table of contents

1. [The bug](#the-bug)
2. [Decision summary](#decision-summary)
3. [Classification: scalar vs structural params](#classification-scalar-vs-structural-params)
4. [Scalar path: pre-parsed plan, applied on the audio thread](#scalar-path-pre-parsed-plan-applied-on-the-audio-thread)
5. [Structural path: replacement object + swap](#structural-path-replacement-object--swap)
6. [Why not the alternatives](#why-not-the-alternatives)
7. [Behavioral notes](#behavioral-notes)
8. [Scope](#scope)

## The bug

`pHandle::SetParams` → `pObject::SetParams` → `Parameters::Set` runs on
the control thread with no patcher `mtx` and no `GraphState` swap. It
writes plain `int`/`float`/`std::string` fields through `void*`
([`parameters.cpp`][src-parameters]) and its `PARM_PARSE` callbacks
grow/shrink the live `inputs`/`outputs` vectors
([`gGate.cpp`][src-ggate], `gSwitch`, `gRoute`) — the very vectors and
fields the audio thread reads while rendering the pinned snapshot. That
violates the stable-objects invariant [#226][gh-226] rests on ("objects
are allocated once and never mutated after publication"): a concurrent
re-parse is a reallocation UAF on the audio thread, and a racing
`std::string` write is an SSO↔heap UAF.

## Decision summary

**Option C from the issue — a hybrid — implemented as two regimes that
already exist in the epic's architecture:**

| Param change | Mechanism |
| --- | --- |
| Scalar (`int`/`float`/atomic variants) | **Deferred RT-safe apply.** Parse on the control thread into a POD plan; deliver through a bounded lock-free queue (the [#225][gh-225] pattern); apply with plain/atomic stores at the top of `Calculate`, only if the target object is in the pinned snapshot. |
| Structural (pin-count re-parse via `PARM_CLEAR`/`PARM_PARSE`) **or any `STRING`/`LIST` param** | **Replacement object + swap.** Build a fresh instance of the same type off the live path, apply the params pre-publication, transfer identity (storage ID, GUI properties, the `pHandle`), rewire preserved connections up to the new pin counts, `RebuildAndPublish()`, retire the old object through the [#227][gh-227] epoch reclaimer. |

Routing: `pHandle::SetParams` on an object owned by a patcher goes
through `patcherImplementation::SetObjectParams` (under `mtx`, like every
other edit). A standalone object (no parent — unit tests) keeps the
synchronous `Parameters::Set`, as do `CreateObjectUnlocked`/`ParseJSON`,
which parse params on freshly built objects **before** publication.

## Classification: scalar vs structural params

The split is decided **per object, at registration time**
(`Parameters::NeedsRebuild()`), not per call:

- **Structural** if the object registered `PARM_CLEAR`/`PARM_PARSE`
  callbacks — those exist precisely to translate params into pin
  structure (`gGate`, `gSwitch`, `gRoute`) — **or** registered any
  `STRING`/`LIST` param (`gReceive`/`gSend` `dataName`, `gText`,
  `gMessage`, `gList`).
- **Scalar** otherwise: every plain/atomic `int`/`float` param. This is
  all the DSP objects' live params (`pSine`/`dSaw` frequency, the filter
  cutoffs/Q, `pLine`, `gMetro` period, the math operands, MIDI channels,
  `gFloat`/`gInt` value, …), which is exactly the set where preserving
  in-flight DSP state matters.

Why strings are structural: a `std::string` cannot be written
allocation-free, and the live fields are read on *both* threads
(`dataName` by `DispatchToReceiver` on the audio thread and by
`EnqueueValue`/`BuildGraph` on the control thread). Applying the write on
either thread races the other. Building the string into a fresh,
not-yet-published object removes the mutation entirely — published
objects' strings become immutable, extending the stable-objects
invariant. It also fixes a latent gap for free: replacing a
`gReceive`/`gSend` runs `SetParent`, which re-anchors the bus
subscription / cached bus address under the *new* `dataName` (the old
in-place write left them stale).

## Scalar path: pre-parsed plan, applied on the audio thread

The epic's target architecture already assigns "live param changes to
existing objects" to the value-command regime. Concretely:

- **Control thread (under `mtx`):** tokenize `args` exactly as
  `Parameters::Set` does, but write the values into a POD
  `ParamMsg { pObject* target; count; ops[kParamOpsCap] }` where each op
  is `{PARM_TYPE, void* dest, int|float value}` — no live field is
  touched. `parms.current` *is* updated eagerly (control-side state, read
  by `GetParams`/`DumpJSON` under the same regime as today), so dumps and
  round-trips reflect the new args immediately. Parse errors
  (`std::stof` on garbage) throw on the control thread, exactly as
  before — never on the audio thread. The msg rides a bounded
  `mpmcQueue` (the [#225][gh-225] `ValueMsg` pattern: POD by value,
  nothing to reclaim; full queue → drop + log, never block).
- **Audio thread (top of `Calculate`, before `DeliverPendingValues`):**
  drain the queue; for each msg, apply the ops with plain stores
  (atomics via their store) **only if `target` is in the pinned
  snapshot's object list**. That membership check makes delivery safe
  across a concurrent `DeleteObject`, exactly like the by-name
  re-resolution of value messages: a deleted object is simply absent and
  the msg is dropped.

Why the dangling-`target` ABA cannot happen: the queue is drained in
full at the top of *every* block, so a msg never survives past the first
`Calculate` after its enqueue; a deleted object's free is gated on the
audio epoch advancing **two further blocks** past retirement
([#227][gh-227]). Enqueue happens-before the delete (the caller holds a
live `pHandle`), so the pointer is compared — never dereferenced — while
the allocation is still parked in the retire list.

Ops per msg are capped (`kParamOpsCap = 8`; today's richest scalar
object registers 2). An object that ever exceeds the cap falls back to
the structural path, which is correct for any object — the cap is a
fallback trigger, not a correctness boundary.

## Structural path: replacement object + swap

A pin-count re-parse is a structural edit, so it uses the structural
machinery. Under `mtx`, `ReplaceObjectUnlocked`:

1. Creates a fresh instance of the same `Type()` from the registry and
   applies `SetParams(args)` synchronously — the object is not yet
   published, so callbacks may freely grow/shrink its pins.
2. Transfers identity: the storage `ID` (so `DumpJSON` references stay
   stable) and the GUI properties; the existing `pHandle` is re-pointed
   at the fresh object, so handle identity is preserved for the C API.
3. `SetParent(this)` (after params, mirroring `CreateObjectUnlocked`) and
   `AssignGraphIds` — fresh pins get fresh dense ids; ids are never
   reused.
4. Rewires the peers' edges onto the fresh object for every pin index
   that survives (`i < min(old, new)`), inlet-first per the [#237
   discipline][gh-237] so no one-sided edge can be recorded. Edges on
   removed pins are dropped — same outcome as the old in-place
   `pop_back`.
5. `UnwireFromPeers()` on the old object, `RebuildAndPublish()`, and
   retires the old object through the epoch reclaimer exactly like
   `DeleteObject` (tagged after its covering graph, freed on the
   background pool).

**DSP-state preservation is not violated where it matters.** The epic's
decision protects in-flight DSP state across *edits elsewhere in the
graph*; a param re-parse targets the object itself, and every structural
object is a generic router with no DSP history. For the scalar set —
where state genuinely matters (oscillator phase, filter history, ramp
position) — the deferred apply mutates nothing structural and preserves
state perfectly. Re-instantiation semantics for arg re-parses is also
the established Max/Pd behavior (editing creation args rebuilds the
object; Max preserves connections, which this does too).

## Why not the alternatives

- **A — deferred apply for everything.** `Parameters::Set` parses,
  allocates, and runs pin-mutating callbacks; none of that is RT-safe,
  and pre-parsing cannot help a `PARM_PARSE` that must reallocate pin
  vectors the audio thread is indexing. Deferred apply is kept only for
  the pre-parsed scalar subset, where the audio-thread work is a handful
  of stores.
- **B — replacement for everything.** Correct but needlessly loses DSP
  state (phase resets, filter zaps — audible clicks) for the common case
  of tweaking a numeric param, and the scalar set is exactly where the
  epic's preserve-state decision has teeth. Kept only where there is no
  DSP state to lose.
- **Quiesce-and-mutate in place** (publish a snapshot without the
  object, wait out the epoch grace, mutate, republish): preserves
  control state, but blocks the control thread on the audio epoch —
  unbounded when the engine is paused (the epoch stalls) — and silences
  the object for the wait. Rejected on the pause-deadlock alone.

## Behavioral notes

- Scalar params take effect at the top of the **next rendered block**;
  `GetParams`/`DumpJSON` reflect the new string immediately. Structural
  re-parses are visible immediately (`GetOutputs`/`GetInputs` query the
  fresh object through the same handle).
- Transient *control* state of structural objects (`gGate`
  `activeOutlet`, `gSwitch` `activeInlet`) resets on re-parse —
  re-instantiation semantics, as in Max/Pd.
- A replaced `gReceive` stays bus-subscribed until the old instance is
  reclaimed (its destructor unsubscribes), so a bus publish inside the
  ~2-block grace can transiently reach both instances — the same
  transient `DeleteObject` has today; not widened by this change.
- If the value queue is full the msg is dropped **with a log**, like the
  #225 value path; `GetParams` will already report the new string. The
  queue is drained every block, so sustained loss requires a stalled
  audio thread.

## Scope

**In scope ([#234][gh-234]):** the routing through
`SetObjectParams`, `Parameters::NeedsRebuild`/plan building, the
`ParamMsg` queue + `Calculate` drain, `ReplaceObjectUnlocked`, unit
tests for both paths, and a focused render-vs-`SetParams` concurrency
test (the seed for the [#229][gh-229] harness extension).

**Out of scope:** deferring `pHandle::SetBang/SetIntData/...` value
pokes (they still run inlet handlers on the calling thread — a remaining
epic gap tracked by [#189][gh-189]/[#229][gh-229], same regime as
`PassData` and orthogonal to params); explicit early unsubscribe of
replaced/deleted `gReceive` instances.

<!-- link references -->
[gh-189]: https://github.com/yvanvds/yse-soundengine/issues/189
[gh-225]: https://github.com/yvanvds/yse-soundengine/issues/225
[gh-226]: https://github.com/yvanvds/yse-soundengine/issues/226
[gh-227]: https://github.com/yvanvds/yse-soundengine/issues/227
[gh-229]: https://github.com/yvanvds/yse-soundengine/issues/229
[gh-234]: https://github.com/yvanvds/yse-soundengine/issues/234
[gh-237]: https://github.com/yvanvds/yse-soundengine/issues/237
[src-parameters]: ../../YseEngine/patcher/parameters.cpp
[src-ggate]: ../../YseEngine/patcher/genericObjects/gGate.cpp
