/**
  @file
  yse_dsp_filter_design.h — the filter-design toolkit of the MSP port.
  C ABI mirror of four YseEngine/dsp/filters/ classes added by #1034 (issue
  #1296): filterDesign (Butterworth / Chebyshev design into second-order
  sections), filterResponse (the curves of a cascade of sections),
  filterBands (the state of a filter editor, the model behind ~filtergraph)
  and zPlane (sections read and edited as poles and zeroes, the model behind
  ~zplane). The engine headers document the design method, the clamping and
  the defaults; this header documents only what the C mirror adds.

  None of these classes processes samples. They hold a design or an editor's
  state and produce coefficient lists for yse_dsp_biquad_cascade_*
  (yse_dsp_filters.h), the biquadCascade form: five values per section,
  a0 a1 a2 b1 b2, with the feedback coefficients subtracted.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter return 0. Every call but a constructor allocates
  nothing, takes no lock and never sets yse_last_error(), so a host may
  redesign a filter on its real-time thread.

  Lists are copied out in the snprintf style. A list getter takes an `out`
  array of `capacity` values and returns the number of values the whole list
  has. It writes as many whole entries (a five-value section, a real /
  imaginary pair, ...) as `capacity` holds, so a short array gets a complete
  prefix and never half a section. Pass NULL and 0 to learn the size first.
  It returns 0 for a NULL handle or one of another class.

  A root (a pole or a zero) crosses as two values, real then imaginary. A
  fixed-size array parameter (`out[5]`, `zeroes[4]`) is required: a NULL one
  makes the call return 0 and write nothing.
*/

#ifndef YSE_C_DSP_FILTER_DESIGN_H_INCLUDED
#define YSE_C_DSP_FILTER_DESIGN_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"
#include "yse_dsp_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── filterDesign: Butterworth and Chebyshev filters of an order (#1101)
   Set the specification, call yse_dsp_filter_design_design(), then read the
   result as sections, as zeroes / poles / gain, or as a transfer function.
   Setters ignore values the engine ignores (non-finite numbers, a ripple or
   attenuation not above 0, a negative sample rate) and enum values out of
   range. */

/** At most this many poles; the order is clamped to 1 .. this. */
#define YSE_DSP_FILTER_DESIGN_MAX_ORDER 32
/** At most this many sections, YSE_DSP_FILTER_DESIGN_MAX_ORDER / 2. */
#define YSE_DSP_FILTER_DESIGN_MAX_SECTIONS 16

/** A Butterworth lowpass of order 2 at 1000 Hz (band edges 1000 and 2000 Hz),
   1 dB ripple, 40 dB attenuation, at the engine's sample rate. Nothing is
   designed until yse_dsp_filter_design_design() runs. */
YSE_C_API YseDspProcessor* yse_dsp_filter_design_create(void);
YSE_C_API void yse_dsp_filter_design_set_response(YseDspProcessor* p, YseDspDesignResponse value);
/** YSE_DSP_DESIGN_LOWPASS for a NULL handle. */
YSE_C_API YseDspDesignResponse yse_dsp_filter_design_get_response(YseDspProcessor* p);
YSE_C_API void yse_dsp_filter_design_set_topology(YseDspProcessor* p, YseDspDesignTopology value);
/** YSE_DSP_DESIGN_BUTTERWORTH for a NULL handle. */
YSE_C_API YseDspDesignTopology yse_dsp_filter_design_get_topology(YseDspProcessor* p);
/** The number of poles, clamped to 1 .. YSE_DSP_FILTER_DESIGN_MAX_ORDER. */
YSE_C_API void yse_dsp_filter_design_set_order(YseDspProcessor* p, unsigned int order);
/** The order asked for (a bandpass or bandstop of odd order gets one more
   pole: see yse_dsp_filter_design_get_pole_count()). */
YSE_C_API unsigned int yse_dsp_filter_design_get_order(YseDspProcessor* p);
/** The cutoff, or the lower band edge, in the design's units. */
YSE_C_API void yse_dsp_filter_design_set_frequency(YseDspProcessor* p, float value);
/** Both band edges, in either order. A non-finite value leaves that edge. */
YSE_C_API void yse_dsp_filter_design_set_frequencies(YseDspProcessor* p, float first, float second);
/** Frequency 0 or 1; any index above 0 reads the second. */
YSE_C_API float yse_dsp_filter_design_get_frequency(YseDspProcessor* p, unsigned int index);
YSE_C_API void yse_dsp_filter_design_set_units(YseDspProcessor* p, YseDspDesignUnits value);
/** YSE_DSP_DESIGN_HERTZ for a NULL handle. */
YSE_C_API YseDspDesignUnits yse_dsp_filter_design_get_units(YseDspProcessor* p);
/** The rate hertz are read against; 0 (the default) is the engine's sample
   rate when the design runs. */
YSE_C_API void yse_dsp_filter_design_set_sample_rate(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_filter_design_get_sample_rate(YseDspProcessor* p);
/** Chebyshev type 1 passband ripple, in dB. */
YSE_C_API void yse_dsp_filter_design_set_passband_ripple(YseDspProcessor* p, float decibels);
YSE_C_API float yse_dsp_filter_design_get_passband_ripple(YseDspProcessor* p);
/** Chebyshev type 2 stopband attenuation, in dB. */
YSE_C_API void yse_dsp_filter_design_set_stopband_attenuation(YseDspProcessor* p, float decibels);
YSE_C_API float yse_dsp_filter_design_get_stopband_attenuation(YseDspProcessor* p);
/** Designs the filter from the current settings. Returns 1, or 0 when the
   frequencies cannot be designed (the design then has no sections and no
   poles) or for a NULL handle. */
YSE_C_API int yse_dsp_filter_design_design(YseDspProcessor* p);
/** The sections of the last design: 0 before the first and after a failed
   one. */
YSE_C_API unsigned int yse_dsp_filter_design_get_sections(YseDspProcessor* p);
/** Section `index`'s five coefficients to out[0..4]: all 0 past the last
   section. Returns 1, or 0 (out untouched) for a NULL handle or out. */
YSE_C_API int yse_dsp_filter_design_get_section(YseDspProcessor* p, unsigned int index, float* out);
/** Every section, five values each: a list getter (see the top of this
   header), ready for yse_dsp_biquad_cascade_set_coefficients(). */
YSE_C_API unsigned int yse_dsp_filter_design_get_coefficients(YseDspProcessor* p, float* out,
                                                              unsigned int capacity);
/** As yse_dsp_filter_design_get_coefficients(), in double precision. */
YSE_C_API unsigned int yse_dsp_filter_design_get_coefficients_double(YseDspProcessor* p,
                                                                     double* out,
                                                                     unsigned int capacity);
/** The poles of the last design, which has as many zeroes. */
YSE_C_API unsigned int yse_dsp_filter_design_get_pole_count(YseDspProcessor* p);
/** The zeroes of the last design as real / imaginary pairs: a list getter
   whose entries are pairs (2 values per zero). */
YSE_C_API unsigned int yse_dsp_filter_design_get_zeroes(YseDspProcessor* p, double* out,
                                                        unsigned int capacity);
/** The poles, as yse_dsp_filter_design_get_zeroes() writes the zeroes. */
YSE_C_API unsigned int yse_dsp_filter_design_get_poles(YseDspProcessor* p, double* out,
                                                       unsigned int capacity);
/** The overall gain of the last design. */
YSE_C_API double yse_dsp_filter_design_get_gain(YseDspProcessor* p);
/** The transfer function's numerator, the coefficients of z^0 .. z^-n for
   the n poles of the last design: a list getter of n + 1 values (before a
   design, and after a failed one, n is 0 and the one value is 0). */
YSE_C_API unsigned int yse_dsp_filter_design_get_numerator(YseDspProcessor* p, double* out,
                                                           unsigned int capacity);
/** The denominator, as the numerator; its first value is 1 after a design
   (0 before one). */
YSE_C_API unsigned int yse_dsp_filter_design_get_denominator(YseDspProcessor* p, double* out,
                                                             unsigned int capacity);
/** The zeroes and poles of one section `coefficients[0..4]` (a0 a1 a2 b1 b2):
   zeroes[0..3] and poles[0..3] get two real / imaginary pairs each, a complex
   pair upper half first, a real pair the larger first. No handle needed.
   Returns 1; 0 when a0 is 0 or a coefficient is not finite (the zeroes then
   read 0; the poles are still written, as 0 for a non-finite coefficient),
   and 0 with nothing written when an array is NULL. */
YSE_C_API int yse_dsp_filter_design_section_roots(const float* coefficients, double* zeroes,
                                                  double* poles);
/** The section with gain `gain`, zeroes zeroes[0..3] and poles poles[0..3]
   (pairs as above) to out[0..4]: the inverse of
   yse_dsp_filter_design_section_roots(). Each pair should be two real roots
   or a conjugate pair; of any other pair the real parts are taken. Returns 1,
   or 0 (out untouched) when an array is NULL. */
YSE_C_API int yse_dsp_filter_design_section_from_roots(double gain, const double* zeroes,
                                                       const double* poles, float* out);

/* ─── filterResponse: the curves of a cascade of sections (#1101)
   Six curves of `points` values each: magnitude (linear gain), unwrapped
   phase (radians), phase delay and group delay (samples) at the frequencies
   yse_dsp_filter_response_get_frequency() gives, and the impulse and step
   responses (samples). Each curve getter is a list getter of `points`
   values. */

/** Room for `capacity` points per curve (clamped to 1 .. 16384; 0 means 512),
   all in use. Every curve is 0 until an analysis runs. */
YSE_C_API YseDspProcessor* yse_dsp_filter_response_create(unsigned int capacity);
/** The points the next analysis computes, clamped to 1 .. the capacity. */
YSE_C_API void yse_dsp_filter_response_set_points(YseDspProcessor* p, unsigned int count);
YSE_C_API unsigned int yse_dsp_filter_response_get_points(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_filter_response_get_capacity(YseDspProcessor* p);
/** Analyses `count` values in sets of five (at most 64 sections; an
   incomplete trailing set is ignored). Returns 1, or 0, changing nothing,
   without one complete set, with a value that is not finite, or with a NULL
   handle or array. */
YSE_C_API int yse_dsp_filter_response_analyse(YseDspProcessor* p, const float* values,
                                              unsigned int count);
/** As yse_dsp_filter_response_analyse(), in double precision. */
YSE_C_API int yse_dsp_filter_response_analyse_double(YseDspProcessor* p, const double* values,
                                                     unsigned int count);
/** Analyses the sections of a yse_dsp_filter_design_create() handle, in double
   precision. Returns 0, changing nothing, when `design` has no sections or is
   not a filter design. */
YSE_C_API int yse_dsp_filter_response_analyse_design(YseDspProcessor* p, YseDspProcessor* design);
/** The sections of the last analysis. */
YSE_C_API unsigned int yse_dsp_filter_response_get_sections(YseDspProcessor* p);
/** The frequency of point `index` in radians per sample: pi * index / points. */
YSE_C_API double yse_dsp_filter_response_get_frequency(YseDspProcessor* p, unsigned int index);
YSE_C_API unsigned int yse_dsp_filter_response_get_magnitude(YseDspProcessor* p, float* out,
                                                             unsigned int capacity);
YSE_C_API unsigned int yse_dsp_filter_response_get_phase(YseDspProcessor* p, float* out,
                                                         unsigned int capacity);
YSE_C_API unsigned int yse_dsp_filter_response_get_phase_delay(YseDspProcessor* p, float* out,
                                                               unsigned int capacity);
YSE_C_API unsigned int yse_dsp_filter_response_get_group_delay(YseDspProcessor* p, float* out,
                                                               unsigned int capacity);
YSE_C_API unsigned int yse_dsp_filter_response_get_impulse(YseDspProcessor* p, float* out,
                                                           unsigned int capacity);
YSE_C_API unsigned int yse_dsp_filter_response_get_step(YseDspProcessor* p, float* out,
                                                        unsigned int capacity);

/* ─── filterBands: the state of a filter editor (#1102)
   Up to 24 biquad bands, each a mode with a frequency (Hz), a linear gain and
   a Q, or five coefficients as they are (YSE_DSP_BAND_DISPLAY). A band index
   at or past 24 is ignored by the setters and reads as a default band. A
   `sample_rate` argument of 0, or any not above 0, means the live engine
   sample rate. */

/** At most this many bands. */
#define YSE_DSP_FILTER_BANDS_MAX 24

/** One band in use: a lowpass at 1000 Hz, gain 1, Q 0.7071 (flat). */
YSE_C_API YseDspProcessor* yse_dsp_filter_bands_create(void);
/** The bands in use, clamped to 1 .. YSE_DSP_FILTER_BANDS_MAX. Bands past
   the count keep their settings. */
YSE_C_API void yse_dsp_filter_bands_set_bands(YseDspProcessor* p, unsigned int count);
YSE_C_API unsigned int yse_dsp_filter_bands_get_bands(YseDspProcessor* p);
/** A mode out of range is ignored. */
YSE_C_API void yse_dsp_filter_bands_set_mode(YseDspProcessor* p, unsigned int band,
                                             YseDspBandMode mode);
/** YSE_DSP_BAND_DISPLAY (0) for a NULL handle. */
YSE_C_API YseDspBandMode yse_dsp_filter_bands_get_mode(YseDspProcessor* p, unsigned int band);
/** Frequency, gain and Q at once, each clamped into the band's constraints. */
YSE_C_API void yse_dsp_filter_bands_set_parameters(YseDspProcessor* p, unsigned int band,
                                                   float frequency, float gain, float q);
YSE_C_API void yse_dsp_filter_bands_set_frequency(YseDspProcessor* p, unsigned int band, float hz);
YSE_C_API float yse_dsp_filter_bands_get_frequency(YseDspProcessor* p, unsigned int band);
YSE_C_API void yse_dsp_filter_bands_set_gain(YseDspProcessor* p, unsigned int band, float value);
YSE_C_API float yse_dsp_filter_bands_get_gain(YseDspProcessor* p, unsigned int band);
/** The Q, or the slope of a shelf. */
YSE_C_API void yse_dsp_filter_bands_set_q(YseDspProcessor* p, unsigned int band, float value);
YSE_C_API float yse_dsp_filter_bands_get_q(YseDspProcessor* p, unsigned int band);
/** Whether a lowpass, highpass, bandpass, bandstop or resonant band reads its
   gain (off by default). */
YSE_C_API void yse_dsp_filter_bands_set_gain_enabled(YseDspProcessor* p, unsigned int band, int on);
YSE_C_API int yse_dsp_filter_bands_get_gain_enabled(YseDspProcessor* p, unsigned int band);
/** Whether a bandpass or peak/notch band uses the matched "imitation analog"
   design (off by default). */
YSE_C_API void yse_dsp_filter_bands_set_analog(YseDspProcessor* p, unsigned int band, int on);
YSE_C_API int yse_dsp_filter_bands_get_analog(YseDspProcessor* p, unsigned int band);
/** Whether a host shows a draggable handle on a DISPLAY band. Stored only. */
YSE_C_API void yse_dsp_filter_bands_set_handle(YseDspProcessor* p, unsigned int band, int on);
YSE_C_API int yse_dsp_filter_bands_get_handle(YseDspProcessor* p, unsigned int band);
/** Bounds on the band's frequency, gain and Q; 0 is none. Clamps the current
   values into them. */
YSE_C_API void yse_dsp_filter_bands_set_constraints(YseDspProcessor* p, unsigned int band,
                                                    float min_frequency, float max_frequency,
                                                    float min_gain, float max_gain, float min_q,
                                                    float max_q);
/** Constraint `which`: 0 .. 5 for the minimum and maximum frequency, gain and
   Q (0 for none, and for `which` past 5). */
YSE_C_API float yse_dsp_filter_bands_get_constraint(YseDspProcessor* p, unsigned int band,
                                                    unsigned int which);
/** The five coefficients a DISPLAY band uses, from values[0..4]. Ignored when
   one is not finite or `values` is NULL. */
YSE_C_API void yse_dsp_filter_bands_set_coefficients(YseDspProcessor* p, unsigned int band,
                                                     const float* values);
/** Coefficient `index` (0 .. 4: a0 a1 a2 b1 b2) of a DISPLAY band. */
YSE_C_API void yse_dsp_filter_bands_set_coefficient(YseDspProcessor* p, unsigned int band,
                                                    unsigned int index, float value);
/** A cascade given as `count` values in sets of five: as many bands (at most
   YSE_DSP_FILTER_BANDS_MAX) become DISPLAY bands with those coefficients and
   the bands in use. Returns the bands set, or 0 (nothing changed) without one
   complete set, with a value that is not finite, or with a NULL array. */
YSE_C_API unsigned int yse_dsp_filter_bands_set_cascade(YseDspProcessor* p, const float* values,
                                                        unsigned int count);
/** Flattens a band: a lowpass or highpass gets Q 0.7071 and gain 1, a
   bandpass or bandstop is left alone, any other gets gain 1. */
YSE_C_API void yse_dsp_filter_bands_flat(YseDspProcessor* p, unsigned int band);
/** The band's frequency over its Q, in Hz. */
YSE_C_API float yse_dsp_filter_bands_get_bandwidth(YseDspProcessor* p, unsigned int band);
/** The band's five coefficients at `sample_rate` to out[0..4]. Returns 1, or
   0 (out untouched) for a NULL handle or out. */
YSE_C_API int yse_dsp_filter_bands_get_coefficients(YseDspProcessor* p, unsigned int band,
                                                    float sample_rate, float* out);
/** The bands in use, five values each, at `sample_rate`: a list getter, ready
   for yse_dsp_biquad_cascade_set_coefficients(). */
YSE_C_API unsigned int yse_dsp_filter_bands_get_sections(YseDspProcessor* p, float sample_rate,
                                                         float* out, unsigned int capacity);
/** The cascade's linear gain and phase (radians, -pi .. pi) at `hz`. Returns
   1, or 0 (nothing written) for a frequency that is not finite, a live
   sample rate of 0, or a NULL handle or output. */
YSE_C_API int yse_dsp_filter_bands_response_at(YseDspProcessor* p, float hz, float sample_rate,
                                               float* amplitude, float* phase);
/** Analyses the cascade into `curves`, a yse_dsp_filter_response_create()
   handle. Returns 1, or 0 when the analysis fails or either handle is NULL or
   of another class. */
YSE_C_API int yse_dsp_filter_bands_response(YseDspProcessor* p, YseDspProcessor* curves,
                                            float sample_rate);

/* ─── zPlane: sections as poles and zeroes (#1103)
   Up to 24 sections, each a0 a1 a2 b1 b2. Section s owns zeroes and poles
   2s and 2s + 1, and its gain is its a0. Setters ignore values that are not
   finite and indices out of range. Nothing depends on the sample rate. */

/** At most this many sections. */
#define YSE_DSP_ZPLANE_MAX_SECTIONS 24

/** One section in use, 1 0 0 0 0 (the input unchanged). */
YSE_C_API YseDspProcessor* yse_dsp_zplane_create(void);
/** The sections in use, clamped to 1 .. YSE_DSP_ZPLANE_MAX_SECTIONS. Sections
   past the count keep their coefficients. */
YSE_C_API void yse_dsp_zplane_set_sections(YseDspProcessor* p, unsigned int count);
YSE_C_API unsigned int yse_dsp_zplane_get_sections(YseDspProcessor* p);
/** The number of poles, rounded up to whole sections (2 .. 48). */
YSE_C_API void yse_dsp_zplane_set_order(YseDspProcessor* p, unsigned int poles);
/** Twice the sections in use. */
YSE_C_API unsigned int yse_dsp_zplane_get_order(YseDspProcessor* p);
/** Section `section`'s coefficients from values[0..4]. Ignored when one is
   not finite or `values` is NULL. */
YSE_C_API void yse_dsp_zplane_set_coefficients(YseDspProcessor* p, unsigned int section,
                                               const float* values);
/** Coefficient `index` (0 .. 4: a0 a1 a2 b1 b2) of `section`. */
YSE_C_API void yse_dsp_zplane_set_coefficient(YseDspProcessor* p, unsigned int section,
                                              unsigned int index, float value);
/** A cascade given as `count` values in sets of five: as many sections (at
   most YSE_DSP_ZPLANE_MAX_SECTIONS) are set and become the sections in use.
   Returns the sections set, or 0 (nothing changed) without one complete set,
   with a value that is not finite, or with a NULL array. */
YSE_C_API unsigned int yse_dsp_zplane_set_cascade(YseDspProcessor* p, const float* values,
                                                  unsigned int count);
/** Section `section`'s coefficients to out[0..4] (1 0 0 0 0 past the last
   section). Returns 1, or 0 (out untouched) for a NULL handle or out. */
YSE_C_API int yse_dsp_zplane_get_section(YseDspProcessor* p, unsigned int section, float* out);
/** The sections in use, five values each: a list getter. */
YSE_C_API unsigned int yse_dsp_zplane_get_coefficients(YseDspProcessor* p, float* out,
                                                       unsigned int capacity);
/** Whether `section` has two finite zeroes (its a0 is not 0). */
YSE_C_API int yse_dsp_zplane_get_finite(YseDspProcessor* p, unsigned int section);
/** Zero `index` (0 .. order - 1) to out[0..1], real then imaginary, in double
   precision; 0 past the order. Returns 1, or 0 (out untouched) for a NULL
   handle or out. */
YSE_C_API int yse_dsp_zplane_get_zero(YseDspProcessor* p, unsigned int index, double* out);
/** Pole `index`, as yse_dsp_zplane_get_zero() reads a zero. */
YSE_C_API int yse_dsp_zplane_get_pole(YseDspProcessor* p, unsigned int index, double* out);
/** The gain of `section`, its a0. */
YSE_C_API float yse_dsp_zplane_get_gain(YseDspProcessor* p, unsigned int section);
/** The zeroes of the sections in use as real / imaginary pairs: a list
   getter whose entries are whole sections (4 values each). */
YSE_C_API unsigned int yse_dsp_zplane_get_zeroes(YseDspProcessor* p, float* out,
                                                 unsigned int capacity);
/** The poles, as yse_dsp_zplane_get_zeroes() writes the zeroes. */
YSE_C_API unsigned int yse_dsp_zplane_get_poles(YseDspProcessor* p, float* out,
                                                unsigned int capacity);
/** The gain of each section in use: a list getter, one value per section. */
YSE_C_API unsigned int yse_dsp_zplane_get_gains(YseDspProcessor* p, float* out,
                                                unsigned int capacity);
/** Moves zero `index` to real + i imaginary, keeping the coefficients real
   (a conjugate partner follows). Returns 1, or 0 (nothing changed) for an
   index past the order, a value that is not finite, or a NULL handle. */
YSE_C_API int yse_dsp_zplane_move_zero(YseDspProcessor* p, unsigned int index, double real,
                                       double imaginary);
/** Moves pole `index`, as yse_dsp_zplane_move_zero() moves a zero; within a
   radius of 0.9999 while constrain-poles is on. */
YSE_C_API int yse_dsp_zplane_move_pole(YseDspProcessor* p, unsigned int index, double real,
                                       double imaginary);
/** Whether yse_dsp_zplane_move_pole() keeps poles inside the unit circle
   (off by default). Coefficients set directly are kept as they are. */
YSE_C_API void yse_dsp_zplane_set_constrain_poles(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_zplane_get_constrain_poles(YseDspProcessor* p);

#ifdef __cplusplus
}
#endif

#endif
