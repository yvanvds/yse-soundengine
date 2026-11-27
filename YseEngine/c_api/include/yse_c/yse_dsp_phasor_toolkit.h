/**
  @file
  yse_dsp_phasor_toolkit.h — the phasor toolkit and clock followers of the MSP
  port. C ABI mirror of the YseEngine/dsp/generators/ classes added by #1034
  (issue #1299): phasorRate, phasorSubdiv, phasorStepDiv, phasorSwing,
  phasorTwist, phasorWhere, phasorWhat, stepCounter, stepFunction,
  sampleHoldMemory, signalStash, snowfall, beatSpan and clockFollower. Most
  of them read a 0..1 ramp (a phasor's output, yse_dsp_oscillators.h) and
  derive rhythm from it. The engine headers document each class's behaviour,
  clamping and defaults; this header documents only what the C mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions write
  `length` samples to host float arrays, return the number of samples
  written, and are allocation-free and safe on a real-time thread. Every call
  here but a constructor and the two documented max_size setters allocates
  nothing, takes no lock and never sets yse_last_error(): the lists and
  patterns live in fixed arrays.

  Names. yse_dsp_CLASS_process() is a block, yse_dsp_CLASS_process_signal()
  a block with an extra per-sample input where the engine has that overload,
  and yse_dsp_CLASS_process_sample() one sample, for a per-sample host loop.
  Where the engine gives a block a second output (an index, a remaining
  time, a count), the block takes it as an optional output array and the
  sample call as an optional `float*`; either may be NULL to skip it.

  Lists cross as host arrays of `count` values. A list setter returns 1 when
  it applied the list and 0 when the handle is refused or the array is NULL
  with a `count` above 0; that changes nothing. A `count` of 0 clears the
  list (the array may then be NULL). The engine drops values past its
  capacity and the values it does not accept.

  Following a beat clock. A beat span (yse_dsp_beat_span_create()) turns the
  one reading a beat clock gives per block, a beat and a tempo, into a beat
  for every sample of the block. The host reads it once per block of at most
  128 samples, from its own transport or from a domain clock it read with
  yse_system_beat_position() / yse_system_current_tempo() (yse_system.h,
  control-thread calls that take a clock's name, so not for the audio thread:
  pass the values over to it). Then it hands the span to
  yse_dsp_clock_follower_process_span() or yse_dsp_phasor_process_span()
  (yse_dsp_oscillators.h). Reading a span is plain arithmetic: it is safe on
  a real-time thread.

  Enum setters ignore a value outside the enum.

  Not mirrored: the clockFollower's per-block report getters are replaced by
  the gathered ones below, and its clockByte(index) by the clock array of
  the process calls.
*/

#ifndef YSE_C_DSP_PHASOR_TOOLKIT_H_INCLUDED
#define YSE_C_DSP_PHASOR_TOOLKIT_H_INCLUDED

#include "yse_common.h"
#include "yse_dsp_processor.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── phasorRate: a ramp time-scaled by a multiplier (#1073) ─────────────── */

YSE_C_API YseDspProcessor* yse_dsp_phasor_rate_create(void);
/** The cycle-length multiplier; 0 or a value that is not finite is ignored. 1. */
YSE_C_API void yse_dsp_phasor_rate_set_multiplier(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_phasor_rate_get_multiplier(YseDspProcessor* p);
/** How the output is kept in phase. YSE_DSP_RATE_SYNC_OFF. */
YSE_C_API void yse_dsp_phasor_rate_set_sync(YseDspProcessor* p, YseDspRateSync mode);
YSE_C_API YseDspRateSync yse_dsp_phasor_rate_get_sync(YseDspProcessor* p);
/** Stop at the end of the current output ramp (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_rate_set_oneshot(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_rate_get_oneshot(YseDspProcessor* p);
/** 1 while a oneshot ramp has ended and holds. */
YSE_C_API int yse_dsp_phasor_rate_is_stopped(YseDspProcessor* p);
/** The next sample is `value` and the ramp continues from it. */
YSE_C_API void yse_dsp_phasor_rate_jump(YseDspProcessor* p, float value);
/** The output is `value` on the sample where the input passes `at` (0..1). */
YSE_C_API void yse_dsp_phasor_rate_jump_at(YseDspProcessor* p, float value, float at);
/** On the next input wrap, restart the wrap count and jump in phase. */
YSE_C_API void yse_dsp_phasor_rate_reset(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_rate_process_sample(YseDspProcessor* p, float phase);
/** One sample at `multiplier` (0 or not finite: the last valid one). */
YSE_C_API float yse_dsp_phasor_rate_process_sample_signal(YseDspProcessor* p, float phase,
                                                          float multiplier);
YSE_C_API unsigned int yse_dsp_phasor_rate_process(YseDspProcessor* p, const float* phase,
                                                   float* out, unsigned int length);
/** The multiplier read per sample. */
YSE_C_API unsigned int yse_dsp_phasor_rate_process_signal(YseDspProcessor* p, const float* phase,
                                                          const float* multiplier, float* out,
                                                          unsigned int length);

/* ─── phasorSubdiv: whole-number sub-ramps of each cycle (#1074)
   `index_out` / `index`, when not NULL, get the number of the step playing
   (-1 for a silent step in YSE_DSP_SUBDIV_SILENT_MINUS_ONE). */

/** The largest subdivision. */
#define YSE_DSP_PHASOR_SUBDIV_MAX_DIV 16384
/** The longest duration or probability pattern. */
#define YSE_DSP_PHASOR_SUBDIV_MAX_PATTERN 64

YSE_C_API YseDspProcessor* yse_dsp_phasor_subdiv_create(void);
/** The number of subdivisions, 1 .. YSE_DSP_PHASOR_SUBDIV_MAX_DIV. 1. The
   getter returns the last one set, landed or not. */
YSE_C_API void yse_dsp_phasor_subdiv_set_div(YseDspProcessor* p, int value);
YSE_C_API int yse_dsp_phasor_subdiv_get_div(YseDspProcessor* p);
/** The duration pattern, step lengths in subdivision units. */
YSE_C_API int yse_dsp_phasor_subdiv_set_pattern(YseDspProcessor* p, const int* steps,
                                                unsigned int count);
YSE_C_API unsigned int yse_dsp_phasor_subdiv_get_pattern_length(YseDspProcessor* p);
/** The probability pattern, chances 0..1. */
YSE_C_API int yse_dsp_phasor_subdiv_set_prob(YseDspProcessor* p, const float* chances,
                                             unsigned int count);
YSE_C_API unsigned int yse_dsp_phasor_subdiv_get_prob_length(YseDspProcessor* p);
/** One probability per duration step (1) or per subdivision unit (0). 0. */
YSE_C_API void yse_dsp_phasor_subdiv_set_lock_prob(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_subdiv_get_lock_prob(YseDspProcessor* p);
YSE_C_API void yse_dsp_phasor_subdiv_set_silent_mode(YseDspProcessor* p, YseDspSubdivSilent mode);
YSE_C_API YseDspSubdivSilent yse_dsp_phasor_subdiv_get_silent_mode(YseDspProcessor* p);
YSE_C_API void yse_dsp_phasor_subdiv_set_sync_update(YseDspProcessor* p, YseDspSubdivUpdate mode);
YSE_C_API YseDspSubdivUpdate yse_dsp_phasor_subdiv_get_sync_update(YseDspProcessor* p);
/** Restart the random source of the probability draws. */
YSE_C_API void yse_dsp_phasor_subdiv_seed(YseDspProcessor* p, unsigned int seed);
YSE_C_API float yse_dsp_phasor_subdiv_process_sample(YseDspProcessor* p, float phase, float* index);
YSE_C_API unsigned int yse_dsp_phasor_subdiv_process(YseDspProcessor* p, const float* phase,
                                                     float* out, float* index_out,
                                                     unsigned int length);

/* ─── phasorStepDiv: one ramp per step of a stepped function (#1075)
   Driven by an input ramp (process, process_sample) or free running in
   milliseconds (process_free, process_free_sample). `index_out` / `index`,
   when not NULL, get the number of the step playing. */

/** The most steps in a pattern. */
#define YSE_DSP_PHASOR_STEP_DIV_MAX_STEPS 64

YSE_C_API YseDspProcessor* yse_dsp_phasor_step_div_create(void);
/** The step times as they are. */
YSE_C_API int yse_dsp_phasor_step_div_set_pattern(YseDspProcessor* p, const float* times,
                                                  unsigned int count);
/** The step times from a list of `value time` pairs, or `value time curve`
   triples with `curves` non-zero; `count` is the number of floats. */
YSE_C_API int yse_dsp_phasor_step_div_set_breakpoints(YseDspProcessor* p, const float* list,
                                                      unsigned int count, int curves);
/** The step times from the moments the steps end. */
YSE_C_API int yse_dsp_phasor_step_div_set_onsets(YseDspProcessor* p, const float* offsets,
                                                 unsigned int count);
/** The number of steps in the last pattern set, landed or not, and its
   time `index` (0 past the end). */
YSE_C_API unsigned int yse_dsp_phasor_step_div_get_pattern_length(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_step_div_get_time(YseDspProcessor* p, unsigned int index);
/** Free running: start again at the end of the sequence (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_step_div_set_loop(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_step_div_get_loop(YseDspProcessor* p);
/** A new pattern waits for a wrap, or for a free-running sequence to end or
   restart (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_step_div_set_sync_update(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_step_div_get_sync_update(YseDspProcessor* p);
/** Free running: play from the first step, from the next sample on. */
YSE_C_API void yse_dsp_phasor_step_div_start(YseDspProcessor* p);
/** Free running: play from the start of step `n` (clamped). */
YSE_C_API void yse_dsp_phasor_step_div_step(YseDspProcessor* p, int n);
/** Free running: 1 while a sequence plays. */
YSE_C_API int yse_dsp_phasor_step_div_is_running(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_step_div_process_sample(YseDspProcessor* p, float phase,
                                                       float* index);
YSE_C_API float yse_dsp_phasor_step_div_process_free_sample(YseDspProcessor* p, float* index);
YSE_C_API unsigned int yse_dsp_phasor_step_div_process(YseDspProcessor* p, const float* phase,
                                                       float* out, float* index_out,
                                                       unsigned int length);
/** Free running. A length that is not a multiple of 128 ends with per-sample
   steps, which the engine makes identical to a block's. */
YSE_C_API unsigned int yse_dsp_phasor_step_div_process_free(YseDspProcessor* p, float* out,
                                                            float* index_out, unsigned int length);

/* ─── phasorSwing: two ramps of unequal length per cycle (#1076) ─────────── */

/** The swing range; a swing outside it is clamped. */
#define YSE_DSP_PHASOR_SWING_MIN 0.15f
#define YSE_DSP_PHASOR_SWING_MAX 0.85f

YSE_C_API YseDspProcessor* yse_dsp_phasor_swing_create(void);
/** The share of the cycle the first ramp takes. 0.5. The getter returns the
   last one set, landed or not. */
YSE_C_API void yse_dsp_phasor_swing_set_swing(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_phasor_swing_get_swing(YseDspProcessor* p);
/** A new swing waits for the input to wrap (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_swing_set_sync_update(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_swing_get_sync_update(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_swing_process_sample(YseDspProcessor* p, float phase);
YSE_C_API unsigned int yse_dsp_phasor_swing_process(YseDspProcessor* p, const float* phase,
                                                    float* out, unsigned int length);

/* ─── phasorTwist: a ramp bent into an exponential curve (#1077) ─────────── */

YSE_C_API YseDspProcessor* yse_dsp_phasor_twist_create(void);
/** The curvature, -1 .. 1; 0 is linear. The getter returns the last one
   set, landed or not. */
YSE_C_API void yse_dsp_phasor_twist_set_curve(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_phasor_twist_get_curve(YseDspProcessor* p);
/** Falling ramps are bent like rising ones (0 / 1). On. */
YSE_C_API void yse_dsp_phasor_twist_set_shape_mode(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_twist_get_shape_mode(YseDspProcessor* p);
/** A new curve waits for the ramp to wrap or turn (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_twist_set_sync_update(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_phasor_twist_get_sync_update(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_twist_process_sample(YseDspProcessor* p, float phase);
YSE_C_API unsigned int yse_dsp_phasor_twist_process(YseDspProcessor* p, const float* phase,
                                                    float* out, unsigned int length);

/* ─── phasorWhere: time elapsed in and left of a cycle (#1078)
   The output is the elapsed time in milliseconds; `remaining_out` /
   `remaining`, when not NULL, get the time left of the cycle. */

YSE_C_API YseDspProcessor* yse_dsp_phasor_where_create(void);
YSE_C_API float yse_dsp_phasor_where_process_sample(YseDspProcessor* p, float phase,
                                                    float* remaining);
YSE_C_API unsigned int yse_dsp_phasor_where_process(YseDspProcessor* p, const float* phase,
                                                    float* out, float* remaining_out,
                                                    unsigned int length);

/* ─── phasorWhat: an impulse where a ramp passes listed phases (#1079) ───── */

/** The capacity of the value list. */
#define YSE_DSP_PHASOR_WHAT_MAX_MATCHES 64

YSE_C_API YseDspProcessor* yse_dsp_phasor_what_create(void);
/** The phase values, each wrapped into 0..1. */
YSE_C_API int yse_dsp_phasor_what_set_matches(YseDspProcessor* p, const float* values,
                                              unsigned int count);
/** Empty the list (with sync on, at the next wrap). */
YSE_C_API void yse_dsp_phasor_what_clear(YseDspProcessor* p);
/** How values are matched. YSE_DSP_PHASOR_WHAT_BOTH. */
YSE_C_API void yse_dsp_phasor_what_set_trigger_mode(YseDspProcessor* p, YseDspPhasorWhatMode mode);
/** A new list waits for the input to wrap (0 / 1). Off. */
YSE_C_API void yse_dsp_phasor_what_set_sync_update(YseDspProcessor* p, int on);
/** The number of values in the list in use (not a waiting one). */
YSE_C_API unsigned int yse_dsp_phasor_what_get_count(YseDspProcessor* p);
YSE_C_API float yse_dsp_phasor_what_process_sample(YseDspProcessor* p, float phase);
YSE_C_API unsigned int yse_dsp_phasor_what_process(YseDspProcessor* p, const float* phase,
                                                   float* out, unsigned int length);

/* ─── stepCounter: impulses after a sequence of jump counts (#1080)
   `counts_out` / `count`, when not NULL, get the count of the step playing;
   `steps_out` / `step` its index in the sequence. */

/** The capacity of the sequence. */
#define YSE_DSP_STEP_COUNTER_MAX_STEPS 64

YSE_C_API YseDspProcessor* yse_dsp_step_counter_create(void);
/** The counts, each rounded to a whole number of at least 1. */
YSE_C_API int yse_dsp_step_counter_set_sequence(YseDspProcessor* p, const float* counts,
                                                unsigned int count);
/** Which jumps count. YSE_DSP_STEP_COUNTER_BOTH. */
YSE_C_API void yse_dsp_step_counter_set_direction(YseDspProcessor* p,
                                                  YseDspStepCounterDirection direction);
/** The first jump after a start or a reset gives an impulse (1) or is the
   first counted (0). 1. */
YSE_C_API void yse_dsp_step_counter_set_start_mode(YseDspProcessor* p, int on);
/** When a new sequence takes effect. YSE_DSP_STEP_COUNTER_IMMEDIATE. */
YSE_C_API void yse_dsp_step_counter_set_sync_update(YseDspProcessor* p, YseDspStepCounterSync mode);
/** The smallest difference between two samples that is a jump. 0.75. */
YSE_C_API void yse_dsp_step_counter_set_threshold(YseDspProcessor* p, float value);
/** The count and the step back to 0, waiting for a first jump. */
YSE_C_API void yse_dsp_step_counter_reset(YseDspProcessor* p);
/** The number of counts in the sequence in use (not a waiting one). */
YSE_C_API unsigned int yse_dsp_step_counter_get_length(YseDspProcessor* p);
/** 1 on an impulse, else 0. */
YSE_C_API float yse_dsp_step_counter_process_sample(YseDspProcessor* p, float in, float* count,
                                                    float* step);
YSE_C_API unsigned int yse_dsp_step_counter_process(YseDspProcessor* p, const float* in, float* out,
                                                    float* counts_out, float* steps_out,
                                                    unsigned int length);

/* ─── stepFunction: a breakpoint function, one segment per cycle (#1081)
   The _signal calls take the step from a signal (rounded down, wrapped into
   the pattern) instead of counting the input's wraps. */

/** The capacity of the pattern. */
#define YSE_DSP_STEP_FUNCTION_MAX_STEPS 64

YSE_C_API YseDspProcessor* yse_dsp_step_function_create(void);
/** The breakpoint values. The curves are kept. */
YSE_C_API int yse_dsp_step_function_set_pattern(YseDspProcessor* p, const float* values,
                                                unsigned int count);
/** One curve per step, -1..1 (0 linear); steps past `count` are linear. */
YSE_C_API int yse_dsp_step_function_set_curve_pattern(YseDspProcessor* p, const float* curves,
                                                      unsigned int count);
/** Values (and curves) from `value time` pairs, or `value time curve` triples
   with `curves` non-zero; `count` is the number of floats. `initial`, when
   not NULL, is a value that comes first. */
YSE_C_API int yse_dsp_step_function_set_breakpoints(YseDspProcessor* p, const float* list,
                                                    unsigned int count, int curves,
                                                    const float* initial);
/** When a new pattern takes effect. YSE_DSP_STEP_FUNCTION_IMMEDIATE. */
YSE_C_API void yse_dsp_step_function_set_sync_update(YseDspProcessor* p,
                                                     YseDspStepFunctionSync mode);
/** Play step `n` (wrapped into the pattern) from the next sample on. */
YSE_C_API void yse_dsp_step_function_step(YseDspProcessor* p, int n);
/** Back to step 0; a waiting pattern takes effect. */
YSE_C_API void yse_dsp_step_function_reset(YseDspProcessor* p);
/** The number of values in the pattern in use (not a waiting one). */
YSE_C_API unsigned int yse_dsp_step_function_get_length(YseDspProcessor* p);
/** The step of the last sample processed. */
YSE_C_API unsigned int yse_dsp_step_function_get_last_step(YseDspProcessor* p);
YSE_C_API float yse_dsp_step_function_process_sample(YseDspProcessor* p, float phase);
YSE_C_API float yse_dsp_step_function_process_sample_signal(YseDspProcessor* p, float phase,
                                                            float step);
YSE_C_API unsigned int yse_dsp_step_function_process(YseDspProcessor* p, const float* phase,
                                                     float* out, unsigned int length);
YSE_C_API unsigned int yse_dsp_step_function_process_signal(YseDspProcessor* p, const float* phase,
                                                            const float* step, float* out,
                                                            unsigned int length);

/* ─── sampleHoldMemory: sample and hold into a ring of values (#1083)
   Three inputs per sample: the input, the sample trigger and the advance
   trigger. */

/** The capacity yse_dsp_sample_hold_memory_create() and
   yse_dsp_signal_stash_create() take for 0. */
#define YSE_DSP_MEMORY_DEFAULT_MAX_SIZE 1000

/** A memory of up to `max_size` values (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_sample_hold_memory_create(unsigned int max_size);
/** Sets the capacity (at least 1) and clears the memory. ALLOCATES: call it
   off the audio thread. If the allocation fails nothing changes. */
YSE_C_API void yse_dsp_sample_hold_memory_set_max_size(YseDspProcessor* p, unsigned int capacity);
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_max_size(YseDspProcessor* p);
/** The number of places in use, 1 .. max size. 1. */
YSE_C_API void yse_dsp_sample_hold_memory_set_size(YseDspProcessor* p, unsigned int places);
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_size(YseDspProcessor* p);
/** The trigger levels. 0.5 each. */
YSE_C_API void yse_dsp_sample_hold_memory_set_sample_level(YseDspProcessor* p, float level);
YSE_C_API void yse_dsp_sample_hold_memory_set_advance_level(YseDspProcessor* p, float level);
/** How the read position moves on an advance. YSE_DSP_MEMORY_FORWARD. */
YSE_C_API void yse_dsp_sample_hold_memory_set_direction(YseDspProcessor* p,
                                                        YseDspMemoryDirection direction);
/** How the advance trigger is read. YSE_DSP_SASH_LEVEL. */
YSE_C_API void yse_dsp_sample_hold_memory_set_mode(YseDspProcessor* p, YseDspSashMode mode);
/** Seed the random source of RANDOM and URN. */
YSE_C_API void yse_dsp_sample_hold_memory_seed(YseDspProcessor* p, unsigned int seed);
/** Fill the memory with the finite values (up to the capacity) and set the
   size to their number. */
YSE_C_API int yse_dsp_sample_hold_memory_load(YseDspProcessor* p, const float* values,
                                              unsigned int count);
/** The stored value at `place` (0 past the capacity). */
YSE_C_API float yse_dsp_sample_hold_memory_get_value(YseDspProcessor* p, unsigned int place);
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_write_position(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_read_position(YseDspProcessor* p);
YSE_C_API float yse_dsp_sample_hold_memory_process_sample(YseDspProcessor* p, float in,
                                                          float sample_trigger,
                                                          float advance_trigger);
YSE_C_API unsigned int yse_dsp_sample_hold_memory_process(YseDspProcessor* p, const float* in,
                                                          const float* sample_trigger,
                                                          const float* advance_trigger, float* out,
                                                          unsigned int length);

/* ─── signalStash: a memory of places stored and recalled by signals (#1084)
   Four inputs per sample: the input, the sample trigger, the advance trigger
   and the write index. */

/** A memory of up to `max_size` values (0 means
   YSE_DSP_MEMORY_DEFAULT_MAX_SIZE). */
YSE_C_API YseDspProcessor* yse_dsp_signal_stash_create(unsigned int max_size);
/** Sets the capacity (at least 1) and clears the memory. ALLOCATES: call it
   off the audio thread. If the allocation fails nothing changes. */
YSE_C_API void yse_dsp_signal_stash_set_max_size(YseDspProcessor* p, unsigned int capacity);
YSE_C_API unsigned int yse_dsp_signal_stash_get_max_size(YseDspProcessor* p);
/** The number of places in use, 1 .. max size. 1. */
YSE_C_API void yse_dsp_signal_stash_set_size(YseDspProcessor* p, unsigned int places);
YSE_C_API unsigned int yse_dsp_signal_stash_get_size(YseDspProcessor* p);
/** The trigger levels. 0.5 each. */
YSE_C_API void yse_dsp_signal_stash_set_sample_level(YseDspProcessor* p, float level);
YSE_C_API void yse_dsp_signal_stash_set_advance_level(YseDspProcessor* p, float level);
/** When each trigger fires. YSE_DSP_STASH_TRIGGER_ASCENDING. */
YSE_C_API void yse_dsp_signal_stash_set_sample_trigger_mode(YseDspProcessor* p,
                                                            YseDspStashTrigger mode);
YSE_C_API void yse_dsp_signal_stash_set_advance_trigger_mode(YseDspProcessor* p,
                                                             YseDspStashTrigger mode);
/** How the read position moves on an advance. YSE_DSP_MEMORY_FORWARD. */
YSE_C_API void yse_dsp_signal_stash_set_direction(YseDspProcessor* p,
                                                  YseDspMemoryDirection direction);
/** What the output reads, and where a recording goes. YSE_DSP_STASH_NEXT. */
YSE_C_API void yse_dsp_signal_stash_set_mode(YseDspProcessor* p, YseDspStashMode mode);
YSE_C_API void yse_dsp_signal_stash_set_write_mode(YseDspProcessor* p, YseDspStashMode mode);
/** How an index outside the memory is read. YSE_DSP_STASH_RANGE_WRAP. */
YSE_C_API void yse_dsp_signal_stash_set_range(YseDspProcessor* p, YseDspStashRange range);
/** How an index between places is read. YSE_DSP_STASH_TRUNCATE. */
YSE_C_API void yse_dsp_signal_stash_set_interpolation(YseDspProcessor* p,
                                                      YseDspStashInterpolation interpolation);
/** Samples a new output lasts; 0 (the default) holds it. */
YSE_C_API void yse_dsp_signal_stash_set_duration(YseDspProcessor* p, unsigned int samples);
/** Seed the random source of RANDOM and URN. */
YSE_C_API void yse_dsp_signal_stash_seed(YseDspProcessor* p, unsigned int seed);
/** Fill the memory with the finite values (up to the capacity) and set the
   size to their number. */
YSE_C_API int yse_dsp_signal_stash_load(YseDspProcessor* p, const float* values,
                                        unsigned int count);
/** Write a finite `value` at the write position, which stays. */
YSE_C_API void yse_dsp_signal_stash_write(YseDspProcessor* p, float value);
/** Set the write position to `index`, read as the write mode says. */
YSE_C_API void yse_dsp_signal_stash_set_write_position(YseDspProcessor* p, float index);
/** Move the write position one place on, wrapping. */
YSE_C_API void yse_dsp_signal_stash_advance_write(YseDspProcessor* p);
/** Move the read position as the direction says and start a duration. */
YSE_C_API void yse_dsp_signal_stash_advance(YseDspProcessor* p);
/** Record the next sample's input whatever the sample trigger does. */
YSE_C_API void yse_dsp_signal_stash_capture(YseDspProcessor* p);
/** The stored value at `place` (0 past the capacity). */
YSE_C_API float yse_dsp_signal_stash_get_value(YseDspProcessor* p, unsigned int place);
YSE_C_API unsigned int yse_dsp_signal_stash_get_write_position(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_signal_stash_get_read_position(YseDspProcessor* p);
YSE_C_API float yse_dsp_signal_stash_process_sample(YseDspProcessor* p, float in,
                                                    float sample_trigger, float advance_trigger,
                                                    float write_index);
YSE_C_API unsigned int yse_dsp_signal_stash_process(YseDspProcessor* p, const float* in,
                                                    const float* sample_trigger,
                                                    const float* advance_trigger,
                                                    const float* write_index, float* out,
                                                    unsigned int length);

/* ─── snowfall: a particle of 1 to 4 values moved by a ramp (#1085)
   The per-dimension setters take the dimension `d` (0 .. 3) and ignore one
   past YSE_DSP_SNOWFALL_MAX_DIMENSIONS, and a value that is not finite. */

/** The most dimensions, and outputs. */
#define YSE_DSP_SNOWFALL_MAX_DIMENSIONS 4

/** A particle of `dimensions` values, 1 to 4 (clamped). */
YSE_C_API YseDspProcessor* yse_dsp_snowfall_create(unsigned int dimensions);
YSE_C_API void yse_dsp_snowfall_set_dimensions(YseDspProcessor* p, unsigned int dimensions);
YSE_C_API unsigned int yse_dsp_snowfall_get_dimensions(YseDspProcessor* p);
/** The starting velocity in units per second, and its random deviation. */
YSE_C_API void yse_dsp_snowfall_set_direction(YseDspProcessor* p, unsigned int d, float value);
YSE_C_API void yse_dsp_snowfall_set_direction_deviation(YseDspProcessor* p, unsigned int d,
                                                        float value);
/** How much the input ramp speeds the motion up. */
YSE_C_API void yse_dsp_snowfall_set_energy_ramp(YseDspProcessor* p, unsigned int d, float value);
/** The starting value, and its random deviation. */
YSE_C_API void yse_dsp_snowfall_set_initial(YseDspProcessor* p, unsigned int d, float value);
YSE_C_API void yse_dsp_snowfall_set_initial_deviation(YseDspProcessor* p, unsigned int d,
                                                      float value);
/** The boundaries, and the outputs they map to. */
YSE_C_API void yse_dsp_snowfall_set_minimum(YseDspProcessor* p, unsigned int d, float value);
YSE_C_API void yse_dsp_snowfall_set_maximum(YseDspProcessor* p, unsigned int d, float value);
YSE_C_API void yse_dsp_snowfall_set_scale_minimum(YseDspProcessor* p, unsigned int d, float value);
YSE_C_API void yse_dsp_snowfall_set_scale_maximum(YseDspProcessor* p, unsigned int d, float value);
/** The factor a bounce multiplies the velocity by. */
YSE_C_API void yse_dsp_snowfall_set_squish(YseDspProcessor* p, unsigned int d, float value);
/** The probability, 0..1, of a reversal at an update. */
YSE_C_API void yse_dsp_snowfall_set_wander_probability(YseDspProcessor* p, unsigned int d,
                                                       float value);
/** The output once a particle ends, in YSE_DSP_SNOWFALL_END_VALUE mode. */
YSE_C_API void yse_dsp_snowfall_set_end_value(YseDspProcessor* p, unsigned int d, float value);
/** The time between trajectory updates in ms (0 or less: none), and its
   random deviation. */
YSE_C_API void yse_dsp_snowfall_set_interval(YseDspProcessor* p, float ms);
YSE_C_API void yse_dsp_snowfall_set_interval_deviation(YseDspProcessor* p, float ms);
YSE_C_API void yse_dsp_snowfall_set_boundary_mode(YseDspProcessor* p, YseDspSnowfallBoundary mode);
YSE_C_API void yse_dsp_snowfall_set_end_mode(YseDspProcessor* p, YseDspSnowfallEnd mode);
YSE_C_API void yse_dsp_snowfall_set_start_mode(YseDspProcessor* p, YseDspSnowfallStart mode);
/** Seed the random source; a seed repeats a run exactly. */
YSE_C_API void yse_dsp_snowfall_seed(YseDspProcessor* p, unsigned int seed);
/** 1 while a particle moves. */
YSE_C_API int yse_dsp_snowfall_is_alive(YseDspProcessor* p);
/** Dimension `d`'s value before scaling, and its velocity in units per
   second (0 past the dimensions). */
YSE_C_API float yse_dsp_snowfall_get_value(YseDspProcessor* p, unsigned int d);
YSE_C_API float yse_dsp_snowfall_get_velocity(YseDspProcessor* p, unsigned int d);
/** One sample: writes up to `capacity` of the dimensions' outputs to `out`
   and returns the number of dimensions (0, writing nothing, when `out` is
   NULL). */
YSE_C_API unsigned int yse_dsp_snowfall_process_sample(YseDspProcessor* p, float ramp, float* out,
                                                       unsigned int capacity);
/** One block. `outputs` is an array of `count` output arrays: output `d` of
   the particle goes to outputs[d]. An entry may be NULL to skip it; an entry
   past the dimensions gets zeroes. NULL `outputs` refuses the call. */
YSE_C_API unsigned int yse_dsp_snowfall_process(YseDspProcessor* p, const float* ramp,
                                                float* const* outputs, unsigned int count,
                                                unsigned int length);

/* ─── beatSpan: one block's beats from a beat-clock reading (#1278)
   See "Following a beat clock" above. */

YSE_C_API YseDspProcessor* yse_dsp_beat_span_create(void);
/** This block's reading: the clock stands at `beat` at the end of a block of
   `length` samples (1 .. 128, clamped), at `bpm`. A reading that is not
   finite is taken as yse_dsp_beat_span_lose(). */
YSE_C_API void yse_dsp_beat_span_read(YseDspProcessor* p, double beat, float bpm,
                                      unsigned int length);
/** No reading this block; the span is not valid until the next read. */
YSE_C_API void yse_dsp_beat_span_lose(YseDspProcessor* p);
/** 1 when the block has a reading. */
YSE_C_API int yse_dsp_beat_span_is_valid(YseDspProcessor* p);
/** The beat at the block's first sample, and at its end (the first sample of
   the next block). */
YSE_C_API double yse_dsp_beat_span_get_start(YseDspProcessor* p);
YSE_C_API double yse_dsp_beat_span_get_end(YseDspProcessor* p);
/** The clock's tempo in BPM at the end of the block. */
YSE_C_API float yse_dsp_beat_span_get_tempo(YseDspProcessor* p);
/** The samples in the block; 0 before the first reading. */
YSE_C_API unsigned int yse_dsp_beat_span_get_length(YseDspProcessor* p);
/** Beats from one sample to the next, and the beat at sample `index`. */
YSE_C_API double yse_dsp_beat_span_get_step(YseDspProcessor* p);
YSE_C_API double yse_dsp_beat_span_get_at(YseDspProcessor* p, unsigned int index);
/** 1 when the clock moved in this block. */
YSE_C_API int yse_dsp_beat_span_is_moving(YseDspProcessor* p);

/* ─── clockFollower: a beat ramp following taps, clicks, MIDI or a clock (#1131)
   Each process call takes an optional `clock` array of `clock_capacity`
   entries (NULL and 0 to skip): the MIDI clock bytes the call sends, in
   order. The engine reports them per 128-sample block without their sample,
   so an entry's offset is the first sample of its block in the call.
   yse_dsp_clock_follower_get_clock_bytes() says how many were sent in all.
   The other reports (taps, clicks, ticks, tempo changed) likewise cover the
   whole of the last process call, together with the tap, midi, tempo, start
   and stop calls made between it and the process call before. A refused
   call leaves them as they were, and reset clears them. */

#define YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO 120.f
/** The tempo range in BPM; a tempo outside it is held to it. */
#define YSE_DSP_CLOCK_FOLLOWER_MIN_TEMPO 1.f
#define YSE_DSP_CLOCK_FOLLOWER_MAX_TEMPO 1000.f
/** Ticks per beat by default, and at most. */
#define YSE_DSP_CLOCK_FOLLOWER_DEFAULT_PPQ 24
#define YSE_DSP_CLOCK_FOLLOWER_MAX_PPQ 192
/** The clock bytes one 128-sample block sends at most. */
#define YSE_DSP_CLOCK_FOLLOWER_MAX_CLOCK_BYTES 64
/** MIDI system real-time bytes. */
#define YSE_DSP_MIDI_CLOCK_TICK 248
#define YSE_DSP_MIDI_CLOCK_START 250
#define YSE_DSP_MIDI_CLOCK_CONTINUE 251
#define YSE_DSP_MIDI_CLOCK_STOP 252

/** One MIDI clock byte a process call sent. */
typedef struct YseDspClockByte {
  unsigned int offset; /**< the first sample of the engine block that sent it */
  int status; /**< 248 tick, 250 start, 251 continue or 252 stop */
} YseDspClockByte;

/** A ramp at YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO, running. */
YSE_C_API YseDspProcessor* yse_dsp_clock_follower_create(void);
/** Start over at `bpm` (not a number: the default tempo); see the engine's
   reset(). Clears the gathered reports. */
YSE_C_API void yse_dsp_clock_follower_reset(YseDspProcessor* p, float bpm);
/** The tempo in BPM. Ignored while following a clock. */
YSE_C_API void yse_dsp_clock_follower_set_tempo(YseDspProcessor* p, float bpm);
YSE_C_API float yse_dsp_clock_follower_get_tempo(YseDspProcessor* p);
/** Shift the output, and the MIDI clock out, by this much of a beat (wrapped
   into 0..1). 0. */
YSE_C_API void yse_dsp_clock_follower_set_offset(YseDspProcessor* p, float phase);
YSE_C_API float yse_dsp_clock_follower_get_offset(YseDspProcessor* p);
YSE_C_API void yse_dsp_clock_follower_set_clock_offset(YseDspProcessor* p, float phase);
YSE_C_API float yse_dsp_clock_follower_get_clock_offset(YseDspProcessor* p);
/** Ticks per beat, in and out, 1 .. YSE_DSP_CLOCK_FOLLOWER_MAX_PPQ. 24. */
YSE_C_API void yse_dsp_clock_follower_set_ppq(YseDspProcessor* p, unsigned int ticks);
YSE_C_API unsigned int yse_dsp_clock_follower_get_ppq(YseDspProcessor* p);
/** Restart the ramp from 0 and run it, or hold it; ignored while following a
   clock. */
YSE_C_API void yse_dsp_clock_follower_start(YseDspProcessor* p);
YSE_C_API void yse_dsp_clock_follower_stop(YseDspProcessor* p);
YSE_C_API int yse_dsp_clock_follower_is_running(YseDspProcessor* p);
/** One tap: a beat, at the first sample of the next process call. */
YSE_C_API void yse_dsp_clock_follower_tap(YseDspProcessor* p);
/** One MIDI byte: 248 tick, 250 start, 251 continue, 252 stop; anything else
   is ignored. */
YSE_C_API void yse_dsp_clock_follower_midi(YseDspProcessor* p, int status);
/** 1 once a process_span call has made it follow a clock, until reset. */
YSE_C_API int yse_dsp_clock_follower_is_following(YseDspProcessor* p);
/** The phase of the next sample, before the offset. */
YSE_C_API float yse_dsp_clock_follower_get_phase(YseDspProcessor* p);
/** What the last process call reported (see above). */
YSE_C_API unsigned int yse_dsp_clock_follower_get_taps(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_clock_follower_get_clicks(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_clock_follower_get_ticks(YseDspProcessor* p);
YSE_C_API int yse_dsp_clock_follower_get_tempo_changed(YseDspProcessor* p);
/** The clock bytes the last process call sent (more than its
   `clock_capacity` means some were not copied). */
YSE_C_API unsigned int yse_dsp_clock_follower_get_clock_bytes(YseDspProcessor* p);
/** The ramp with no click track. */
YSE_C_API unsigned int yse_dsp_clock_follower_process(YseDspProcessor* p, float* out,
                                                      YseDspClockByte* clock,
                                                      unsigned int clock_capacity,
                                                      unsigned int length);
/** The ramp, reading `click` as a click track. */
YSE_C_API unsigned int yse_dsp_clock_follower_process_click(YseDspProcessor* p, const float* click,
                                                            float* out, YseDspClockByte* clock,
                                                            unsigned int clock_capacity,
                                                            unsigned int length);
/** One block following `span`, a beat span read for this block: the ramp is
   the clock's beat, wrapped. Like yse_dsp_phasor_process_span(), it writes
   the span's length (at most 128; 128 for a span never read) and returns
   it, or 0 and writes nothing when `out` has room (`capacity`) for fewer or
   `span` is NULL or not a beat span. */
YSE_C_API unsigned int yse_dsp_clock_follower_process_span(YseDspProcessor* p,
                                                           YseDspProcessor* span, float* out,
                                                           unsigned int capacity,
                                                           YseDspClockByte* clock,
                                                           unsigned int clock_capacity);

#ifdef __cplusplus
}
#endif

#endif
