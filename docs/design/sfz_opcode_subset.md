# SFZ v1 opcode subset specification

Status: **design, pre-implementation**. Tracking issue: [#172][gh-172].
Parent epic: [#148 — Instruments: SFZ sampler, virtual-analog/wavetable,
and FM/DX7 voices][gh-148]. Blocked-by (now satisfied): [#151][gh-151] —
the synth-core voice contract, fixed in
[docs/design/synth_core.md][doc-synth].

This document is the single source of truth for **which SFZ opcodes the
YSE sampler implements** and exactly what each one means in this engine.
It is written *before* any parser or voice code so the two implementing
sub-issues — [#173][gh-173] (SFZ parser + region model) and
[#174][gh-174] (sampler voice) — can each treat it as a fixed contract
and build against it without re-litigating scope.

A sampler is the one instrument whose scope creeps without bound: SFZ has
hundreds of opcodes across v1, v2 and the ARIA extensions. The purpose of
this document is to **draw the line once**, in writing, with a rationale
per decision, so the parser's data model and the voice's RT-ready region
table are both pinned before either is coded.

Design decisions already fixed by the epic ([#148][gh-148]) and carried
in unchanged:

- **SFZ-first.** The sampler is designed around a chosen SFZ v1 opcode
  subset from day one. SFZ is an open, royalty-free text format
  (specification at [sfzformat.com][sfz-spec]).
- **sfizz is the semantic reference, not a dependency.** Where this spec
  is silent on an edge case, the tie-breaker is "what does
  [sfizz][sfizz] (BSD-2-licensed) do?". We read sfizz for behaviour; we
  do **not** link, vendor, or copy it.
- **All parsing is offline.** The parser runs on the setup / slow-pool
  thread only and produces RT-ready structures. The audio thread never
  parses text, opens files, or allocates.
- **Full preload is the v1 default.** Every region's sample is loaded
  fully into RAM at setup; there is no disk streaming for the sampler in
  v1. See [§9](#preload-policy).

This follows the design-issue-first pattern proven by
[synth_core.md][doc-synth] ([#151][gh-151]) and
[live_coding_dsl.md][doc-dsl] ([#120][gh-120]).

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [How this builds on the synth core](#how-this-builds-on-the-synth-core)
3. [File model: headers and inheritance](#file-model-headers-and-inheritance)
4. [The opcode subset (normative table)](#the-opcode-subset-normative-table)
5. [Region selection](#region-selection)
6. [Pitch and tuning](#pitch-and-tuning)
7. [Loop handling](#loop-handling)
8. [The amplitude EG (`ampeg_*`)](#the-amplitude-eg-ampeg_)
9. [Preload policy](#preload-policy)
10. [Buffer sharing and the engine loaders](#buffer-sharing-and-the-engine-loaders)
11. [The `samplerConfig` convenience facade](#the-samplerconfig-convenience-facade)
12. [Unknown-opcode and error policy](#unknown-opcode-and-error-policy)
13. [The RT-ready region model](#the-rt-ready-region-model)
14. [Validation strategy](#validation-strategy)
15. [Explicitly out of scope](#explicitly-out-of-scope)
16. [Worked examples](#worked-examples)
17. [Cross-references](#cross-references)

---

## Goals and non-goals

### Goals

- **A bounded, written v1 subset.** Exactly the opcodes in
  [§4](#the-opcode-subset-normative-table) are implemented. Everything
  else is skipped-and-logged ([§12](#unknown-opcode-and-error-policy)),
  never a parse failure. The subset is deliberately small: enough to load
  the common multisampled-instrument idiom (key/velocity mapping, root
  key, loop, amplitude envelope), nothing more.
- **A `dspVoice` subclass, nothing structural.** The sampler is a
  `SYNTH::samplerVoice : public SYNTH::dspVoice`
  ([synth_core.md §3][doc-synth]). It gets polyphony, allocation,
  stealing, keyboard/pedal state and lifecycle from the synth core for
  free. This document specifies only *what one sampler voice does with a
  note* and *how a parsed SFZ instrument feeds it*.
- **Offline parse → RT-ready region table.** The parser
  ([#173][gh-173]) turns SFZ text into an immutable, flattened
  `sfzInstrument` (a vector of resolved regions plus shared sample
  buffers). The voice ([#174][gh-174]) does an allocation-free lookup
  into that table on NOTE_ON and never sees the text.
- **Reuse the existing sample machinery.** Sample data lives in the
  engine's existing `DSP::fileBuffer` / `INTERNAL::abstractSoundFile`
  (libsndfile-backed), pitch-shifted with the established 4-point cubic
  interpolator (`DSP::interpolate4`). No new file backend, no new
  resampler.

### Non-goals (explicit)

Inherited from the epic and reaffirmed here; not added under these names
without a new design pass:

- **No SFZ v2 / ARIA extensions**, no scripting/`v2` opcodes, no XML
  `<control>` macros beyond `default_path`.
- **No SoundFont 2 (`.sf2`) import.** SFZ only.
- **No disk streaming for the sampler in v1.** Full preload only
  ([§9](#preload-policy)). The epic reserves the right to flip this
  default in a later design pass; until then, streaming is out.
- **No filters, no LFOs, no per-region effects, no pitch/filter EGs.**
  Only the *amplitude* EG (`ampeg_*`) is in v1
  ([§4](#the-opcode-subset-normative-table)). `fil_*`, `cutoff`,
  `lfoN_*`, `pitcheg_*`, `fileg_*`, effect sends → out.
- **No round-robin, no crossfades, no key/CC switching.**
  `seq_position`/`seq_length`, `xfin_*`/`xfout_*`, `sw_*`, `on_locc*`
  → out, each with rationale in [§4](#the-opcode-subset-normative-table).
- **No per-note 3D positioning.** Inherited from the synth core: one
  synth = one `YSE::sound` = one position ([synth_core.md §1][doc-synth]).

---

## How this builds on the synth core

The sampler is not a new subsystem. It is a voice type plugged into the
existing synth core. Concretely:

| Synth-core contract ([synth_core.md][doc-synth]) | What the sampler supplies |
|---|---|
| `dspVoice` subclass with `process(SOUND_STATUS&)` + `clone()` (§3) | `SYNTH::samplerVoice` — reads the shared region table, renders one region per note |
| `addVoices(prototype, n, channel, low, high)` (§4, §12) | An SFZ instrument becomes one voice group; the prototype carries a shared-pointer to the parsed `sfzInstrument` |
| Allocation & stealing (§4) | Unchanged. The sampler adds nothing to polyphony; it is just a heavier `process()` |
| `frequency(midiNote)` / `getVelocity()` atomics (§3) | The voice reads the delivered MIDI note + velocity to pick and pitch a region |
| `intent` gate: `SS_WANTSTOPLAY` → attack, `SS_WANTSTOSTOP` → release, settle `SS_STOPPED` (§3) | Drives the `ampeg_*` envelope and, for `loop_mode=one_shot`, sample-end → `SS_STOPPED` |
| Everything is allocated in `clone()` on the slow pool (§3, §8) | The voice's scratch buffers and interpolator are sized in `clone()`; the *sample data* is loaded at parse time and shared (see [§10](#buffer-sharing-and-the-engine-loaders)) |

Key consequence: **NOTE_ON must resolve a region without allocating.**
The voice receives the MIDI note and velocity through the base's atomics
(`getFrequency()` gives Hz; the voice also needs the raw note number and
velocity 0–127 space for `hikey`/`hivel` comparisons — see
[§13](#the-rt-ready-region-model) for how the note number is carried),
and looks it up in the pre-flattened region table. The table layout in
[§13](#the-rt-ready-region-model) makes that lookup a bounded linear scan
over a small array.

There is exactly **one active region per voice per note** in v1. Because
round-robin and layering-within-an-instrument are out
([§4](#the-opcode-subset-normative-table)), a note selects a single
region; if several regions match, the tie-break rule in
[§5](#region-selection) picks one deterministically. (Layering two
*timbres* is still available the synth-core way: add two voice groups.)

---

## File model: headers and inheritance

An SFZ file is a flat list of `<header>` lines, each followed by
`opcode=value` assignments until the next header. The subset recognises
exactly these headers:

| Header | In v1? | Meaning in this subset |
|---|---|---|
| `<region>` | **IN** | One playable mapping. The only header that produces a region. |
| `<group>` | **IN** | A set of default opcodes inherited by every `<region>` that follows it, until the next `<group>`. |
| `<global>` | **IN** | Defaults inherited by *every* region in the file. At most one; if repeated, last wins (logged). |
| `<control>` | **partial** | Only `default_path` is read from it (see below). All other control opcodes skipped-and-logged. |
| `<master>` | **OUT** | An ARIA layer between `<global>` and `<group>`. Folded away: if present, its opcodes are treated as an extra inheritance layer *only if trivially supported*; in v1 we **skip `<master>` headers and log**, and its opcodes attach to nothing. Rationale: `<master>` is not part of SFZ v1; supporting it means a 4-level inheritance chain for marginal benefit. Files that rely on `<master>` for mapping will mis-map — an acceptable v1 limitation, surfaced by the parse log. |
| `<curve>`, `<effect>`, `<midi>`, `<sample>` (embedded) | **OUT** | Skipped-and-logged. Curves, effects, embedded base64 samples are v2/ARIA. |

### Opcode inheritance (the only merge rule)

Resolution is a simple three-level override chain, innermost wins:

```
effective(region) = <global> opcodes
                    ⊕ enclosing <group> opcodes
                    ⊕ the <region>'s own opcodes
```

where `⊕` means "later layer overrides earlier for any opcode it sets".
This is computed **once, at parse time**, and each resulting region is
stored fully flattened — the voice never walks a hierarchy. A `<group>`
scopes to the regions textually between it and the next `<group>`
(standard SFZ semantics). `<global>` applies file-wide regardless of
position.

### `default_path`

`default_path` (read from `<control>`, or tolerated anywhere for
leniency) is prepended to every relative `sample=` path. Paths are
resolved **at parse time** relative to `default_path`, which is itself
relative to the `.sfz` file's directory. Backslashes in `sample=` /
`default_path` are normalised to the platform separator (SFZ files from
Windows tools use `\`). Absolute `sample=` paths ignore `default_path`.

### `sample`

`sample=<path>` is mandatory for a region to be playable. A `<region>`
with no resolvable `sample` (missing opcode, or file not found) is
**dropped with a logged warning**; it never enters the region table.
The reserved value `sample=*silence` (an ARIA convention some v1 files
use) maps to a region that produces silence for its mapped range —
**supported**, because it is a cheap and common way to "mute" a key
range, and it needs no file.

---

## The opcode subset (normative table)

This is the contract. **IN** = implemented; **OUT** = skipped-and-logged
([§12](#unknown-opcode-and-error-policy)). Anything not in this table is
OUT by default.

### Structural / routing

| Opcode | Status | Rationale / notes |
|---|---|---|
| `sample` | **IN** | Path to the audio file (relative to `default_path`). Mandatory per region. `*silence` supported. |
| `default_path` | **IN** | Prefix for relative sample paths. Parse-time resolution. |
| (headers `<region>`/`<group>`/`<global>`) | **IN** | See [§3](#file-model-headers-and-inheritance). |

### Key / velocity mapping

| Opcode | Status | Default | Rationale / notes |
|---|---|---|---|
| `lokey` | **IN** | `0` | Low end of the key range. |
| `hikey` | **IN** | `127` | High end of the key range. |
| `key` | **IN** | — | Shorthand: sets `lokey = hikey = pitch_keycenter = key`. |
| `pitch_keycenter` | **IN** | `60` | The note at which the sample plays at its recorded pitch. |
| `lovel` | **IN** | `1` | Low end of the velocity range (SFZ uses 1..127). |
| `hivel` | **IN** | `127` | High end of the velocity range. Together these give velocity layers. |
| `lorand` / `hirand` | **OUT** | — | Random layering; ties into round-robin, which is out. |
| `lochan` / `hichan` | **OUT** | — | Channel filtering is done by the synth-core voice *group* (`addVoices` channel arg), not per region. Rationale: avoids two competing channel filters. |
| `lobend`/`hibend`, `locc`/`hicc`, `sw_*` | **OUT** | — | Bend/CC/key-switch region gating are v2-flavoured expression features; out for v1. |

Key/velocity names accept both note numbers (`0`–`127`) and note names
(`c4`, `f#3`) per the SFZ spec; the parser normalises names to numbers.
**Middle-C convention: `c4 = 60`** (the sfizz/SFZ default), fixed here so
note-name parsing is unambiguous.

### Pitch / tuning

| Opcode | Status | Default | Rationale / notes |
|---|---|---|---|
| `pitch_keycenter` | **IN** | `60` | (Listed above; the anchor for pitch tracking.) |
| `tune` (a.k.a. `pitch`) | **IN** | `0` | Fine tune in cents, ±100. |
| `transpose` | **IN** | `0` | Coarse tune in semitones. |
| `pitch_keytrack` | **IN** | `100` | Cents of pitch change per key. `100` = normal chromatic tracking; `0` = fixed pitch (every key plays the sample untransposed). Only `0` and `100` are guaranteed exact in v1; other values interpolate linearly. |
| `bend_up` / `bend_down` | **OUT** | — | Per-region bend range; the synth core already delivers a normalised pitch-wheel to the voice ([synth_core.md §5][doc-synth]). The sampler voice applies a fixed ±2 semitone default bend. |

### Loop / playback position

| Opcode | Status | Default | Rationale / notes |
|---|---|---|---|
| `loop_mode` | **IN** | see [§7](#loop-handling) | `no_loop`, `one_shot`, `loop_continuous`, `loop_sustain`. |
| `loop_start` | **IN** | file loop marker, else `0` | Loop start frame. |
| `loop_end` | **IN** | file loop marker, else last frame | Loop end frame (inclusive per SFZ). |
| `offset` | **IN** | `0` | Start playback this many frames into the sample. |
| `end` | **IN** | last frame | Last playable frame; `end=-1` disables the region (SFZ convention) — treated as a dropped region. |
| `count` | **OUT** | — | Fixed loop-repeat count; niche, out for v1. |
| `offset_random`, `sync_beats`, etc. | **OUT** | — | Randomised/temporal offsets; out. |

### Amplitude

| Opcode | Status | Default | Rationale / notes |
|---|---|---|---|
| `volume` | **IN** | `0` | Region gain in dB, applied to output. |
| `pan` | **IN** | `0` | −100..100. Applied only when the voice group renders ≥2 channels; mono synths ignore it (logged once). |
| `amp_veltrack` | **IN** | `100` | Percent of velocity→amplitude tracking. `100` = full velocity dynamics (velocity 127 = 0 dB, lower velocities attenuate per the SFZ curve); `0` = velocity ignored for amplitude. **IN** because it is the single most audible expressive opcode and trivial to apply as a gain — omitting it makes every multisample play flat. |
| `amp_keytrack`, `amp_keycenter` | **OUT** | — | Key-dependent gain; niche, out for v1. |
| `xfin_lovel`/`xfin_hivel`/`xfout_lovel`/`xfout_hivel` | **OUT** | — | Velocity **crossfades**. Rationale: crossfading requires *two regions sounding at once per note*, which breaks the "one region per voice per note" invariant ([§2](#how-this-builds-on-the-synth-core)) and doubles per-note voice cost. Hard velocity splits (`lovel`/`hivel`) cover the common case; smooth velocity crossfades are a deliberate v2-era feature. |
| `xfin_lokey`/`…`/`xf_keycurve`, `xfin_locc*` | **OUT** | — | Same reasoning as velocity crossfades. |

### Envelope — amplitude EG only

| Opcode | Status | Default | Rationale / notes |
|---|---|---|---|
| `ampeg_delay` | **IN** | `0` | Seconds of silence before attack. Cheap, and DX-style pads use it. |
| `ampeg_attack` | **IN** | `0` | Attack time, seconds. |
| `ampeg_hold` | **IN** | `0` | Hold at peak after attack, seconds. |
| `ampeg_decay` | **IN** | `0` | Decay time to sustain, seconds. |
| `ampeg_sustain` | **IN** | `100` | Sustain level, **percent** (SFZ convention), 0..100. |
| `ampeg_release` | **IN** | `0` (see note) | Release time, seconds. A tiny floor (~5 ms) is enforced to avoid a click at note-off even when the file says `0`. |
| `ampeg_*_onccN`, `ampeg_vel2*` | **OUT** | — | CC- and velocity-modulated envelope segments; out. Fixed envelope only in v1. |
| `fileg_*`, `pitcheg_*` | **OUT** | — | Filter/pitch envelopes; no filter in v1, and pitch EG is out of scope. |

The full DAHDSR set (`delay`, `attack`, `hold`, `decay`, `sustain`,
`release`) is **IN** — it maps one-for-one onto the engine's existing
`DSP::ADSRenvelope` breakpoint model ([§8](#the-amplitude-eg-ampeg_)), so
supporting the whole set costs nothing beyond building the breakpoints.

### Groups / choke

| Opcode | Status | Rationale / notes |
|---|---|---|
| `group` (a.k.a. `polyphony_group`) | **OUT** | Choke groups (`group`/`off_by`). Rationale below. |
| `off_by` | **OUT** | See below. |
| `off_mode` | **OUT** | See below. |
| `seq_position` / `seq_length` | **OUT** | **Round-robin.** Rationale below. |

**Round-robin (`seq_position`/`seq_length`) — OUT.** Round-robin cycles
several samples across successive hits of the same key. It requires
per-key hit-counter *state that persists across voices* and lives in the
instrument, not the voice — which the synth-core voice model has no place
for (voices are stateless between notes; the allocator owns cross-note
state). Adding it means threading a mutable per-region counter through
the shared, otherwise-immutable region table. Deferred as a named future
enhancement, not smuggled in.

**Choke groups (`group`/`off_by`/`off_mode`) — OUT.** "Playing a region
in group N silences sounding regions whose `off_by = N`" (the classic
hi-hat open/closed choke) is cross-voice behaviour: it must reach *into
the allocator* to stop another voice. The synth core's stealing/lifecycle
path ([synth_core.md §4][doc-synth]) has no choke primitive. Wiring one
in is a synth-core change, out of scope for the sampler's first cut.
Deferred as a named enhancement.

Both are recorded here explicitly (rather than silently dropped) because
they are the two features most likely to be requested next; pinning them
as *known, deliberate omissions with a clear reason* is the point of this
table.

---

## Region selection

Given a NOTE_ON `(note, velocity)` — where `note` is the MIDI note
number 0–127 and `velocity` is remapped to SFZ's 1–127 space — a region
**matches** iff:

```
region.lokey <= note     <= region.hikey
region.lovel <= velocity <= region.hivel
```

The synth-core *group* has already filtered by MIDI channel and by the
group's own `[low, high]` key window before the voice is allocated, so
region selection does **not** re-check channel.

### Tie-break (deterministic, since v1 has no layering)

If more than one region matches (overlapping key/velocity zones — legal
and common in hand-written SFZ), v1 plays **exactly one**, chosen by:

1. **Narrowest velocity range wins** (`hivel - lovel` smallest) — a
   tightly-targeted velocity layer beats a catch-all.
2. Tie → **narrowest key range wins**.
3. Tie → **last region in file order wins** (SFZ's "later overrides"
   spirit).

Rationale: without round-robin or crossfades there is no defined way to
sound two matching regions per note, so a *stable, documented* single
choice is better than an arbitrary one. This rule is the primary target
of the golden-file tests ([§14](#validation-strategy)): "for note N,
velocity V, which region sounds?" must match sfizz's *primary* region for
the non-overlapping case, and match *this rule* for the overlapping case
(where sfizz would layer — a documented divergence).

If **no** region matches, the voice produces silence and immediately
settles `SS_STOPPED` (the note is dropped cleanly, no stuck voice).

---

## Pitch and tuning

A matched region plays its sample resampled so the recorded pitch tracks
the played note. The playback speed (ratio) fed to the interpolator is:

```
semitoneOffset = (note - pitch_keycenter) * (pitch_keytrack / 100)
                 + transpose
cents          = tune
speed          = 2^( (semitoneOffset + cents/100) / 12 )
                 * (fileSampleRate / deviceSampleRate)
```

- `note - pitch_keycenter` is the chromatic distance from the sample's
  root; `pitch_keytrack/100` scales it (`100` = normal, `0` = fixed).
- `transpose` shifts coarse semitones; `tune` shifts fine cents.
- The trailing `fileSampleRate / deviceSampleRate` term is the same
  sample-rate correction the engine's file playback already applies
  (`_sampleRateAdjustment` in `abstractSoundFile`).

Resampling uses the engine's existing **4-point cubic interpolator**
(`DSP::interpolate4`) — the established precedent for fractional-position
buffer reads and re-pitching. No new resampler is introduced. A voice
owns one `interpolate4` instance, sized in `clone()`.

Pitch-wheel: the synth core delivers a normalised wheel position in
`[-1,1]` to the voice ([synth_core.md §5][doc-synth]); the sampler
applies a fixed **±2 semitone** default range on top of `speed`. This is
not configurable per region in v1 (`bend_up`/`bend_down` are OUT).

---

## Loop handling

`loop_mode` selects one of four behaviours. Loop points default to the
sample file's embedded loop markers (libsndfile exposes these) when the
opcodes are absent; `loop_start` / `loop_end` override them. Points are
in frames; `loop_end` is inclusive (SFZ convention).

| `loop_mode` | Behaviour |
|---|---|
| `no_loop` | Play once from `offset` to `end` (or file end), then the amp EG release governs the tail. Default when the file has **no** loop markers. |
| `one_shot` | Play once start→end **ignoring note-off** — the note plays to completion regardless of key release, then settles `SS_STOPPED`. Used for drums/percussion. Note-off does *not* trigger the EG release. |
| `loop_continuous` | Loop `[loop_start, loop_end]` indefinitely while the note is held *and* through the release tail, until the amp EG finishes. Default when the file **has** loop markers. |
| `loop_sustain` | Loop `[loop_start, loop_end]` only while the note is held (EG in sustain); once note-off enters the EG release phase, play through `loop_end` to the sample end (do not loop during release). |

Implementation notes:

- Looping is done in the voice's read loop against the shared sample
  buffer, wrapping the fractional read position across the loop boundary
  (the wrap must be interpolation-safe — keep the 4 taps the cubic
  interpolator needs valid across the seam; guard-samples or an explicit
  boundary check, mirroring the wrap fix landed for the chorus delay
  line).
- `one_shot` is the one mode where the **note ignores `SS_WANTSTOSTOP`
  for sample advancement** but still lets the master/steal declick fade
  apply — the engine-owned steal fade ([synth_core.md §4][doc-synth])
  still guarantees a click-free steal even mid-one-shot.
- Loop points out of range (past sample end, or `loop_start >=
  loop_end`) are clamped and logged; the region still plays as
  `no_loop`.

---

## The amplitude EG (`ampeg_*`)

The `ampeg_*` opcodes map directly onto the engine's existing
`DSP::ADSRenvelope`, which is a breakpoint envelope with an optional
sustain-loop region (`loopStart`/`loopEnd` breakpoints) and `ATTACK` /
`RESUME` / `RELEASE` playback phases driven by the note gate. The parser
builds the breakpoint list once (at setup) and the voice drives it from
the `intent` gate:

| Phase | Breakpoints built from | Driven by |
|---|---|---|
| delay | `ampeg_delay` s at value 0 | `SS_WANTSTOPLAY` |
| attack | ramp 0 → 1 over `ampeg_attack` s | `ATTACK` |
| hold | `ampeg_hold` s at value 1 | `ATTACK` |
| decay | ramp 1 → `ampeg_sustain/100` over `ampeg_decay` s | `ATTACK` |
| sustain | the `loopStart`/`loopEnd` sustain region at `ampeg_sustain/100` | `RESUME` while held |
| release | ramp current → 0 over `ampeg_release` s | `RELEASE` on `SS_WANTSTOSTOP` |

- `ampeg_sustain` is a **percent** (0..100) in SFZ; divide by 100 for the
  linear envelope value.
- A **~5 ms floor** is applied to `ampeg_release` even when the file
  specifies 0, so a released note never hard-cuts and clicks. (This is
  the voice's *own* envelope release; it is independent of the engine's
  steal-declick fade, which handles the exhausted-polyphony case.)
- When the release finishes (`ADSRenvelope::isAtEnd()`), the voice
  settles its `intent` to `SS_STOPPED` so the allocator reclaims the slot
  — exactly the `dspVoice` contract from
  [synth_core.md §3][doc-synth].
- The envelope multiplies the resampled, `volume`- and
  `amp_veltrack`-scaled sample stream. Velocity scaling
  (`amp_veltrack`) is a per-note constant gain computed on NOTE_ON, not a
  per-sample modulation.

No filter EG, no pitch EG, no CC/velocity-modulated segments in v1 (all
OUT per [§4](#the-opcode-subset-normative-table)).

---

## Preload policy

**v1 loads every region's sample fully into RAM at parse/setup time.
There is no disk streaming for the sampler.** Rationale and consequences:

- **Real-time safety is structural.** With the whole sample resident, the
  voice's `process()` is a pure in-RAM interpolated read — no disk I/O,
  no streaming double-buffer state machine, no underrun handling on the
  audio thread. This is the same reason the synth core allocates
  everything in `clone()`.
- **Polyphony is cheap and correct.** Many voices can read the *same*
  resident buffer at *different* positions simultaneously (a struck chord
  across one velocity layer) with no per-voice file handle and no
  contention — see [§10](#buffer-sharing-and-the-engine-loaders).
- **Loading happens on the slow pool.** The parse job and the sample
  loads run on the single-threaded setup/slow pool (the same pool that
  clones voices and loads `sound` file buffers), off the audio thread. A
  sampler instrument is not playable until all its samples finish
  loading and it reaches `OBJECT_READY` — identical to how a file-backed
  `YSE::sound` is not playable until its buffer loads
  ([synth_core.md §8][doc-synth]).

### Memory budget guidance

Full preload trades RAM for RT-safety. Guidance (advisory, surfaced in
docs, not enforced by code in v1):

- Uncompressed cost ≈ `Σ_regions (uniqueFileFrames × channels × 4 bytes)`
  (engine buffers are 32-bit float). **Unique files, not regions** — many
  regions sharing one multisample file cost that file once
  ([§10](#buffer-sharing-and-the-engine-loaders)).
- A 60-key, 3-velocity-layer, stereo, 2 s-per-sample 44.1 kHz instrument
  is roughly `180 × 2 × 88200 × 4 B ≈ 127 MB` if every layer is a
  distinct 2 s file. This is the kind of number that motivates streaming;
  it is why the epic reserves the option to flip the default later.
- **The default may be revisited** ([#148][gh-148] non-goals) if a
  concrete large-library use case lands. The streaming machinery already
  exists in `abstractSoundFile` (front/back double-buffer, slow-pool
  refill), so flipping the default is a *wiring* change, not new
  infrastructure — but it is explicitly a **future** decision, not v1.

---

## Buffer sharing and the engine loaders

The sampler reuses the engine's existing file machinery rather than
inventing its own:

- **`INTERNAL::abstractSoundFile`** (libsndfile-backed) is the loader. It
  already exposes channel count, frame length, a `FILESTATE`
  (`NEW`/`LOADING`/`READY`/`INVALID`), and a slow-pool load job
  (`threadPoolJob::run()`), plus a `read(filebuffer, pos, length, speed,
  loop, intent, volume)` that does exactly the fractional-position,
  speed-scaled, optionally-looping read the sampler needs. The sampler's
  per-voice read path is modelled on this (or reuses it directly for the
  non-streaming case).
- **Shared, de-duplicated sample buffers.** Regions that name the same
  resolved file path share **one** loaded buffer. The parser builds a
  `path → shared sample buffer` map at setup; each region holds a
  (non-owning) reference into it. This is the whole reason the memory
  budget counts *unique files*: a 128-key multisample split from one WAV
  loads that WAV once. The shared buffers are **immutable after load**,
  so any number of voices read them concurrently on the audio thread
  without synchronisation.
- **`DSP::fileBuffer`** is the concrete resident buffer type (a
  `drawableBuffer` with `load(path, channel)`), suitable for the resident
  full-preload store. One `fileBuffer` per channel per unique file.
- Lifetime: the shared sample buffers are owned by the parsed
  `sfzInstrument`, which is owned (shared-pointer) by the voice-group
  prototype and every clone. They are freed on the slow pool when the
  synth is deleted, after the `OBJECT_DELETE_PENDING` fence guarantees the
  audio thread no longer reads them ([synth_core.md §8][doc-synth]).

No modification to `abstractSoundFile` / `fileBuffer` is *required* by
this spec; if the parser needs a "load fully, non-streaming, keep
resident, hand me the buffers" entry point that does not yet exist as a
clean call, that is a small additive helper for [#173][gh-173] to define,
not a change to the streaming state machine.

---

## The `samplerConfig` convenience facade

The epic keeps the old chainable `samplerConfig` descriptor (from the
deleted JUCE-era `synthInterface.hpp`) alive as a **convenience facade**:
a one-call way to build a single-sample instrument without writing an
`.sfz` file. It is defined as *sugar that generates a one-region SFZ
instrument internally* — it introduces no second code path; it emits the
same flattened `sfzInstrument` the parser produces.

The old descriptor's surface (recovered from history) and its mapping:

| `samplerConfig` method | Old default | Generated SFZ opcode(s) |
|---|---|---|
| `name(const char*)` | — | Instrument label only (not an SFZ opcode; kept for `removeSound`-style identification). |
| `file(const char*)` | — | `sample=<file>` (absolute, so `default_path` is irrelevant). |
| `channel(U8)` | `0` (omni) | **Not** an SFZ opcode — maps to the synth-core `addVoices(..., channel, ...)` argument. |
| `root(U8 rootNote)` | `60` | `pitch_keycenter=<rootNote>`. |
| `range(U8 low, U8 high)` | `0`–`127` | `lokey=<low>` `hikey=<high>` — and also the `addVoices(..., low, high)` window. |
| `envelope(Flt attack, Flt release, Flt maxLength)` | `0, 0.1, 10` | `ampeg_attack=<attack>` `ampeg_release=<release>`. `maxLength` caps one-shot length: with no loop, `end` is clamped to `maxLength` seconds; it does **not** map to a native SFZ opcode. |

Fixed choices for the generated region (documented so the facade's
behaviour is predictable):

- `loop_mode` defaults to `no_loop` (the descriptor never exposed
  looping), unless the file carries loop markers, in which case
  `loop_continuous` — matching the parser's file-marker default
  ([§7](#loop-handling)).
- `amp_veltrack` defaults to `100` (velocity is expressive by default),
  matching the SFZ default.
- The facade builds the instrument through the **same parse/flatten and
  slow-pool load** path as a real `.sfz` — so a `samplerConfig` sampler
  is playable exactly when its file finishes loading, and behaves
  identically to the equivalent hand-written one-region file.

Exact C++ surface (chainable, mirroring the old shape and the modern
synth API) is [#174][gh-174]'s to finalise; this table fixes the
*semantics* it must produce.

---

## Unknown-opcode and error policy

The parser is **lenient by default** — an SFZ file in the wild is
routinely full of opcodes we do not implement, and refusing to load it
would make the sampler useless. The policy:

| Situation | Action |
|---|---|
| Unknown / OUT opcode | **Skip and log** (debug/info level: file, line, opcode). Parsing continues. Never a failure. |
| Known opcode, malformed value | Skip that opcode, log a **warning**, apply the default. Region still loads. |
| Unknown header | Skip its whole block, log, continue. |
| `<region>` with no resolvable `sample` | **Drop that region**, log a warning. Other regions load. |
| File not found (a `sample` path) | Drop that region, log a warning. |
| `.sfz` file itself unreadable / empty / zero valid regions | The instrument fails to reach `OBJECT_READY`; `synth`/`sound` reports not-valid, logged as an error. This is the *only* hard failure. |

Logging uses the engine's existing log facility (off the audio thread —
all parsing is on the slow pool). The log is the primary user-facing
signal for "why did my instrument sound wrong": a mis-mapped `<master>`
file, an unsupported round-robin, a missing sample all surface there.

Rationale for skip-and-log over strict rejection: it matches every real
SFZ player (sfizz, ARIA, sforzando all ignore unknown opcodes), and it
lets the subset grow later without breaking files that already load.

---

## The RT-ready region model

The parser's output — consumed by the voice — is an immutable, flattened
structure with no text, no hierarchy, no allocation on read. Shape
(illustrative, not a final header; [#173][gh-173] owns the types):

```cpp
struct sfzRegion {
  // selection (integer compares on the audio thread)
  int   lokey, hikey;          // 0..127
  int   lovel, hivel;          // 1..127
  int   pitchKeycenter;        // 0..127
  // pitch
  float tuneCents;             // tune
  int   transposeSemis;        // transpose
  float pitchKeytrack;         // /100
  // playback
  int   sampleIndex;           // index into the instrument's shared buffer table
  long  offset, endFrame;      // frames
  int   loopMode;              // enum { NO_LOOP, ONE_SHOT, LOOP_CONTINUOUS, LOOP_SUSTAIN }
  long  loopStart, loopEnd;    // frames
  // amplitude
  float volumeDb;              // volume
  float pan;                   // -100..100
  float ampVeltrack;           // /100
  // amp EG (seconds; sustain 0..1)
  float egDelay, egAttack, egHold, egDecay, egSustain, egRelease;
};

struct sfzInstrument {
  std::vector<sfzRegion>              regions;   // flattened, group/global merged
  std::vector<sharedSampleBuffer>     samples;   // de-duped by path; immutable after load
  // ... + the resolved default_path, source file, parse log summary
};
```

- **One region per voice per note**: NOTE_ON scans `regions` for a match
  ([§5](#region-selection)), applies the tie-break, records the winning
  `sfzRegion*` (or a copy of its scalar fields) into the voice, arms the
  amp EG, and renders. The scan is bounded and allocation-free.
- The voice needs the **raw MIDI note number** (for `lokey`/`hikey`) and
  **velocity in 1..127** (for `lovel`/`hivel`) in addition to the Hz
  frequency the base already stores. The base `dspVoice` stores frequency
  as Hz via `frequency(midiNote)`; the sampler recovers the integer note
  from the delivered value (or the allocator delivers the raw note number
  alongside — a small extension the voice reads, defined by
  [#174][gh-174] within the synth-core atomics contract). Velocity is
  already delivered in `[0,1]` (`getVelocity()`) and remapped to 1..127
  for comparison.
- `samples` is shared and immutable, so it is safe for concurrent voices.

---

## Validation strategy

The subset is only meaningful if "which region sounds for note N /
velocity V" is *testable* against a reference. The strategy
([#173][gh-173]/[#174][gh-174] tests):

1. **Golden-file region-resolution tests.** A set of small `.sfz`
   fixtures (single region; key split; velocity split; overlapping zones;
   `key=` shorthand; `default_path`; missing sample; unknown opcodes)
   each paired with a table of `(note, velocity) → expected region
   index`. The parser resolves each and the test asserts the mapping.
   These are deterministic and need no audio device.
2. **Cross-check against sfizz.** For the *non-overlapping* fixtures, the
   expected table is generated by observing sfizz's region choice (sfizz
   as an offline oracle during test authoring — **not** a runtime
   dependency, not linked). For *overlapping* fixtures, the expected
   value follows **this document's tie-break rule**
   ([§5](#region-selection)), which is a documented divergence from
   sfizz's layering; the test encodes our rule, and a comment records the
   sfizz difference.
3. **Pitch/tuning unit tests.** Assert `speed` for representative
   `(note, pitch_keycenter, transpose, tune, pitch_keytrack)` tuples
   against the closed-form in [§6](#pitch-and-tuning) (rate-agnostic:
   factor out `fileSampleRate/deviceSampleRate` so the test is
   sample-rate independent, per the project's rate-agnostic test
   precedent).
4. **Loop-mode behaviour tests.** Drive a short synthetic buffer with a
   known loop region through each `loop_mode` and assert the read
   position sequence (loops while held, one-shot ignores note-off,
   sustain stops looping on release).
5. **Envelope mapping test.** Assert the `ampeg_*` → `ADSRenvelope`
   breakpoint construction (delay/attack/hold/decay/sustain/release
   times, sustain level, the ~5 ms release floor).
6. **Lenient-parse tests.** A file stuffed with OUT opcodes, a malformed
   value, an unknown header, and a missing-sample region must still load
   its valid regions and log the rest — assert region count and that no
   exception/failure occurs.

The region-resolution golden files are the **primary** contract test:
they pin [§5](#region-selection) so the parser and voice cannot silently
disagree on what plays.

---

## Explicitly out of scope

Consolidated for quick reference (each justified in
[§4](#the-opcode-subset-normative-table) or the non-goals):

- SFZ v2 / ARIA opcodes and headers (`<curve>`, `<effect>`, embedded
  `<sample>`, scripting).
- `<master>` header inheritance layer.
- Round-robin (`seq_position`/`seq_length`), random layers
  (`lorand`/`hirand`).
- Velocity/key **crossfades** (`xf*`).
- Choke / mute groups (`group`/`off_by`/`off_mode`).
- Filters (`cutoff`, `fil_*`), LFOs (`lfoN_*`), filter/pitch EGs
  (`fileg_*`, `pitcheg_*`), effect sends.
- Key/CC switching and region CC/bend gating (`sw_*`, `locc`/`hicc`,
  `lobend`/`hibend`, `on_locc*`).
- CC- and velocity-modulated envelope/pitch/amp segments (`*_onccN`,
  `*_vel2*`).
- Disk streaming for the sampler (full preload only in v1).
- SoundFont 2 (`.sf2`).

Round-robin and choke groups are the two most likely next enhancements
and are the ones written up with the most explicit rationale so a future
issue can pick them up with the trade-off already stated.

---

## Worked examples

### Example 1 — a minimal one-region instrument

```
<region>
sample=piano_C4.wav
pitch_keycenter=60
```

Plays `piano_C4.wav` across all keys, pitch-tracked from note 60; every
velocity; `no_loop`; instant attack/release (with the ~5 ms release
floor). Equivalent to `samplerConfig().file("piano_C4.wav").root(60)`.

### Example 2 — a key split with a shared default path

```
<control>
default_path=samples/

<group>
loop_mode=loop_continuous
ampeg_release=0.3

<region> sample=bass.wav  lokey=0   hikey=47  pitch_keycenter=36
<region> sample=lead.wav  lokey=48  hikey=127 pitch_keycenter=72
```

Two regions inheriting the group's loop mode and release. `bass.wav`
resolves to `samples/bass.wav`. A note ≤47 selects the bass region, ≥48
the lead. (Note: to sound *both* on the same key you would use two synth
voice groups, not one instrument — see
[§2](#how-this-builds-on-the-synth-core).)

### Example 3 — velocity layers

```
<global> pitch_keycenter=60
<region> sample=soft.wav  lovel=1   hivel=63
<region> sample=hard.wav  lovel=64  hivel=127
```

Velocity 1–63 → `soft.wav`; 64–127 → `hard.wav`. Non-overlapping, so the
choice is unambiguous and cross-checked directly against sfizz in the
golden tests.

### Example 4 — a one-shot drum

```
<region>
sample=kick.wav
loop_mode=one_shot
lokey=36 hikey=36 pitch_keycenter=36
```

Key 36 triggers the kick; it plays to completion ignoring note-off, then
settles `SS_STOPPED`.

### Example 5 — an unsupported file loads leniently

```
<region>
sample=strings.wav
pitch_keycenter=48
cutoff=800          // OUT: filter — skipped+logged
lfo01_freq=5        // OUT: LFO — skipped+logged
seq_position=1      // OUT: round-robin — skipped+logged
seq_length=3        // OUT
```

The region loads and plays (pitch-tracked, full range, no filter/LFO);
three OUT opcodes are logged. No parse failure — the instrument is usable,
just without the unsupported modulation.

---

## Cross-references

This document is the contract for the sampler track of Epic
[#148][gh-148]. Each implementing sub-issue cites it and, if
implementation forces a change here, must update this document in the
same PR:

- [#173][gh-173] — DSP: SFZ parser + region model. Implements
  [§3](#file-model-headers-and-inheritance),
  [§4](#the-opcode-subset-normative-table),
  [§12](#unknown-opcode-and-error-policy),
  [§13](#the-rt-ready-region-model); owns the concrete `sfzRegion` /
  `sfzInstrument` types and the shared-buffer de-dup.
- [#174][gh-174] — DSP: Sampler voice (`SYNTH::samplerVoice`).
  Implements [§5](#region-selection), [§6](#pitch-and-tuning),
  [§7](#loop-handling), [§8](#the-amplitude-eg-ampeg_), and the
  [§11](#the-samplerconfig-convenience-facade) facade; consumes the
  synth-core `dspVoice` contract.

Upstream contract this builds on:

- [docs/design/synth_core.md][doc-synth] ([#151][gh-151]) — the voice
  model (`dspVoice`, `clone()`, the `intent` gate, allocation/stealing,
  slow-pool setup). The sampler is a `dspVoice` subclass under that
  contract; every RT and lifecycle rule there applies unchanged.

Current-engine anchors this design builds on (paths as of writing):

- `INTERNAL::abstractSoundFile` (libsndfile loader, streaming/preload,
  `read()`) — `YseEngine/internal/abstractSoundFile.{h,cpp}`.
- `DSP::fileBuffer` (resident buffer with `load`) —
  `YseEngine/dsp/fileBuffer.{hpp,cpp}`.
- `DSP::interpolate4` (4-point cubic interpolator) —
  `YseEngine/dsp/interpolate4.{hpp,cpp}`.
- `DSP::ADSRenvelope` (breakpoint envelope, sustain loop) —
  `YseEngine/dsp/ADSRenvelope.hpp`.
- `SYNTH::dspVoice` (voice base) — `YseEngine/synth/dspVoice.hpp`.
- `SOUND_STATUS` / `OBJECT_*` lifecycle — `YseEngine/headers/enums.hpp`.
- Old `samplerConfig` shape — `YseEngine/synth/synthInterface.hpp` at
  commit `d57fe15^` (JUCE-era, since removed by [#150][gh-150]).

[gh-120]: https://github.com/yvanvds/yse-soundengine/issues/120
[gh-145]: https://github.com/yvanvds/yse-soundengine/issues/145
[gh-148]: https://github.com/yvanvds/yse-soundengine/issues/148
[gh-150]: https://github.com/yvanvds/yse-soundengine/issues/150
[gh-151]: https://github.com/yvanvds/yse-soundengine/issues/151
[gh-172]: https://github.com/yvanvds/yse-soundengine/issues/172
[gh-173]: https://github.com/yvanvds/yse-soundengine/issues/173
[gh-174]: https://github.com/yvanvds/yse-soundengine/issues/174
[doc-synth]: synth_core.md
[doc-dsl]: live_coding_dsl.md
[sfz-spec]: https://sfzformat.com/
[sfizz]: https://github.com/sfztools/sfizz
