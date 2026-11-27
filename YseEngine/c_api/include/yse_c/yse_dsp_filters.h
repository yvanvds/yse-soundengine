/**
  @file
  yse_dsp_filters.h — filter and smoothing processors of the MSP port.
  C ABI mirror of the YseEngine/dsp/filters/ classes added by #1034 (issue
  #1173): onePole, lores, reson, stateVariableFilter, allpass, phaseShift,
  biquadCascade, crossover, filterBank, filterCoefficients, slide, rampSmooth
  and slewLimiter. The engine headers document each filter's equations,
  clamping and defaults; this header documents only what the C mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions take
  host float arrays of `length` samples, return the number of samples
  written, and are allocation-free and safe on a real-time thread.

  Each filter has up to three ways to run it:

  - yse_dsp_CLASS_process(): a block at the current settings.
  - yse_dsp_CLASS_process_signal(): a block whose settings are read per
    sample from host arrays (one value per sample, each `length` long),
    which also update the getters — the patcher's signal inlets.
  - yse_dsp_CLASS_process_sample(): one sample, for a per-sample host loop.

  Not mirrored: the static coefficient formulas (Coefficients, Q, Advance,
  Effective, Samples, RatioFrequency, ...) and the per-instance coefficient
  readouts of the fixed-design filters (lores, reson, ...) — the engine
  headers give the formulas, and yse_dsp_filter_coefficients_compute() covers
  the biquad designs.
*/

#ifndef YSE_C_DSP_FILTERS_H_INCLUDED
#define YSE_C_DSP_FILTERS_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"
#include "yse_dsp_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── onePole: single-pole lowpass, 6 dB/octave (#1086) ──────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_one_pole_create(void);
/** The cutoff, read as the mode says. Non-finite: ignored. */
YSE_C_API void yse_dsp_one_pole_set_frequency(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_one_pole_get_frequency(YseDspProcessor* p);
YSE_C_API void yse_dsp_one_pole_set_mode(YseDspProcessor* p, YseDspFrequencyMode mode);
/** YSE_DSP_FREQUENCY_HZ for a NULL handle. */
YSE_C_API YseDspFrequencyMode yse_dsp_one_pole_get_mode(YseDspProcessor* p);
YSE_C_API void yse_dsp_one_pole_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_one_pole_process(YseDspProcessor* p, const float* in, float* out,
                                                unsigned int length);
/** The frequency is taken from the first sample of each 128-sample block of
   `frequency`, as the engine reads a signal frequency. */
YSE_C_API unsigned int yse_dsp_one_pole_process_signal(YseDspProcessor* p, const float* in,
                                                       const float* frequency, float* out,
                                                       unsigned int length);
YSE_C_API float yse_dsp_one_pole_process_sample(YseDspProcessor* p, float in);

/* ─── lores: two-pole resonant lowpass (#1087) ───────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_lores_create(void);
YSE_C_API void yse_dsp_lores_set_cutoff(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_lores_get_cutoff(YseDspProcessor* p);
/** 0 (no peak) .. 1 (clamped to 0.999). */
YSE_C_API void yse_dsp_lores_set_resonance(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_lores_get_resonance(YseDspProcessor* p);
YSE_C_API void yse_dsp_lores_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_lores_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);
YSE_C_API unsigned int yse_dsp_lores_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* cutoff, const float* resonance,
                                                    float* out, unsigned int length);
YSE_C_API float yse_dsp_lores_process_sample(YseDspProcessor* p, float in);

/* ─── reson: constant-gain resonant bandpass (#1088) ─────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_reson_create(void);
YSE_C_API void yse_dsp_reson_set_gain(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_reson_get_gain(YseDspProcessor* p);
YSE_C_API void yse_dsp_reson_set_frequency(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_reson_get_frequency(YseDspProcessor* p);
YSE_C_API void yse_dsp_reson_set_q(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_reson_get_q(YseDspProcessor* p);
YSE_C_API void yse_dsp_reson_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_reson_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);
YSE_C_API unsigned int yse_dsp_reson_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* gain, const float* frequency,
                                                    const float* q, float* out,
                                                    unsigned int length);
YSE_C_API float yse_dsp_reson_process_sample(YseDspProcessor* p, float in);

/* ─── stateVariableFilter: lowpass, highpass, bandpass, notch at once (#1089)
   The four outputs are optional: pass NULL for the ones you do not need. A
   process call with all four NULL still runs the filter and returns length. */

YSE_C_API YseDspProcessor* yse_dsp_svf_create(void);
YSE_C_API void yse_dsp_svf_set_frequency(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_svf_get_frequency(YseDspProcessor* p);
/** 0 (no peak) .. 1 (sharpest). */
YSE_C_API void yse_dsp_svf_set_resonance(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_svf_get_resonance(YseDspProcessor* p);
YSE_C_API void yse_dsp_svf_set_mode(YseDspProcessor* p, YseDspFrequencyMode mode);
/** YSE_DSP_FREQUENCY_HZ for a NULL handle. */
YSE_C_API YseDspFrequencyMode yse_dsp_svf_get_mode(YseDspProcessor* p);
YSE_C_API void yse_dsp_svf_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_svf_process(YseDspProcessor* p, const float* in, float* lowpass,
                                           float* highpass, float* bandpass, float* notch,
                                           unsigned int length);
YSE_C_API unsigned int yse_dsp_svf_process_signal(YseDspProcessor* p, const float* in,
                                                  const float* frequency, const float* resonance,
                                                  float* lowpass, float* highpass, float* bandpass,
                                                  float* notch, unsigned int length);
/** One sample; each output pointer may be NULL. Writes 0 to the non-NULL
   outputs for a NULL handle. */
YSE_C_API void yse_dsp_svf_process_sample(YseDspProcessor* p, float in, float* lowpass,
                                          float* highpass, float* bandpass, float* notch);

/* ─── allpass: Schroeder allpass (#1093) ─────────────────────────────────── */

/** A filter whose delay can reach `maximum_delay_ms` (clamped to 0 .. 60000;
   a non-finite value means the default 10 ms), with memory sized for sample
   rates up to `highest_sample_rate` (0 means 192000). */
YSE_C_API YseDspProcessor* yse_dsp_allpass_create(float maximum_delay_ms,
                                                  unsigned int highest_sample_rate);
YSE_C_API void yse_dsp_allpass_set_delay(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_allpass_get_delay(YseDspProcessor* p);
/** Clamped to -1 .. 1. */
YSE_C_API void yse_dsp_allpass_set_gain(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_allpass_get_gain(YseDspProcessor* p);
YSE_C_API float yse_dsp_allpass_get_maximum_delay(YseDspProcessor* p);
/** The longest delay in ms at the live sample rate. */
YSE_C_API float yse_dsp_allpass_get_capacity(YseDspProcessor* p);
/** The delay in samples the filter reads at the live sample rate. */
YSE_C_API float yse_dsp_allpass_get_delay_samples(YseDspProcessor* p);
YSE_C_API void yse_dsp_allpass_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_allpass_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length);
/** `delay` in ms. */
YSE_C_API unsigned int yse_dsp_allpass_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* delay, const float* gain,
                                                      float* out, unsigned int length);
YSE_C_API float yse_dsp_allpass_process_sample(YseDspProcessor* p, float in);

/* ─── phaseShift: second-order allpass (#1098) ───────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_phase_shift_create(void);
YSE_C_API void yse_dsp_phase_shift_set_frequency(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_phase_shift_get_frequency(YseDspProcessor* p);
YSE_C_API void yse_dsp_phase_shift_set_q(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_phase_shift_get_q(YseDspProcessor* p);
YSE_C_API void yse_dsp_phase_shift_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_phase_shift_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length);
YSE_C_API unsigned int yse_dsp_phase_shift_process_signal(YseDspProcessor* p, const float* in,
                                                          const float* frequency, const float* q,
                                                          float* out, unsigned int length);
YSE_C_API float yse_dsp_phase_shift_process_sample(YseDspProcessor* p, float in);

/* ─── biquadCascade: a series of biquads (#1091) ─────────────────────────── */

/** Room for `capacity` sections (clamped to 1 .. 64; 0 means 16), none in use:
   the cascade passes its input until coefficients are set. */
YSE_C_API YseDspProcessor* yse_dsp_biquad_cascade_create(unsigned int capacity);
/** Sets the sections from `count` values in sets of five (a0 a1 a2 b1 b2 per
   section, as the engine's biQuad takes them); count / 5 sections are used,
   at most the capacity. Fewer than five values, or NULL, is a no-op. */
YSE_C_API void yse_dsp_biquad_cascade_set_coefficients(YseDspProcessor* p, const float* values,
                                                       unsigned int count);
/** Copies section `section`'s five target coefficients (a0 a1 a2 b1 b2) to
   out[0..4]: all 0 for a section not in use. Returns 1, or 0 (out
   untouched) for a NULL handle or NULL out. */
YSE_C_API int yse_dsp_biquad_cascade_get_coefficients(YseDspProcessor* p, unsigned int section,
                                                      float* out);
YSE_C_API unsigned int yse_dsp_biquad_cascade_get_sections(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_biquad_cascade_get_capacity(YseDspProcessor* p);
/** Smooth coefficient changes (on by default). */
YSE_C_API void yse_dsp_biquad_cascade_set_smooth(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_biquad_cascade_get_smooth(YseDspProcessor* p);
/** Pass the input unchanged while on. */
YSE_C_API void yse_dsp_biquad_cascade_set_bypass(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_biquad_cascade_get_bypass(YseDspProcessor* p);
/** Output silence while on (unless bypassed). */
YSE_C_API void yse_dsp_biquad_cascade_set_zero(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_biquad_cascade_get_zero(YseDspProcessor* p);
/** Sets the coefficients and state of every section to 0. */
YSE_C_API void yse_dsp_biquad_cascade_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_biquad_cascade_process(YseDspProcessor* p, const float* in,
                                                      float* out, unsigned int length);
YSE_C_API float yse_dsp_biquad_cascade_process_sample(YseDspProcessor* p, float in);

/* ─── crossover: third-order crossover, 18 dB/octave bands that sum flat (#1095)
   Both outputs are optional (NULL to skip). */

YSE_C_API YseDspProcessor* yse_dsp_crossover_create(void);
YSE_C_API void yse_dsp_crossover_set_cutoff(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_crossover_get_cutoff(YseDspProcessor* p);
YSE_C_API void yse_dsp_crossover_clear(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_crossover_process(YseDspProcessor* p, const float* in,
                                                 float* lowpass, float* highpass,
                                                 unsigned int length);
YSE_C_API unsigned int yse_dsp_crossover_process_signal(YseDspProcessor* p, const float* in,
                                                        const float* cutoff, float* lowpass,
                                                        float* highpass, unsigned int length);
/** One sample; either output may be NULL. Writes 0 to the non-NULL outputs
   for a NULL handle. */
YSE_C_API void yse_dsp_crossover_process_sample(YseDspProcessor* p, float in, float* lowpass,
                                                float* highpass);

/* ─── filterBank: a bank of parallel bandpasses (#1096)
   Filter `index` addresses one filter; an index beyond the bank is ignored by
   the setters and reads the last filter in the getters, as in the engine. */

/** A bank of `filters` filters, clamped to 1 .. 128 (0 means 4). */
YSE_C_API YseDspProcessor* yse_dsp_filter_bank_create(unsigned int filters);
YSE_C_API unsigned int yse_dsp_filter_bank_get_size(YseDspProcessor* p);
YSE_C_API void yse_dsp_filter_bank_set_frequency(YseDspProcessor* p, unsigned int index, float hz);
YSE_C_API float yse_dsp_filter_bank_get_frequency(YseDspProcessor* p, unsigned int index);
YSE_C_API void yse_dsp_filter_bank_set_q(YseDspProcessor* p, unsigned int index, float value);
YSE_C_API float yse_dsp_filter_bank_get_q(YseDspProcessor* p, unsigned int index);
YSE_C_API void yse_dsp_filter_bank_set_gain(YseDspProcessor* p, unsigned int index, float value);
YSE_C_API float yse_dsp_filter_bank_get_gain(YseDspProcessor* p, unsigned int index);
YSE_C_API void yse_dsp_filter_bank_set_frequency_all(YseDspProcessor* p, float hz);
YSE_C_API void yse_dsp_filter_bank_set_q_all(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_filter_bank_set_gain_all(YseDspProcessor* p, float value);
/** Filter i at first * ratio^i Hz. A ratio not above 0 is ignored. */
YSE_C_API void yse_dsp_filter_bank_set_frequency_ratio(YseDspProcessor* p, float first,
                                                       float ratio);
/** Filter i at first * (i + 1) Hz: a harmonic series. */
YSE_C_API void yse_dsp_filter_bank_set_harmonic(YseDspProcessor* p, float first);
/** Copies filter `index`'s five coefficients (a0 a1 a2 b1 b2) at the live
   sample rate to out[0..4]. Returns 1, or 0 (out untouched) for a NULL
   handle or NULL out. */
YSE_C_API int yse_dsp_filter_bank_get_coefficients(YseDspProcessor* p, unsigned int index,
                                                   float* out);
YSE_C_API void yse_dsp_filter_bank_clear(YseDspProcessor* p);
/** One block of `in` through every filter. `outputs` holds `output_count`
   arrays of `length` samples; outputs[i] receives filter i. Entries past the
   bank size, and NULL entries, are skipped. `outputs` itself may be NULL
   (output_count is then ignored) to run the bank without reading it. */
YSE_C_API unsigned int yse_dsp_filter_bank_process(YseDspProcessor* p, const float* in,
                                                   float* const* outputs, unsigned int output_count,
                                                   unsigned int length);

/* ─── filterCoefficients: signal-rate biquad coefficients (#1092)
   A coefficient generator with no audio input: each block it writes the five
   coefficients (a0 a1 a2 b1 b2) of its design as five signals, for a biquad a
   host runs itself. Every output is optional (NULL to skip). */

/** Starts as a Butterworth lowpass at 1000 Hz, gain 1. */
YSE_C_API YseDspProcessor* yse_dsp_filter_coefficients_create(void);
/** A type out of range is ignored. */
YSE_C_API void yse_dsp_filter_coefficients_set_type(YseDspProcessor* p, YseDspFilterType type);
/** YSE_DSP_FILTER_OFF for a NULL handle. */
YSE_C_API YseDspFilterType yse_dsp_filter_coefficients_get_type(YseDspProcessor* p);
YSE_C_API void yse_dsp_filter_coefficients_set_frequency(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_filter_coefficients_get_frequency(YseDspProcessor* p);
/** Linear gain. */
YSE_C_API void yse_dsp_filter_coefficients_set_gain(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_filter_coefficients_get_gain(YseDspProcessor* p);
/** The Q, or the slope of a shelf. */
YSE_C_API void yse_dsp_filter_coefficients_set_q(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_filter_coefficients_get_q(YseDspProcessor* p);
/** Compute on one sample in `factor` and hold between (clamped to 1 .. 4096). */
YSE_C_API void yse_dsp_filter_coefficients_set_resampling(YseDspProcessor* p, unsigned int factor);
YSE_C_API unsigned int yse_dsp_filter_coefficients_get_resampling(YseDspProcessor* p);
/** The five coefficients for the current settings at the live sample rate,
   to out[0..4]. Returns 1, or 0 (out untouched) for a NULL handle or out. */
YSE_C_API int yse_dsp_filter_coefficients_get_coefficients(YseDspProcessor* p, float* out);
/** `length` samples of each coefficient at the current settings. */
YSE_C_API unsigned int yse_dsp_filter_coefficients_process(YseDspProcessor* p, float* a0, float* a1,
                                                           float* a2, float* b1, float* b2,
                                                           unsigned int length);
/** The frequency, gain and Q read per sample from three arrays of `length`
   samples; all three are required. */
YSE_C_API unsigned int
yse_dsp_filter_coefficients_process_signal(YseDspProcessor* p, const float* frequency,
                                           const float* gain, const float* q, float* a0, float* a1,
                                           float* a2, float* b1, float* b2, unsigned int length);
/** The five coefficients (a0 a1 a2 b1 b2) of `type` for `frequency` Hz,
   linear `gain` and `q` at `sample_rate`, to out[0..4] — the engine's
   filterCoefficients::Compute. No handle needed. Returns 1, or 0 for a NULL
   out. A non-finite setting or a sample rate not above 0 gives all 0. */
YSE_C_API int yse_dsp_filter_coefficients_compute(YseDspFilterType type, float frequency,
                                                  float gain, float q, float sample_rate,
                                                  float* out);
/** As yse_dsp_filter_coefficients_compute(), with the "imitation analog"
   matched design for BANDPASS, GAINBPASS and PEAKNOTCH
   (filterCoefficients::ComputeMatched); other types are Compute's. */
YSE_C_API int yse_dsp_filter_coefficients_compute_matched(YseDspFilterType type, float frequency,
                                                          float gain, float q, float sample_rate,
                                                          float* out);

/* ─── slide: logarithmic slide (#1099) ───────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_slide_create(void);
/** The slide-up amount in samples (used while the input is above the output). */
YSE_C_API void yse_dsp_slide_set_up(YseDspProcessor* p, float amount);
YSE_C_API float yse_dsp_slide_get_up(YseDspProcessor* p);
/** The slide-down amount in samples. */
YSE_C_API void yse_dsp_slide_set_down(YseDspProcessor* p, float amount);
YSE_C_API float yse_dsp_slide_get_down(YseDspProcessor* p);
/** The current output. */
YSE_C_API float yse_dsp_slide_get_value(YseDspProcessor* p);
/** Sets the output to 0. */
YSE_C_API void yse_dsp_slide_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_slide_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);
YSE_C_API unsigned int yse_dsp_slide_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* up, const float* down, float* out,
                                                    unsigned int length);
YSE_C_API float yse_dsp_slide_process_sample(YseDspProcessor* p, float in);

/* ─── rampSmooth: linear smoothing (#1100) ───────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_ramp_smooth_create(void);
/** The ramp-up length in samples. */
YSE_C_API void yse_dsp_ramp_smooth_set_up(YseDspProcessor* p, float samples);
YSE_C_API float yse_dsp_ramp_smooth_get_up(YseDspProcessor* p);
/** The ramp-down length in samples. */
YSE_C_API void yse_dsp_ramp_smooth_set_down(YseDspProcessor* p, float samples);
YSE_C_API float yse_dsp_ramp_smooth_get_down(YseDspProcessor* p);
/** Both lengths at once. */
YSE_C_API void yse_dsp_ramp_smooth_set_ramp(YseDspProcessor* p, float samples);
/** The current output. */
YSE_C_API float yse_dsp_ramp_smooth_get_value(YseDspProcessor* p);
/** Sets the output, and the value it heads for, to 0. */
YSE_C_API void yse_dsp_ramp_smooth_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_ramp_smooth_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length);
YSE_C_API unsigned int yse_dsp_ramp_smooth_process_signal(YseDspProcessor* p, const float* in,
                                                          const float* up, const float* down,
                                                          float* out, unsigned int length);
YSE_C_API float yse_dsp_ramp_smooth_process_sample(YseDspProcessor* p, float in);

/* ─── slewLimiter: limit the sample-to-sample change (#1107) ─────────────── */

YSE_C_API YseDspProcessor* yse_dsp_slew_limiter_create(void);
/** The smallest change per sample (the steepest fall when negative). */
YSE_C_API void yse_dsp_slew_limiter_set_minimum(YseDspProcessor* p, float slope);
YSE_C_API float yse_dsp_slew_limiter_get_minimum(YseDspProcessor* p);
/** The largest change per sample (the steepest rise when positive). */
YSE_C_API void yse_dsp_slew_limiter_set_maximum(YseDspProcessor* p, float slope);
YSE_C_API float yse_dsp_slew_limiter_get_maximum(YseDspProcessor* p);
/** Sets the output the next change starts from. Non-finite: ignored. */
YSE_C_API void yse_dsp_slew_limiter_set_value(YseDspProcessor* p, float value);
/** The current output. */
YSE_C_API float yse_dsp_slew_limiter_get_value(YseDspProcessor* p);
/** Sets the output to 0. */
YSE_C_API void yse_dsp_slew_limiter_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_slew_limiter_process(YseDspProcessor* p, const float* in, float* out,
                                                    unsigned int length);
YSE_C_API unsigned int yse_dsp_slew_limiter_process_signal(YseDspProcessor* p, const float* in,
                                                           const float* minimum,
                                                           const float* maximum, float* out,
                                                           unsigned int length);
YSE_C_API float yse_dsp_slew_limiter_process_sample(YseDspProcessor* p, float in);

#ifdef __cplusplus
}
#endif

#endif
