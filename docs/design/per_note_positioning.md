# Per-note 3D positioning and position handlers

Status: **design gate, pre-implementation**. Tracking issue:
[#168][gh-168]. Parent epic: [#147 — Per-note 3D positioning & position
handlers][gh-147].

This document settles the one genuinely open architectural fork of
[#147][gh-147] *before* implementation code lands, and fixes the record
for the sub-issues that follow — [#169][gh-169] (per-note positioned
sound infrastructure), [#170][gh-170] (position-handler interface +
built-in handlers), [#171][gh-171] (C-API surface) — so each treats the
route decision and the handler contract here as a fixed contract.

It designs against the *finished* synth voice core: [#154][gh-154]
(keyboard state) is merged, and the voice model, allocator, stealing
policy, aggregate `outputSource`, and message inbox are already specified
in [docs/design/synth_core.md][doc-synth] ([#151][gh-151]). This document
extends that contract; it does not re-litigate it. Where the two touch —
the aggregate output source, the message op-set, the `clone()` prototype
pattern, voice lifecycle — this document cites the synth-core section and
builds on it.

This follows the design-issue-first pattern proven by
[docs/design/synth_core.md][doc-synth] ([#151][gh-151]) and
[docs/design/patcher_graphstate.md][doc-graph] ([#226][gh-226]), per the
[#120][gh-120]/[#151][gh-151] precedent that both routes touch the
engine's most delicate machinery — the sound lifecycle or the
spatialization pipeline — so the trade-off deserves a written decision.

> **Amended 2026-07-07, pre-merge design review.** The Route 2 decision
> **stands**, but the argument is re-weighted: the deciding factor is
> the **granularity-correctness case of [§5](#what-per-note-spatialization-means-here)**
> (per-note occlusion and per-note virtualization are *wrong at that
> granularity*, and Route 1 forces them on with no off switch) — not
> implementation risk, and not consistency with the recently settled
> synth-core model, which per the project review stance
> ([docs/project_vision.md](../project_vision.md)) is context, never a
> verdict. Also added: a forward note on per-voice participation in
> reverb space (Route 2's honest ceiling), the framing of
> `HANDLER_PARAM` as an early instance of the addressable-parameter
> direction, and a fix to the swarm example's shared-state mechanism.

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [The fork](#the-fork)
3. [Decision summary](#decision-summary)
4. [Why not Route 1](#why-not-route-1)
5. [What "per-note" spatialization means here](#what-per-note-spatialization-means-here)
6. [The extracted `panner` component](#the-extracted-panner-component)
7. [The multichannel aggregate and its attachment](#the-multichannel-aggregate-and-its-attachment)
8. [The `positionHandler` contract](#the-positionhandler-contract)
9. [Message op-set additions](#message-op-set-additions)
10. [Voice lifecycle, release tail, and handler end-of-life](#voice-lifecycle-release-tail-and-handler-end-of-life)
11. [Voice stealing interaction](#voice-stealing-interaction)
12. [Threading model](#threading-model)
13. [Real-time budget](#real-time-budget)
14. [Public API surface](#public-api-surface)
15. [Worked example: the swarm](#worked-example-the-swarm)
16. [Scope of the sub-issues](#scope-of-the-sub-issues)
17. [Cross-references](#cross-references)

---

## Goals and non-goals

### Goals

- **Each sounding note gets its own 3D direction.** A 16-voice synth
  behind one `YSE::sound` today spatializes all voices at one shared
  position ([synth_core.md §1][doc-synth]). This epic gives each voice its
  own azimuth/elevation (and, cheaply, its own distance rolloff), steered
  from note-on until end-of-release.
- **Behaviour is pluggable and RT-safe.** A `positionHandler` is an
  RT-safe object the user derives from — static, random-spread,
  orbit/swarm — attached per-synth and instantiated per-note via the same
  `clone()` prototype pattern as `dspVoice`
  ([synth_core.md §3][doc-synth]). Handlers **steer position only**; the
  synth keeps ownership of controller/aftertouch forwarding and exposes
  those values to handlers to read.
- **Zero note-rate lifecycle churn.** Note-on/off must not allocate,
  register, connect, or free anything on any thread. Firing sixty notes a
  second must cost sixty inbox pushes and sixty audio-thread allocator
  hits — nothing more. This is the load-bearing constraint that decides
  the fork.
- **Reuse, don't fork, the spatialization math.** The pan/distance/doppler
  derivation is battle-tested (issues [#202][gh-202]–[#215][gh-215]); the
  design must reuse it, not clone a second copy that drifts.

### Non-goals (explicit)

Deliberately out of scope for this epic; they will not be added under
these names without a new design pass:

- **No per-note occlusion.** Occlusion is a user raycast run on the
  control thread once per sound ([soundImplementation.cpp `update()`
  comment, issue #209][src-occl]). Multiplying that by polyphony is both
  expensive and unwanted. Occlusion stays **per-aggregate** — see
  [§5](#what-per-note-spatialization-means-here). This matches the epic's
  non-goal ("no per-note occlusion/zone logic beyond what the chosen route
  inherits").
- **No per-note virtualization.** The `VirtualSoundFinder` budget is a
  global list of `YSE::sound`s ([issue #205][src-occl]); flooding it with
  note-rate entries would wreck the virtualization heuristic. Each synth
  contributes **one** entry, at its aggregate position.
- **No per-note reverb send.** Reverb/underwater/insert routing stays at
  the aggregate (channel) level.
- **No custom handlers from C or Dart.** The C surface ([#171][gh-171])
  exposes the *built-in* handler types only; the audio-thread callback
  plumbing needed to subclass a handler from C is the same known gap that
  blocks user voices from C ([synth_core.md §1 non-goals][doc-synth]).
- **No handler scripting** and **no dynamic per-note reallocation.** A
  handler group is fixed at attach time, exactly like a voice group.

---

## The fork

[#147][gh-147] leaves exactly one question open. Both answers touch the
engine's most delicate machinery, which is why scoping deferred the choice
to this document rather than the epic.

**Route 1 — a pre-warmed pool of note-sounds.** Give every note its own
`YSE::sound` over the live `sound::create(dspSourceObject&, …)` overload
([soundInterface.cpp:208][src-create]). Per-note spatialization comes for
free: each note *is* a sound, so it inherits the whole pan pipeline
(`update()` → `computeFinalGains()` → `toChannels()`) unchanged. The catch
is the sound-manager lifecycle. It was never designed for note-rate churn:
`create()` registers an impl and pushes it through the slow-pool
`managerSetupJob`, `readyCheck()`/`doThisWhenReady()` promote it, a
per-note `parent->connect()` links it into the channel, and teardown runs
the `OBJECT_RELEASE → OBJECT_DELETE_PENDING → OBJECT_DELETE` fence plus a
slow-pool `managerDeleteJob` ([soundManager.cpp][src-mgr]). Doing that
per note is a non-starter, so Route 1 must **neutralize** the churn by
pooling: pre-create N note-sounds at attach time, keep them all
`OBJECT_READY` and channel-connected, and park/reactivate them silently on
note-on/off without ever touching create/setup/connect/delete at note
rate.

**Route 2 — per-voice `Pos` inside the synth.** Keep the synth as one
aggregate behind one sound ([synth_core.md §9][doc-synth]). Extract the
spatialization math — currently the private static helpers and per-sound
smoothing state of `SOUND::implementationObject` — into a reusable
`panner` component, and run one `panner` per voice inside
`outputSource::process()`. The aggregate becomes a device-width
multichannel bed the voices pan *themselves* into; the owning sound plays
that bed straight through.

The tie-breakers the epic named — **CPU** (per-note gain-smoothing
state), **correctness** (doppler/occlusion per note?), and **blast
radius** — are weighed in [§3](#decision-summary) and
[§4](#why-not-route-1).

---

## Decision summary

**Route 2 is chosen.** Per-voice positioning lives inside the synth,
over an extracted `panner` component; the sound-manager lifecycle is not
touched.

| Question | Decision |
| --- | --- |
| Route | **Route 2 — per-voice `Pos` inside the synth.** The synth stays one aggregate behind one `YSE::sound` ([synth_core.md §9][doc-synth]); per-voice pan happens inside `outputSource::process()`. |
| Spatialization reuse | **Extract a `panner` component** from `SOUND::implementationObject`'s pan helpers + smoothing state. One `panner` per voice. The sound subsystem and the panner share one implementation, so they cannot drift. |
| Note-rate churn | **Eliminated, not neutralized.** Notes never create/register/connect/free anything. A note is an allocator slot flip on the audio thread + one bounded inbox push — identical to [synth_core.md §4][doc-synth]. |
| What is per-note | **Direction (azimuth/elevation) and distance rolloff.** Optionally per-note doppler (opt-in). Occlusion, virtualization, and reverb send stay **per-aggregate** — see [§5](#what-per-note-spatialization-means-here). |
| Handler model | RT-safe object, `clone()`-per-note prototype like `dspVoice`; lifecycle hooks `noteOn` / `update(delta)` / `release-end`; reads velocity/aftertouch/controllers; steers position only. See [§8](#the-positionhandler-contract). |
| Position update path | Handlers run **autonomously on the audio thread** (zero per-block main→audio traffic). Imperative per-note / per-handler-param updates ride the existing synth inbox as a bounded, allocation-free op ([§9](#message-op-set-additions)). |
| Sound-side change | **One narrow seam:** the synth's owning sound plays a pre-spatialized, device-width bed **without re-panning** it. A create-time flag, read once on the audio thread — not a lifecycle change. See [§7](#the-multichannel-aggregate-and-its-attachment). |

The deciding factor is **granularity correctness**
([§5](#what-per-note-spatialization-means-here)): not everything a
`YSE::sound` computes *should* be per-note. Per-note occlusion (a user
raycast per note per control tick) and per-note virtualization
(note-rate churn in the global budget heuristic) are wrong at that
granularity — and under Route 1 they come along automatically, with no
off switch short of re-plumbing the sound impl. Route 2 is the only
route that lets the engine pick the split: pan and distance per voice,
occlusion / virtualization / reverb per aggregate. That argument stands
on the problem domain alone and would survive even if Route 1 were free
to build.

Second-order but real: Route 2 confines the new code to a fresh,
unit-testable `panner` component and the synth's own mix loop, is
note-rate-safe **by construction** (notes were already lifecycle-free in
the synth core), and avoids re-purposing the race-audited sound-manager
lifecycle ([#185][gh-185]–[#201][gh-201],
[#283][gh-283]–[#290][gh-290]) for a park/reactivate pattern it was
never built for. Consistency with the [synth_core.md §9][doc-synth]
aggregate model is a genuine cost saved, but — per the project review
stance — a prior decision is context, not a verdict; had §5 pointed the
other way, the synth-core model would have been the thing to reopen.
The extra CPU (a per-voice smoothing-gain vector) is the same order
either way.

---

## Why not Route 1

Route 1 is attractive precisely because per-sound spatialization already
works — but the three tie-breakers all point the other way.

- **Correctness — it inherits the wrong things "for free" (decisive).**
  The appeal of Route 1 is that doppler, distance, occlusion, and
  virtualization all come along automatically. But per-note occlusion (a
  user raycast per note per control tick) and per-note virtualization
  (note-rate churn in the global `VirtualSoundFinder`) are things the
  epic explicitly does **not** want ([§1 non-goals](#goals-and-non-goals))
  — and in a performance-instrument engine they are wrong at that
  granularity, not merely expensive. "For free" here means "cannot be
  switched off without re-plumbing the sound impl" — exactly the wrong
  default. This is the argument that decides the fork on the problem
  domain alone.

- **Blast radius (supporting).** Pooling note-sounds means inventing a
  "parked but ready" state for `SOUND::implementationObject`: an impl that
  stays in `inUse`, stays `connectedToParent`, stays out of the
  `VirtualSoundFinder` while silent, and can be re-armed with a new source
  pointer, a new `Pos`, and a fresh `SS_WANTSTOPLAY` — all on the audio
  thread, all without the `create/setup/readyCheck/doThisWhenReady`
  handshake that today is the *only* path an impl reaches a playable
  state. Every one of those invariants is load-bearing in the
  [#185][gh-185]–[#201][gh-201]/[#283][gh-283]–[#290][gh-290] audits
  (e.g. the `OBJECT_DELETE_PENDING` disconnect fence, the `_mgrNext`
  single-link exclusivity between `toLoad` and `inUse`, the
  `source_dsp` acquire/release handshake). Reusing that machinery for a
  purpose it was not designed for is the highest-risk change available in
  the engine.

- **Structural cost (supporting, not decisive on its own).**
  [synth_core.md §9][doc-synth] fixes "one synth = one `dspSourceObject`
  = one `YSE::sound` = one position" and builds the aggregate
  `outputSource` around it. Route 1 discards the aggregate: a synth
  becomes a *manager of N sounds*, the voice-group/allocator/stealing
  design still owns the voices, but each voice's audio now has to reach
  its own sound impl — a second ownership graph layered on the first.
  Two lifecycles for one synth. (Stated as a structural observation, not
  an appeal to precedent: if the correctness case above had favoured
  Route 1, the synth-core model would have been the thing to revise.)

- **CPU is not the discriminator.** Both routes carry the one unavoidable
  per-note cost the epic flagged: a per-note gain-smoothing vector
  (`lastGain` today is `[outChannel][srcChannel]`,
  [soundImplementation.h][src-lastgain]). Route 1 pays it inside a *full*
  `SOUND::implementationObject` that also drags file/streaming, doppler
  slew, fader, virtualization, and occlusion state per note; Route 2 pays
  it inside a lean `panner` that holds only the pan state. Route 2 is the
  lighter of the two, but the difference is second-order — CPU does not
  decide this fork, blast radius does.

Route 1 is not foreclosed forever: if a future need arises for genuinely
independent per-note sounds (per-note occlusion zones, per-note reverb
sends), the note-sound pool is the way to get them. This epic does not
need them, so it takes the contained route.

---

## What "per-note" spatialization means here

The crucial scoping call: **not everything a `YSE::sound` computes should
be per-note.** Route 2 lets us pick. The split:

| Spatial quantity | Granularity | Rationale |
| --- | --- | --- |
| Azimuth / elevation (pan) | **per voice** | The whole point of the epic. Cheap: pan gains recomputed once per control tick per active voice. |
| Distance rolloff (attenuation) | **per voice** | Cheap (one `Dist()` + a gain scalar) and correct — a near note *should* be louder than a far one. Folds into the same per-voice gain vector. |
| Doppler | **per voice, opt-in** | Meaningful for orbits/swarms (each note has its own velocity), but for a *synth* voice doppler must retune the oscillator, not resample a file. Exposed as a `dopplerRatio` the voice multiplies into its pitch; voices that ignore it pay nothing. Off by default. See [§6](#the-extracted-panner-component). |
| Occlusion | **per aggregate** | A control-thread user raycast ([#209][src-occl]); per-note would multiply raycasts by polyphony. Stays on the owning sound. |
| Virtualization priority | **per aggregate** | The `VirtualSoundFinder` is a global per-`sound` budget ([#205][src-occl]); note-rate entries would break its heuristic. One entry per synth. |
| Reverb / underwater / insert send | **per aggregate** | Channel-level routing; unchanged. |

So the owning `YSE::sound` still carries the synth's **aggregate**
position — used for occlusion, virtualization, reverb send, and as the
fallback/reference position — while per-voice `panner`s refine only the
**directional pan and distance** *within* that aggregate. This is both
the cheap answer and the correct one, and it is only *available* under
Route 2, where the synth keeps a single aggregate. (Under Route 1 every
quantity is per-note whether you want it or not.)

**Forward note — the honest ceiling of per-aggregate reverb.** For a
swarm spread across a large space, all notes sharing the aggregate's
reverb send is an audible fidelity limit: a note deep in one acoustic
zone and a note in another get the same wet character. The
vision-aligned successor (see
[send_return_buses.md §12b](send_return_buses.md#forward-notes-spatial-reverb-and-modulated-sends)
and [docs/project_vision.md](../project_vision.md)) is **per-voice send
estimation into zone-bound return buses** — per-voice send gains derived
from each voice's position, accumulated into the owning channel's send
taps. The device-width bed does not preclude this (send estimation
happens *before* the bed collapses voices); it is a named future design
pass, not part of this epic. Route 1's pooled note-sounds remain the
fallback if genuinely independent per-note sound citizenship is ever
needed; nothing here forecloses it.

---

## The extracted `panner` component

Route 2's reuse promise is kept by lifting the spatialization math out of
`SOUND::implementationObject` into a standalone component (working name
`DSP::panner`; final home in [#169][gh-169]). The sound subsystem then
*also* uses it, so there is exactly one copy of the pan math and it cannot
drift. Per the epic's downstream note, the extracted component is a
reusable engine asset and earns a short doc page of its own once it lands.

**What moves.** The pan helpers on `SOUND::implementationObject` are
*already* pure static functions kept "unit-testable in isolation" — they
lift almost verbatim:

- `computeSourceAngle(relative, dir, listenerForward)` — azimuth, wrapped
  to `(-π, π]` ([#204][gh-202]).
- `computeHorizontalFraction(dir)` — zenith-flyover taming ([#210][gh-202]).
- `computeSpeakerOverlap(a, b)` — density compensation ([#207][gh-202]).
- `computePanRatio(initGain, power, speakerCount)` — per-speaker power
  share with the antipodal-NaN guard ([#202][gh-202]).
- `computeDopplerRatio(sourceVel, listenerVel, dist, scale)` — the
  multiplicative doppler ratio ([#208][gh-202]).
- `computeVirtualDist(distance, size, volume)` — kept on the sound side
  (virtualization is per-aggregate), but the helper is shared.
- `gainAccumulate(src, fader, dest, len, lastGain, finalGain)` — the fused
  MAC + 50-sample smoothing ramp ([#213][gh-202]).
- `computeFinalGains()` — the cardioid-pan + normalisation + rolloff
  derivation, run behind a `gainDirty` flag ([#212][gh-202]).

**What state the component holds** (per instance — this *is* the "per-note
gain-smoothing state" the epic flagged):

```cpp
// working shape; final form in #169
class panner {
public:
  panner(int outputChannels, int sourceChannels);   // sizes gain vectors; slow-pool only

  // control-rate: recompute cached per-speaker gains from a new position.
  // Reads the global listener. Pure math, allocation-free, RT-safe.
  void update(const Pos& pos, Flt delta);

  // audio-rate: distribute one source block across the output bed with the
  // 50-sample smoothing ramp. dest is the device-width aggregate bed.
  void spread(const DSP::buffer& src, std::vector<DSP::buffer>& dest);

  Flt dopplerRatio() const;     // 1.0 unless per-note doppler is enabled
private:
  Pos lastPos, velocityVec;
  Flt distance, angle, horizFraction;
  std::vector<std::vector<Flt>> finalGainCache;  // [outCh][srcCh], sized in ctor
  std::vector<std::vector<Flt>> lastGain;        // smoothing state, sized in ctor
  Bool gainDirty;
};
```

`update()` mirrors `SOUND::implementationObject::update()`: it reads the
global listener (`INTERNAL::ListenerImpl().newPos` / `.forward` / `.vel`
— unchanged, one shared listener for all voices), computes
distance/angle/horizFraction, sets `gainDirty`, and — when doppler is
enabled — derives `dopplerRatio` from the voice's own velocity. `spread()`
mirrors `toChannels()`: it consumes `gainDirty` to recompute
`finalGainCache` at most once per tick and applies `gainAccumulate` per
`[outCh][srcCh]`.

**RT-safety.** All gain vectors are sized in the constructor, which runs
**only on the slow pool** (the panner is built inside the voice's
`clone()`, [§8](#the-positionhandler-contract)/[synth_core.md
§8][doc-synth]). `update()` and `spread()` allocate nothing, lock nothing,
touch no file — the same discipline as a voice `process()`. The listener
reads are the existing atomics (`aPos forward`, `aPos vel`).

**Migration note (not a refactor of the sound path).** The sound
subsystem keeps behaving bit-identically. In the first landing the sound
impl may simply call the moved free helpers; whether `SOUND::implementationObject`
is later refactored to *hold a `panner`* is an implementation choice for
[#169][gh-169] and must preserve the [#212][gh-202]/[#213][gh-202]
bit-exactness tests. Extraction is a lift, not a redesign of the working
pan pipeline — no broadening of scope on a subsystem that is not this
epic's target.

---

## The multichannel aggregate and its attachment

Under Route 2 the voices pan *themselves*, so the aggregate can no longer
be a mono sum the owning sound pans afterward — that would pan twice. The
aggregate `outputSource` ([synth_core.md §2][doc-synth]) becomes
**device-width**: one `buffer` per device output channel. Each active
voice renders its mono block, then its `panner.spread()` distributes that
block across the aggregate's output buffers. `outputSource::process()`
gains one step:

```
for each active voice v:
    v.process(intent)                       // mono voice output (unchanged)
    v.handlerUpdate(delta)                  // steer v's position (§8)
    v.panner.update(v.position, delta)      // control-rate pan recompute
    v.panner.spread(v.samples[0], samples)  // mix into the device-width bed
```

The stealing declick ([synth_core.md §4][doc-synth]) still applies to the
voice's mono output *before* `spread()`, so a stolen note fades cleanly
across whatever direction it currently occupies.

**The one sound-side seam.** The owning `YSE::sound` must render this bed
**straight through to matching output channels without applying its own
pan** — otherwise it re-pans an already-panned signal. This is the single
place Route 2 touches the sound subsystem, and it is a narrow one: a
create-time flag (working name `PT_DSP` *pre-spatialized* variant, or a
`bool preSpatialized` on the synth create path) that `dsp()`/`toChannels()`
read **once** on the audio thread to select a 1:1 passthrough instead of
the cardioid pan. It changes *which gain path* the sound runs, not the
lifecycle, threading, or ownership of the sound manager. The exact
spelling — a new `playerType`, a flag on the existing `PT_DSP` path, or a
dedicated create overload — is [#169][gh-169]'s to fix; the *requirement*
is fixed here: **no double-pan, chosen at create time, read once, no new
audio-thread branch per block beyond a predictable flag test.**

The aggregate still carries the synth's single position for occlusion /
virtualization / reverb ([§5](#what-per-note-spatialization-means-here)):
those read the sound's `pos` exactly as today. Only the directional pan is
delegated inward to the voices.

---

## The `positionHandler` contract

A `positionHandler` decides *where a note is* over its lifetime. It is the
positional analogue of `dspVoice`: user-derived, engine-owned polyphony,
`clone()`-per-note, RT-safe. It **steers position only** — it never
produces audio, never touches keyboard state, never forwards controllers
(the synth owns all of that and *hands the values to* the handler to
read).

```cpp
namespace YSE { namespace SYNTH {

class API positionHandler {
public:
  virtual ~positionHandler() {}

  // ---- user MUST implement -------------------------------------------

  // Return a fully-constructed, independently-owned clone of the derived
  // type, with everything it will touch in the hooks already allocated.
  // Runs ONLY on the slow pool (never the audio thread) — like dspVoice::clone().
  virtual positionHandler* clone() = 0;

  // Note begins. `ctx` exposes the voice's read-only note state (see below).
  // Return the note's initial position. Audio thread; allocation-free.
  virtual Pos noteOn(const voiceContext& ctx) = 0;

  // Control-rate steer, once per audio block. `delta` is the wall time since
  // the last update (seconds), matching SOUND update()'s Time().delta() idiom
  // so behaviour is frame-rate-independent. Return the note's new position.
  // Audio thread; allocation-free.
  virtual Pos update(const voiceContext& ctx, Flt delta) = 0;

  // The note has entered release (decay tail); position control CONTINUES
  // until the tail ends (§10). Optional hook — default is a no-op, so a
  // handler that ignores release simply keeps orbiting through the tail.
  virtual void onRelease(const voiceContext& ctx) {}
};

}} // namespace YSE::SYNTH
```

`voiceContext` is a small read-only view the synth fills for the handler —
never a heap object, never owned by the handler:

```cpp
struct voiceContext {
  Flt frequency;    // Hz (the sounding pitch)
  Flt velocity;     // [0,1]
  Flt aftertouch;   // [0,1], live
  Flt pitchWheel;   // [-1,1], live
  Int channel;      // MIDI channel
  Int note;         // MIDI note number
  // controllers: read a live CC the synth already stores (§5 of synth_core.md)
  Flt controller(int number) const;
  // shared handler params: read the group's param block, written by
  // HANDLER_PARAM messages on the audio thread (§9). Same-thread read —
  // no atomics needed in handler code.
  Flt handlerParam(int index) const;
};
```

Contract:

1. **`clone()`-per-note prototype.** `synth.positionHandler(prototype)`
   does not store the prototype — it becomes the group's blueprint, cloned
   once per voice slot at `addVoices()` setup time on the slow pool, right
   alongside the voice clones ([synth_core.md §8][doc-synth]). One handler
   instance per voice slot, allocated once, reused for every note that
   slot ever plays. The prototype must outlive setup; the engine neither
   copies nor owns it (same caveat as `dspVoice` and
   `sound::create(dspSourceObject&, …)`).
2. **Hooks are RT-safe.** `noteOn`, `update`, and `onRelease` run on the
   audio thread inside `outputSource::process()`. **No allocation, no
   locks, no blocking I/O, no logging** — identical rules to a voice
   `process()` and `onNoteEvent` ([synth_core.md §7][doc-synth]). Any state
   a handler shares with another thread (e.g. a swarm centre driven from
   the main thread) must be atomic or arrive via the inbox
   ([§9](#message-op-set-additions)).
3. **Steers position only.** The return value of `noteOn`/`update` is the
   voice's position, fed straight into that voice's `panner.update()`. A
   handler cannot change pitch, gain, envelope, or routing — those belong
   to the voice and the keyboard state.
4. **Reads, does not write, note state.** The handler may read velocity,
   aftertouch, pitch-wheel, and controllers through `voiceContext`
   (delivered from the voice's atomics, [synth_core.md §3][doc-synth]) to
   modulate position — e.g. velocity → orbit radius, aftertouch → height.
   It must not attempt to mutate them.
5. **One handler group per synth.** Like a voice group, the handler
   assignment is fixed at attach time; there is no lock-free path to swap
   it while the audio thread reads it ([synth_core.md §1 non-goals][doc-synth]).
   A synth with no handler attached falls back to the aggregate position
   for every voice — i.e. the [synth_core.md §9][doc-synth] baseline, so
   this epic is strictly additive.

Handler-to-voice binding follows the allocator: when the allocator picks a
slot for a NOTE_ON ([synth_core.md §4][doc-synth]), that slot's paired
handler instance gets `noteOn(ctx)` and the returned `Pos` seeds the
slot's `panner`. Thereafter the slot's handler is `update()`d every block
until the voice reports `SS_STOPPED` ([§10](#voice-lifecycle-release-tail-and-handler-end-of-life)).

---

## Message op-set additions

Handlers run autonomously on the audio thread, so the common case
generates **zero** per-block main→audio traffic — a swarm of 32 orbiting
notes sends nothing once the notes are on. But two imperative paths need a
main→audio channel, and both reuse the existing per-impl
`lfQueue<messageObject>` inbox ([synth_core.md §6][doc-synth]) with the
same bounded, non-allocating `try_push` (drop-with-log on overflow, never
allocate):

| New `MESSAGE` | Payload | Effect |
| --- | --- | --- |
| `HANDLER_PARAM` | `Int index; Flt value;` | Update a shared handler parameter (e.g. swarm centre X, orbit radius). Applied to the group's handler prototype-shared param block; all live handlers read it next `update()`. |
| `NOTE_POSITION` | `Int channel; Int note; Pos pos;` | Imperative override: place the voice(s) sounding `note` on `channel` at `pos`. For app-driven positioning where the *main thread* owns the trajectory. Sets the slot's position directly; a subsequent handler `update()` may resume steering unless the handler is the static/passthrough handler. |

`NOTE_POSITION` is the "message-based, allocation-free, note-rate position
update" the epic calls for: it satisfies apps that compute positions on
the main thread (e.g. a game object's world position) and need them on the
audio thread at note rate without allocating. It is addressed by
`(channel, note)` — the same identity the keyboard state already keys on
([synth_core.md §5][doc-synth]) — so the main thread needs no knowledge of
voice-slot indices. Because it rides the existing inbox, it inherits the
inbox's SPSC discipline and overflow policy verbatim; no new queue, no new
thread contract.

The `Pos` payload (three `Flt`) fits the existing tagged-union message
([synth_core.md §6][doc-synth]) with a new named struct per op — no
positional aliasing, matching the synth-core message discipline.

**Forward note — `HANDLER_PARAM` is an early instance of a broader
direction.** Per the project vision
([docs/project_vision.md](../project_vision.md)), engine parameters are
expected to become *addressable* modulation targets, written at control
rate by patcher objects, live-coded expressions, or physics — not only
by direct API calls. `HANDLER_PARAM` (like the send-level messages of
[send_return_buses.md §11](send_return_buses.md#public-api-surface-proposed))
is designed for exactly that write pattern: bounded, allocation-free,
safe every control tick. Implementations of [#170][gh-170] should treat
"a patcher outlet steers the swarm centre" as an expected caller, not a
special case.

---

## Voice lifecycle, release tail, and handler end-of-life

Position control must run to **end-of-release**, not to note-off. A note
released while orbiting should keep orbiting through its decay tail — a
handler that stopped at note-off would freeze the tail in place, audibly
wrong for a moving source.

The synth core already models this: a voice's `intent` goes
`SS_WANTSTOSTOP` at NOTE_OFF and the voice reports `SS_STOPPED` only when
its envelope finishes release ([synth_core.md §3][doc-synth]). The handler
binds to those same transitions, which the allocator already tracks per
slot:

- **Attack/sustain** (`SS_WANTSTOPLAY` → `SS_PLAYING`): handler
  `update(delta)` every block.
- **Release begins** (allocator sets the slot to `SS_WANTSTOSTOP` on
  NOTE_OFF, [synth_core.md §5][doc-synth]): the synth calls
  `handler.onRelease(ctx)` **once**, on the transition edge, so a handler
  that wants to change behaviour for the tail (e.g. drift outward as it
  fades) learns the note is dying. Handlers that ignore release keep
  `update()`-ing unchanged.
- **Release ends** (voice reports `SS_STOPPED`; the allocator returns the
  slot to the free pool, [synth_core.md §3][doc-synth]): the handler stops
  being `update()`-d. Its instance is **not** freed — it is the slot's
  permanent, pre-allocated handler, reused for the next note that lands on
  the slot. The next `noteOn(ctx)` re-seeds it.

So the handler learns "entered release" from the `onRelease` edge and
"finished release" implicitly (the engine simply stops calling it when the
slot frees). No new lifecycle state is introduced: the handler's lifetime
is exactly the voice slot's active window, which the allocator already
computes. This is why per-note positioning adds **no** manager-level
lifecycle work — it rides the voice allocator's existing per-slot state
machine.

---

## Voice stealing interaction

Stealing is where per-note position and voice lifecycle collide, so the
handover is specified explicitly. Recall the synth-core policy: under
polyphony exhaustion the allocator steals a slot, force-fades the old
note's audio over `STEAL_FADE` (~5 ms), then re-arms the slot with the new
note ([synth_core.md §4][doc-synth]).

The handler follows the audio, not a separate schedule:

1. **During the steal fade**, the slot keeps rendering the *old* note and
   the *old* handler keeps `update()`-ing it. The position continues to
   evolve as the tail fades, so a stolen orbiting note fades along its
   current arc — no positional freeze, no click.
2. **When the fade reaches 0** and the slot resets, the slot's handler is
   **re-seeded** for the new note: `noteOn(ctx)` runs with the *new*
   note's `voiceContext`, returning the new note's initial position, and
   the slot's `panner` is reset to that position (`gainDirty` forces a
   clean recompute; the smoothing ramp starts from the faded-to-zero gain,
   so the new note fades *up* from silence at its own direction — no
   positional glide from the stolen note's last position to the new one).
3. **The handler instance is reused, not swapped.** Because one handler
   instance is permanently paired with the slot, stealing never allocates
   or frees a handler — it just calls `noteOn` again. A handler carrying
   per-note internal state (an orbit phase, an RNG draw) must therefore
   **fully reinitialise that state in `noteOn`**, not assume construction
   state. This is the one explicit obligation stealing places on handler
   authors, and it is documented on the `noteOn` hook: *`noteOn` must
   establish the note's complete initial state; the instance may have just
   finished another note.*

Because the panner is reset (not glided) on re-arm, and the audio is
force-faded independently, a stolen note's positional handover is
click-free and direction-clean for any user handler, mirroring the
audio-side guarantee.

---

## Threading model

No new thread context is introduced; the epic slots into the three
contexts of [synth_core.md §10][doc-synth].

1. **Interface / main thread(s).** `positionHandler(prototype)` records a
   pending group blueprint (allocation-free descriptor) and re-arms setup,
   exactly like `addVoices` ([synth_core.md §8][doc-synth]).
   `HANDLER_PARAM` / `NOTE_POSITION` are bounded `try_push`es onto the
   existing inbox. None of these touch voices, handlers, panners, or
   buffers.
2. **Slow pool (single-threaded setup/delete).** The **only** place
   handlers and panners are allocated. `impl->setup()` clones each voice
   *and* its paired handler, and constructs each slot's `panner`
   (sizing its gain vectors to device width × source channels). Freed in
   the manager delete job with the voices. Serialised with all other setup
   — no race with another subsystem's allocation.
3. **Audio thread.** Inside `outputSource::process()`: drain the inbox
   (now also `HANDLER_PARAM`/`NOTE_POSITION`), run the allocator, then per
   active voice run `handler.update` → `panner.update` → voice `process` →
   `panner.spread`. Reads the global listener atomics. Allocates nothing,
   locks nothing.

Cross-thread data adds nothing new in kind: handler params and imperative
positions flow through the same SPSC inbox as notes; the listener is the
same shared atomic set the sound path reads; per-voice position is written
and read on the audio thread only (the handler produces it, the panner
consumes it, same thread, same block).

There is no lock on any audio-thread path, and — the load-bearing property
— **no note-rate allocation, registration, connection, or free on any
thread.**

---

## Real-time budget

Per active voice, per block, the epic adds:

```
+ handler.update(delta)     // pure position math (orbit: 2 trig; static: nil)
+ panner.update(pos, delta) // recompute gains ONLY when gainDirty (once/tick)
+ panner.spread(...)        // gainAccumulate over [outCh][srcCh] — the pan mix
```

- **Pan recompute is amortised.** `panner.update()` sets `gainDirty`;
  `spread()` recomputes `finalGainCache` at most once per control tick,
  exactly the [#212][gh-202] optimisation that already spares a stationary
  sound ~5 of every 6 recomputations. A still note (static handler) costs
  essentially one MAC pass over the bed.
- **The mix cost is the honest new cost.** A mono voice summed into a mono
  aggregate was `O(bufferLen)`; spread across `D` device outputs it is
  `O(D × bufferLen)` per voice. For stereo `D = 2` this doubles the
  aggregate mix term of [synth_core.md §11][doc-synth]; for larger layouts
  it scales with output width, as any spatial mix must. This is the
  irreducible price of per-note direction and is bounded by
  `numActiveVoices × D × bufferLen`.
- **Handler cost is the user's budget**, like a voice `process()`. Built-in
  handlers (static, spread, orbit) are a handful of FLOPs; a pathological
  handler is the author's problem, enforced by review not code.
- **No per-note state growth at runtime.** Every panner gain vector and
  every handler is sized/allocated in `clone()`/`setup()` on the slow pool.
  Note-rate activity touches only pre-allocated storage.

The bench suite ([#181][gh-181]) gains an "N simultaneous positioned
notes" entry (the epic's stated benchmark) so regressions in the
`D × voices` mix term are caught.

---

## Public API surface

The synth interface ([synth_core.md §12][doc-synth]) gains one attach
method and two positional setters; everything else is unchanged. Chainable
and non-copyable, as before.

```cpp
namespace YSE {

class API synth /* = SYNTH::interfaceObject */ {
public:
  // ... all of synth_core.md §12 ...

  // Clone `prototype` once per voice slot as this synth's position handler.
  // With no handler attached, every voice uses the aggregate position
  // (the synth_core.md §9 baseline) — this call is purely additive.
  // `prototype` must outlive setup; the engine neither copies nor owns it.
  synth& positionHandler(SYNTH::positionHandler& prototype);

  // Update a shared handler parameter (e.g. swarm centre, orbit radius),
  // by index. Bounded inbox push; all live handlers read it next block.
  synth& handlerParam(int index, float value);

  // Imperative per-note position (app-driven trajectories). Places the
  // voice(s) sounding `note` on `channel` at `pos`. Bounded inbox push.
  synth& notePosition(int channel, int note, const Pos& pos);
};

} // namespace YSE
```

The C surface ([#171][gh-171]) mirrors this as
`yse_synth_position_handler_<builtin>` (built-in handler types only —
static / spread / orbit; user-defined handlers from C stay deferred, same
gap as user voices), `yse_synth_handler_param`, and
`yse_synth_note_position`, following the opaque-handle + `set_last_error`
conventions of the synth C surface ([#157][gh-157]) and
`c_api/yse_dsp_modules.cpp`.

Built-in handlers shipped by [#170][gh-170]:

- **`staticHandler`** — returns a fixed position (or the aggregate
  position); the trivial default.
- **`spreadHandler`** — deterministic or RNG spread around a centre, drawn
  fresh in `noteOn` (so stealing re-randomises correctly, [§11](#voice-stealing-interaction)).
- **`orbitHandler`** — the swarm workhorse: each note orbits a shared
  centre at a radius/speed it may derive from velocity/aftertouch.

---

## Worked example: the swarm

The epic's showcase, walked through the chosen design end to end to
validate it. Goal: N notes orbiting a shared centre, the centre steerable
from the main thread, each note's orbit radius set by its velocity.

```cpp
// 1. Author an orbit handler (RT-safe; steers position only).
class OrbitHandler : public YSE::SYNTH::positionHandler {
public:
  positionHandler* clone() override { return new OrbitHandler(*this); }

  Pos noteOn(const voiceContext& ctx) override {
    phase_  = 0.f;                        // FULL reinit — slot may be reused (§11)
    radius_ = 1.f + 3.f * ctx.velocity;   // velocity → orbit radius
    speed_  = 2.f;                        // rad/s
    return positionAt(ctx, phase_);
  }
  Pos update(const voiceContext& ctx, Flt delta) override {
    phase_ += speed_ * delta;             // advance orbit (frame-rate-independent)
    return positionAt(ctx, phase_);
  }
  void onRelease(const voiceContext&) override {
    speed_ *= 0.5f;                       // slow the orbit through the tail
  }
private:
  Pos positionAt(const voiceContext& ctx, Flt p) const {
    // shared centre from the group's param block (indices 0..2), written
    // by HANDLER_PARAM on the audio thread (§9) — same-thread read, no
    // atomics in handler code.
    Pos c(ctx.handlerParam(0), ctx.handlerParam(1), ctx.handlerParam(2));
    return Pos(c.x + radius_ * cosf(p), c.y, c.z + radius_ * sinf(p));
  }
  Flt phase_ = 0.f, radius_ = 1.f, speed_ = 2.f;
};

// 2. Build the synth: 32 voices, one orbit handler cloned per slot.
OrbitHandler orbitProto;
YSE::SYNTH::sineVoice voiceProto;               // reference voice (#152)
YSE::synth swarm;
swarm.create()
     .addVoices(voiceProto, 32)                 // 32-voice polyphony
     .positionHandler(orbitProto);              // one handler per slot

// 3. Attach behind ONE sound (aggregate). Route 2: pre-spatialized bed.
YSE::sound out;
out.create(swarm, nullptr, 0.8f);               // §7 passthrough attachment

// 4. Once READY, fire the swarm. No allocation, no lifecycle churn.
for (int i = 0; i < 32; ++i)
    swarm.noteOn(1, 48 + i, 0.4f + 0.02f * i);  // 32 notes, varied velocity → varied radius

// 5. Steer the swarm centre from the main thread at note rate — one bounded push.
swarm.handlerParam(0, gameObjectX);             // HANDLER_PARAM → all 32 orbits recentre
```

Tracing it through the design:

- **Setup (slow pool):** `create` + `addVoices` + `positionHandler`
  produce one pending-group blueprint. `impl->setup()` clones 32
  `sineVoice`s, 32 `OrbitHandler`s, and constructs 32 device-width
  `panner`s — all off the audio thread, once. The synth reaches
  `OBJECT_READY`.
- **32 note-ons (audio thread):** each is one inbox push on the main
  thread and one allocator slot-flip on the audio thread. The allocator
  seeds each slot's handler with `noteOn(ctx)` (radius from velocity) and
  its panner with the returned position. **Nothing is created, connected,
  registered, or freed** — the manager never sees a note. Route 1's entire
  problem does not exist here.
- **Every block (audio thread):** for each of the 32 active voices,
  `handler.update(delta)` advances the orbit, `panner.update` recomputes
  its pan gains (once per tick, `gainDirty`), the sine voice renders mono,
  and `panner.spread` mixes it into the device-width aggregate bed at its
  own direction. The bed plays through the owning sound un-re-panned
  ([§7](#the-multichannel-aggregate-and-its-attachment)). The synth
  contributes **one** entry to the `VirtualSoundFinder` and **one**
  occlusion query, at the aggregate position
  ([§5](#what-per-note-spatialization-means-here)).
- **Steering the centre:** `handlerParam(0, x)` is one bounded inbox push;
  drained on the audio thread, it updates the shared param all 32 handlers
  read next block. Zero per-note main→audio traffic.
- **Release + steal:** letting a key up sets that slot `SS_WANTSTOSTOP`;
  `onRelease` halves the orbit speed and the note keeps orbiting through
  its decay tail until `SS_STOPPED` frees the slot
  ([§10](#voice-lifecycle-release-tail-and-handler-end-of-life)). A 33rd
  note steals the oldest slot: the stolen note fades along its arc, then
  `noteOn` fully re-inits the handler (phase 0, new radius) for the new
  note ([§11](#voice-stealing-interaction)).

The swarm exercises every part of the design — clone-per-note handler,
autonomous audio-thread steering, shared param via the inbox, per-voice
pan into the aggregate bed, release-tail continuation, and stealing
re-init — with **no note-rate allocation and no sound-manager churn**,
which is exactly the property Route 2 was chosen to guarantee.

---

## Scope of the sub-issues

Per the epic, the implementing sub-issues reference the chosen route
(Route 2) and this document; if implementation forces a change here, it
updates this document in the same PR.

- **[#169][gh-169] — ENGINE: Per-note positioned sound infrastructure.**
  Extract the `panner` component ([§6](#the-extracted-panner-component))
  preserving the [#212][gh-202]/[#213][gh-202] bit-exactness; make the
  aggregate `outputSource` device-width and add per-voice `spread`
  ([§7](#the-multichannel-aggregate-and-its-attachment)); add the
  pre-spatialized sound attachment seam. Bench: N positioned notes.
- **[#170][gh-170] — ENGINE: Position handler interface + built-in
  handlers.** The `positionHandler` base + `voiceContext`
  ([§8](#the-positionhandler-contract)), clone-per-slot wiring in setup,
  the `onRelease` edge and steal re-init
  ([§10](#voice-lifecycle-release-tail-and-handler-end-of-life)/[§11](#voice-stealing-interaction)),
  and the `staticHandler`/`spreadHandler`/`orbitHandler` built-ins.
- **[#171][gh-171] — C-API: Per-note positioning surface.** Mirror
  [§14](#public-api-surface): `yse_synth_position_handler_<builtin>`,
  `yse_synth_handler_param`, `yse_synth_note_position`; built-in handlers
  only.

The `HANDLER_PARAM` / `NOTE_POSITION` message ops
([§9](#message-op-set-additions)) extend the synth inbox and land with
[#170][gh-170]. The extracted `panner`, per the epic's downstream note,
warrants its own short doc page once [#169][gh-169] lands.

---

## Cross-references

This document is the contract for the implementation phase of
[#147][gh-147]. Current-engine anchors it builds on (paths as of writing):

- `sound::create(dspSourceObject&, …)` / `create(synth&, …)` —
  `YseEngine/sound/soundInterface.cpp` (the Route 1 overload and the
  existing synth attachment).
- Spatialization math + per-sound smoothing state —
  `YseEngine/sound/soundImplementation.h` (the `compute*` / `gainAccumulate`
  static helpers, `lastGain`, `finalGainCache`, `gainDirty`) and
  `soundImplementation.cpp` `update()` / `dsp()` / `toChannels()`.
- Sound-manager lifecycle (slow-pool setup/delete, `OBJECT_*` fences,
  per-sound channel connect) — `YseEngine/sound/soundManager.cpp`.
- Listener singleton — `YseEngine/implementations/listenerImplementation.h`
  (`newPos`, `aPos forward`, `aPos vel`).
- `Pos` — `YseEngine/utils/vector.hpp` / `utils/atomicPos.h`.
- Synth voice model, aggregate `outputSource`, allocator/stealing, inbox,
  manager, `clone()` prototype — [docs/design/synth_core.md][doc-synth]
  ([#151][gh-151]).
- `lfQueue`, `managerSetupJob`/`managerDeleteJob`, `SOUND_STATUS`,
  `OBJECT_IMPLEMENTATION_STATE` — as catalogued in
  [synth_core.md §14][doc-synth].

Spatialization review issues the extracted math must preserve:
[#202][gh-202] (antipodal NaN), #204 (relative pan mirror), #205
(virtualization metric), #206 (virtual fade), #207 (density
compensation), #208 (multiplicative doppler), #209 (occlusion off the
audio thread), #210 (zenith flyover), #212 (gain cache), #213 (fused MAC).

<!-- link references -->
[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-147]: https://github.com/yvanvds/yse-soundengine/issues/147
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-154]: https://github.com/yvanvds/yse-soundengine/issues/154
[gh-157]: https://github.com/yvanvds/yse-soundengine/issues/157
[gh-168]: https://github.com/yvanvds/yse-soundengine/issues/168
[gh-169]: https://github.com/yvanvds/yse-soundengine/issues/169
[gh-170]: https://github.com/yvanvds/yse-soundengine/issues/170
[gh-171]: https://github.com/yvanvds/yse-soundengine/issues/171
[gh-181]: https://github.com/yvanvds/yse-soundengine/issues/181
[gh-185]: https://github.com/yvanvds/yse-soundengine/issues/185
[gh-201]: https://github.com/yvanvds/yse-soundengine/issues/201
[gh-202]: https://github.com/yvanvds/yse-soundengine/issues/202
[gh-215]: https://github.com/yvanvds/yse-soundengine/issues/215
[gh-226]: https://github.com/yvanvds/yse-soundengine/issues/226
[gh-283]: https://github.com/yvanvds/yse-soundengine/issues/283
[gh-290]: https://github.com/yvanvds/yse-soundengine/issues/290
[doc-synth]: synth_core.md
[doc-graph]: patcher_graphstate.md
[src-create]: ../../YseEngine/sound/soundInterface.cpp
[src-mgr]: ../../YseEngine/sound/soundManager.cpp
[src-occl]: ../../YseEngine/sound/soundImplementation.cpp
[src-lastgain]: ../../YseEngine/sound/soundImplementation.h
