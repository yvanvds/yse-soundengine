/**
  @file
  yse_dsp_operators.h — the signal operators of the MSP port. C ABI mirror of
  the YseEngine/dsp/operators/ classes added by #1034 (issue #1300):
  accumulator, reverseSubtract, reverseDivide, modulo, bitwise, bitnot,
  bitshift, bitsafe, compare, minimum, maximum, degrade, delta, downsample,
  fold, overdrive, cartToPolar, polarToCart, phaseWrap, round, trunc, scale,
  trig, arcTangent2, ampToDb, dbToAmp, msToSamples and samplesToMs. The engine
  headers document each operator's formula, clamping and defaults; this
  header documents only what the C mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions write
  `length` samples to host float arrays, return the number of samples
  written, and are allocation-free and safe on a real-time thread. Every call
  here but a constructor allocates nothing, takes no lock and never sets
  yse_last_error().

  The operators mirror the engine classes one to one. trig, compare and
  bitwise are single engine classes with a function or operation enum, so
  they are single C classes with that enum; the arithmetic operators are
  separate engine classes, so they are separate C classes.

  Names. Each operator has up to four ways to run it:

  - yse_dsp_CLASS_value(): one sample without a handle, for the stateless
    operators whose engine class has a static per-sample formula. It takes
    every setting as an argument.
  - yse_dsp_CLASS_process_sample(): one sample through a handle, for the
    operators that keep state or settings (an accumulator's sum, a fold's
    range).
  - yse_dsp_CLASS_process(): a block at the current settings. The binary
    operators (reverse subtract, minimum, compare, ...) take their right
    operand here as one number.
  - yse_dsp_CLASS_process_signal(): a block whose right operand or settings
    are read per sample from host arrays, one value per sample, each
    `length` long. Settings read this way also update the getters.

  The two converters with two outputs (cartToPolar, polarToCart) take both
  outputs as optional arrays: pass NULL for one you do not need. A call with
  both NULL still runs and returns `length`.

  Enums. An enum setter ignores a value outside the enum. A constructor that
  takes an enum uses the engine default for a value outside it. A value call
  given an enum outside it returns 0.

  Not mirrored: the free helper functions beside the classes — BitModeFromInt,
  BitLeftIsInteger, BitRightIsInteger, FloatToBits and BitsToFloat
  (bitwise.hpp), and AmpToDb, DbToAmp, MsToSamples and SamplesToMs
  (units.hpp). The unit ones are the value calls of the unit classes (the
  sample-rate ones take the rate as an argument; yse_system.h reports the
  engine's). bitsafe's in-place apply() is process() with the same array as
  input and output.
*/

#ifndef YSE_C_DSP_OPERATORS_H_INCLUDED
#define YSE_C_DSP_OPERATORS_H_INCLUDED

#include "yse_common.h"
#include "yse_dsp_processor.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── accumulator: a running sum (#1047) ─────────────────────────────────── */

/** An accumulator whose sum starts at `initial`. */
YSE_C_API YseDspProcessor* yse_dsp_accumulator_create(double initial);
/** Sets the sum: the next output is `value` plus that sample's input. */
YSE_C_API void yse_dsp_accumulator_set(YseDspProcessor* p, double value);
YSE_C_API void yse_dsp_accumulator_reset(YseDspProcessor* p);
/** The sum, kept in double precision. */
YSE_C_API double yse_dsp_accumulator_get_sum(YseDspProcessor* p);
YSE_C_API float yse_dsp_accumulator_process_sample(YseDspProcessor* p, float in);
/** One sample; a `reset` that is not 0 sets the sum to 0 first. */
YSE_C_API float yse_dsp_accumulator_process_sample_reset(YseDspProcessor* p, float in, float reset);
YSE_C_API unsigned int yse_dsp_accumulator_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length);
/** The sum is set to 0 before every sample where `reset` is not 0. */
YSE_C_API unsigned int yse_dsp_accumulator_process_reset(YseDspProcessor* p, const float* in,
                                                         const float* reset, float* out,
                                                         unsigned int length);

/* ─── reverseSubtract: right - in (#1046) ────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_reverse_subtract_create(void);
YSE_C_API float yse_dsp_reverse_subtract_value(float in, float right);
YSE_C_API unsigned int yse_dsp_reverse_subtract_process(YseDspProcessor* p, const float* in,
                                                        float right, float* out,
                                                        unsigned int length);
YSE_C_API unsigned int yse_dsp_reverse_subtract_process_signal(YseDspProcessor* p, const float* in,
                                                               const float* right, float* out,
                                                               unsigned int length);

/* ─── reverseDivide: right / in, 0 where in is 0 (#1046) ─────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_reverse_divide_create(void);
YSE_C_API float yse_dsp_reverse_divide_value(float in, float right);
YSE_C_API unsigned int yse_dsp_reverse_divide_process(YseDspProcessor* p, const float* in,
                                                      float right, float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_reverse_divide_process_signal(YseDspProcessor* p, const float* in,
                                                             const float* right, float* out,
                                                             unsigned int length);

/* ─── modulo: the fmod remainder, 0 for a divisor of 0 (#1046) ───────────── */

YSE_C_API YseDspProcessor* yse_dsp_modulo_create(void);
YSE_C_API float yse_dsp_modulo_value(float in, float divisor);
YSE_C_API unsigned int yse_dsp_modulo_process(YseDspProcessor* p, const float* in, float divisor,
                                              float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_modulo_process_signal(YseDspProcessor* p, const float* in,
                                                     const float* divisor, float* out,
                                                     unsigned int length);

/* ─── bitwise: and, or, xor with a mask or a second signal (#1051)
   A mask is a 32-bit pattern, used as it is in every mode. */

/** An operation `op` against `mask`, reading its operands as `mode`. */
YSE_C_API YseDspProcessor* yse_dsp_bitwise_create(YseDspBitwiseOp op, unsigned int mask,
                                                  YseDspBitMode mode);
YSE_C_API void yse_dsp_bitwise_set_op(YseDspProcessor* p, YseDspBitwiseOp op);
YSE_C_API YseDspBitwiseOp yse_dsp_bitwise_get_op(YseDspProcessor* p);
YSE_C_API void yse_dsp_bitwise_set_mask(YseDspProcessor* p, unsigned int mask);
YSE_C_API unsigned int yse_dsp_bitwise_get_mask(YseDspProcessor* p);
YSE_C_API void yse_dsp_bitwise_set_mode(YseDspProcessor* p, YseDspBitMode mode);
YSE_C_API YseDspBitMode yse_dsp_bitwise_get_mode(YseDspProcessor* p);
/** `left` against the 32 bits `right_bits`. */
YSE_C_API float yse_dsp_bitwise_value(YseDspBitwiseOp op, YseDspBitMode mode, float left,
                                      unsigned int right_bits);
/** A block against the mask. */
YSE_C_API unsigned int yse_dsp_bitwise_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length);
/** A block against `right` sample by sample, read with the right half of the
   mode. */
YSE_C_API unsigned int yse_dsp_bitwise_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length);

/* ─── bitnot: every bit inverted (#1051) ─────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_bitnot_create(YseDspBitMode mode);
YSE_C_API void yse_dsp_bitnot_set_mode(YseDspProcessor* p, YseDspBitMode mode);
YSE_C_API YseDspBitMode yse_dsp_bitnot_get_mode(YseDspProcessor* p);
YSE_C_API float yse_dsp_bitnot_value(YseDspBitMode mode, float in);
YSE_C_API unsigned int yse_dsp_bitnot_process(YseDspProcessor* p, const float* in, float* out,
                                              unsigned int length);

/* ─── bitshift: bits moved left (positive) or right (negative) (#1051) ───── */

YSE_C_API YseDspProcessor* yse_dsp_bitshift_create(int shift, YseDspBitMode mode);
YSE_C_API void yse_dsp_bitshift_set_shift(YseDspProcessor* p, int shift);
YSE_C_API int yse_dsp_bitshift_get_shift(YseDspProcessor* p);
YSE_C_API void yse_dsp_bitshift_set_mode(YseDspProcessor* p, YseDspBitMode mode);
YSE_C_API YseDspBitMode yse_dsp_bitshift_get_mode(YseDspProcessor* p);
YSE_C_API float yse_dsp_bitshift_value(YseDspBitMode mode, int shift, float in);
YSE_C_API unsigned int yse_dsp_bitshift_process(YseDspProcessor* p, const float* in, float* out,
                                                unsigned int length);

/* ─── bitsafe: NaN and infinity replaced with 0 (#1051) ──────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_bitsafe_create(void);
YSE_C_API float yse_dsp_bitsafe_value(float in);
/** `out` may be `in`: the engine's in-place apply(). */
YSE_C_API unsigned int yse_dsp_bitsafe_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length);

/* ─── compare: 1 where `in OP right` holds, else 0 (#1048) ──────────────── */

YSE_C_API YseDspProcessor* yse_dsp_compare_create(YseDspCompareOp op);
YSE_C_API void yse_dsp_compare_set_operator(YseDspProcessor* p, YseDspCompareOp op);
YSE_C_API YseDspCompareOp yse_dsp_compare_get_operator(YseDspProcessor* p);
/** The equality tolerance; a negative value is taken as 0. 0. */
YSE_C_API void yse_dsp_compare_set_fuzzy(YseDspProcessor* p, float tolerance);
YSE_C_API float yse_dsp_compare_get_fuzzy(YseDspProcessor* p);
YSE_C_API float yse_dsp_compare_value(YseDspCompareOp op, float in, float right, float fuzzy);
YSE_C_API unsigned int yse_dsp_compare_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_compare_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length);

/* ─── minimum, maximum: the smaller / larger of two operands (#1048) ─────── */

YSE_C_API YseDspProcessor* yse_dsp_minimum_create(void);
YSE_C_API float yse_dsp_minimum_value(float in, float right);
YSE_C_API unsigned int yse_dsp_minimum_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_minimum_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length);

YSE_C_API YseDspProcessor* yse_dsp_maximum_create(void);
YSE_C_API float yse_dsp_maximum_value(float in, float right);
YSE_C_API unsigned int yse_dsp_maximum_process(YseDspProcessor* p, const float* in, float right,
                                               float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_maximum_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* right, float* out,
                                                      unsigned int length);

/* ─── degrade: a bitcrusher, sample-rate ratio and bit depth (#1106) ─────── */

/** The bits until they are set, and the range they are clamped to. */
#define YSE_DSP_DEGRADE_DEFAULT_BITS 24
#define YSE_DSP_DEGRADE_MIN_BITS 1
#define YSE_DSP_DEGRADE_MAX_BITS 24

YSE_C_API YseDspProcessor* yse_dsp_degrade_create(void);
/** The fraction of the sample rate at which the input is taken, clamped to
   0 .. 1; NaN is ignored. 1. */
YSE_C_API void yse_dsp_degrade_set_ratio(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_degrade_get_ratio(YseDspProcessor* p);
YSE_C_API void yse_dsp_degrade_set_bits(YseDspProcessor* p, int bits);
YSE_C_API int yse_dsp_degrade_get_bits(YseDspProcessor* p);
/** Sets the held sample to 0 and makes the next sample a take. */
YSE_C_API void yse_dsp_degrade_reset(YseDspProcessor* p);
/** The sample last taken, before quantization. */
YSE_C_API float yse_dsp_degrade_get_held(YseDspProcessor* p);
YSE_C_API float yse_dsp_degrade_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_degrade_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length);
YSE_C_API unsigned int yse_dsp_degrade_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* ratio, const float* bits,
                                                      float* out, unsigned int length);

/* ─── delta: in[n] - in[n - 1] (#1108) ───────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_delta_create(void);
/** Sets the remembered sample: the next output is that input minus `value`. */
YSE_C_API void yse_dsp_delta_set(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_delta_reset(YseDspProcessor* p);
YSE_C_API float yse_dsp_delta_get_previous(YseDspProcessor* p);
YSE_C_API float yse_dsp_delta_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_delta_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);

/* ─── downsample: a take every `period` samples, held (#1109) ───────────── */

/** The shortest period; a shorter one is raised to it. */
#define YSE_DSP_DOWNSAMPLE_MIN_PERIOD 1.0f

YSE_C_API YseDspProcessor* yse_dsp_downsample_create(void);
/** Samples each take is held; NaN is ignored. 1. */
YSE_C_API void yse_dsp_downsample_set_period(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_downsample_get_period(YseDspProcessor* p);
/** Sets the held sample to 0 and makes the next sample a take. */
YSE_C_API void yse_dsp_downsample_reset(YseDspProcessor* p);
YSE_C_API float yse_dsp_downsample_get_held(YseDspProcessor* p);
YSE_C_API float yse_dsp_downsample_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_downsample_process(YseDspProcessor* p, const float* in, float* out,
                                                  unsigned int length);
YSE_C_API unsigned int yse_dsp_downsample_process_signal(YseDspProcessor* p, const float* in,
                                                         const float* period, float* out,
                                                         unsigned int length);

/* ─── fold: folded, wrapped or clipped into low .. high (#1113) ──────────── */

YSE_C_API YseDspProcessor* yse_dsp_fold_create(void);
/** YSE_DSP_FOLD_MODE_FOLD. */
YSE_C_API void yse_dsp_fold_set_mode(YseDspProcessor* p, YseDspFoldMode mode);
YSE_C_API YseDspFoldMode yse_dsp_fold_get_mode(YseDspProcessor* p);
/** A limit that is not finite is ignored. 0 .. 1. */
YSE_C_API void yse_dsp_fold_set_low(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_fold_set_high(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_fold_set_range(YseDspProcessor* p, float low, float high);
YSE_C_API float yse_dsp_fold_get_low(YseDspProcessor* p);
YSE_C_API float yse_dsp_fold_get_high(YseDspProcessor* p);
/** `value` limited to `low` .. `high` (either order) as `mode` says: the
   engine's fold::Limit(). Both limits must be finite; a setter ignores a
   limit that is not, this call does not check. */
YSE_C_API float yse_dsp_fold_value(float value, float low, float high, YseDspFoldMode mode);
YSE_C_API float yse_dsp_fold_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_fold_process(YseDspProcessor* p, const float* in, float* out,
                                            unsigned int length);
YSE_C_API unsigned int yse_dsp_fold_process_signal(YseDspProcessor* p, const float* in,
                                                   const float* low, const float* high, float* out,
                                                   unsigned int length);

/* ─── overdrive: a soft clipper with a drive factor (#1112) ──────────────── */

YSE_C_API YseDspProcessor* yse_dsp_overdrive_create(void);
/** A drive that is not finite is ignored. 1 (linear). */
YSE_C_API void yse_dsp_overdrive_set_drive(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_overdrive_get_drive(YseDspProcessor* p);
YSE_C_API float yse_dsp_overdrive_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_overdrive_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length);
YSE_C_API unsigned int yse_dsp_overdrive_process_signal(YseDspProcessor* p, const float* in,
                                                        const float* drive, float* out,
                                                        unsigned int length);

/* ─── cartToPolar: (re, im) to magnitude and phase (#1053)
   Both outputs are optional (NULL to skip). */

YSE_C_API YseDspProcessor* yse_dsp_cart_to_polar_create(void);
/** One point; either output may be NULL. */
YSE_C_API void yse_dsp_cart_to_polar_value(float re, float im, float* magnitude, float* phase);
YSE_C_API unsigned int yse_dsp_cart_to_polar_process(YseDspProcessor* p, const float* re, float im,
                                                     float* magnitude, float* phase,
                                                     unsigned int length);
YSE_C_API unsigned int yse_dsp_cart_to_polar_process_signal(YseDspProcessor* p, const float* re,
                                                            const float* im, float* magnitude,
                                                            float* phase, unsigned int length);

/* ─── polarToCart: magnitude and phase to (re, im) (#1053)
   Both outputs are optional (NULL to skip). */

YSE_C_API YseDspProcessor* yse_dsp_polar_to_cart_create(void);
/** One point; either output may be NULL. */
YSE_C_API void yse_dsp_polar_to_cart_value(float magnitude, float phase, float* re, float* im);
YSE_C_API unsigned int yse_dsp_polar_to_cart_process(YseDspProcessor* p, const float* magnitude,
                                                     float phase, float* re, float* im,
                                                     unsigned int length);
YSE_C_API unsigned int yse_dsp_polar_to_cart_process_signal(YseDspProcessor* p,
                                                            const float* magnitude,
                                                            const float* phase, float* re,
                                                            float* im, unsigned int length);

/* ─── phaseWrap: angles wrapped into -pi .. pi (#1053) ───────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_phase_wrap_create(void);
YSE_C_API float yse_dsp_phase_wrap_value(float in);
YSE_C_API unsigned int yse_dsp_phase_wrap_process(YseDspProcessor* p, const float* in, float* out,
                                                  unsigned int length);

/* ─── round: to a multiple of a step (#1050) ─────────────────────────────── */

/** A rounder with `step` (0 passes the signal through), to the nearest
   multiple if `nearest` is not 0, else down. */
YSE_C_API YseDspProcessor* yse_dsp_round_create(float step, int nearest);
YSE_C_API void yse_dsp_round_set_step(YseDspProcessor* p, float step);
YSE_C_API float yse_dsp_round_get_step(YseDspProcessor* p);
YSE_C_API void yse_dsp_round_set_nearest(YseDspProcessor* p, int nearest);
YSE_C_API int yse_dsp_round_get_nearest(YseDspProcessor* p);
YSE_C_API float yse_dsp_round_value(float in, float step, int nearest);
/** A block against the step that was set. */
YSE_C_API unsigned int yse_dsp_round_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);
/** A block against `step` sample by sample. The getter keeps the step that
   was set. */
YSE_C_API unsigned int yse_dsp_round_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* step, float* out,
                                                    unsigned int length);

/* ─── trunc: the fraction dropped, toward zero (#1050) ───────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_trunc_create(void);
YSE_C_API float yse_dsp_trunc_value(float in);
YSE_C_API unsigned int yse_dsp_trunc_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);

/* ─── scale: in_low .. in_high mapped onto out_low .. out_high (#1114) ───── */

YSE_C_API YseDspProcessor* yse_dsp_scale_create(void);
/** YSE_DSP_SCALE_MODERN. */
YSE_C_API void yse_dsp_scale_set_mode(YseDspProcessor* p, YseDspScaleMode mode);
YSE_C_API YseDspScaleMode yse_dsp_scale_get_mode(YseDspProcessor* p);
/** A setting that is not finite is ignored. 0 .. 1 onto 0 .. 1, exponent 1. */
YSE_C_API void yse_dsp_scale_set_in_low(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_scale_set_in_high(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_scale_set_out_low(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_scale_set_out_high(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_scale_set_exponent(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_scale_set_in_range(YseDspProcessor* p, float low, float high);
YSE_C_API void yse_dsp_scale_set_out_range(YseDspProcessor* p, float low, float high);
YSE_C_API float yse_dsp_scale_get_in_low(YseDspProcessor* p);
YSE_C_API float yse_dsp_scale_get_in_high(YseDspProcessor* p);
YSE_C_API float yse_dsp_scale_get_out_low(YseDspProcessor* p);
YSE_C_API float yse_dsp_scale_get_out_high(YseDspProcessor* p);
YSE_C_API float yse_dsp_scale_get_exponent(YseDspProcessor* p);
/** `value` mapped as `mode` says: the engine's scale::Map(). */
YSE_C_API float yse_dsp_scale_value(float value, float in_low, float in_high, float out_low,
                                    float out_high, float exponent, YseDspScaleMode mode);
YSE_C_API float yse_dsp_scale_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_scale_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length);
/** Every setting read per sample from its array. */
YSE_C_API unsigned int yse_dsp_scale_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* in_low, const float* in_high,
                                                    const float* out_low, const float* out_high,
                                                    const float* exponent, float* out,
                                                    unsigned int length);

/* ─── trig: a trigonometric or hyperbolic function, radians (#1049) ──────── */

YSE_C_API YseDspProcessor* yse_dsp_trig_create(YseDspTrigFunction fn);
YSE_C_API void yse_dsp_trig_set_function(YseDspProcessor* p, YseDspTrigFunction fn);
YSE_C_API YseDspTrigFunction yse_dsp_trig_get_function(YseDspProcessor* p);
YSE_C_API float yse_dsp_trig_value(YseDspTrigFunction fn, float in);
YSE_C_API unsigned int yse_dsp_trig_process(YseDspProcessor* p, const float* in, float* out,
                                            unsigned int length);

/* ─── arcTangent2: the angle of (x, y), y first (#1049) ─────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_arc_tangent2_create(void);
YSE_C_API float yse_dsp_arc_tangent2_value(float y, float x);
YSE_C_API unsigned int yse_dsp_arc_tangent2_process(YseDspProcessor* p, const float* y, float x,
                                                    float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_arc_tangent2_process_signal(YseDspProcessor* p, const float* y,
                                                           const float* x, float* out,
                                                           unsigned int length);

/* ─── ampToDb, dbToAmp: amplitude <-> dBFS (#1052) ───────────────────────── */

/** The decibel value silence converts to, and the level at or below which
   a level converts to an amplitude of 0. */
#define YSE_DSP_DB_FLOOR (-999.0f)

YSE_C_API YseDspProcessor* yse_dsp_amp_to_db_create(void);
YSE_C_API float yse_dsp_amp_to_db_value(float amplitude);
YSE_C_API unsigned int yse_dsp_amp_to_db_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length);

YSE_C_API YseDspProcessor* yse_dsp_db_to_amp_create(void);
YSE_C_API float yse_dsp_db_to_amp_value(float db);
YSE_C_API unsigned int yse_dsp_db_to_amp_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length);

/* ─── msToSamples, samplesToMs: at the live sample rate (#1052)
   The blocks read the engine's sample rate once per 128 samples; the value
   calls take the rate as an argument. */

YSE_C_API YseDspProcessor* yse_dsp_ms_to_samples_create(void);
YSE_C_API float yse_dsp_ms_to_samples_value(float ms, float sample_rate);
YSE_C_API unsigned int yse_dsp_ms_to_samples_process(YseDspProcessor* p, const float* in,
                                                     float* out, unsigned int length);

YSE_C_API YseDspProcessor* yse_dsp_samples_to_ms_create(void);
/** 0 for a sample rate of 0 or below. */
YSE_C_API float yse_dsp_samples_to_ms_value(float samples, float sample_rate);
YSE_C_API unsigned int yse_dsp_samples_to_ms_process(YseDspProcessor* p, const float* in,
                                                     float* out, unsigned int length);

#ifdef __cplusplus
}
#endif

#endif
