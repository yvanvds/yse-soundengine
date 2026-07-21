/*
  yse_synth.h — polyphonic synthesiser voice pool rendered behind one sound.
  C ABI mirror of YseEngine/synth/synthInterface.hpp (YSE::synth) and
  §12 ("Public API surface") of docs/design/synth_core.md.

  A synth owns polyphony, voice allocation, stealing and keyboard/pedal
  state; you drive it with note, controller and pedal events. Build the
  voice pool from a built-in voice type with yse_synth_add_voices_*, attach
  it behind a positioned sound with yse_synth_attach_to_sound(), then play
  notes. Cloning happens off the audio thread on the engine's setup pool, so
  a synth becomes playable a short moment after add-voices returns (poll
  yse_synth_get_num_voices() for the cloned count) — exactly like a
  file-backed sound is not playable until its buffer finishes loading.

  Scope: the BUILT-IN voice types are exposed via yse_synth_add_voices_*: the
  reference sine + ADSR voice (issue #152), and the three instruments (issue
  #178) — the SFZ sampler (add_voices_sampler, fed a YseSfzInstrument loaded
  through yse_instrument.h), the virtual-analog + wavetable voice
  (add_voices_va, with per-parameter setters below), and the DX7-class 6-op FM
  voice (add_voices_fm, patched from a YseDx7Bank or the headline setters
  below). Defining a CUSTOM dspVoice subclass from C is deliberately DEFERRED —
  it needs the same audio-thread dspSourceObject callback plumbing that keeps
  dspSourceObject unwrapped (see yse_dsp.h and docs/design/synth_core.md §1
  non-goals).

  Parameter reach (parity, #178): the VA voice's live patch (YSE::SYNTH::vaParams)
  is fully covered by the yse_synth_va_set_* setters below — every field is a
  glitch-free atomic, so each setter is safe to call while voices play. The FM
  voice's patch (YSE::SYNTH::fmPatch) is plain DX7 voice data; its full 155-
  parameter set is authored by importing a DX7 bank and selecting a patch
  (yse_synth_fm_set_patch), and the headline globals + per-operator level/
  frequency are also reachable directly via the yse_synth_fm_set_* setters. FM
  patch edits take effect on the NEXT note-on (the FM core bakes operator state
  at key-down, like the hardware between notes), so the FM setters are not
  glitch-free mid-note — unlike the VA ones. The sampler voice has no live tone
  parameters: its sound is the immutable SFZ instrument, authored at load time
  through yse_instrument.h (yse_sfz_load / yse_sfz_load_config).

  Per-note 3D positioning (issue #171, §14 of
  docs/design/per_note_positioning.md) is exposed by the same policy: only the
  BUILT-IN position handlers (static / random-spread / orbit) are selectable,
  via yse_synth_set_position_handler with the YseSynthPositionHandler enum.
  Defining a CUSTOM position handler (subclassing YSE::SYNTH::positionHandler
  from C) is DEFERRED for the same reason a custom dspVoice is — its noteOn /
  update / onRelease hooks run on the AUDIO THREAD, which needs the callback
  plumbing that keeps dspSourceObject unwrapped (see yse_dsp.h). Per-note
  position readback here is a single best-effort snapshot
  (yse_synth_get_voice_position), not a continuous readback stream.

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle. Status queries return 0 / false on
  NULL. Operations that can fail (add-voices, attach) return YseStatus and
  populate yse_last_error() on failure. Channels follow the engine's synth
  API: 0 = omni / all channels, 1..16 = a specific MIDI channel. Velocity,
  controller and aftertouch values are normalised to [0, 1]; the pitch wheel
  to [-1, 1].
*/

#ifndef YSE_C_SYNTH_H_INCLUDED
#define YSE_C_SYNTH_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h" /* YseSynthPositionHandler, YseSynthHandlerParam, YseVaWaveform, YseLfoType */
#include "yse_instrument.h" /* YseSfzInstrument, YseDx7Bank (sampler / FM assets) */

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_synth_destroy. The handle also owns the built-in
   voice prototypes created by yse_synth_add_voices_*, freeing them on
   destroy; keep the synth alive until after any sound rendering it is
   destroyed (see yse_synth_attach_to_sound). */
typedef struct YseSynth YseSynth;

/* Forward declarations — see yse_sound.h / yse_channel.h for ownership. */
typedef struct YseSound YseSound;
typedef struct YseChannel YseChannel;

/* Audio-thread note-rewrite hook, mirroring YSE::synth::onNoteEvent
   (docs/design/synth_core.md §7). Invoked by the engine on the AUDIO THREAD
   for every note-on / note-off, before keyboard bookkeeping and voice
   allocation. It may rewrite *note_number and *velocity in place — the
   classic use is transposition, retuning, velocity curves or note filtering.

   note_on is 1 for a note-on, 0 for a note-off. Same real-time rules as a
   voice's render: no allocation, no locks, no blocking I/O. There is no
   user_data slot — the engine hook is a bare captureless function pointer,
   so state must be reached through globals (matches the C++ API, which
   accepts only a free function / captureless lambda). */
typedef void(YSE_C_CALLBACK* YseSynthNoteCallback)(int note_on, float* note_number,
                                                   float* velocity);

/* ─── lifecycle ───────────────────────────────────────────────────────── */

/* Create and register a synth. Ready to receive add-voices and note events
   immediately (runs the C++ constructor and YSE::synth::create()). Returns
   NULL on allocation failure with yse_last_error() set. */
YSE_C_API YseSynth* yse_synth_create(void);
YSE_C_API void yse_synth_destroy(YseSynth* h);

/* Whether the synth has a live implementation (registered with the engine). */
YSE_C_API int yse_synth_is_valid(YseSynth* h);

/* Assign a bus-addressable name to the synth (mirrors YSE::synth::name, issue
   #388). Once named "foo", the engine subscribes it to the global named bus
   addresses synth.foo.note / .off / .cc / .bend / .aftertouch / .alloff, so
   note and controller events published by name reach the synth engine-side
   with no host round-trip. Payload shapes are locked by
   docs/design/live_coding_dsl.md ("Mapping to synth events"); delivery
   reuses the same RT-safe message inbox as the note/controller functions
   above. Anonymous synths (the default) are not addressable.

   NULL or "" clears the name and removes the subscriptions; renaming
   re-subscribes under the new name. Names are unique per synth: a duplicate
   is rejected and logged engine-side (first registration wins — there is no
   error return, matching the C++ API). Only effective while the engine is
   between init and close. */
YSE_C_API void yse_synth_set_name(YseSynth* h, const char* name);

/* ─── voice groups (built-in voices only) ─────────────────────────────── */

/* Add a group of `num_voices` built-in sine voices (sine oscillator shaped
   by an ADSR envelope) responding to note numbers in
   [lowest_note, highest_note] on `channel` (0 = omni). May be called several
   times to build layered or split keyboards. attack / decay / release are in
   seconds; sustain is a level in [0, 1]. Must be called before the synth is
   played; adding voices after the pool is built is rejected (a non-goal, see
   docs/design/synth_core.md §1). Returns YseStatus; on failure yse_last_error()
   is set and no group is added. */
YSE_C_API YseStatus yse_synth_add_voices_sine(YseSynth* h, int num_voices, int channel,
                                              int lowest_note, int highest_note, float attack,
                                              float decay, float sustain, float release);

/* ─── instrument voice groups (issues #178, #390) ─────────────────────────
   Each adds `num_voices` clones of the named built-in instrument voice,
   responding to note numbers in [lowest_note, highest_note] on `channel`
   (0 = omni), mirroring YSE::synth::addVoices 1:1 — C has no default
   arguments, so pass 0, 0, 127 for the historical omni / full-key-range
   behaviour. The channel + note-range window is the voice group's allocation
   filter; within that window the SFZ regions / patch still decide how a note
   actually sounds. Like add_voices_sine, they may be called several times to
   build layered, split, or multitimbral pools, and must be called BEFORE the
   synth is played; adding voices after the pool is built is rejected. Return
   YseStatus; on failure yse_last_error() is set and no group is added. */

/* Add a group of SFZ sampler voices rendering `instrument` (loaded via
   yse_sfz_load / yse_sfz_load_config). The instrument's region table and PCM
   are shared with the voice group, which retains its own reference — so the
   YseSfzInstrument handle may be destroyed right after this returns.
   YSE_ERR_INVALID_ARGUMENT if `instrument` is NULL or already destroyed. */
YSE_C_API YseStatus yse_synth_add_voices_sampler(YseSynth* h, YseSfzInstrument* instrument,
                                                 int num_voices, int channel, int lowest_note,
                                                 int highest_note);

/* Add a group of virtual-analog + wavetable voices with a fresh default patch.
   This establishes the synth's VA patch; the yse_synth_va_set_* setters below
   steer it. Call once per synth (a second call replaces which patch the setters
   target — layering multiple VA groups is out of scope for the C API). */
YSE_C_API YseStatus yse_synth_add_voices_va(YseSynth* h, int num_voices, int channel,
                                            int lowest_note, int highest_note);

/* Add a group of DX7-class 6-operator FM voices with the built-in sine test
   patch. This establishes the synth's FM patch; select a DX7 voice into it with
   yse_synth_fm_set_patch, or dial the headline params with yse_synth_fm_set_*.
   Call once per synth (see add_voices_va's note). */
YSE_C_API YseStatus yse_synth_add_voices_fm(YseSynth* h, int num_voices, int channel,
                                            int lowest_note, int highest_note);

/* ─── VA patch parameters (issue #178) ─────────────────────────────────────
   Steer the synth's VA patch (established by yse_synth_add_voices_va). Every
   value is a glitch-free atomic read on the audio thread, so these are safe to
   call while voices play. All are null-safe no-ops on a NULL handle or a synth
   with no VA group. `osc` is the oscillator index 0..2 (out-of-range ignored).
   Times are in seconds; sustains and normalised depths in [0, 1] unless noted. */
YSE_C_API void yse_synth_va_set_osc_wave(YseSynth* h, int osc, YseVaWaveform wave);
YSE_C_API void yse_synth_va_set_osc_detune(YseSynth* h, int osc, float semitones);
YSE_C_API void yse_synth_va_set_osc_level(YseSynth* h, int osc, float level);
YSE_C_API void yse_synth_va_set_osc_pulse_width(YseSynth* h, int osc, float width);
YSE_C_API void yse_synth_va_set_wavetable_position(YseSynth* h, float position);
YSE_C_API void yse_synth_va_set_cutoff(YseSynth* h, float hz);
YSE_C_API void yse_synth_va_set_resonance(YseSynth* h, float resonance);
YSE_C_API void yse_synth_va_set_key_tracking(YseSynth* h, float amount);
YSE_C_API void yse_synth_va_set_filter_env_amount(YseSynth* h, float octaves);
YSE_C_API void yse_synth_va_set_filter_vel_amount(YseSynth* h, float octaves);
YSE_C_API void yse_synth_va_set_amp_attack(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_amp_decay(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_amp_sustain(YseSynth* h, float level);
YSE_C_API void yse_synth_va_set_amp_release(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_amp_vel_amount(YseSynth* h, float amount);
YSE_C_API void yse_synth_va_set_filter_attack(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_filter_decay(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_filter_sustain(YseSynth* h, float level);
YSE_C_API void yse_synth_va_set_filter_release(YseSynth* h, float seconds);
YSE_C_API void yse_synth_va_set_lfo_type(YseSynth* h, YseLfoType type);
YSE_C_API void yse_synth_va_set_lfo_rate(YseSynth* h, float hz);
YSE_C_API void yse_synth_va_set_lfo_to_pitch(YseSynth* h, float semitones);
YSE_C_API void yse_synth_va_set_lfo_to_cutoff(YseSynth* h, float octaves);
YSE_C_API void yse_synth_va_set_lfo_to_wavetable(YseSynth* h, float amount);
YSE_C_API void yse_synth_va_set_gain(YseSynth* h, float gain);

/* Install a single-cycle waveform into the VA wavetable morph bank at `slot`
   (used by YSE_VA_WAVETABLE mode). `cycle` points to `length` normalised
   samples (one period). SETUP-THREAD only — this reshapes table storage; call
   before the synth is played, not while voices render. Null-safe no-op on a
   NULL handle / cycle, an empty length, or a synth with no VA group. */
YSE_C_API void yse_synth_va_load_wavetable(YseSynth* h, int slot, const float* cycle,
                                           size_t length);

/* ─── FM patch (issue #178) ────────────────────────────────────────────────
   Author the synth's FM patch (established by yse_synth_add_voices_fm). Edits
   take effect on the NEXT note-on (the FM core bakes operator state at key-
   down), so these are not glitch-free mid-note. All are null-safe no-ops on a
   NULL handle or a synth with no FM group. */

/* Copy patch `index` from a DX7 bank (imported via yse_dx7_import_sysex) into
   the synth's FM patch — the way to reach the full 155-parameter DX7 voice from
   C. The patch is copied, so `bank` may be destroyed afterwards.
   YSE_ERR_INVALID_ARGUMENT for a NULL / destroyed bank or an out-of-range
   index; YSE_ERR_INVALID_HANDLE for a NULL synth or one with no FM group. */
YSE_C_API YseStatus yse_synth_fm_set_patch(YseSynth* h, YseDx7Bank* bank, int index);

/* Headline global params (DX7 ranges; clamped defensively engine-side). */
YSE_C_API void yse_synth_fm_set_algorithm(YseSynth* h, int algorithm); /* 0..31 */
YSE_C_API void yse_synth_fm_set_feedback(YseSynth* h, int feedback); /* 0..7  */
YSE_C_API void yse_synth_fm_set_transpose(YseSynth* h, int transpose); /* 0..48, 24 = none */
YSE_C_API void yse_synth_fm_set_lfo_speed(YseSynth* h, int speed); /* 0..99 */
YSE_C_API void yse_synth_fm_set_lfo_delay(YseSynth* h, int delay); /* 0..99 */
YSE_C_API void yse_synth_fm_set_lfo_waveform(YseSynth* h, int waveform); /* 0..5  */
YSE_C_API void yse_synth_fm_set_lfo_pitch_mod_depth(YseSynth* h, int depth); /* 0..99 */
YSE_C_API void yse_synth_fm_set_lfo_amp_mod_depth(YseSynth* h, int depth); /* 0..99 */
YSE_C_API void yse_synth_fm_set_pitch_mod_sens(YseSynth* h, int sensitivity); /* 0..7 */

/* Headline per-operator params. `op` is the operator index 0..5 (OP1..OP6);
   out-of-range is ignored. */
YSE_C_API void yse_synth_fm_set_op_output_level(YseSynth* h, int op, int level); /* 0..99 */
YSE_C_API void yse_synth_fm_set_op_freq_coarse(YseSynth* h, int op, int coarse); /* 0..31 */
YSE_C_API void yse_synth_fm_set_op_freq_fine(YseSynth* h, int op, int fine); /* 0..99 */
YSE_C_API void yse_synth_fm_set_op_detune(YseSynth* h, int op, int detune); /* 0..14, 7 = centre */
YSE_C_API void yse_synth_fm_set_op_osc_mode(YseSynth* h, int op, int mode); /* 0 ratio, 1 fixed */
YSE_C_API void yse_synth_fm_set_op_enabled(YseSynth* h, int op, int enabled); /* 0/1 */

/* Total number of allocated (cloned) voices across every group. Zero until
   the setup pool finishes cloning; poll it to know the synth is playable. */
YSE_C_API int yse_synth_get_num_voices(YseSynth* h);

/* ─── notes and control ───────────────────────────────────────────────── */

/* Start / release a note. velocity is normalised to [0, 1]. */
YSE_C_API void yse_synth_note_on(YseSynth* h, int channel, int note_number, float velocity);
YSE_C_API void yse_synth_note_off(YseSynth* h, int channel, int note_number, float velocity);

/* Release every held note on `channel` (0 = all channels). A bulk note-off:
   voices enter their normal release, they are not cut. */
YSE_C_API void yse_synth_all_notes_off(YseSynth* h, int channel);

/* Bend every voice on `channel`. value is normalised to [-1, 1] (0 = centre). */
YSE_C_API void yse_synth_pitch_wheel(YseSynth* h, int channel, float value);

/* Send a control-change. value is normalised to [0, 1]. CC 64 / 66 / 67 act
   as the sustain / sostenuto / soft pedals; other CC numbers are stored as
   the channel's last controller value. */
YSE_C_API void yse_synth_controller(YseSynth* h, int channel, int number, float value);

/* Apply aftertouch pressure, normalised to [0, 1]. note_number == -1 is
   channel-wide; otherwise only the voice(s) sounding that note receive it. */
YSE_C_API void yse_synth_aftertouch(YseSynth* h, int channel, int note_number, float value);

/* Pedals (down is a boolean: non-zero = down). */
YSE_C_API void yse_synth_sustain(YseSynth* h, int channel, int down);
YSE_C_API void yse_synth_sostenuto(YseSynth* h, int channel, int down);
YSE_C_API void yse_synth_soft_pedal(YseSynth* h, int channel, int down);

/* Install (or clear, with NULL) the audio-thread note-rewrite hook. The
   engine stores the pointer atomically; passing NULL disables the hook.
   See the YseSynthNoteCallback contract above. */
YSE_C_API void yse_synth_set_note_callback(YseSynth* h, YseSynthNoteCallback cb);

/* ─── attachment ──────────────────────────────────────────────────────── */

/* Render this synth behind `sound`, which supplies the single 3D position,
   channel routing and master play/stop intent (mirrors the C++
   YSE::sound::create(synth&, channel*, volume)). Build the synth's voices
   with yse_synth_add_voices_* before calling this. `channel` may be NULL for
   the master channel. volume is a linear gain.

   Lifetime: the synth must outlive the sound — destroy the sound before the
   synth. Returns YseStatus; on failure (e.g. the sound could not be created)
   yse_last_error() is set. */
YSE_C_API YseStatus yse_synth_attach_to_sound(YseSynth* h, YseSound* sound, YseChannel* channel,
                                              float volume);

/* ─── per-note 3D positioning (issue #171) ────────────────────────────────

   Configuration for yse_synth_set_position_handler. One flat, ffigen-friendly
   struct covering every built-in handler; only the fields belonging to the
   selected kind are read. Pass NULL to take the engine's defaults for that
   kind. Mirrors the chainable setters on YSE::SYNTH::staticHandler /
   randomSpreadHandler / orbitHandler. All positions are in the same coordinate
   frame as a sound position. */
typedef struct YseSynthPositionParams {
  /* YSE_POSITION_HANDLER_STATIC — the single fixed position. */
  float static_x;
  float static_y;
  float static_z;

  /* YSE_POSITION_HANDLER_RANDOM_SPREAD */
  float spread_radius; /* radius of the scatter sphere around the centre */
  unsigned int spread_seed; /* base RNG seed (a given seed reproduces the scatter) */

  /* YSE_POSITION_HANDLER_ORBIT */
  float orbit_radius; /* base orbit radius */
  float orbit_velocity_radius; /* extra radius added at full velocity */
  float orbit_aftertouch_widen; /* fraction of extra radius at full aftertouch */
  float orbit_rate; /* orbit angular speed, radians per second */
  float orbit_height; /* vertical offset of the orbit plane */
  float orbit_release_slow; /* rate multiplier once the note is released */
} YseSynthPositionParams;

/* Attach one of the built-in per-note position handlers, giving every voice its
   own 3D position and movement (mirrors YSE::synth::positionHandler with a
   shipped handler prototype). `kind` selects the handler; `params` configures
   it (NULL = engine defaults for that kind). The handle keeps the prototype
   alive; the engine clones it once per voice slot on the setup pool.

   Must be called BEFORE the synth is attached/played — like add-voices, the
   engine rejects a handler swap after the voice pool is built (it logs a
   warning and keeps the existing handler; the call still returns YSE_OK).
   Returns YSE_ERR_INVALID_ARGUMENT for an unknown `kind`; yse_last_error() is
   set on failure.

   Custom C-side handlers (subclassing YSE::SYNTH::positionHandler from C) are
   DEFERRED — the hooks run on the AUDIO THREAD, the same callback-plumbing gap
   that keeps custom dspVoice / dspSourceObject unwrapped (see yse_dsp.h and the
   scope note at the top of this header). Only the built-ins are reachable. */
YSE_C_API YseStatus yse_synth_set_position_handler(YseSynth* h, YseSynthPositionHandler kind,
                                                   const YseSynthPositionParams* params);

/* Update a shared handler parameter at runtime (message-based, RT-safe). All of
   the synth's live handlers read the block next audio block, so this steers the
   swarm / spread centre from the control thread. `index` is a
   YseSynthHandlerParam (0..2 = centre X / Y / Z); out-of-range indices are
   ignored engine-side. A bounded, allocation-free message — safe to call every
   control tick. */
YSE_C_API void yse_synth_handler_param(YseSynth* h, int index, float value);

/* Imperatively place the voice(s) sounding `note_number` on `channel` at
   (x, y, z) — app-driven trajectories (mirrors YSE::synth::notePosition). A
   bounded, allocation-free message. When a handler is attached it re-steers the
   voice next block, so this is primarily for the no-handler case. */
YSE_C_API void yse_synth_note_position(YseSynth* h, int channel, int note_number, float x, float y,
                                       float z);

/* Best-effort snapshot of the current position of a voice sounding
   (channel, note_number); writes the origin (0, 0, 0) if none is sounding
   (mirrors YSE::synth::getVoicePosition, intended for tests / metering). Any of
   the out pointers may be NULL. This is a single snapshot, not a per-note
   readback stream. On a NULL synth handle it writes the origin. */
YSE_C_API void yse_synth_get_voice_position(YseSynth* h, int channel, int note_number, float* x,
                                            float* y, float* z);

#ifdef __cplusplus
}
#endif

#endif
