# Domain clocks and clip transport

As-built description of the named beat clocks (issue #249) and the clip transport that plays against
them (issue #250, MIDI-out #350). User-facing tutorial:
[documentation/source/tutorials/15_clocks_and_clips.rst](../../documentation/source/tutorials/15_clocks_and_clips.rst).
Moved out of PROJECT_OVERVIEW.md by issue #890.

## Domain clocks

A set of named musical (beat) clocks derived from the single sample clock (issue
[#249](https://github.com/yvanvds/yse-soundengine/issues/249), a capability request from Phi's
polytemporal timing model). Each clock is a **beat accumulator**:
`CLOCK::Manager().update(blockSeconds)` runs once per rendered 128-sample block (wired into
`deviceManager::renderOneBlock`, or once per callback from `doOnCallback` while nothing renders —
issue [#944](https://github.com/yvanvds/yse-soundengine/issues/944)) and advances each clock by
`blockSeconds × tempo / 60`, so beat position is the running integral of tempo — no absolute-time
schedule — and a beat deadline resolves to one block whatever the device buffer size. Because every
clock derives from the one sample clock, polytemporal relationships stay exact and deterministic.

Tempo is a **playable, rampable control**: `setTempo(name, bpm, rampSeconds)` slews linearly
(instant when `rampSeconds` is 0) and is never clamped (0 pauses, negative runs backward). Clocks
are created/destroyed/queried by name at runtime. The manager follows the PLAYER lock-free lifecycle
(canonical `forward_list` under a mutex → SPSC inbox → audio-owned `inUse` working list → slow-pool
delete job); the audio thread never allocates, locks, or frees. `beatPosition` / `currentTempo` read
published atomics and are safe to poll from the UI thread at frame rate (playhead display). Public
surface: `YSE::system::createClock / destroyClock / clockExists / setTempo / beatPosition /
currentTempo`, mirrored in the C ABI as `yse_system_*_clock` / `yse_system_set_tempo` /
`yse_system_beat_position` / `yse_system_current_tempo`. Clip transports that bind to these clocks
are [§12c](../../PROJECT_OVERVIEW.md#12c-clip-transport); the patcher binds to them through
`PATCHER::clockBridge` (§8). **Ownership is shared**
([#707](https://github.com/yvanvds/yse-soundengine/issues/707)): `lookup(name)` returns a
`std::shared_ptr<domainClock>` and every holder keeps its share, so `destroyClock` retires a clock
rather than deleting it — it vanishes from queries at once, the audio thread stops advancing it, and
the slow-pool delete job only drops the *manager's* share. A holder that is still bound therefore
reads a **frozen beat** instead of freed memory, which is also what a wait armed on a destroyed
clock needs to mean (it never comes due). The share is taken on the control thread or the background
pool and released in the holder's destructor; no read path ever touches the refcount, so the audio
thread still sees nothing but a plain pointer load. Neither holder could have honoured the previous
"the clock must outlive you" contract — the patcher's bridge never releases a binding by design, and
a transport cannot observe the audio thread letting go of a pointer it published.

## Clip transport

A `YSE::clip` loops a flat, immutable list of beat-timed note events (`clipEvent`: `startBeat`,
`durationBeats`, `channel`, `pitch`, `velocity`, optional per-note `pitchBend`) against a bound
[domain clock](../../PROJECT_OVERVIEW.md#12b-domain-clocks), dispatched from the audio thread so the
UI never dispatches a note (issue [#250](https://github.com/yvanvds/yse-soundengine/issues/250), a
Phi capability request). Every audio block, `CLIP::Manager().update()` (wired into
`deviceManager::doOnCallback` right after `CLOCK::Manager().update()`, so the clocks are already
advanced) converts the block's beat boundaries into a `(from, to]` window on the clock and fires
exactly the events whose crossings fall inside it — events are *evaluated per block*, never
scheduled ahead in absolute time, so tempo changes on the clock bend the clip immediately with no
rescheduling. `startBeat` is taken modulo the loop length, so events repeat every loop.

The event list is **replaceable while playing**: `setEvents` publishes a new immutable list that the
audio thread swaps in at the next block boundary through an atomic single-slot handoff plus a
lock-free `retired` queue the control thread reclaims — no allocation, lock, or free on the audio
thread. **Sounding-note bookkeeping survives the swap**: each note-on records its own absolute
off-beat in a bounded audio-thread-owned set, so a note that vanished from the new list still gets
its note-off on time. `play` / `stop` / `isPlaying`; `stop` releases everything sounding. Multiple
clips run concurrently, each on its own clock.

Output targets two sinks behind the same templated seam (the firing core is templated over the sink
type, unit-tested against a recording sink). **Internal synths:** one or more `YSE::synth`
instances, reached through the same RT-safe `SYNTH::interfaceObject` note API MIDI-file playback
uses. **External MIDI-out** (issue [#350](https://github.com/yvanvds/yse-soundengine/issues/350),
builds with `YSE_ENABLE_MIDI_DEVICE`): `clip::connect(midiOut&)` routes playback to an RtMidi output
port — but an RtMidi send cannot happen on the audio callback, so the audio thread encodes the wire
bytes, stamps every event fired in a block with the block's absolute send deadline (paced one block
per rendered block, resynced when the audio thread falls behind), and `try_push`es them onto
`MIDI::outSender`'s bounded lock-free SPSC queue; a dedicated sender thread drains the queue and
performs the sends when each message comes due (`midi/midiOutSender.h`, lazily started on first
connect, stopped + flushed from `global::close`). Per-block deadlines keep the transport's
note-off-before-note-on ordering intact (per-event sub-block stamps could reorder same-pitch off/on
pairs and hang hardware notes). Lifecycle mirrors the CLOCK / MIDI-file managers (canonical
`forward_list` under a mutex → SPSC inbox → audio-owned `inUse` working list → slow-pool delete
job). Public surface mirrored in the C ABI as `yse_clip_*`
([clip/clip.hpp](../../YseEngine/clip/clip.hpp) →
[c_api/include/yse_c/yse_clip.h](../../YseEngine/c_api/include/yse_c/yse_clip.h)), including
`yse_clip_connect_midi_out` / `yse_clip_disconnect_midi_out`. Binding a clock shares its lifetime
([#707](https://github.com/yvanvds/yse-soundengine/issues/707),
[§12b](../../PROJECT_OVERVIEW.md#12b-domain-clocks)), so clip and clock can be destroyed in either
order — a `destroyClock` under a bound clip stops the clock rather than freeing it, and the clip
stops firing.
