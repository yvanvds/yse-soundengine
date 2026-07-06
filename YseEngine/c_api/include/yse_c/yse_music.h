/*
  yse_music.h — music-theory primitives (note, positioned note, scale,
  motif) + the generative player sequencer.
  C ABI mirror of YseEngine/music/ + YseEngine/player/playerInterface.hpp.

  All four primitive types and the player are wrapped as opaque
  handles. Notes and pNotes are value-like in C++ but exposed as heap
  allocations here so the Dart side can NativeFinalizer-back them
  uniformly.

  Note: where the C++ API exposes operator overloads (pitch arithmetic
  on notes), only the explicit setPitch / getPitch is wrapped — the
  arithmetic operators are easy to replicate at the Dart layer.
*/

#ifndef YSE_C_MUSIC_H_INCLUDED
#define YSE_C_MUSIC_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_note_destroy. */
typedef struct YseNote YseNote;
/* Owned — release with yse_pnote_destroy. */
typedef struct YsePNote YsePNote;
/* Owned — release with yse_scale_destroy. */
typedef struct YseScale YseScale;
/* Owned — release with yse_motif_destroy. */
typedef struct YseMotif YseMotif;
/* Owned — release with yse_player_destroy. */
typedef struct YsePlayer YsePlayer;

/* Forward declaration — the synth a player drives. Owned by the caller and
   created via yse_synth_create (see yse_synth.h); it must outlive the player.
   The player feeds every note it generates into this synth's lock-free inbox. */
typedef struct YseSynth YseSynth;

/* ─── note ────────────────────────────────────────────────────────── */

YSE_C_API YseNote* yse_note_create(float pitch, float volume, float length, int channel);
YSE_C_API void yse_note_destroy(YseNote* n);

YSE_C_API void yse_note_set(YseNote* n, float pitch, float volume, float length, int channel);
YSE_C_API void yse_note_set_pitch(YseNote* n, float pitch);
YSE_C_API void yse_note_set_volume(YseNote* n, float volume);
YSE_C_API void yse_note_set_length(YseNote* n, float length);
YSE_C_API void yse_note_set_channel(YseNote* n, int channel);
YSE_C_API float yse_note_get_pitch(YseNote* n);
YSE_C_API float yse_note_get_volume(YseNote* n);
YSE_C_API float yse_note_get_length(YseNote* n);
YSE_C_API int yse_note_get_channel(YseNote* n);

/* ─── pNote — positioned note ─────────────────────────────────────── */

YSE_C_API YsePNote* yse_pnote_create(float position, float pitch, float volume, float length,
                                     int channel);
YSE_C_API void yse_pnote_destroy(YsePNote* n);
YSE_C_API void yse_pnote_set_position(YsePNote* n, float position);
YSE_C_API float yse_pnote_get_position(YsePNote* n);
/* Inherited from note: */
YSE_C_API void yse_pnote_set_pitch(YsePNote* n, float pitch);
YSE_C_API void yse_pnote_set_volume(YsePNote* n, float volume);
YSE_C_API void yse_pnote_set_length(YsePNote* n, float length);
YSE_C_API float yse_pnote_get_pitch(YsePNote* n);
YSE_C_API float yse_pnote_get_volume(YsePNote* n);
YSE_C_API float yse_pnote_get_length(YsePNote* n);

/* ─── scale — set of allowed pitches ──────────────────────────────── */

YSE_C_API YseScale* yse_scale_create(void);
YSE_C_API void yse_scale_destroy(YseScale* s);
YSE_C_API void yse_scale_add(YseScale* s, float pitch, float octave_step);
YSE_C_API void yse_scale_remove(YseScale* s, float pitch, float octave_step);
YSE_C_API int yse_scale_has(YseScale* s, float pitch);
YSE_C_API float yse_scale_nearest(YseScale* s, float pitch);
YSE_C_API unsigned int yse_scale_size(YseScale* s);
YSE_C_API void yse_scale_clear(YseScale* s);

/* ─── motif — re-usable phrase of positioned notes ────────────────── */

YSE_C_API YseMotif* yse_motif_create(void);
YSE_C_API void yse_motif_destroy(YseMotif* m);
YSE_C_API void yse_motif_add(YseMotif* m, YsePNote* note);
YSE_C_API void yse_motif_clear(YseMotif* m);
YSE_C_API void yse_motif_set_length(YseMotif* m, float length);
YSE_C_API void yse_motif_set_length_auto(YseMotif* m);
YSE_C_API void yse_motif_transpose(YseMotif* m, float pitch);
YSE_C_API void yse_motif_set_first_pitch(YseMotif* m, YseScale* valid_pitches);
YSE_C_API float yse_motif_get_length(YseMotif* m);
YSE_C_API int yse_motif_empty(YseMotif* m);
YSE_C_API unsigned int yse_motif_size(YseMotif* m);

/* ─── player — generative sequencer ───────────────────────────────── */

/* Create a generative player bound to `synth` and register it with the engine.
   `synth` must be a live handle from yse_synth_create and must outlive the
   player — every note the player generates is delivered to it. Returns NULL
   (with yse_last_error() set) when `synth` is NULL or on allocation failure.
   This is the only create path: a player has no useful state until it is bound
   to a synth, so unlike most create functions it takes its target up front
   (issue #268). */
YSE_C_API YsePlayer* yse_player_create(YseSynth* synth);
YSE_C_API void yse_player_destroy(YsePlayer* p);
YSE_C_API void yse_player_play(YsePlayer* p);
YSE_C_API void yse_player_stop(YsePlayer* p);
YSE_C_API int yse_player_is_playing(YsePlayer* p);

YSE_C_API void yse_player_set_minimum_pitch(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_maximum_pitch(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_minimum_velocity(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_maximum_velocity(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_minimum_gap(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_maximum_gap(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_minimum_length(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_maximum_length(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_set_voices(YsePlayer* p, unsigned int target, float fade_time);

YSE_C_API void yse_player_set_scale(YsePlayer* p, YseScale* scale, float fade_time);
YSE_C_API void yse_player_add_motif(YsePlayer* p, YseMotif* motif, unsigned int weight);
YSE_C_API void yse_player_remove_motif(YsePlayer* p, YseMotif* motif);
YSE_C_API void yse_player_adjust_motif_weight(YsePlayer* p, YseMotif* motif, unsigned int weight);

YSE_C_API void yse_player_play_partial_motifs(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_play_motifs(YsePlayer* p, float target, float fade_time);
YSE_C_API void yse_player_fit_motifs_to_scale(YsePlayer* p, float target, float fade_time);

#ifdef __cplusplus
}
#endif

#endif
