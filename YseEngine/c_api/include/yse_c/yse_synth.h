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

  Scope: only the BUILT-IN reference voice (sine + ADSR, issue #152) is
  exposed, via yse_synth_add_voices_sine. Defining a custom dspVoice
  subclass from C is deliberately DEFERRED — it needs the same audio-thread
  dspSourceObject callback plumbing that keeps dspSourceObject unwrapped
  (see yse_dsp.h and docs/design/synth_core.md §1 non-goals). Instrument
  voices (#148) will add more yse_synth_add_voices_* variants later.

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

#ifdef __cplusplus
}
#endif

#endif
