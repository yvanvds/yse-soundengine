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

YSE_C_API void          yse_dsp_object_destroy(YseDspObject* obj);

/* ─── inherited dspObject control surface ─────────────────────────────── */

YSE_C_API void  yse_dsp_object_set_bypass(YseDspObject* obj, int on);
YSE_C_API int   yse_dsp_object_get_bypass(YseDspObject* obj);
YSE_C_API void  yse_dsp_object_set_impact(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_object_get_impact(YseDspObject* obj);
YSE_C_API void  yse_dsp_object_set_lfo_type(YseDspObject* obj, YseLfoType type);
YSE_C_API YseLfoType yse_dsp_object_get_lfo_type(YseDspObject* obj);
YSE_C_API void  yse_dsp_object_set_lfo_frequency(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_object_get_lfo_frequency(YseDspObject* obj);
YSE_C_API void  yse_dsp_object_link(YseDspObject* head, YseDspObject* next);

/* ─── filter modules ─────────────────────────────────────────────────── */

YSE_C_API void  yse_dsp_lowpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_lowpass_get_frequency(YseDspObject* obj);

YSE_C_API void  yse_dsp_highpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_highpass_get_frequency(YseDspObject* obj);

YSE_C_API void  yse_dsp_bandpass_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_bandpass_get_frequency(YseDspObject* obj);
YSE_C_API void  yse_dsp_bandpass_set_q(YseDspObject* obj, float q);
YSE_C_API float yse_dsp_bandpass_get_q(YseDspObject* obj);

YSE_C_API void  yse_dsp_sweep_set_speed(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_sweep_get_speed(YseDspObject* obj);
YSE_C_API void  yse_dsp_sweep_set_depth(YseDspObject* obj, int v0_to_100);
YSE_C_API int   yse_dsp_sweep_get_depth(YseDspObject* obj);
YSE_C_API void  yse_dsp_sweep_set_frequency(YseDspObject* obj, int v0_to_100);
YSE_C_API int   yse_dsp_sweep_get_frequency(YseDspObject* obj);

/* ─── delay modules ──────────────────────────────────────────────────── */

YSE_C_API void  yse_dsp_basic_delay_set_tap(YseDspObject* obj, YseDspDelayTap tap, float time_ms, float gain);
YSE_C_API float yse_dsp_basic_delay_get_time(YseDspObject* obj, YseDspDelayTap tap);
YSE_C_API float yse_dsp_basic_delay_get_gain(YseDspObject* obj, YseDspDelayTap tap);

YSE_C_API void  yse_dsp_lowpass_delay_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_lowpass_delay_get_frequency(YseDspObject* obj);
YSE_C_API void  yse_dsp_highpass_delay_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_highpass_delay_get_frequency(YseDspObject* obj);

/* ─── modulation modules ─────────────────────────────────────────────── */

YSE_C_API void  yse_dsp_phaser_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_phaser_get_frequency(YseDspObject* obj);
YSE_C_API void  yse_dsp_phaser_set_range(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_phaser_get_range(YseDspObject* obj);

YSE_C_API void  yse_dsp_ring_modulator_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_ring_modulator_get_frequency(YseDspObject* obj);

YSE_C_API void  yse_dsp_difference_set_frequency(YseDspObject* obj, float hz);
YSE_C_API float yse_dsp_difference_get_frequency(YseDspObject* obj);
YSE_C_API void  yse_dsp_difference_set_amplitude(YseDspObject* obj, float value);
YSE_C_API float yse_dsp_difference_get_amplitude(YseDspObject* obj);

YSE_C_API void         yse_dsp_granulator_set_grain_frequency(YseDspObject* obj, unsigned int per_second);
YSE_C_API unsigned int yse_dsp_granulator_get_grain_frequency(YseDspObject* obj);
YSE_C_API void         yse_dsp_granulator_set_grain_length(YseDspObject* obj, unsigned int samples, unsigned int random_samples);
YSE_C_API unsigned int yse_dsp_granulator_get_grain_length(YseDspObject* obj);
YSE_C_API void         yse_dsp_granulator_set_grain_transpose(YseDspObject* obj, float pitch, float random);
YSE_C_API float        yse_dsp_granulator_get_grain_transpose(YseDspObject* obj);
YSE_C_API void         yse_dsp_granulator_set_gain(YseDspObject* obj, float value);
YSE_C_API float        yse_dsp_granulator_get_gain(YseDspObject* obj);

#ifdef __cplusplus
}
#endif

#endif
