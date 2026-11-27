/**
  @file
  yse_dsp_envelopes.h — envelopes, ramps and sequencers of the MSP port.
  C ABI mirror of the YseEngine/dsp/generators/ classes added by #1034 (issue
  #1298): adsr, curveSegments, lineSegments, zigzag, techno, eventSequencer,
  updown, shapeEnvelope and triggeredRamp. The engine headers document each
  class's shapes, timing and defaults; this header documents only what the C
  mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions write
  `length` samples to host float arrays, return the number of samples
  written, and are allocation-free and safe on a real-time thread. Every call
  here but a constructor allocates nothing, takes no lock and never sets
  yse_last_error(), the list and event edits included: the lists are sized
  to the capacity a constructor takes.

  Lists cross as host arrays. A list setter (segments, breakpoints, a zigzag
  line, techno's positions) takes parallel arrays of `count` values; a NULL
  array the list needs makes it change nothing and return 0. Stored entries
  are read back one index at a time.

  Polled events. Several classes report an event per engine block (an
  envelope that started or finished, a zigzag that reached the end of its
  range, the events a sequencer fired). A process call spans several
  128-sample blocks, so the C mirror reports them for the whole of the last
  process call: yse_dsp_CLASS_get_finished() and the like are polled after a
  process call and cover every sample it wrote. A call refused for a NULL
  handle, another class's handle or a NULL array leaves them as they were;
  the class's reset clears them.
  Per-sample side outputs (adsr's gate, lineSegments' activity, zigzag's
  index, techno's envelope and step position) are optional output arrays,
  NULL to skip.

  Enum setters ignore a value outside the enum.

  Not mirrored: the patcher's ~seq messages "dump", "read" and "write" are
  not part of eventSequencer. A host dumps a sequencer through the stored
  event getters and loads one with yse_dsp_event_sequencer_add().
*/

#ifndef YSE_C_DSP_ENVELOPES_H_INCLUDED
#define YSE_C_DSP_ENVELOPES_H_INCLUDED

#include <stddef.h>

#include "yse_common.h"
#include "yse_dsp_processor.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── adsr: a parametric attack / decay / sustain / release (#1064)
   `gate_out`, when not NULL, gets the gate output of the same samples: 1
   during attack, decay and sustain, else 0. The `gate` input of the
   _gate calls triggers the envelope as a 0 / non-zero signal. */

YSE_C_API YseDspProcessor* yse_dsp_adsr_create(void);
/** Start (non-zero) or release (0) at the first sample of the next process
   call. Not a finite number: ignored. */
YSE_C_API void yse_dsp_adsr_trigger(YseDspProcessor* p, float amplitude);
/** Times in milliseconds: attack, decay and release default to 0. */
YSE_C_API void yse_dsp_adsr_set_attack(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_adsr_get_attack(YseDspProcessor* p);
YSE_C_API void yse_dsp_adsr_set_decay(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_adsr_get_decay(YseDspProcessor* p);
/** The sustain level as a factor of the amplitude. 1. */
YSE_C_API void yse_dsp_adsr_set_sustain(YseDspProcessor* p, float factor);
YSE_C_API float yse_dsp_adsr_get_sustain(YseDspProcessor* p);
YSE_C_API void yse_dsp_adsr_set_release(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_adsr_get_release(YseDspProcessor* p);
/** The ramp down before a non-legato restart. 5 ms. */
YSE_C_API void yse_dsp_adsr_set_retrigger(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_adsr_get_retrigger(YseDspProcessor* p);
/** The longest sustain in milliseconds; negative (the default, read back as
   -1) holds until released. */
YSE_C_API void yse_dsp_adsr_set_max_sustain(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_adsr_get_max_sustain(YseDspProcessor* p);
/** Restart from the current level without a ramp down (0 / 1). Off. */
YSE_C_API void yse_dsp_adsr_set_legato(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_adsr_get_legato(YseDspProcessor* p);
/** Run every start to the end, ignoring triggers until at rest. Off. */
YSE_C_API void yse_dsp_adsr_set_play_to_end(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_adsr_get_play_to_end(YseDspProcessor* p);
/** Back to rest at once with no pending trigger; keeps the settings. */
YSE_C_API void yse_dsp_adsr_reset(YseDspProcessor* p);
/** 1 when the envelope started from rest during the last process call. */
YSE_C_API int yse_dsp_adsr_get_started(YseDspProcessor* p);
/** 1 when the envelope came to rest during the last process call. */
YSE_C_API int yse_dsp_adsr_get_finished(YseDspProcessor* p);
/** 1 unless the envelope is at rest. */
YSE_C_API int yse_dsp_adsr_is_active(YseDspProcessor* p);
/** The segment after the last sample; YSE_DSP_ADSR_IDLE for a bad handle. */
YSE_C_API YseDspAdsrStage yse_dsp_adsr_get_stage(YseDspProcessor* p);
/** The level of the last sample. */
YSE_C_API float yse_dsp_adsr_get_level(YseDspProcessor* p);
/** Triggered by yse_dsp_adsr_trigger() only, at the set times. */
YSE_C_API unsigned int yse_dsp_adsr_process(YseDspProcessor* p, float* out, float* gate_out,
                                            unsigned int length);
/** Triggered by a gate signal too. */
YSE_C_API unsigned int yse_dsp_adsr_process_gate(YseDspProcessor* p, const float* gate, float* out,
                                                 float* gate_out, unsigned int length);
/** Per-sample attack, decay and release (ms) and sustain level. */
YSE_C_API unsigned int yse_dsp_adsr_process_signal(YseDspProcessor* p, const float* attack,
                                                   const float* decay, const float* sustain,
                                                   const float* release, float* out,
                                                   float* gate_out, unsigned int length);
/** A gate signal and per-sample times and sustain. */
YSE_C_API unsigned int yse_dsp_adsr_process_gate_signal(YseDspProcessor* p, const float* gate,
                                                        const float* attack, const float* decay,
                                                        const float* sustain, const float* release,
                                                        float* out, float* gate_out,
                                                        unsigned int length);

/* ─── curveSegments: curved ramps played back to back (#1065) ────────────── */

/** The capacity yse_dsp_curve_segments_create() takes for 0. */
#define YSE_DSP_CURVE_SEGMENTS_DEFAULT_POINTS 128
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_CURVE_SEGMENTS_MAX_POINTS 4096

/** Room for `capacity` segments (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_curve_segments_create(unsigned int capacity);
YSE_C_API unsigned int yse_dsp_curve_segments_get_capacity(YseDspProcessor* p);
/** Play `count` segments: targets, times (ms) and curves (-1..1, NULL for
   all 0), from the current value. Returns how many were taken (a list past
   the capacity is cut); 0, changing nothing, for a count of 0, a NULL
   target or time array, or a target that is not finite. */
YSE_C_API unsigned int yse_dsp_curve_segments_set(YseDspProcessor* p, const float* targets,
                                                  const float* times, const float* curves,
                                                  unsigned int count);
/** Set the value at once and drop the list. Not finite: ignored. */
YSE_C_API void yse_dsp_curve_segments_jump(YseDspProcessor* p, float value);
/** Hold the value and drop the list. */
YSE_C_API void yse_dsp_curve_segments_stop(YseDspProcessor* p);
/** Hold the value, keeping the place in the list; resume carries on. */
YSE_C_API void yse_dsp_curve_segments_pause(YseDspProcessor* p);
YSE_C_API void yse_dsp_curve_segments_resume(YseDspProcessor* p);
/** The curve formula. YSE_DSP_CURVE_POWER. */
YSE_C_API void yse_dsp_curve_segments_set_shape(YseDspProcessor* p, YseDspCurveShape shape);
YSE_C_API YseDspCurveShape yse_dsp_curve_segments_get_shape(YseDspProcessor* p);
/** The value of the last sample. */
YSE_C_API float yse_dsp_curve_segments_get_value(YseDspProcessor* p);
/** 1 while a list plays (not paused, not finished). */
YSE_C_API int yse_dsp_curve_segments_is_active(YseDspProcessor* p);
YSE_C_API int yse_dsp_curve_segments_is_paused(YseDspProcessor* p);
/** Segments not yet finished, the running one included. */
YSE_C_API unsigned int yse_dsp_curve_segments_get_remaining(YseDspProcessor* p);
/** 1 when the list ran out during the last process call. */
YSE_C_API int yse_dsp_curve_segments_get_finished(YseDspProcessor* p);
/** Back to value 0 with no list; keeps the shape. */
YSE_C_API void yse_dsp_curve_segments_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_curve_segments_process(YseDspProcessor* p, float* out,
                                                      unsigned int length);

/* ─── lineSegments: straight ramps played back to back (#1042)
   `activity`, when not NULL, gets the busy state of the same samples: 1
   while a list heads for a target, else 0. */

/** The capacity yse_dsp_line_segments_create() takes for 0. */
#define YSE_DSP_LINE_SEGMENTS_DEFAULT_POINTS 128
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_LINE_SEGMENTS_MAX_POINTS 4096

/** Room for `capacity` segments (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_line_segments_create(unsigned int capacity);
YSE_C_API unsigned int yse_dsp_line_segments_get_capacity(YseDspProcessor* p);
/** Play `count` segments: targets and times (ms), from the current value.
   Returns how many were taken; 0, changing nothing, for a count of 0, a
   NULL array or a target that is not finite. */
YSE_C_API unsigned int yse_dsp_line_segments_set(YseDspProcessor* p, const float* targets,
                                                 const float* times, unsigned int count);
/** One segment to `target` over `ms`; a time of 0 jumps at once and counts
   as a finished list in the next process call. */
YSE_C_API void yse_dsp_line_segments_set_target(YseDspProcessor* p, float target, float ms);
/** Set the value at once and drop the list, without counting as finished. */
YSE_C_API void yse_dsp_line_segments_jump(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_line_segments_stop(YseDspProcessor* p);
YSE_C_API void yse_dsp_line_segments_pause(YseDspProcessor* p);
YSE_C_API void yse_dsp_line_segments_resume(YseDspProcessor* p);
/** The value of the last sample (or of the last jump). */
YSE_C_API float yse_dsp_line_segments_get_value(YseDspProcessor* p);
YSE_C_API int yse_dsp_line_segments_is_active(YseDspProcessor* p);
YSE_C_API int yse_dsp_line_segments_is_paused(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_line_segments_get_remaining(YseDspProcessor* p);
/** 1 when the list ran out (or a set_target jumped) during the last process
   call. */
YSE_C_API int yse_dsp_line_segments_get_finished(YseDspProcessor* p);
/** Back to value 0 with no list. */
YSE_C_API void yse_dsp_line_segments_reset(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_line_segments_process(YseDspProcessor* p, float* out,
                                                     float* activity, unsigned int length);

/* ─── zigzag: an editable breakpoint function at any speed (#1067)
   `index_out`, when not NULL, gets the index of the same samples: the point
   the running ramp heads for, the point last reached once play stops, or the
   input index in START mode with an input signal. */

/** The capacity yse_dsp_zigzag_create() takes for 0. */
#define YSE_DSP_ZIGZAG_DEFAULT_POINTS 2048
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_ZIGZAG_MAX_POINTS 65536

/** Room for `capacity` points (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_zigzag_create(unsigned int capacity);
YSE_C_API unsigned int yse_dsp_zigzag_get_capacity(YseDspProcessor* p);
/** How many points the list holds. */
YSE_C_API unsigned int yse_dsp_zigzag_get_size(YseDspProcessor* p);
/** Point `index`'s value and delta (ms); 0 past the end. */
YSE_C_API float yse_dsp_zigzag_get_point_value(YseDspProcessor* p, unsigned int index);
YSE_C_API float yse_dsp_zigzag_get_point_delta(YseDspProcessor* p, unsigned int index);
/** Replace the list with `count` values and deltas (ms) and stop. Returns
   how many points were taken; 0, changing nothing, for a NULL array or a
   value that is not finite. */
YSE_C_API unsigned int yse_dsp_zigzag_set_line(YseDspProcessor* p, const float* values,
                                               const float* deltas, unsigned int count);
/** set_line, then play when points were taken. */
YSE_C_API unsigned int yse_dsp_zigzag_line(YseDspProcessor* p, const float* values,
                                           const float* deltas, unsigned int count);
/** Empty the list and stop. */
YSE_C_API void yse_dsp_zigzag_clear(YseDspProcessor* p);
/** Add a point at the end; with `trigger` not 0, ramp to it over `ms`.
   0 when the list is full or the value is not finite. */
YSE_C_API int yse_dsp_zigzag_append(YseDspProcessor* p, float value, float ms, int trigger);
/** Insert a point before `index` (at the end past it). 0 when full or the
   value is not finite. */
YSE_C_API int yse_dsp_zigzag_insert(YseDspProcessor* p, unsigned int index, float value, float ms);
/** Remove point `index`. 0 past the end. */
YSE_C_API int yse_dsp_zigzag_remove(YseDspProcessor* p, unsigned int index);
/** Set point `index`'s value and delta without playing. 0 past the end or
   when the value is not finite. */
YSE_C_API int yse_dsp_zigzag_set_point(YseDspProcessor* p, unsigned int index, float value,
                                       float ms);
/** Start the list from the start of the range (the end, playing backward). */
YSE_C_API void yse_dsp_zigzag_play(YseDspProcessor* p);
/** Jump or step as the mode says. */
YSE_C_API void yse_dsp_zigzag_bang(YseDspProcessor* p);
/** Ramp to point `index` (clamped to the list) over `ms`, and play on. */
YSE_C_API void yse_dsp_zigzag_jump(YseDspProcessor* p, int index, float ms);
YSE_C_API void yse_dsp_zigzag_jump_start(YseDspProcessor* p, float ms);
YSE_C_API void yse_dsp_zigzag_jump_end(YseDspProcessor* p, float ms);
/** Jump `points` on from the current index (back when negative). */
YSE_C_API void yse_dsp_zigzag_skip(YseDspProcessor* p, int points, float ms);
YSE_C_API void yse_dsp_zigzag_next(YseDspProcessor* p, float ms);
YSE_C_API void yse_dsp_zigzag_prev(YseDspProcessor* p, float ms);
/** Hold the output where it is. */
YSE_C_API void yse_dsp_zigzag_stop(YseDspProcessor* p);
/** Set the output at once and stop. Not finite: ignored. */
YSE_C_API void yse_dsp_zigzag_hold(YseDspProcessor* p, float value);
/** The time a bang takes to reach its point. 0 ms. */
YSE_C_API void yse_dsp_zigzag_set_bang_delta(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_zigzag_get_bang_delta(YseDspProcessor* p);
/** The first point of the range. 0. */
YSE_C_API void yse_dsp_zigzag_set_start(YseDspProcessor* p, unsigned int index);
/** The last point of the range; negative (the default) for the last point
   of the list. */
YSE_C_API void yse_dsp_zigzag_set_end(YseDspProcessor* p, int index);
/** set_start and set_end together. */
YSE_C_API void yse_dsp_zigzag_bound(YseDspProcessor* p, unsigned int first, int last);
/** The play rate: 1 as written, negative backward, 0 holds. Not finite:
   ignored. */
YSE_C_API void yse_dsp_zigzag_set_speed(YseDspProcessor* p, float rate);
YSE_C_API float yse_dsp_zigzag_get_speed(YseDspProcessor* p);
/** What happens at the end of the range. YSE_DSP_ZIGZAG_LOOP_OFF. */
YSE_C_API void yse_dsp_zigzag_set_loop_mode(YseDspProcessor* p, YseDspZigzagLoop mode);
YSE_C_API YseDspZigzagLoop yse_dsp_zigzag_get_loop_mode(YseDspProcessor* p);
/** What a bang and an input signal do. YSE_DSP_ZIGZAG_START. */
YSE_C_API void yse_dsp_zigzag_set_mode(YseDspProcessor* p, YseDspZigzagMode mode);
YSE_C_API YseDspZigzagMode yse_dsp_zigzag_get_mode(YseDspProcessor* p);
/** The value of the last sample. */
YSE_C_API float yse_dsp_zigzag_get_value(YseDspProcessor* p);
/** The point the running ramp heads for, else the point last reached. */
YSE_C_API unsigned int yse_dsp_zigzag_get_index(YseDspProcessor* p);
/** 1 while a ramp runs. */
YSE_C_API int yse_dsp_zigzag_is_active(YseDspProcessor* p);
/** How many times play reached the end of the range (and stopped, looped or
   turned) during the last process call. */
YSE_C_API unsigned int yse_dsp_zigzag_get_ends(YseDspProcessor* p);
/** 1 when get_ends is not 0. */
YSE_C_API int yse_dsp_zigzag_get_finished(YseDspProcessor* p);
/** Back to the state after construction, keeping the capacity. */
YSE_C_API void yse_dsp_zigzag_reset(YseDspProcessor* p);
/** The function, played. */
YSE_C_API unsigned int yse_dsp_zigzag_process(YseDspProcessor* p, float* out, float* index_out,
                                              unsigned int length);
/** With an input signal (an index in START mode, a trigger in the others)
   and a speed signal (read from the first sample of every 128-sample
   block); either may be NULL for none. */
YSE_C_API unsigned int yse_dsp_zigzag_process_signal(YseDspProcessor* p, const float* input,
                                                     const float* speed, float* out,
                                                     float* index_out, unsigned int length);

/* ─── techno: a step sequencer read by a phase (#1068)
   A step index at or past the capacity is ignored by a setter and reads 0.
   `envelope` and `position`, when not NULL, get the amplitude envelope and
   the position in steps of the same samples. */

/** The capacity yse_dsp_techno_create() takes for 0. */
#define YSE_DSP_TECHNO_DEFAULT_STEPS 16
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_TECHNO_MAX_STEPS 1024

/** Room for `capacity` steps (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_techno_create(unsigned int capacity);
YSE_C_API unsigned int yse_dsp_techno_get_capacity(YseDspProcessor* p);
/** Use the first `steps` steps (clamped to 1 .. the capacity), evenly
   spaced. */
YSE_C_API void yse_dsp_techno_set_length(YseDspProcessor* p, unsigned int steps);
YSE_C_API unsigned int yse_dsp_techno_get_length(YseDspProcessor* p);
/** Step `step`'s pitch in Hz. 440. */
YSE_C_API void yse_dsp_techno_set_pitch(YseDspProcessor* p, unsigned int step, float hz);
YSE_C_API float yse_dsp_techno_get_pitch(YseDspProcessor* p, unsigned int step);
/** Step `step`'s amplitude. 1. */
YSE_C_API void yse_dsp_techno_set_amplitude(YseDspProcessor* p, unsigned int step, float value);
YSE_C_API float yse_dsp_techno_get_amplitude(YseDspProcessor* p, unsigned int step);
/** The exponent of the pitch slide into step `step`. 0 (no slide). */
YSE_C_API void yse_dsp_techno_set_curve(YseDspProcessor* p, unsigned int step, float exponent);
YSE_C_API float yse_dsp_techno_get_curve(YseDspProcessor* p, unsigned int step);
/** The exponents of step `step`'s rise and decay. 1. */
YSE_C_API void yse_dsp_techno_set_attack(YseDspProcessor* p, unsigned int step, float exponent);
YSE_C_API float yse_dsp_techno_get_attack(YseDspProcessor* p, unsigned int step);
YSE_C_API void yse_dsp_techno_set_decay(YseDspProcessor* p, unsigned int step, float exponent);
YSE_C_API float yse_dsp_techno_get_decay(YseDspProcessor* p, unsigned int step);
/** Where step `step` starts, as a fraction of the sequence; clamped between
   its neighbours. Ignored, and read as 0, at or past the length. */
YSE_C_API void yse_dsp_techno_set_position(YseDspProcessor* p, unsigned int step, float start);
YSE_C_API float yse_dsp_techno_get_position(YseDspProcessor* p, unsigned int step);
/** Repeat a pattern of `count` step starts over the sequence. 1 on success;
   0, changing nothing, when the pattern does not fit the length or is out of
   order or range, or `starts` is NULL with a count above 0. A count of 0
   spaces the steps evenly. */
YSE_C_API int yse_dsp_techno_repeat_positions(YseDspProcessor* p, const float* starts,
                                              unsigned int count);
/** The pitch in Hz for a 0..1 phase over the sequence. */
YSE_C_API unsigned int yse_dsp_techno_process(YseDspProcessor* p, const float* phase, float* out,
                                              float* envelope, float* position,
                                              unsigned int length);

/* ─── eventSequencer: text events fired by a phase (#1069) ───────────────── */

/** The capacity yse_dsp_event_sequencer_create() takes for 0, in events over
   all sequences. */
#define YSE_DSP_EVENT_SEQUENCER_DEFAULT_EVENTS 1024
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_EVENT_SEQUENCER_MAX_EVENTS 65536
/** The room for one event's text, the terminator included. */
#define YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY 128
/** The fired capacity yse_dsp_event_sequencer_create() takes for 0. */
#define YSE_DSP_EVENT_SEQUENCER_DEFAULT_FIRED 256

/** One event a process call fired, copied into the host's array. */
typedef struct YseDspEventFiring {
  unsigned int offset; /**< the sample of the process call that fired it */
  float phase; /**< the event's phase */
  unsigned int length; /**< the text's length in bytes */
  char text[YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY]; /**< the text, terminated */
} YseDspEventFiring;

/** Room for `capacity` events (0 means the default) and `fired_capacity`
   events fired per 128-sample block (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_event_sequencer_create(unsigned int capacity,
                                                          unsigned int fired_capacity);
YSE_C_API unsigned int yse_dsp_event_sequencer_get_capacity(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_event_sequencer_get_fired_capacity(YseDspProcessor* p);
/** How many events are stored, over all sequences. */
YSE_C_API unsigned int yse_dsp_event_sequencer_get_size(YseDspProcessor* p);
/** How many events sequence `sequence` holds. */
YSE_C_API unsigned int yse_dsp_event_sequencer_get_count(YseDspProcessor* p, int sequence);
/** Store `text` (`length` bytes, no terminator needed) at `phase` in
   `sequence`. 0, changing nothing, when the store is full, the phase is not
   finite, the text is NULL or empty, holds a line break or a 0 byte, or is
   not shorter than YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY. */
YSE_C_API int yse_dsp_event_sequencer_add(YseDspProcessor* p, int sequence, float phase,
                                          const char* text, unsigned int length);
/** Delete `sequence`'s events with a phase from `from` to `to` (either
   order, both included) and, when `text` is not NULL, that text. Returns how
   many went. */
YSE_C_API unsigned int yse_dsp_event_sequencer_remove(YseDspProcessor* p, int sequence, float from,
                                                      float to, const char* text,
                                                      unsigned int length);
/** Delete every event of `sequence`; returns how many went. */
YSE_C_API unsigned int yse_dsp_event_sequencer_clear(YseDspProcessor* p, int sequence);
/** Delete every event of every sequence. */
YSE_C_API void yse_dsp_event_sequencer_clear_all(YseDspProcessor* p);
/** The sequence played and recorded into. 0. */
YSE_C_API void yse_dsp_event_sequencer_select(YseDspProcessor* p, int sequence);
YSE_C_API int yse_dsp_event_sequencer_get_selected(YseDspProcessor* p);
/** Play the current sequence (0 / 1). Off. */
YSE_C_API void yse_dsp_event_sequencer_set_play(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_event_sequencer_is_playing(YseDspProcessor* p);
/** Record into the current sequence, emptying it first (0 / 1). Off. */
YSE_C_API void yse_dsp_event_sequencer_set_record(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_event_sequencer_is_recording(YseDspProcessor* p);
/** Record into the current sequence, keeping what is there (0 / 1). Off. */
YSE_C_API void yse_dsp_event_sequencer_set_overdub(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_event_sequencer_is_overdubbing(YseDspProcessor* p);
/** Store `text` in the current sequence at the phase of the last sample,
   while recording or overdubbing. 0 otherwise, or for add's reasons. */
YSE_C_API int yse_dsp_event_sequencer_record_event(YseDspProcessor* p, const char* text,
                                                   unsigned int length);
/** The phase of the last sample; 0 before the first. */
YSE_C_API float yse_dsp_event_sequencer_get_phase(YseDspProcessor* p);
/** The `index`-th stored event (0 .. get_size - 1, sorted by sequence, phase
   and the order they were stored): its sequence, its phase and its text's
   length; 0 past the end. */
YSE_C_API int yse_dsp_event_sequencer_get_event_sequence(YseDspProcessor* p, unsigned int index);
YSE_C_API float yse_dsp_event_sequencer_get_event_phase(YseDspProcessor* p, unsigned int index);
YSE_C_API unsigned int yse_dsp_event_sequencer_get_event_length(YseDspProcessor* p,
                                                                unsigned int index);
/** The `index`-th stored event's text, snprintf style: returns its full
   length and writes as much as fits in `cap` bytes, terminated. NULL and 0
   return the length; past the end (or a bad handle) the text is "". */
YSE_C_API size_t yse_dsp_event_sequencer_get_event_text(YseDspProcessor* p, unsigned int index,
                                                        char* buf, size_t cap);
/** One call driven by the phase array (a sample that is not finite holds
   the previous phase). Writes no samples: it returns `length`, or 0 for a
   bad handle or a NULL phase. The events it fires are copied, in the order
   they fired, into `fired` up to `capacity` entries (`fired` may be NULL with
   a capacity of 0); get_fired then says how many fired in all. */
YSE_C_API unsigned int yse_dsp_event_sequencer_process(YseDspProcessor* p, const float* phase,
                                                       YseDspEventFiring* fired,
                                                       unsigned int capacity, unsigned int length);
/** How many events the last process call fired (more than its `capacity`
   means some were not copied). */
YSE_C_API unsigned int yse_dsp_event_sequencer_get_fired(YseDspProcessor* p);
/** Events the last process call crossed but could not fire because a
   128-sample block fired more than the fired capacity. */
YSE_C_API unsigned int yse_dsp_event_sequencer_get_missed(YseDspProcessor* p);
/** Back to the state after construction, keeping the capacities. */
YSE_C_API void yse_dsp_event_sequencer_reset(YseDspProcessor* p);

/* ─── updown: a trapezoid with fixed attack and release times (#1070) ────── */

YSE_C_API YseDspProcessor* yse_dsp_updown_create(void);
/** The rise and fall times in milliseconds. 10 each; below 0 is instant. */
YSE_C_API void yse_dsp_updown_set_up(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_updown_get_up(YseDspProcessor* p);
YSE_C_API void yse_dsp_updown_set_down(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_updown_get_down(YseDspProcessor* p);
/** The output at the top of the trapezoid. 1. */
YSE_C_API void yse_dsp_updown_set_level(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_updown_get_level(YseDspProcessor* p);
/** The jump between two samples that counts as the ramp resetting. 0.5. */
YSE_C_API void yse_dsp_updown_set_reset_threshold(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_updown_get_reset_threshold(YseDspProcessor* p);
/** Drop the envelope to 0: the next sample starts a new rise. */
YSE_C_API void yse_dsp_updown_restart(YseDspProcessor* p);
/** The envelope's position, 0..1 (the output is this times the level). */
YSE_C_API float yse_dsp_updown_get_envelope(YseDspProcessor* p);
/** The envelope for a ramp or gate input. */
YSE_C_API unsigned int yse_dsp_updown_process(YseDspProcessor* p, const float* input, float* out,
                                              unsigned int length);

/* ─── shapeEnvelope: a breakpoint envelope over a ramp's length (#1071) ──── */

/** The capacity yse_dsp_shape_envelope_create() takes for 0. */
#define YSE_DSP_SHAPE_ENVELOPE_DEFAULT_POINTS 256
/** The largest capacity; a larger one is clamped. */
#define YSE_DSP_SHAPE_ENVELOPE_MAX_POINTS 4096

/** Room for `capacity` breakpoints (0 means the default). */
YSE_C_API YseDspProcessor* yse_dsp_shape_envelope_create(unsigned int capacity);
YSE_C_API unsigned int yse_dsp_shape_envelope_get_capacity(YseDspProcessor* p);
/** Set the envelope: `initial`, then `count` breakpoints of values, times
   (ms) and curves (-1..1, NULL for all 0). 1 on success; 0, changing
   nothing, when `count` is past the capacity, a value is not finite, or
   `values` or `times` is NULL with a count above 0. */
YSE_C_API int yse_dsp_shape_envelope_set_breakpoints(YseDspProcessor* p, float initial,
                                                     const float* values, const float* times,
                                                     const float* curves, unsigned int count);
/** The playing envelope's breakpoint count and initial value. */
YSE_C_API unsigned int yse_dsp_shape_envelope_get_count(YseDspProcessor* p);
YSE_C_API float yse_dsp_shape_envelope_get_initial(YseDspProcessor* p);
/** Breakpoint `point`'s value, time and curve; 0 past the count. */
YSE_C_API float yse_dsp_shape_envelope_get_value(YseDspProcessor* p, unsigned int point);
YSE_C_API float yse_dsp_shape_envelope_get_time(YseDspProcessor* p, unsigned int point);
YSE_C_API float yse_dsp_shape_envelope_get_curve(YseDspProcessor* p, unsigned int point);
/** 1 while an envelope waits for the next start (sync update). */
YSE_C_API int yse_dsp_shape_envelope_is_pending(YseDspProcessor* p);
/** How many leading segments play at their own times. 0. */
YSE_C_API void yse_dsp_shape_envelope_set_constant(YseDspProcessor* p, unsigned int segments);
YSE_C_API unsigned int yse_dsp_shape_envelope_get_constant(YseDspProcessor* p);
/** Play segments with their curvature (0 / 1). Off. */
YSE_C_API void yse_dsp_shape_envelope_set_curved(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_shape_envelope_get_curved(YseDspProcessor* p);
/** Hold a new envelope until the next start of the ramp (0 / 1). Off. */
YSE_C_API void yse_dsp_shape_envelope_set_sync_update(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_shape_envelope_get_sync_update(YseDspProcessor* p);
/** The envelope for a ramp input. */
YSE_C_API unsigned int yse_dsp_shape_envelope_process(YseDspProcessor* p, const float* input,
                                                      float* out, unsigned int length);

/* ─── triggeredRamp: a one-shot ramp started by a trigger signal (#1072) ── */

YSE_C_API YseDspProcessor* yse_dsp_triggered_ramp_create(void);
/** The ramp's length in milliseconds. 100. */
YSE_C_API void yse_dsp_triggered_ramp_set_duration(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_triggered_ramp_get_duration(YseDspProcessor* p);
/** The values the ramp starts from and ends on. 0 and 1. */
YSE_C_API void yse_dsp_triggered_ramp_set_start(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_triggered_ramp_get_start(YseDspProcessor* p);
YSE_C_API void yse_dsp_triggered_ramp_set_end(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_triggered_ramp_get_end(YseDspProcessor* p);
/** Curvature, -1 to 1 (clamped); 0 is linear. */
YSE_C_API void yse_dsp_triggered_ramp_set_curve(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_triggered_ramp_get_curve(YseDspProcessor* p);
/** When the duration is read. YSE_DSP_RAMP_DURATION_AT_START. */
YSE_C_API void yse_dsp_triggered_ramp_set_mode(YseDspProcessor* p, YseDspRampDurationMode mode);
YSE_C_API YseDspRampDurationMode yse_dsp_triggered_ramp_get_mode(YseDspProcessor* p);
/** Output 0 once the ramp has ended and before the first trigger (0 / 1).
   Off. */
YSE_C_API void yse_dsp_triggered_ramp_set_reset_to_zero(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_triggered_ramp_get_reset_to_zero(YseDspProcessor* p);
/** A trigger restarts a running ramp (0 / 1). Off. */
YSE_C_API void yse_dsp_triggered_ramp_set_retrigger(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_triggered_ramp_get_retrigger(YseDspProcessor* p);
/** 1 while a ramp runs. */
YSE_C_API int yse_dsp_triggered_ramp_is_running(YseDspProcessor* p);
/** The phase of the current or last ramp, 0..1. */
YSE_C_API float yse_dsp_triggered_ramp_get_phase(YseDspProcessor* p);
/** The ramp for a trigger signal, at the set duration, start and end. */
YSE_C_API unsigned int yse_dsp_triggered_ramp_process(YseDspProcessor* p, const float* trigger,
                                                      float* out, unsigned int length);
/** The ramp with a per-sample duration (ms), start and end. */
YSE_C_API unsigned int yse_dsp_triggered_ramp_process_signal(YseDspProcessor* p,
                                                             const float* trigger,
                                                             const float* duration,
                                                             const float* start, const float* end,
                                                             float* out, unsigned int length);

#ifdef __cplusplus
}
#endif

#endif
