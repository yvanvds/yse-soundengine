/**
  @file
  yse_dsp_oscillators.h — oscillators, noise and phase shapers of the MSP port.
  C ABI mirror of the YseEngine/dsp/generators/ classes added by #1034 (issue
  #1297): phasor, cycle, blSaw, blRect, blTri, pinkNoise, bandLimitedRandom,
  click, pulseTrain, kink, oscillatorBank, triangleShape, trapezoidShape and
  sampleCounter. The engine headers document each generator's waveform,
  clamping and defaults; this header documents only what the C mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions write
  `length` samples to host float arrays, return the number of samples
  written, and are allocation-free and safe on a real-time thread. The one
  setter that can allocate, yse_dsp_oscillator_bank_set_table(), says so.

  A generator has no audio input, so its process functions take only an
  output array, plus the settings a call takes as numbers. The engine's
  overloads map to the C names like this:

  - yse_dsp_CLASS_process(): a block at constant settings.
  - yse_dsp_CLASS_process_signal(): a block whose every setting is read per
    sample from host arrays (one value per sample, each `length` long).
  - yse_dsp_CLASS_process_frequency() and the like: a block with only the
    named setting per sample, where the engine has that overload.
  - yse_dsp_CLASS_process_sync(): the band-limited oscillators' hard sync.

  The phase shapers (kink, triangleShape, trapezoidShape) read a phase array,
  so their process functions take one as input; `out` may be the same array.
  Their pure per-sample formulas are handle-free yse_dsp_CLASS_value() calls.

  Seeds: pinkNoise and bandLimitedRandom start from YSE_DSP_NOISE_DEFAULT_SEED,
  and yse_dsp_CLASS_seed() restarts them, so a host can make a render
  repeatable.

  phasor follows a beat clock through yse_dsp_phasor_process_span(), which
  takes a beat-span handle from yse_dsp_phasor_toolkit.h (#1299).

  Not mirrored: cycle's sampleView table (the sample buffers come with #1303)
  and oscillatorBank::swapTable (it exchanges a std::vector, which cannot
  cross the ABI; set_table covers the same use off the audio thread).
*/

#ifndef YSE_C_DSP_OSCILLATORS_H_INCLUDED
#define YSE_C_DSP_OSCILLATORS_H_INCLUDED

#include "yse_common.h"
#include "yse_dsp_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── phasor: a 0..1 phase ramp (#1045) ──────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_phasor_create(void);
/** Hold a new frequency until the ramp wraps (0 / 1). Off by default. */
YSE_C_API void yse_dsp_phasor_set_sync_update(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_get_sync_update(YseDspProcessor* p);
/** Take the frequency of the next block at once, even with sync update on. */
YSE_C_API void yse_dsp_phasor_update(YseDspProcessor* p);
/** Jump to a phase now, wrapped into 0..1. */
YSE_C_API void yse_dsp_phasor_set_phase(YseDspProcessor* p, float value);
/** The phase the next sample will have, in 0..1. */
YSE_C_API float yse_dsp_phasor_get_phase(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_phasor_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length);
YSE_C_API unsigned int yse_dsp_phasor_process_signal(YseDspProcessor* p, const float* frequency,
                                                     float* out, unsigned int length);
/** One block following a beat clock: one cycle every `period_beats` beats of
   `span`, a yse_dsp_beat_span_create() handle read for this block (#1278).
   Unlocked (`lock` 0) the ramp keeps its own phase and takes its speed from
   the clock; locked, the phase is the clock's beat over `period_beats` plus
   `offset`, wrapped. A period that is not above 0, or a span without a
   reading, holds the ramp. The block is the span's length
   (yse_dsp_beat_span_get_length(), at most 128; 128 for a span never read):
   it writes that many samples and returns the count, or 0 and writes nothing
   when `out` has room (`capacity`) for fewer or `span` is NULL or not a beat
   span. One span is one block, so a host reads the span again before each
   call. */
YSE_C_API unsigned int yse_dsp_phasor_process_span(YseDspProcessor* p, YseDspProcessor* span,
                                                   double period_beats, int lock, float offset,
                                                   float* out, unsigned int capacity);

/* ─── cycle: a wavetable oscillator with a phase input (#1054) ───────────── */

/** Points in the built-in cosine table. */
#define YSE_DSP_CYCLE_TABLE_SIZE 16384

/** A cosine oscillator at phase 0. */
YSE_C_API YseDspProcessor* yse_dsp_cycle_create(void);
/** Read `size` samples at `samples` as the waveform from the next block on.
   The table is NOT copied: the array must stay valid until another table is
   set or the handle is destroyed. NULL or a size of 0 is an empty table,
   which outputs silence. */
YSE_C_API void yse_dsp_cycle_set_table(YseDspProcessor* p, const float* samples, unsigned int size);
/** Go back to the built-in cosine table. */
YSE_C_API void yse_dsp_cycle_cosine(YseDspProcessor* p);
/** The number of points in the table in use. */
YSE_C_API unsigned int yse_dsp_cycle_get_table_size(YseDspProcessor* p);
/** Set the running phase now, wrapped into 0..1. */
YSE_C_API void yse_dsp_cycle_reset(YseDspProcessor* p, float phase);
/** The running phase the next sample starts from, without the phase input. */
YSE_C_API float yse_dsp_cycle_get_phase(YseDspProcessor* p);
/** A frequency in Hz and a phase offset in cycles, both constant. */
YSE_C_API unsigned int yse_dsp_cycle_process(YseDspProcessor* p, float frequency, float phase,
                                             float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_cycle_process_frequency(YseDspProcessor* p, const float* frequency,
                                                       float phase, float* out,
                                                       unsigned int length);
YSE_C_API unsigned int yse_dsp_cycle_process_phase(YseDspProcessor* p, float frequency,
                                                   const float* phase, float* out,
                                                   unsigned int length);
YSE_C_API unsigned int yse_dsp_cycle_process_signal(YseDspProcessor* p, const float* frequency,
                                                    const float* phase, float* out,
                                                    unsigned int length);

/* ─── blSaw: antialiased sawtooth (#1055) ────────────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_bl_saw_create(void);
YSE_C_API void yse_dsp_bl_saw_set_phase(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_saw_get_phase(YseDspProcessor* p);
/** The level a sync signal must rise across to reset the phase. 0.5. */
YSE_C_API void yse_dsp_bl_saw_set_sync_threshold(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_saw_get_sync_threshold(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_bl_saw_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length);
YSE_C_API unsigned int yse_dsp_bl_saw_process_signal(YseDspProcessor* p, const float* frequency,
                                                     float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_bl_saw_process_sync(YseDspProcessor* p, const float* frequency,
                                                   const float* sync, float* out,
                                                   unsigned int length);

/* ─── blRect: antialiased pulse with a variable width (#1055) ────────────── */

YSE_C_API YseDspProcessor* yse_dsp_bl_rect_create(void);
/** The pulse width used while none is passed per sample. 0.5. */
YSE_C_API void yse_dsp_bl_rect_set_width(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_rect_get_width(YseDspProcessor* p);
YSE_C_API void yse_dsp_bl_rect_set_phase(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_rect_get_phase(YseDspProcessor* p);
YSE_C_API void yse_dsp_bl_rect_set_sync_threshold(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_rect_get_sync_threshold(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_bl_rect_process(YseDspProcessor* p, float frequency, float* out,
                                               unsigned int length);
/** A per-sample frequency at the set width. */
YSE_C_API unsigned int yse_dsp_bl_rect_process_frequency(YseDspProcessor* p, const float* frequency,
                                                         float* out, unsigned int length);
/** A per-sample frequency and width. */
YSE_C_API unsigned int yse_dsp_bl_rect_process_signal(YseDspProcessor* p, const float* frequency,
                                                      const float* width, float* out,
                                                      unsigned int length);
YSE_C_API unsigned int yse_dsp_bl_rect_process_sync(YseDspProcessor* p, const float* frequency,
                                                    const float* width, const float* sync,
                                                    float* out, unsigned int length);

/* ─── blTri: antialiased triangle with a variable duty (#1055) ───────────── */

YSE_C_API YseDspProcessor* yse_dsp_bl_tri_create(void);
/** The duty cycle used while none is passed per sample. 0.5. */
YSE_C_API void yse_dsp_bl_tri_set_duty(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_tri_get_duty(YseDspProcessor* p);
YSE_C_API void yse_dsp_bl_tri_set_phase(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_tri_get_phase(YseDspProcessor* p);
YSE_C_API void yse_dsp_bl_tri_set_sync_threshold(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_bl_tri_get_sync_threshold(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_bl_tri_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length);
/** A per-sample frequency at the set duty. */
YSE_C_API unsigned int yse_dsp_bl_tri_process_frequency(YseDspProcessor* p, const float* frequency,
                                                        float* out, unsigned int length);
/** A per-sample frequency and duty. */
YSE_C_API unsigned int yse_dsp_bl_tri_process_signal(YseDspProcessor* p, const float* frequency,
                                                     const float* duty, float* out,
                                                     unsigned int length);
YSE_C_API unsigned int yse_dsp_bl_tri_process_sync(YseDspProcessor* p, const float* frequency,
                                                   const float* duty, const float* sync, float* out,
                                                   unsigned int length);

/* ─── pinkNoise (#1056) and bandLimitedRandom (#1057) ────────────────────── */

/** The seed a new noise generator starts from (that of the engine's white
   noise). */
#define YSE_DSP_NOISE_DEFAULT_SEED 404933u

YSE_C_API YseDspProcessor* yse_dsp_pink_noise_create(void);
/** Restart the sequence from `seed` and clear the filter: the next samples
   are those of a new generator with this seed. */
YSE_C_API void yse_dsp_pink_noise_seed(YseDspProcessor* p, unsigned int seed);
YSE_C_API unsigned int yse_dsp_pink_noise_process(YseDspProcessor* p, float* out,
                                                  unsigned int length);

YSE_C_API YseDspProcessor* yse_dsp_band_limited_random_create(void);
/** Restart the sequence from `seed`; the output goes back to 0. */
YSE_C_API void yse_dsp_band_limited_random_seed(YseDspProcessor* p, unsigned int seed);
/** A new random value every 1 / frequency seconds, with lines in between. */
YSE_C_API unsigned int yse_dsp_band_limited_random_process(YseDspProcessor* p, float frequency,
                                                           float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_band_limited_random_process_signal(YseDspProcessor* p,
                                                                  const float* frequency,
                                                                  float* out, unsigned int length);

/* ─── click: an impulse or a short stored waveform on a trigger (#1058) ──── */

/** The longest waveform yse_dsp_click_set() keeps, in samples. */
#define YSE_DSP_CLICK_MAX_LENGTH 256

YSE_C_API YseDspProcessor* yse_dsp_click_create(void);
/** Copy the first `count` values as the waveform; more than
   YSE_DSP_CLICK_MAX_LENGTH keeps the first that many. NULL or 0 goes back to
   the unit impulse. */
YSE_C_API void yse_dsp_click_set(YseDspProcessor* p, const float* values, unsigned int count);
/** How many samples the waveform has: 1 to YSE_DSP_CLICK_MAX_LENGTH. */
YSE_C_API unsigned int yse_dsp_click_get_length(YseDspProcessor* p);
/** Sample `index` of the waveform, or 0 past its end. */
YSE_C_API float yse_dsp_click_get_value(YseDspProcessor* p, unsigned int index);
/** Play the waveform, times `scale`, from the start of the next process call. */
YSE_C_API void yse_dsp_click_trigger(YseDspProcessor* p, float scale);
/** Stop playing and forget a pending trigger. The waveform stays. */
YSE_C_API void yse_dsp_click_reset(YseDspProcessor* p);
/** A block triggered by yse_dsp_click_trigger() only. */
YSE_C_API unsigned int yse_dsp_click_process(YseDspProcessor* p, float* out, unsigned int length);
/** A block also triggered at every 0 to non-zero step of `triggers`. */
YSE_C_API unsigned int yse_dsp_click_process_signal(YseDspProcessor* p, const float* triggers,
                                                    float* out, unsigned int length);

/* ─── pulseTrain: a 0/1 pulse train in milliseconds (#1059)
   The engine lists the onsets of its last 128-sample block. The C mirror
   reports them for the whole call instead, as an optional second output:
   `onsets`, when not NULL, gets 1 at every onset sample and 0 elsewhere. An
   onset is a sample at 1 after one at 0, or the first sample after
   yse_dsp_pulse_train_trigger(), whatever the output was. */

YSE_C_API YseDspProcessor* yse_dsp_pulse_train_create(void);
/** The period in ms; 0 or less stops the train. 1000. */
YSE_C_API void yse_dsp_pulse_train_set_interval(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_pulse_train_get_interval(YseDspProcessor* p);
/** The part of the period at 1, 0..1. 0.5. */
YSE_C_API void yse_dsp_pulse_train_set_width(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_pulse_train_get_width(YseDspProcessor* p);
/** The onset delay as a fraction of the period, wrapped into 0..1. 0. */
YSE_C_API void yse_dsp_pulse_train_set_phase(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_pulse_train_get_phase(YseDspProcessor* p);
/** 1 defers an interval change to the start of the next cycle. 0. */
YSE_C_API void yse_dsp_pulse_train_set_defer_interval(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_pulse_train_get_defer_interval(YseDspProcessor* p);
/** Start the on part at the first sample of the next process call. */
YSE_C_API void yse_dsp_pulse_train_trigger(YseDspProcessor* p);
/** Back to the state after construction; keeps the settings. */
YSE_C_API void yse_dsp_pulse_train_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_pulse_train_process(YseDspProcessor* p, float* out, float* onsets,
                                                   unsigned int length);
/** A per-sample interval in ms, at the set width and phase. */
YSE_C_API unsigned int yse_dsp_pulse_train_process_interval(YseDspProcessor* p,
                                                            const float* interval, float* out,
                                                            float* onsets, unsigned int length);
/** A per-sample interval, width and phase. */
YSE_C_API unsigned int yse_dsp_pulse_train_process_signal(YseDspProcessor* p, const float* interval,
                                                          const float* width, const float* phase,
                                                          float* out, float* onsets,
                                                          unsigned int length);

/* ─── kink: a 0..1 phase bent by a slope (#1060) ─────────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_kink_create(void);
/** One sample, without a handle: `phase` bent by `slope`. */
YSE_C_API float yse_dsp_kink_value(float phase, float slope);
YSE_C_API unsigned int yse_dsp_kink_process(YseDspProcessor* p, const float* phase, float slope,
                                            float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_kink_process_signal(YseDspProcessor* p, const float* phase,
                                                   const float* slope, float* out,
                                                   unsigned int length);

/* ─── oscillatorBank: additive synthesis and resynthesis (#1061) ─────────── */

/** The capacity yse_dsp_oscillator_bank_create() takes for 0. */
#define YSE_DSP_OSCILLATOR_BANK_DEFAULT_OSCILLATORS 64
/** The table size yse_dsp_oscillator_bank_create() takes for 0. */
#define YSE_DSP_OSCILLATOR_BANK_DEFAULT_TABLE_SIZE 4096
/** The smallest and the largest table. */
#define YSE_DSP_OSCILLATOR_BANK_MIN_TABLE_SIZE 2
#define YSE_DSP_OSCILLATOR_BANK_MAX_TABLE_SIZE 65536

/** A bank of `capacity` oscillators (0 means
   YSE_DSP_OSCILLATOR_BANK_DEFAULT_OSCILLATORS), reading the table between
   points if `interpolating` is not 0, with a table of `table_size` points (0
   means YSE_DSP_OSCILLATOR_BANK_DEFAULT_TABLE_SIZE; others are rounded to a
   power of two within the limits). */
YSE_C_API YseDspProcessor* yse_dsp_oscillator_bank_create(unsigned int capacity, int interpolating,
                                                          unsigned int table_size);
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_capacity(YseDspProcessor* p);
/** Run oscillators 0 to count - 1; clamped to 1 .. the capacity. */
YSE_C_API void yse_dsp_oscillator_bank_set_size(YseDspProcessor* p, unsigned int count);
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_size(YseDspProcessor* p);
/** Ramp a new frequency over `samples`; 0 or 1: no ramp. */
YSE_C_API void yse_dsp_oscillator_bank_set_frequency_smoothing(YseDspProcessor* p,
                                                               unsigned int samples);
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_frequency_smoothing(YseDspProcessor* p);
/** Ramp a new magnitude over `samples`; 0 or 1: no ramp. */
YSE_C_API void yse_dsp_oscillator_bank_set_magnitude_smoothing(YseDspProcessor* p,
                                                               unsigned int samples);
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_magnitude_smoothing(YseDspProcessor* p);
/** Hold values sent per sample until the next index 0 (0 / 1). */
YSE_C_API void yse_dsp_oscillator_bank_set_frame_sync(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_oscillator_bank_get_frame_sync(YseDspProcessor* p);
YSE_C_API int yse_dsp_oscillator_bank_get_interpolating(YseDspProcessor* p);
/** A table of `points` points, rounded as the constructor rounds it. Goes
   back to the sine. */
YSE_C_API void yse_dsp_oscillator_bank_set_table_size(YseDspProcessor* p, unsigned int points);
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_table_size(YseDspProcessor* p);
/** Copy `count` samples as the table in place of the sine (the first
   table-size samples are used, missing ones are 0). NULL or 0 goes back to
   the sine. ALLOCATES when the table is larger than any this bank had
   before, so the first call at a size belongs off the real-time thread. */
YSE_C_API void yse_dsp_oscillator_bank_set_table(YseDspProcessor* p, const float* samples,
                                                 unsigned int count);
/** 1 while the table is one from yse_dsp_oscillator_bank_set_table(). */
YSE_C_API int yse_dsp_oscillator_bank_get_custom_table(YseDspProcessor* p);
/** Oscillator `index` ramps to `frequency` and `magnitude`. An index at or
   past the capacity is ignored, here and in the per-oscillator calls below. */
YSE_C_API void yse_dsp_oscillator_bank_set(YseDspProcessor* p, unsigned int index, float frequency,
                                           float magnitude);
YSE_C_API void yse_dsp_oscillator_bank_set_frequency(YseDspProcessor* p, unsigned int index,
                                                     float frequency);
YSE_C_API void yse_dsp_oscillator_bank_set_magnitude(YseDspProcessor* p, unsigned int index,
                                                     float magnitude);
/** Jump oscillator `index` to `phase`, in cycles, wrapped. */
YSE_C_API void yse_dsp_oscillator_bank_set_phase(YseDspProcessor* p, unsigned int index,
                                                 float phase);
/** `count` frequency / magnitude pairs (2 * count values) go to oscillators 0
   to count - 1; every other oscillator ramps to magnitude 0. */
YSE_C_API void yse_dsp_oscillator_bank_set_pairs(YseDspProcessor* p, const float* pairs,
                                                 unsigned int count);
/** Every oscillator from `first` on ramps to magnitude 0. */
YSE_C_API void yse_dsp_oscillator_bank_fade_from(YseDspProcessor* p, unsigned int first);
/** Every frequency and magnitude to 0 at once. Phases stay. */
YSE_C_API void yse_dsp_oscillator_bank_clear(YseDspProcessor* p);
/** Every magnitude to 0 at once. Frequencies stay. */
YSE_C_API void yse_dsp_oscillator_bank_silence(YseDspProcessor* p);
/** Clear, and every phase back to 0. */
YSE_C_API void yse_dsp_oscillator_bank_reset(YseDspProcessor* p);
/** Oscillator `index`'s values as they are now (mid-ramp included); 0 past
   the capacity. */
YSE_C_API float yse_dsp_oscillator_bank_get_frequency(YseDspProcessor* p, unsigned int index);
YSE_C_API float yse_dsp_oscillator_bank_get_magnitude(YseDspProcessor* p, unsigned int index);
YSE_C_API float yse_dsp_oscillator_bank_get_phase(YseDspProcessor* p, unsigned int index);
/** A block with the oscillators as they are set. */
YSE_C_API unsigned int yse_dsp_oscillator_bank_process(YseDspProcessor* p, float* out,
                                                       unsigned int length);
/** A block setting oscillators per sample: at sample i oscillator index[i]
   gets frequency[i] and magnitude[i] (and, with frame sync, phase[i]).
   `phase` is optional: NULL lets the phases run on through a frame. */
YSE_C_API unsigned int
yse_dsp_oscillator_bank_process_signal(YseDspProcessor* p, const float* frequency,
                                       const float* magnitude, const float* phase,
                                       const float* index, float* out, unsigned int length);

/* ─── triangleShape and trapezoidShape: shapes of a 0..1 phase (#1063) ───── */

YSE_C_API YseDspProcessor* yse_dsp_triangle_shape_create(void);
/** One sample, without a handle: the triangle at `phase` with its peak at
   `peak`, scaled into lo..hi (-1 and 1 for the class's default range). */
YSE_C_API float yse_dsp_triangle_shape_value(float phase, float peak, float lo, float hi);
/** The output range, -1 .. 1 by default. A non-finite value keeps the old
   one. */
YSE_C_API void yse_dsp_triangle_shape_set_range(YseDspProcessor* p, float lo, float hi);
YSE_C_API float yse_dsp_triangle_shape_get_lo(YseDspProcessor* p);
YSE_C_API float yse_dsp_triangle_shape_get_hi(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_triangle_shape_process(YseDspProcessor* p, const float* phase,
                                                      float peak, float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_triangle_shape_process_signal(YseDspProcessor* p, const float* phase,
                                                             const float* peak, float* out,
                                                             unsigned int length);

/** The trapezoid's default ramp-up and ramp-down points. */
#define YSE_DSP_TRAPEZOID_DEFAULT_UP 0.1f
#define YSE_DSP_TRAPEZOID_DEFAULT_DOWN 0.9f

YSE_C_API YseDspProcessor* yse_dsp_trapezoid_shape_create(void);
/** One sample, without a handle: the trapezoid at `phase`, scaled into
   lo..hi, the phase wrapped if `wrap` is not 0 and clipped otherwise. */
YSE_C_API float yse_dsp_trapezoid_shape_value(float phase, float up, float down, float lo, float hi,
                                              int wrap);
/** The output range, 0 .. 1 by default. A non-finite value keeps the old
   one. */
YSE_C_API void yse_dsp_trapezoid_shape_set_range(YseDspProcessor* p, float lo, float hi);
YSE_C_API float yse_dsp_trapezoid_shape_get_lo(YseDspProcessor* p);
YSE_C_API float yse_dsp_trapezoid_shape_get_hi(YseDspProcessor* p);
/** Wrap the phase into 0..1 (1, the default) or clip it (0). */
YSE_C_API void yse_dsp_trapezoid_shape_set_wrap(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_trapezoid_shape_get_wrap(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_trapezoid_shape_process(YseDspProcessor* p, const float* phase,
                                                       float up, float down, float* out,
                                                       unsigned int length);
YSE_C_API unsigned int yse_dsp_trapezoid_shape_process_signal(YseDspProcessor* p,
                                                              const float* phase, const float* up,
                                                              const float* down, float* out,
                                                              unsigned int length);

/* ─── sampleCounter: a signal counting samples (#1066)
   `carry`, when not NULL, gets the carry signal of the same samples: how
   many times the counter has wrapped since it started, as of each sample. */

YSE_C_API YseDspProcessor* yse_dsp_sample_counter_create(void);
/** The value counting starts from and wraps back to. Does not restart. */
YSE_C_API void yse_dsp_sample_counter_set_minimum(YseDspProcessor* p, int value);
YSE_C_API int yse_dsp_sample_counter_get_minimum(YseDspProcessor* p);
/** The count limit, never output; 0 means no limit. Does not restart. */
YSE_C_API void yse_dsp_sample_counter_set_limit(YseDspProcessor* p, int value);
YSE_C_API int yse_dsp_sample_counter_get_limit(YseDspProcessor* p);
/** Go to the minimum and count from there. */
YSE_C_API void yse_dsp_sample_counter_start(YseDspProcessor* p);
/** Set the minimum to `value`, go to it and count from there. */
YSE_C_API void yse_dsp_sample_counter_start_at(YseDspProcessor* p, int value);
/** Stop counting and go back to the minimum. */
YSE_C_API void yse_dsp_sample_counter_stop(YseDspProcessor* p);
YSE_C_API int yse_dsp_sample_counter_is_counting(YseDspProcessor* p);
/** The value the next sample will have. */
YSE_C_API long long yse_dsp_sample_counter_get_count(YseDspProcessor* p);
/** How many times the counter has wrapped since it started. */
YSE_C_API long long yse_dsp_sample_counter_get_carries(YseDspProcessor* p);
/** A block started and stopped by start / stop only. */
YSE_C_API unsigned int yse_dsp_sample_counter_process(YseDspProcessor* p, float* out, float* carry,
                                                      unsigned int length);
/** A block counting while `enable` is not 0. */
YSE_C_API unsigned int yse_dsp_sample_counter_process_signal(YseDspProcessor* p,
                                                             const float* enable, float* out,
                                                             float* carry, unsigned int length);

#ifdef __cplusplus
}
#endif

#endif
