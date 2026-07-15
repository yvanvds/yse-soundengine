/*
  yse_dsp_modules.h — chainable DSP effects (dspObject subclasses).
  C ABI mirror of YseEngine/dsp/modules/ headers + ringModulator + the inherited
  dspObject control surface from YseEngine/dsp/dspObject.hpp.

  Every effect handle is a YseDspObject* — all the inherited methods
  (bypass, impact, lfo type/frequency, link into a chain) accept the
  shared handle type. Subclass-specific setters trust the caller and
  static_cast (same contract as DSP buffers).

  Attach a chain to a sound with yse_sound_set_dsp() (see yse_sound.h).
*/

#ifndef YSE_C_DSP_MODULES_H_INCLUDED
#define YSE_C_DSP_MODULES_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_dsp_object_destroy. Covers every effect
   subclass via the shared handle type. */
typedef struct YseDspObject YseDspObject;

/* ─── constructors ────────────────────────────────────────────────────── */

YSE_C_API YseDspObject* yse_dsp_lowpass_create(void);
YSE_C_API YseDspObject* yse_dsp_highpass_create(void);
YSE_C_API YseDspObject* yse_dsp_bandpass_create(void);
YSE_C_API YseDspObject* yse_dsp_sweep_create(YseDspSweepShape shape);

YSE_C_API YseDspObject* yse_dsp_basic_delay_create(void);
YSE_C_API YseDspObject* yse_dsp_lowpass_delay_create(void);
YSE_C_API YseDspObject* yse_dsp_highpass_delay_create(void);

YSE_C_API YseDspObject* yse_dsp_phaser_create(void);
YSE_C_API YseDspObject* yse_dsp_ring_modulator_create(void);
YSE_C_API YseDspObject* yse_dsp_difference_create(void);
YSE_C_API YseDspObject* yse_dsp_granulator_create(unsigned int pool_size, unsigned int max_grains);

/* Mix-grade effect modules (issues #160-#163). Each is a chainable insert or
   send-return effect; drop one on a channel with yse_channel_set_dsp() or a
   sound with yse_sound_set_dsp(). Their wet/dry balance is the inherited
   yse_dsp_object_set_impact(). */
YSE_C_API YseDspObject* yse_dsp_feedback_delay_create(void); /* #160 */
YSE_C_API YseDspObject* yse_dsp_chorus_create(void); /* #161 */
YSE_C_API YseDspObject* yse_dsp_plate_reverb_create(void); /* #162 */
YSE_C_API YseDspObject* yse_dsp_eq_create(void); /* #163 */
YSE_C_API YseDspObject* yse_dsp_compressor_create(void); /* #163 */

YSE_C_API void yse_dsp_object_destroy(YseDspObject* obj);

/* ─── inherited dspObject control surface ─────────────────────────────── */

YSE_C_API void yse_dsp_object_set_bypass(YseDspObject* obj, int on);
YSE_C_API int yse_dsp_object_get_bypass(YseDspObject* obj);
YSE_C_API void yse_dsp_object_set_impact(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_object_get_impact(YseDspObject* obj);
YSE_C_API void yse_dsp_object_set_lfo_type(YseDspObject* obj, YseLfoType type);
YSE_C_API YseLfoType yse_dsp_object_get_lfo_type(YseDspObject* obj);
YSE_C_API void yse_dsp_object_set_lfo_frequency(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_object_get_lfo_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_object_link(YseDspObject* head, YseDspObject* next);

/* ─── filter modules ─────────────────────────────────────────────────── */

YSE_C_API void yse_dsp_lowpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_lowpass_get_frequency(YseDspObject* obj);

YSE_C_API void yse_dsp_highpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_highpass_get_frequency(YseDspObject* obj);

YSE_C_API void yse_dsp_bandpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_bandpass_get_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_bandpass_set_q(YseDspObject* obj, float q);
YSE_C_API float yse_dsp_bandpass_get_q(YseDspObject* obj);

YSE_C_API void yse_dsp_sweep_set_speed(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_sweep_get_speed(YseDspObject* obj);
YSE_C_API void yse_dsp_sweep_set_depth(YseDspObject* obj, int v0_to_100);
YSE_C_API int yse_dsp_sweep_get_depth(YseDspObject* obj);
YSE_C_API void yse_dsp_sweep_set_frequency(YseDspObject* obj, int v0_to_100);
YSE_C_API int yse_dsp_sweep_get_frequency(YseDspObject* obj);

/* ─── delay modules ──────────────────────────────────────────────────── */

YSE_C_API void yse_dsp_basic_delay_set_tap(YseDspObject* obj, YseDspDelayTap tap, float time_ms,
                                           float gain);
YSE_C_API float yse_dsp_basic_delay_get_time(YseDspObject* obj, YseDspDelayTap tap);
YSE_C_API float yse_dsp_basic_delay_get_gain(YseDspObject* obj, YseDspDelayTap tap);

YSE_C_API void yse_dsp_lowpass_delay_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_lowpass_delay_get_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_highpass_delay_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_highpass_delay_get_frequency(YseDspObject* obj);

/* ─── modulation modules ─────────────────────────────────────────────── */

YSE_C_API void yse_dsp_phaser_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_phaser_get_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_phaser_set_range(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_phaser_get_range(YseDspObject* obj);

YSE_C_API void yse_dsp_ring_modulator_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_ring_modulator_get_frequency(YseDspObject* obj);

YSE_C_API void yse_dsp_difference_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_difference_get_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_difference_set_amplitude(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_difference_get_amplitude(YseDspObject* obj);

YSE_C_API void yse_dsp_granulator_set_grain_frequency(YseDspObject* obj, unsigned int per_second);
YSE_C_API unsigned int yse_dsp_granulator_get_grain_frequency(YseDspObject* obj);
YSE_C_API void yse_dsp_granulator_set_grain_length(YseDspObject* obj, unsigned int samples,
                                                   unsigned int random_samples);
YSE_C_API unsigned int yse_dsp_granulator_get_grain_length(YseDspObject* obj);
YSE_C_API void yse_dsp_granulator_set_grain_transpose(YseDspObject* obj, float pitch, float random);
YSE_C_API float yse_dsp_granulator_get_grain_transpose(YseDspObject* obj);
YSE_C_API void yse_dsp_granulator_set_gain(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_granulator_get_gain(YseDspObject* obj);

/* ─── feedback delay (#160) ──────────────────────────────────────────────
 * A recirculating delay for a channel insert or a send return: per-channel
 * delay line, a damping low-pass in the feedback path, and cross-feed between
 * channel pairs for ping-pong. impact(1) is echoes only (send use). */
YSE_C_API void yse_dsp_feedback_delay_set_time(YseDspObject* obj, float ms);
YSE_C_API float yse_dsp_feedback_delay_get_time(YseDspObject* obj);
YSE_C_API void yse_dsp_feedback_delay_set_feedback(YseDspObject* obj, float amount);
YSE_C_API float yse_dsp_feedback_delay_get_feedback(YseDspObject* obj);
YSE_C_API void yse_dsp_feedback_delay_set_damping(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_feedback_delay_get_damping(YseDspObject* obj);
YSE_C_API void yse_dsp_feedback_delay_set_crossfeed(YseDspObject* obj, float amount);
YSE_C_API float yse_dsp_feedback_delay_get_crossfeed(YseDspObject* obj);

/* ─── chorus / flanger (#161) ────────────────────────────────────────────
 * One modulated-delay module with a mode() switch between chorus and flanger.
 * spread() fans a per-channel LFO phase offset for stereo width. */
YSE_C_API void yse_dsp_chorus_set_mode(YseDspObject* obj, YseChorusMode mode);
YSE_C_API YseChorusMode yse_dsp_chorus_get_mode(YseDspObject* obj);
YSE_C_API void yse_dsp_chorus_set_rate(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_chorus_get_rate(YseDspObject* obj);
YSE_C_API void yse_dsp_chorus_set_depth(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_chorus_get_depth(YseDspObject* obj);
YSE_C_API void yse_dsp_chorus_set_feedback(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_chorus_get_feedback(YseDspObject* obj);
YSE_C_API void yse_dsp_chorus_set_spread(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_chorus_get_spread(YseDspObject* obj);

/* ─── plate reverb (#162) ────────────────────────────────────────────────
 * Dattorro plate reverb for a channel insert or a send return. Distinct from
 * the engine's global spatial reverb; impact(0.25) is a natural insert mix,
 * impact(1) is fully wet (send-return use). */
YSE_C_API void yse_dsp_plate_reverb_set_decay(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_plate_reverb_get_decay(YseDspObject* obj);
YSE_C_API void yse_dsp_plate_reverb_set_damping(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_plate_reverb_get_damping(YseDspObject* obj);
YSE_C_API void yse_dsp_plate_reverb_set_predelay(YseDspObject* obj, float ms);
YSE_C_API float yse_dsp_plate_reverb_get_predelay(YseDspObject* obj);

/* ─── parametric EQ (#163) ───────────────────────────────────────────────
 * Four cascaded bands (low shelf, two peaks, high shelf); each parameter is
 * addressed by a YseEqBand. Gain is in dB (0 = flat / band bypass). */
YSE_C_API void yse_dsp_eq_set_frequency(YseDspObject* obj, YseEqBand band, float hz);
YSE_C_API float yse_dsp_eq_get_frequency(YseDspObject* obj, YseEqBand band);
YSE_C_API void yse_dsp_eq_set_gain(YseDspObject* obj, YseEqBand band, float db);
YSE_C_API float yse_dsp_eq_get_gain(YseDspObject* obj, YseEqBand band);
YSE_C_API void yse_dsp_eq_set_q(YseDspObject* obj, YseEqBand band, float value);
YSE_C_API float yse_dsp_eq_get_q(YseDspObject* obj, YseEqBand band);

/* ─── compressor (#163) ──────────────────────────────────────────────────
 * Feed-forward, stereo-linked dynamics. gain_reduction_db is a read-only
 * meter of the reduction on the last processed sample (<= 0 dB). */
YSE_C_API void yse_dsp_compressor_set_detector(YseDspObject* obj, YseCompressorDetector mode);
YSE_C_API YseCompressorDetector yse_dsp_compressor_get_detector(YseDspObject* obj);
YSE_C_API void yse_dsp_compressor_set_threshold(YseDspObject* obj, float db);
YSE_C_API float yse_dsp_compressor_get_threshold(YseDspObject* obj);
YSE_C_API void yse_dsp_compressor_set_ratio(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_compressor_get_ratio(YseDspObject* obj);
YSE_C_API void yse_dsp_compressor_set_attack(YseDspObject* obj, float ms);
YSE_C_API float yse_dsp_compressor_get_attack(YseDspObject* obj);
YSE_C_API void yse_dsp_compressor_set_release(YseDspObject* obj, float ms);
YSE_C_API float yse_dsp_compressor_get_release(YseDspObject* obj);
YSE_C_API void yse_dsp_compressor_set_makeup(YseDspObject* obj, float db);
YSE_C_API float yse_dsp_compressor_get_makeup(YseDspObject* obj);
YSE_C_API float yse_dsp_compressor_get_gain_reduction_db(YseDspObject* obj);

#ifdef __cplusplus
}
#endif

#endif
