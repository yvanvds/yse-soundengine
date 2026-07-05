# Synth core architecture specification

Status: **design, pre-implementation**. Tracking issue: [#151][gh-151].
Parent epic: [#145 — Synth voice architecture: engine-native rewrite of
YSE::synth][gh-145].

This document is the single source of truth for the rewritten
`YSE::synth` subsystem. It is written *before* any implementation code so
the sub-issues of [#145][gh-145] — [#152][gh-152] (voice base),
[#153][gh-153] (object/manager/allocator), [#154][gh-154] (keyboard
state), [#155][gh-155] (MIDI routing), [#156][gh-156] (player
reconnection), [#157][gh-157] (C API) — can each treat it as a fixed
contract and focus on plumbing without re-litigating the shape.

The old JUCE-era `synth/` directory ([#150][gh-150], merged) is gone. It
survives here only as an *API-shape* reference: the chainable interface,
the `dspVoice` `clone()` prototype pattern, the intent lifecycle, and the
`onNoteEvent` audio-thread callback. Everything about the *internals*
(voice storage, allocation, threading, lifecycle) is specified against
the **current** engine — the `DSP::dspSourceObject` generator base, the
`OBJECT_*` lifecycle, `managerSetupJob`/`managerDeleteJob`, and the
`lfQueue` SPSC inbox — not the deleted `juce::Synthesiser` code.

This follows the design-issue-first pattern proven by
[docs/design/live_coding_dsl.md][doc-dsl] ([#120][gh-120]).

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [Object model](#object-model)
3. [The voice model](#the-voice-model)
4. [Voice allocator and stealing policy](#voice-allocator-and-stealing-policy)
5. [Keyboard and pedal state machine](#keyboard-and-pedal-state-machine)
6. [Message op-set](#message-op-set)
7. [The `onNoteEvent` callback contract](#the-onnoteevent-callback-contract)
8. [Manager and lifecycle](#manager-and-lifecycle)
9. [Attachment to a `YSE::sound`](#attachment-to-a-yse-sound)
10. [Threading model](#threading-model)
11. [Real-time budget](#real-time-budget)
12. [Public API surface](#public-api-surface)
13. [Worked examples](#worked-examples)
14. [Cross-references](#cross-references)

---

## Goals and non-goals

### Goals

- **One synth = one `dspSourceObject` = one `YSE::sound` = one 3D
  position.** The whole voice pool renders into a single aggregate
  generator attached behind one sound. Per-note positioning is
  deliberately *out* of this epic — it forks the spatialization design
  and is settled separately in [#168][gh-168]. Keeping the baseline at
  one position lets Epic A ship without waiting on that fork.
- **User-subclassable voices.** A `dspVoice` is a `DSP::dspSourceObject`
  the user derives from. The engine owns polyphony, allocation,
  stealing, keyboard state and lifecycle; the user owns only *what one
  voice sounds like*.
- **Real-time discipline is structural, not advisory.** Every voice
  instance and every buffer it needs is allocated **once**, off the
  audio thread, at `addVoices()` time (via the `clone()` prototype
  pattern on the slow pool). The audio callback allocates nothing, locks
  nothing, blocks on nothing. Control flows in through a lock-free SPSC
  `lfQueue` inbox and is drained on the audio thread.
- **Unblock the music layer.** `YSE::player` is non-instantiable today —
  its only `create` path took a `synth&` and is commented out. A working
  synth is the dependency that makes the player, scales, and motifs
  playable again ([#156][gh-156]).
- **Deterministic, click-free stealing.** When polyphony is exhausted,
  the stealing policy is fixed (steal oldest-in-release, else oldest) and
  the tail of a stolen note is force-faded by the engine, independent of
  the voice's own envelope, so stealing never clicks.

### Non-goals (explicit)

These are out of scope for the synth *core* and will not be added under
these names without a new design pass:

- **No instrument models.** The sampler, virtual-analog/wavetable, and
  FM/DX7 voices are the Instruments epic ([#148][gh-148]); their voices
  are `dspVoice` subclasses that consume *this* contract. The core ships
  with exactly one reference voice — a sine + ADSR ([#152][gh-152]) — used
  by every downstream test.
- **No per-note 3D positioning / position handlers.** Deferred to
  [#168][gh-168] and its epic. The core exposes *one* position, inherited
  from the owning `YSE::sound`.
- **No per-voice insert effect chains.** A voice renders into the
  aggregate; channel/insert effects are the Effects epic
  ([#146][gh-146]).
- **No MPE.** Per-note pitch/pressure channels are not modeled. The
  keyboard state machine is standard MIDI 1.0 (16 channels, per-channel
  pedals and wheel).
- **No user-defined voices from C.** The C surface ([#157][gh-157])
  exposes the *built-in* voice types only; the `dspSourceObject` callback
  plumbing needed to subclass a voice from C is a known gap and stays
  deferred.
- **No dynamic per-voice reallocation.** Polyphony is fixed at
  `addVoices()` time. Growing or shrinking a voice group at runtime means
  tearing the synth down and rebuilding it. There is no lock-free path to
  resize the pool while the audio thread reads it.

---

## Object model

The synth is a standard YSE subsystem and reuses the canonical four-part
split documented in the engine (see the comment in the pre-existing
`sound.hpp` forward-declaration hub: *"Every subSystem consists out of
several classes … an interface, implementation, manager, message and a
message enumeration"*). The synth mirrors the `sound` subsystem's layout
one-for-one:

| Role | New file(s) | Mirrors (existing) |
|------|-------------|--------------------|
| Forward-decl hub + `MESSAGE` enum + `typedef synth` | `synth/synth.hpp` | `sound/sound.hpp` |
| Interface (`YSE::synth`) | `synth/synthInterface.hpp/.cpp` | `sound/soundInterface.hpp/.cpp` |
| Implementation (audio-thread object) | `synth/synthImplementation.h/.cpp` | `sound/soundImplementation.h/.cpp` |
| Message (`messageObject`) | `synth/synthMessage.h` | `sound/soundMessage.h` |
| Manager singleton | `synth/synthManager.h/.cpp` | `sound/soundManager.h/.cpp` |
| Voice base class | `synth/dspVoice.hpp/.cpp` | *(new; derives `DSP::dspSourceObject`)* |

```cpp
namespace YSE {
  namespace SYNTH {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;
    class dspVoice;

    enum MESSAGE {
      NOTE_ON, NOTE_OFF, ALL_NOTES_OFF,
      PITCH_WHEEL, CONTROLLER, AFTERTOUCH,
      SUSTAIN, SOSTENUTO, SOFTPEDAL,
    };
  }
  // users just write `YSE::synth` to get an interface object
  typedef SYNTH::interfaceObject synth;
}
```

Note there is **no `CALLBACK` message**. The old enum carried a `CALLBACK`
op because the interface installed `onNoteEvent` via the message queue.
In the new design `onNoteEvent` is a single atomic function pointer set
directly on the impl (see [§7](#the-onnoteevent-callback-contract)); it
never crosses the note inbox, so it needs no op-code.

The public `YSE::synth` (a.k.a. `SYNTH::interfaceObject`) is a thin,
chainable pimpl. Like `YSE::sound`, it is **non-copyable** — the impl
holds the interface address, so the address must be stable.

The audio-thread object is `SYNTH::implementationObject`. It owns:

- the **voice groups** (each an `addVoices()` call: a fixed array of
  cloned `dspVoice*`, plus the group's channel filter and note range),
- the **keyboard state** (per-channel held-note bookkeeping, pedal flags,
  pitch-wheel and controller values),
- the per-impl **`lfQueue<messageObject>` inbox**,
- the **aggregate output source** — an internal `DSP::dspSourceObject`
  (`outputSource` below) that sums the voices and is what the owning
  `YSE::sound` renders,
- the `OBJECT_*` lifecycle state and the `onNoteEvent` pointer.

### The aggregate output source

The engine's `YSE::sound` renders a single `DSP::dspSourceObject` (base
declared in `dsp/dspObject.hpp`: `std::vector<buffer> samples;` and
`virtual void process(SOUND_STATUS& intent) = 0`). A synth has many
voices, so the impl owns one internal aggregator:

```cpp
// inside SYNTH::implementationObject
class outputSource : public DSP::dspSourceObject {
public:
  outputSource(implementationObject& owner, int channels)
    : dspSourceObject(channels), synth(owner) {}
  void process(SOUND_STATUS& masterIntent) override;  // drain + allocate + mix
  void frequency(Flt) override {}                      // no-op: notes carry pitch
private:
  implementationObject& synth;
};
```

`outputSource::process()` is the synth's per-block heartbeat and runs on
the audio thread. Each block, in order:

1. **Drain the inbox** — `while (messages.try_pop(m)) parseMessage(m);`.
   This applies the keyboard state machine and runs the allocator, so all
   note events for this block take effect before any audio is produced.
   (This mirrors how `sound`'s `sync()` drains its per-impl message queue
   at the top of the audio path.)
2. **Clear** `samples` (one `buffer` per output channel).
3. **Render active voices** — for each active voice, drive it with its
   own per-voice `SOUND_STATUS` and mix its output into `samples`
   (see [§4](#voice-allocator-and-stealing-policy) for the mixing and
   declick detail).
4. Honour `masterIntent` (the sound-level `SS_WANTSTO*` play/stop/pause)
   as a global gate over the sum — a sound-level stop silences and frees
   the whole pool.

The impl keeps the inbox drain **inside** `outputSource::process()`
rather than in the manager tick so that control and rendering are
consistent within a single block regardless of manager/graph ordering.
The manager tick ([§8](#manager-and-lifecycle)) is reserved for
lifecycle only (setup, promote, delete).

---

## The voice model

A voice is a `DSP::dspSourceObject` subclass. The base already provides
the two things the sound renderer needs — a `std::vector<buffer> samples`
output and a `process(SOUND_STATUS&)` entry point — plus a pure-virtual
`frequency(Flt)`. `SYNTH::dspVoice` extends that base with the pieces the
allocator needs: a `clone()` prototype hook, a velocity channel, and a
default atomic implementation of `frequency()`.

```cpp
namespace YSE { namespace SYNTH {

class API dspVoice : public DSP::dspSourceObject {
public:
  dspVoice(int outputChannels = 1) : dspSourceObject(outputChannels) {}
  virtual ~dspVoice() {}

  // ---- user MUST implement -------------------------------------------
  //
  // Fill `samples` for one block. `intent` is this voice's own
  // SOUND_STATUS (SS_WANTSTOPLAY on note start, SS_WANTSTOSTOP on note
  // release, ...). Honour it to drive your amplitude envelope. Must be
  // allocation-free, lock-free, non-blocking. Runs on the audio thread.
  void process(SOUND_STATUS& intent) override = 0;

  // Return a NEW heap instance of your derived type, fully constructed
  // and with all buffers allocated, so its process() is allocation-free.
  // Called only on the setup thread (never the audio thread) — see §8.
  //   dspVoice* clone() override { return new MyVoice(*this); }
  virtual dspVoice* clone() = 0;

  // ---- delivered by the allocator (audio thread) ---------------------
  // Frequency arrives as a MIDI note number and is stored as Hz.
  void frequency(Flt midiNote) override {
    _frequency.store(DSP::MidiToFreq(midiNote), std::memory_order_relaxed);
  }
  Flt  getFrequency() const { return _frequency.load(std::memory_order_relaxed); }
  void velocity(Flt v)      { _velocity.store(v, std::memory_order_relaxed); }
  Flt  getVelocity() const  { return _velocity.load(std::memory_order_relaxed); }
  Flt  getAftertouch() const{ return _aftertouch.load(std::memory_order_relaxed); }
  // Channel pitch-wheel position in [-1,1], forwarded by the keyboard state
  // machine (§5). A voice reads this alongside getFrequency() and chooses its
  // own bend range; the core only delivers the normalised position.
  Flt  getPitchWheel() const{ return _pitchWheel.load(std::memory_order_relaxed); }

private:
  aFlt _frequency{440.f};
  aFlt _velocity{0.f};
  aFlt _aftertouch{0.f};
  aFlt _pitchWheel{0.f};
  friend class implementationObject;   // sets _aftertouch / _pitchWheel, drives intent
};

}} // namespace YSE::SYNTH
```

The reference sine voice from [#152][gh-152] is the minimal legal
implementation: one `DSP::sine` (or `oscillator`) shaped by one
`DSP::ADSRenvelope`, keyed off `intent`, pitched from `getFrequency()`,
scaled by `getVelocity()`, allocating everything in its constructor so
`clone()` (a copy-construct) is allocation-clean.

### `clone()` — the prototype contract

`addVoices(prototype, n, …)` does **not** store the prototype. It clones
it `n` times. The prototype pattern is the whole reason voices can be
user types the engine has never seen: the engine calls a virtual, the
user's `new` does the work.

Contract:

1. `clone()` returns a brand-new, independently-owned `dspVoice*` of the
   derived type. The engine takes ownership and frees it in the manager
   delete job ([§8](#manager-and-lifecycle)).
2. The returned voice must be **fully allocated** — every buffer, table,
   filter state it will ever touch in `process()` exists before it
   returns. `process()` is thereafter allocation-free.
3. `clone()` runs **only on the setup thread** (the single-threaded slow
   pool), never on the audio thread. Allocation there is fine.
4. The prototype passed to `addVoices()` must stay alive until the
   `addVoices` setup completes (the clone reads it). The engine does not
   copy or own the prototype; after setup it is never touched again and
   the caller may destroy it. This matches the ownership caveat on
   `sound::create(dspSourceObject&, …)`.

### Per-voice state delivery

All note data reaches a voice through atomics written by the allocator
(which runs on the audio thread, in `parseMessage`) and read by the voice
in `process()`:

| Datum | Set by | Read by | Mechanism |
|-------|--------|---------|-----------|
| frequency (Hz) | allocator on NOTE_ON | voice `process()` | `aFlt _frequency` via `frequency(midiNote)` |
| velocity [0,1] | allocator on NOTE_ON | voice `process()` | `aFlt _velocity` |
| aftertouch [0,1] | allocator on AFTERTOUCH | voice `process()` | `aFlt _aftertouch` |
| pitch wheel [-1,1] | keyboard on PITCH_WHEEL + primed on NOTE_ON | voice `process()` | `aFlt _pitchWheel` |
| gate / phase | allocator | voice `process()` | the `SOUND_STATUS& intent` argument |

The `intent` argument is the gate. The allocator holds one
`SOUND_STATUS` per voice slot and passes it by reference into
`voice->process(intent)`:

- NOTE_ON → `SS_WANTSTOPLAY` (voice does attack; on the next block the
  voice itself may settle it to `SS_PLAYING`).
- NOTE_OFF → `SS_WANTSTOSTOP` (voice does release).
- when the voice's envelope reaches the end of release, the voice reports
  `SS_STOPPED` (the standard `dspSourceObject` intent settle), and the
  allocator returns the slot to the free pool.

Because the atomics are written and read on the same thread (both audio
thread — the inbox is drained in `outputSource::process()`), the atomicity
is a *belt-and-suspenders* guarantee that also lets a user's
`onNoteEvent` or a voice safely read them without a data race with future
cross-thread extensions. `std::memory_order_relaxed` is sufficient today.

---

## Voice allocator and stealing policy

Voices are organised into **groups**. Each `addVoices(prototype, n,
channel, low, high)` call creates one group: `n` identical voices that
respond to note numbers in `[low, high]` on MIDI `channel` (`channel == 0`
means omni). A synth may have several groups (e.g. a bass layer on the
low keys, a lead on the high keys, or two timbres stacked on the same
range). Allocation is always *within* the group whose filter matches the
incoming note.

### Allocation on NOTE_ON

For a NOTE_ON `(channel, note, velocity)`:

1. Select every group whose channel filter and note range match. (A note
   may sound in more than one matching group — layering is intentional;
   each matching group allocates independently.)
2. Within each matching group, find a slot:
   - **Free slot** — a voice currently `SS_STOPPED`. Take it.
   - **No free slot → steal.** Apply the stealing policy below.
3. On the chosen slot: write `frequency(note)`, `velocity(v)`, record the
   `(channel, note)` identity and a monotonically increasing **age
   stamp**, and set the slot's intent to `SS_WANTSTOPLAY`.

### Stealing policy (fixed)

When a matching group has no free voice, steal in this priority order —
this is the policy locked by [#145][gh-145] and is not configurable in
the core:

1. **Oldest voice currently in release** (`SS_WANTSTOSTOP` /
   post-note-off, envelope tailing out). These are already dying; stealing
   one is least audible. "Oldest" = smallest age stamp.
2. **Else the oldest voice overall** (smallest age stamp among the
   still-held voices).

### Click-free stealing

A stolen voice must not click, and the engine cannot assume anything
about the user voice's release length (it could be seconds). So the
declick is **engine-owned**, applied in the aggregate mix, not delegated
to the voice:

1. When a slot is chosen for stealing, the allocator marks it
   `STEALING` with a countdown of `STEAL_FADE` samples (**≈ 5 ms** at the
   device sample rate — a small, fixed compile-time constant).
2. While a slot is `STEALING`, `outputSource::process()` keeps calling the
   *old* note's `process()` but multiplies its output by a linear ramp
   descending from 1 to 0 across `STEAL_FADE` samples as it mixes it in.
3. When the ramp reaches 0, the slot is reset (the voice's note is
   cleared), then immediately re-armed with the **new** note's frequency,
   velocity and a fresh `SS_WANTSTOPLAY`.

Consequence, stated plainly: a *stolen* note begins up to `STEAL_FADE`
(≈ 5 ms) later than a note that lands on a free voice. This latency
applies only under polyphony exhaustion and is inaudible in practice; it
is the price of a guaranteed click-free handoff that works for any user
voice. Notes that find a free slot start with zero added latency.

### ALL_NOTES_OFF and sound-level stop

`allNotesOff(channel)` releases every held note on that channel (all
channels if `channel == 0`) — it is a bulk NOTE_OFF, so voices enter
their normal release, not an instant cut. A *sound-level* stop
(`masterIntent == SS_WANTSTOSTOP`, e.g. the owning `YSE::sound` is
stopped) is the hard cut: the aggregate applies its own short master
declink fade over the sum and frees the whole pool.

---

## Keyboard and pedal state machine

The impl keeps per-channel keyboard state (16 MIDI channels). Per
channel it tracks:

- **Held notes** — the set of note numbers physically down (between their
  NOTE_ON and NOTE_OFF), each mapped to the voice slot(s) sounding it.
- **Sustain (CC 64) down/up.**
- **Sostenuto (CC 66) down/up**, plus the **sostenuto capture set**.
- **Soft pedal (CC 67) down/up.**
- Current **pitch-wheel** value and the last value of each **controller**
  and **channel/poly aftertouch**.

### NOTE_ON

1. Run `onNoteEvent(true, &note, &velocity)` if installed
   ([§7](#the-onnoteevent-callback-contract)) — it may rewrite note and
   velocity in place.
2. If **soft pedal** is down on this channel, scale velocity by
   `SOFT_PEDAL_GAIN` (**0.7**, fixed). Soft pedal affects only notes that
   start while it is held; it does not retroactively change sounding
   voices.
3. Add the note to the held set and allocate voice(s)
   ([§4](#voice-allocator-and-stealing-policy)).
4. Re-emit the current pitch-wheel value to the new voice so it starts
   in tune with the channel's current bend.

### NOTE_OFF

1. Run `onNoteEvent(false, &note, &velocity)` if installed.
2. Remove the note from the held set.
3. Decide whether the note actually releases now:
   - **Sustain down** → the note is *not* released. It moves to a
     "sustained-released" set and keeps sounding. It will be released when
     sustain goes up.
   - **Sostenuto down AND the note is in the sostenuto capture set** →
     same deferral: it is held until sostenuto goes up.
   - **Otherwise** → release now: set the sounding voice(s) to
     `SS_WANTSTOSTOP`.

A note held by *both* pedals is released only when *both* have let go of
their claim on it (sustain up **and** it is not — or no longer — held by
sostenuto).

### Pedals

- **Sustain (CC 64).** Down: subsequent NOTE_OFFs on this channel defer
  (see above). Up: every note in the sustained-released set is released
  now (voices → `SS_WANTSTOSTOP`), unless still claimed by sostenuto.
- **Sostenuto (CC 66).** Down: snapshot the currently-held notes into the
  sostenuto capture set — *only these* notes are sustained by the
  sostenuto pedal. Notes played *after* the pedal goes down are
  unaffected by it. Up: release any captured note whose key is no longer
  held (unless sustain still holds it), and clear the capture set.
- **Soft pedal (CC 67).** Down: scale the velocity of *future* NOTE_ONs
  by `SOFT_PEDAL_GAIN`. Up: future NOTE_ONs are full-velocity again.

### Pitch wheel, controllers, aftertouch

- **Pitch wheel** — one value per channel, in `[-1, 1]` (interface
  normalizes raw MIDI 0..16383 to this). Applied to all voices on the
  channel. The core delivers the channel value to voices; how a voice
  bends (bend range in semitones) is the voice's concern, read alongside
  `getFrequency()`. A default bend range of ±2 semitones is recommended
  for built-in voices but is not enforced by the core.
- **Controllers** — `controller(channel, number, value)` with `value` in
  `[0, 1]`. CC 64/66/67 are intercepted by the pedal logic above; all
  other CC numbers are stored per channel and made visible to voices as
  the "last controller value" (voices opt in to whichever CCs they care
  about). The core does not itself map CCs to synthesis parameters.
- **Aftertouch** — `aftertouch(channel, note, value)`, `value` in
  `[0, 1]`, delivered to the voice(s) sounding `note` on `channel` via
  the per-voice `_aftertouch` atomic. Channel-wide aftertouch is
  expressed as `note == -1` (broadcast to all voices on the channel).

---

## Message op-set

Everything the interface thread wants to change on the audio-thread impl
crosses the per-impl `lfQueue<messageObject>` inbox
(`utils/lfQueue.hpp`), exactly as `sound` and `channel` do. The interface
`push`es; the impl `try_pop`s in `outputSource::process()` on the audio
thread. The queue is pre-sized; the interface uses the non-allocating
`try_push` and drops (with a logged warning) on overflow rather than
allocating, so a flood of MIDI can never allocate on any thread.

`addVoices` is **not** an inbox op — it allocates (clones voices) and
therefore goes through the manager setup path
([§8](#manager-and-lifecycle)), off the audio thread. The inbox carries
only bounded, allocation-free control events.

The message is a tagged union, following `sound/soundMessage.h`:

```cpp
namespace YSE { namespace SYNTH {
class messageObject {
public:
  MESSAGE ID;
  union {
    struct { Int channel; Int note;    Flt velocity; } noteOn;   // NOTE_ON
    struct { Int channel; Int note;    Flt velocity; } noteOff;  // NOTE_OFF
    struct { Int channel; }                            allOff;   // ALL_NOTES_OFF (0 = all)
    struct { Int channel; Flt value; }                 wheel;    // PITCH_WHEEL  [-1,1]
    struct { Int channel; Int number; Flt value; }     cc;       // CONTROLLER   [0,1]
    struct { Int channel; Int note;    Flt value; }    touch;    // AFTERTOUCH   (note -1 = channel)
    struct { Int channel; Bool down; }                 pedal;    // SUSTAIN / SOSTENUTO / SOFTPEDAL
  };
};
}}
```

| `MESSAGE` | Payload | Effect on the impl |
|-----------|---------|--------------------|
| `NOTE_ON` | channel, note, velocity | keyboard NOTE_ON → allocate/steal |
| `NOTE_OFF` | channel, note, velocity | keyboard NOTE_OFF → release/defer |
| `ALL_NOTES_OFF` | channel (0 = all) | bulk release on channel(s) |
| `PITCH_WHEEL` | channel, value [-1,1] | store + forward to channel's voices |
| `CONTROLLER` | channel, number, value [0,1] | pedals (64/66/67) or store CC |
| `AFTERTOUCH` | channel, note, value [0,1] | per-voice (or channel-wide) pressure |
| `SUSTAIN` | channel, down | sustain pedal transition |
| `SOSTENUTO` | channel, down | sostenuto pedal transition + capture |
| `SOFTPEDAL` | channel, down | soft-pedal velocity scaling flag |

The old `synthMessage.h` used a loosely-typed union (`U16 uIntValue[3];
Flt vecValue[3]; …`) that carried a latent field-aliasing bug. The new
message uses **named per-op structs** in the union so each op reads and
writes exactly the fields it means — no positional aliasing.

---

## The `onNoteEvent` callback contract

`onNoteEvent` lets user code rewrite a note *in flight*, on the audio
thread, before the allocator sees it — the classic use is retuning,
transposition, velocity curves, or note filtering.

```cpp
synth.onNoteEvent(
  [](bool noteOn, float* noteNumber, float* velocity) {
    // audio thread. rewrite *noteNumber / *velocity in place.
  });
```

Signature (unchanged from the old API shape):

```cpp
void (*)(bool noteOn, float* noteNumber, float* velocity);
```

Contract:

- **Thread.** Runs on the audio thread, inside `parseMessage` for
  `NOTE_ON` / `NOTE_OFF`, *before* keyboard bookkeeping and allocation.
- **Mutability.** May overwrite `*noteNumber` and `*velocity`. The
  rewritten values are what the keyboard state and allocator use — the
  held-note set is keyed on the *rewritten* note, so a NOTE_OFF must
  rewrite to the same note a matching NOTE_ON produced, or the release
  will not find its voice. (This is a user responsibility; the core does
  not track the pre-rewrite identity.)
- **Bounds.** User code must keep `*noteNumber` in `[0, 127]` and
  `*velocity` in `[0, 1]`. The core does not clamp; out-of-range values
  produce out-of-range frequencies / gains.
- **RT rules.** No allocation, no locks, no blocking I/O — same rules as
  a voice `process()`. Any state the callback touches that is also
  written from another thread must be atomic.
- **Installation.** Set via an atomic function pointer on the impl (a
  plain `std::atomic<fn>` store from the interface thread, acquire-load on
  the audio thread). It does **not** go through the message inbox — there
  is no `CALLBACK` op. Passing `nullptr` clears it. A cleared callback is
  simply not invoked (the note passes through unmodified).

Only a free function or captureless lambda (convertible to the function
pointer) is accepted — deliberately, so the callback carries no
heap-allocated closure the audio thread would have to reason about.

---

## Manager and lifecycle

`SYNTH::managerObject` is a singleton that mirrors `soundManager`, using
the modern job-based idiom from `internal/managerJobs.hpp` and the
`OBJECT_*` state machine (`headers/enums.hpp`), **not** the old
hand-rolled `forward_list` walk.

### The `OBJECT_*` lifecycle

The impl runs the standard object lifecycle:

```
OBJECT_CONSTRUCTED → OBJECT_CREATED → OBJECT_SETTING_UP → OBJECT_SETUP
                   → OBJECT_READY → OBJECT_RELEASE
                   → OBJECT_DELETE_PENDING → OBJECT_DELETE
```

with the documented use-after-free fence around `OBJECT_DELETE_PENDING`:
the audio thread must remove the impl from any audio-visible list before
the slow pool frees it. The synth's audio-visible reference is its
aggregate `outputSource` held by the owning `YSE::sound`; disconnection
follows the same handshake `sound` uses for its own source.

### Manager wiring

The manager opts into the setup and delete job templates exactly as the
sound manager does:

```cpp
class managerObject {
  // required by managerJobs.hpp templates:
  using ImplementationType = implementationObject;
  friend class INTERNAL::managerSetupJob<managerObject>;
  friend class INTERNAL::managerDeleteJob<managerObject>;

  INTERNAL::managerSetupJob<managerObject>  mgrSetup;
  INTERNAL::managerDeleteJob<managerObject> mgrDelete;

  std::forward_list<implementationObject*>  inUse;
  lfQueue<implementationObject*>            toLoadInbox;    // main → audio
  std::forward_list<implementationObject*>  toLoad;         // audio-owned
  std::forward_list<implementationObject>   implementations;// canonical owner
  std::mutex implementationsMutex;
  aBool runDelete;
};
```

The `update()` tick (called on the audio thread, once per callback) runs
the canonical loop: drain `toLoadInbox` → schedule `mgrSetup` on the slow
pool for impls that `tryClaimForSetup()` (a
`compare_exchange OBJECT_CREATED → OBJECT_SETTING_UP`) → promote
`OBJECT_SETUP` impls to `OBJECT_READY` → schedule `mgrDelete` when
`runDelete` is set → drive `OBJECT_RELEASE → OBJECT_DELETE` with the
disconnect fence.

### Setup — where voices are cloned

`managerSetupJob` runs on the **single-threaded slow pool**, which is
where every allocation happens. This is where the synth clones voice
prototypes:

- `synth.create()` registers the impl (`OBJECT_CONSTRUCTED →
  OBJECT_CREATED`) and pushes it to `toLoadInbox`.
- `addVoices(prototype, n, channel, low, high)` records a *pending group
  request* (a small, allocation-free descriptor: a pointer to the
  prototype, `n`, and the filter) and re-arms setup. `impl->setup()`
  (run by `mgrSetup` on the slow pool) walks the pending group requests
  and, for each, calls `prototype.clone()` `n` times, allocating the
  group's voice array. Only after all pending groups are built does the
  impl reach `OBJECT_SETUP` and get promoted to `OBJECT_READY`.
- Notes that arrive before the group they target is `READY` are dropped
  (logged at debug level). The expected call order is `create()` →
  `addVoices()` (possibly several) → play. Because setup is async on the
  slow pool, a synth is not instantly playable after `addVoices()`; it
  becomes playable when it reaches `OBJECT_READY`, consistent with how a
  file-backed `sound` is not playable until its buffer finishes loading.

### Delete — where voices are freed

When the interface object is destroyed (or the synth is otherwise
released), the impl transitions toward `OBJECT_DELETE`. The
`managerDeleteJob` (`implementations.remove_if(Impl::canBeDeleted)`) frees
the impl on the slow pool once the `OBJECT_DELETE_PENDING` fence
guarantees the audio thread no longer references its `outputSource`. The
impl's destructor `delete`s every cloned voice in every group. User
prototypes are never freed by the engine (the engine never owned them).

---

## Attachment to a `YSE::sound`

A synth produces sound only once its aggregate is attached behind a
`YSE::sound`, which supplies the single 3D position, the channel routing,
and the master play/stop intent. The engine already has the exact seam:
`sound::create(DSP::dspSourceObject&, channel*, volume)` — it stores the
source pointer atomically (`source_dsp.store(&ptr, release)`), points the
sound's render buffer at the source's `samples`, and sets `playerType =
PT_DSP`. On the audio thread the sound loads the source with acquire
ordering and calls `src->process(status_dsp)`.

The synth adds one thin overload:

```cpp
// YSE::sound
sound& create(YSE::synth& synth, channel* ch = nullptr, float volume = 1.f);
```

It:

1. ensures the synth's impl exists and is registered
   (`synth.create()` if the user has not called it),
2. delegates to the existing `dspSourceObject` create path, passing the
   synth impl's aggregate `outputSource` as the source,
3. records the back-link `synth ↔ sound` so the synth knows its single
   position/channel context and so lifetimes are coordinated.

Lifetime coupling (documented, caller-owned — same shape as the raw
`dspSourceObject` overload): the `YSE::synth` must outlive the `YSE::sound`
that renders it, i.e. until after the sound's destructor **and** the
slow-pool delete tick that actually frees the sound impl. The synth's own
`OBJECT_DELETE_PENDING` fence prevents the synth impl from being freed
while the sound's audio path still holds its `outputSource`.

No new audio-thread code path is opened: the synth reuses the sound's
existing `PT_DSP` render path. All synth-specific work (drain, allocate,
mix) happens inside `outputSource::process()`, which the sound calls
through the pointer it already knows how to call.

---

## Threading model

Three thread contexts matter, matching the rest of the engine:

1. **Interface thread(s).** Whoever calls `synth.noteOn(...)`,
   `pitchWheel(...)`, etc. — the app/main thread, the MIDI-in dispatch
   ([#155][gh-155]), or the player ([#156][gh-156]). These calls only
   `try_push` a `messageObject` onto the inbox (bounded, allocation-free)
   and return. They never touch voices, keyboard state, or buffers.
2. **Slow pool (single-threaded setup/delete).** Runs `mgrSetup` /
   `mgrDelete`. The **only** place voices are allocated (`clone()`) and
   freed. Serialised with file loads and other subsystems' setup, so
   voice construction cannot race another setup job.
3. **Audio thread.** Drains the inbox, runs the keyboard state machine
   and allocator, calls every voice's `process()`, mixes, and applies
   stealing declick — all inside `outputSource::process()` — plus the
   manager `update()` lifecycle tick. Allocates nothing, locks nothing.

Cross-thread data:

- **Control** flows interface → audio through the per-impl
  `lfQueue<messageObject>` (SPSC, one producer per impl in the common
  case; MIDI/player are the producers). Drops on overflow rather than
  allocates.
- **New impls / setup requests** flow interface → audio → slow pool
  through `toLoadInbox` and the `OBJECT_*` claim handshake.
- **Note data** (frequency/velocity/aftertouch) is written and read on
  the audio thread but stored in `aFlt` atomics so a voice or an
  `onNoteEvent` callback reads consistent values, and so future
  cross-thread readers (metering, C-API introspection) are race-free by
  construction.
- **`onNoteEvent`** pointer is an `std::atomic<fn>`: release-store from
  the interface thread, acquire-load on the audio thread.

There is no lock on any audio-thread path. The only mutex
(`implementationsMutex`) is taken by the manager setup/delete jobs and
the registration path — never by the audio callback.

---

## Real-time budget

The per-block cost of a synth is:

```
cost ≈ Σ(active voices) voice.process()  +  numActiveVoices × bufferLen   (the mix add)
     +  inbox drain (bounded by messages this block)
```

for `bufferLen == STANDARD_BUFFERSIZE` samples per output channel. This
must fit inside the audio callback alongside every other sound and
channel. Design consequences:

- **Allocation is banned on the audio thread, structurally.** Voices and
  their buffers are built in `clone()` on the slow pool. `process()` may
  resize a scratch buffer only on a *detected* length change (the
  `basicDelay` idiom: `if (buf.getLength() != cached) resize(...)`), never
  unconditionally — and even that should be avoided in voices by sizing
  to `STANDARD_BUFFERSIZE` in `clone()`.
- **Cost scales with polyphony.** `addVoices(n)` fixes the worst-case
  cost at `n × per-voice cost` per group. A voice's `process()` is the
  user's RT budget to spend; the core adds only the O(voices) mix and a
  bounded inbox drain. Idle (`SS_STOPPED`) voices are skipped, so cost
  tracks *active* voices, not allocated ones.
- **Guidance for voice authors** (non-binding, enforced by review not
  code): read `aFlt` params once per block where possible; smooth
  parameter changes to avoid zipper noise; guard against denormals in
  feedback paths; do not call anything that can allocate, lock, log, or
  touch the filesystem.
- **Stealing is O(voices in group)** — a linear scan for the oldest /
  oldest-in-release slot. Groups are small (polyphony counts), so this is
  negligible and allocation-free.

The bench suite ([#181][gh-181]) will add voice-count scaling and
sine-voice cost entries so regressions in this budget are caught.

---

## Public API surface

The interface is chainable and non-copyable, mirroring `YSE::sound`.
Signatures modernised from the old header shape to current engine types
(`Flt`, `MUSIC::note`, normalized ranges):

```cpp
namespace YSE {

class API synth /* = SYNTH::interfaceObject */ {
public:
  synth();
  ~synth();
  synth(const synth&) = delete;              // impl holds our address

  synth& create();

  // Clone `prototype` numVoices times into a group responding to
  // [lowestNote, highestNote] on `channel` (0 = omni). May be called
  // multiple times to build layered / split groups. `prototype` must
  // outlive the resulting setup; the engine neither copies nor owns it.
  synth& addVoices(SYNTH::dspVoice& prototype, int numVoices,
                   int channel = 0, int lowestNote = 0, int highestNote = 127);

  synth& noteOn (int channel, int noteNumber, float velocity);
  synth& noteOff(int channel, int noteNumber, float velocity = 0.f);
  synth& noteOn (const MUSIC::note& note);   // uses note pitch/volume/channel
  synth& noteOff(const MUSIC::note& note);

  synth& pitchWheel(int channel, float value);            // [-1, 1]
  synth& controller(int channel, int number, float value);// [0, 1]
  synth& aftertouch(int channel, int noteNumber, float value); // [0,1]; note -1 = channel
  synth& sustain   (int channel, bool down);
  synth& sostenuto (int channel, bool down);
  synth& softPedal (int channel, bool down);

  synth& allNotesOff(int channel = 0);       // 0 = all channels

  synth& onNoteEvent(void(*func)(bool noteOn, float* noteNumber, float* velocity));

  int getNumVoices() const;                  // total across all groups

private:
  SYNTH::implementationObject* pimpl = nullptr;
  friend class SYNTH::implementationObject;
  friend class sound;                        // for create(synth&, ...)
  friend class MIDI::fileImpl;               // MIDI-file → synth (#155)
};

} // namespace YSE
```

Range conventions differ deliberately from raw MIDI: the interface takes
**normalized** values (`velocity`, `controller`, `aftertouch` in `[0,1]`;
`pitchWheel` in `[-1,1]`) and the MIDI layer ([#155][gh-155]) is
responsible for converting raw 7-bit / 14-bit MIDI into these. This keeps
the synth API device-agnostic and matches how `MUSIC::note` already
expresses velocity as `[0,1]`.

The C surface ([#157][gh-157]) mirrors this as `yse_synth_create`,
`yse_synth_add_voices_<builtin>` (built-in voice types only),
`yse_synth_note_on/off`, `yse_synth_pitch_wheel`, `yse_synth_controller`,
`yse_synth_aftertouch`, the three pedal setters, and
`yse_synth_all_notes_off`, following the opaque-handle + `set_last_error`
conventions of `c_api/yse_dsp_modules.cpp`. User-defined voices from C
stay deferred (the `dspSourceObject` callback plumbing gap).

---

## Worked examples

### Example 1 — a synth with the reference sine voice

```cpp
YSE::SYNTH::sineVoice proto;      // the reference voice from #152
proto.attack(0.01f).release(0.3f);

YSE::synth synth;
synth.create()
     .addVoices(proto, 16);       // 16-voice omni polyphony, full range

YSE::sound out;
out.create(synth, nullptr, 0.8f); // attach behind one sound at master

// ... once the synth reaches OBJECT_READY ...
synth.noteOn(1, 60, 0.9f);        // middle C, velocity 0.9, channel 1
// later:
synth.noteOff(1, 60);
```

### Example 2 — split keyboard (two groups)

```cpp
YSE::synth synth;
synth.create()
     .addVoices(bassProto, 4, /*channel*/1, /*low*/0,  /*high*/47)   // bass below C3
     .addVoices(leadProto, 8, /*channel*/1, /*low*/48, /*high*/127); // lead C3 and up
```

A NOTE_ON at note 40 allocates in the bass group only; note 72 in the
lead group only. Each group steals within itself.

### Example 3 — sustain pedal

```cpp
synth.sustain(1, true);   // pedal down
synth.noteOn(1, 60, 1.f);
synth.noteOff(1, 60);     // note keeps sounding — sustained
synth.noteOn(1, 64, 1.f);
synth.noteOff(1, 64);     // also sustained
synth.sustain(1, false);  // pedal up → 60 and 64 both release now
```

### Example 4 — retuning via `onNoteEvent`

```cpp
// Snap every incoming note to the nearest note in a whole-tone scale,
// on the audio thread, before allocation.
synth.onNoteEvent([](bool on, float* note, float* vel) {
  int n = (int)(*note + 0.5f);
  *note = (float)(n - (n % 2));   // fold to even note numbers
  // *vel unchanged
});
```

The rewritten note is what the keyboard state and allocator use; a
matching NOTE_OFF is rewritten identically, so it releases the right
voice.

### Example 5 — voice stealing

```cpp
YSE::synth synth;
synth.create().addVoices(proto, 2);   // only 2 voices
synth.noteOn(1, 60, 1.f);             // voice A
synth.noteOn(1, 64, 1.f);             // voice B
synth.noteOn(1, 67, 1.f);             // no free voice:
//   if A or B is in release → steal the oldest of those;
//   else steal A (oldest overall). A's tail is force-faded over ~5 ms,
//   then note 67 starts on that slot — no click.
```

---

## Cross-references

This document is the contract for Epic A ([#145][gh-145]). Each
implementing sub-issue cites it and, if implementation forces a change
here, must update this document in the same PR:

- [#150][gh-150] — INFRA: Remove the dead JUCE-era `synth/` subsystem
  (merged; the API-shape reference this doc draws on).
- [#152][gh-152] — DSP: `dspVoice` base class + reference sine voice
  (implements [§3](#the-voice-model)).
- [#153][gh-153] — ENGINE: synth object, manager, and voice allocator
  (implements [§2](#object-model), [§4](#voice-allocator-and-stealing-policy),
  [§8](#manager-and-lifecycle), [§9](#attachment-to-a-yse-sound)).
- [#154][gh-154] — ENGINE: keyboard state and controllers (implements
  [§5](#keyboard-and-pedal-state-machine), [§6](#message-op-set),
  [§7](#the-onnoteevent-callback-contract)).
- [#155][gh-155] — MIDI: route MIDI device + file input into synths
  (produces the [§6](#message-op-set) messages from raw MIDI).
- [#156][gh-156] — MUSIC: reconnect player to synth (drives
  [§12](#public-api-surface) from `MUSIC::note`).
- [#157][gh-157] — C-API: synth surface (mirrors
  [§12](#public-api-surface)).

Downstream epics that consume this contract:

- [#148][gh-148] — Instruments (sampler / VA / FM) are `dspVoice`
  subclasses per [§3](#the-voice-model); the SFZ opcode design
  ([#172][gh-172]) and instrument voices build on the voice model here.
- [#147][gh-147] / [#168][gh-168] — Per-note 3D positioning replaces the
  "one synth = one position" baseline in [§1](#goals-and-non-goals); that
  design fork is deliberately deferred out of this document.

Current-engine anchors this design builds on (paths as of writing):

- `DSP::dspSourceObject` / `dspObject` — `YseEngine/dsp/dspObject.hpp`.
- `SOUND_STATUS` / `SOUND_INTENT` — `YseEngine/headers/enums.hpp`.
- `OBJECT_IMPLEMENTATION_STATE` — `YseEngine/headers/enums.hpp`.
- `lfQueue` — `YseEngine/utils/lfQueue.hpp`.
- `managerSetupJob` / `managerDeleteJob` — `YseEngine/internal/managerJobs.hpp`.
- `sound::create(dspSourceObject&, …)` — `YseEngine/sound/soundInterface.*`,
  `YseEngine/sound/soundImplementation.*`.
- `attachReverb` / `underWaterEffect` attach precedents —
  `YseEngine/channel/channelImplementation.cpp`,
  `YseEngine/internal/underWaterEffect.*`.
- `MUSIC::note` — `YseEngine/music/note.hpp`.
- `DSP::MidiToFreq` — `YseEngine/dsp/math.*`.
- `DSP::ADSRenvelope`, oscillators — `YseEngine/dsp/ADSRenvelope.hpp`,
  `YseEngine/dsp/oscillators.hpp`.
- C API dspObject pattern — `YseEngine/c_api/yse_dsp_modules.cpp`.

[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-145]: https://github.com/yvanvds/yse-soundengine/issues/145
[gh-146]: https://github.com/yvanvds/yse-soundengine/issues/146
[gh-147]: https://github.com/yvanvds/yse-soundengine/issues/147
[gh-148]: https://github.com/yvanvds/yse-soundengine/issues/148
[gh-150]: https://github.com/yvanvds/yse-soundengine/issues/150
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-152]: https://github.com/yvanvds/yse-soundengine/issues/152
[gh-153]: https://github.com/yvanvds/yse-soundengine/issues/153
[gh-154]: https://github.com/yvanvds/yse-soundengine/issues/154
[gh-155]: https://github.com/yvanvds/yse-soundengine/issues/155
[gh-156]: https://github.com/yvanvds/yse-soundengine/issues/156
[gh-157]: https://github.com/yvanvds/yse-soundengine/issues/157
[gh-168]: https://github.com/yvanvds/yse-soundengine/issues/168
[gh-172]: https://github.com/yvanvds/yse-soundengine/issues/172
[gh-181]: https://github.com/yvanvds/yse-soundengine/issues/181
[doc-dsl]: live_coding_dsl.md
