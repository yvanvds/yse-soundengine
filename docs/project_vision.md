# Project vision

Status: living document. Filed as [#324][gh-324] after the 2026-07-07
design discussion around the [#164][gh-164]/[#168][gh-168]/[#172][gh-172]
design gates. This is the orientation every design issue can assume
without re-deriving it; when a scoping decision needs a tie-break, break
it against this document.

## What YSE is — and what it is not

YSE began as a 3D game-audio engine, and much of its machinery
(spatialization, occlusion, virtualization, localized reverb) carries
that heritage. It is also growing DAW-grade machinery: channel insert
chains, send/return buses, synth voices, an SFZ sampler, MIDI.

**Neither heritage is the identity.** YSE is not a game-audio engine,
and it is not a DAW engine. It is the engine for a new kind of
instrument: a performance environment for **experimental electronic
music**, combining Pd-style patchers, live coding, instruments, and MIDI
tracks — used live, to build performances — with a visualization layer
where game elements (physics engines, swarms, particle systems) control
the trajectories and behaviour of sounds.

The one-sentence stance:

> **YSE is an authored signal graph, played by spatial and physical
> controllers.**

In a game engine, the world is authoritative and audio *renders* it — a
simulation stance. In a DAW, the mix graph is authoritative and
everything is authored — a composition stance. YSE is the third thing:
there is exactly **one audio topology** — channels, inserts, sends,
returns, instruments — and the 3D layer **owns no audio paths at all**.
It drives parameters. Space is a modulation domain on equal footing with
LFOs, envelopes, and MIDI. A physics engine is a sequencer with
different math. A swarm is a polyphonic LFO over positions. The listener
is the mix perspective.

## Space as a signal

`Pos` is a first-class signal, in both directions:

- **Write direction — things drive positions.** Position handlers
  ([docs/design/per_note_positioning.md](design/per_note_positioning.md)),
  physics bodies, patcher outlets, live-coded expressions: anything can
  steer where a sound or a note lives.
- **Read direction — positions drive things.** Distances, angles, zone
  crossings, and collisions are control signals and triggers: a particle
  crossing a plane fires a note (a step sequencer whose steps are
  *places*); the distance between two swarm centroids opens a filter; a
  collision becomes a percussion hit with velocity from impact force.

Closing the loop — positions triggering notes whose handlers move
positions — is generative-system territory: the score stops being a
timeline and becomes **a space with behaviours**. That, more than any
single feature, is what makes YSE a new kind of engine rather than glue
between two old kinds.

## How the heritage maps forward

The game-audio features are not removed — they are re-read as
instances of "spatial modulation of the authored graph", and kept
exactly as useful as they are:

| Heritage feature | Vision form |
|---|---|
| Localized / zone reverb (`REVERB::Manager`, in-place parameter blend) | Two tools. (1) A **morphing reverb module**: a reverb whose preset interpolation is a *control input* — listener proximity is the default binding, but a slider, patcher outlet, or live-coded ramp can drive the same morph. (2) **Zone-bound return buses**: a reverb zone becomes a return bus tied to a region of space, with proximity modulating the *send levels* into it ([send_return_buses.md §12b](design/send_return_buses.md)). The legacy in-place path is kept until then but must not grow features. |
| Underwater effect (hard-wired in the channel path) | An ordinary channel **insert** whose parameters carry a spatial binding (listener depth in a medium volume). The special case in `channelImplementation::dsp()` eventually disappears. |
| One-shot world sounds | **Notes, through instruments.** A one-shot enters the world as a sampler/synth note, positioned by handlers or `notePosition()`, varied by velocity layers and round-robin ([sfz_opcode_subset.md](design/sfz_opcode_subset.md)). A game-style "emitter" wrapper over pooled sounds may exist someday as a convenience; it is not architecture. |
| `YSE::sound` | Narrows to what it is genuinely good at: **streaming and long-form material** (beds, stems, ambiences), and the **carrier** that attaches DSP sources, patchers, and synths to the graph. |
| Occlusion, virtualization | Optional per-sound **citizenship features**, available to whoever wants them — not the model everything else must aspire to. Per-note they are *wrong at that granularity* (a raycast per note per tick; note-rate churn in a global budget), which is why the synth keeps them per-aggregate. |
| Doppler | For file playback, resampling (as today). For synth voices, a **pitch-ratio input the voice applies to its oscillator** — what doppler *should* mean for synthesis. |
| The listener | The mix perspective — the point the audience hears from. One listener; `relative()` sounds bypass it. |

## Where this is heading (named, not yet scoped)

The direction the pieces converge on: **engine parameters become
addressable.** Send levels, reverb morph position, handler parameters,
positions — one address space that MIDI tracks, patcher cables
(`gSend`/`gReceive` already exist), live-coded expressions, and physics
all write to, at control rate, through the existing bounded-inbox
discipline. The early instances are already specified: `HANDLER_PARAM`
([per_note_positioning.md §9](design/per_note_positioning.md)) and
ramped send levels ([send_return_buses.md §11](design/send_return_buses.md))
are both designed for control-rate external writers.

This is deliberately a **future design pass**, not a commitment made
here — it needs its own gate (namespacing, binding lifetime when targets
die, C-API exposure). Until then, new designs should simply avoid
closing the door: any user-facing scalar parameter should be writable
through a bounded message at control rate without allocation.

Alongside it, the natural spatial vocabulary for patchers (`pos-of`,
`distance`, `zone-cross`, …) turns the read direction of
[Space as a signal](#space-as-a-signal) into patchable objects. Same
rule: future pass, doors open.

## Design-review stance

The 2026-07 review of the three design gates settled a working stance
for all future scoping decisions, recorded here so it does not have to
be re-argued:

1. **Effort is not an argument.** "It would be much more work" carries
   no weight in a project where implementation is cheap. What carries
   weight: RT-safety, correctness at the chosen granularity, and whether
   a cut is intrinsic to the problem domain.
2. **Earlier decisions are context, never verdicts.** A prior design
   doc is part of an argument, not a trump card. If a new fork shows the
   earlier decision is no longer the best one, the earlier decision is
   what gets reopened.
3. **Don't elevate engine conveniences to domain semantics.** The
   canonical failure: cutting SFZ layering because the synth core
   allocates one voice per note. The format's semantics, the mixing
   tradition, or the performance practice being modelled decide what a
   feature *means*; the engine adapts.
4. **The real-time bar is absolute.** Glitch-free live performance is
   the hard requirement everything else bends around: no allocation, no
   locks, no blocking I/O on the audio path — ever ([CLAUDE.md](../CLAUDE.md)).
5. **State ceilings honestly.** When a v1 accepts a quality or fidelity
   ceiling (resampler aliasing, per-aggregate reverb), the design doc
   says so and names the successor, so the ceiling is a decision rather
   than a discovery.

## Cross-references

- [docs/design/send_return_buses.md](design/send_return_buses.md) — the
  routing fabric ([#164][gh-164]); its §12b carries the spatial-reverb
  trajectory.
- [docs/design/per_note_positioning.md](design/per_note_positioning.md) —
  space as a per-note modulation target ([#168][gh-168]).
- [docs/design/sfz_opcode_subset.md](design/sfz_opcode_subset.md) — the
  sampler contract ([#172][gh-172]).
- [docs/design/synth_core.md](design/synth_core.md) — the voice model
  ([#151][gh-151]).
- [docs/design/live_coding_dsl.md](design/live_coding_dsl.md) — the
  live-coding surface ([#120][gh-120]).
- [docs/design/patcher_graphstate.md](design/patcher_graphstate.md) —
  the patcher's audio-thread ownership model ([#226][gh-226]).

[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-164]: https://github.com/yvanvds/yse-soundengine/issues/164
[gh-168]: https://github.com/yvanvds/yse-soundengine/issues/168
[gh-172]: https://github.com/yvanvds/yse-soundengine/issues/172
[gh-226]: https://github.com/yvanvds/yse-soundengine/issues/226
[gh-324]: https://github.com/yvanvds/yse-soundengine/issues/324
