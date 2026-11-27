/**
  @file
  yse_dsp_analysis.h — the signal analysers and meters of the MSP port. C ABI
  mirror of the YseEngine/dsp/analysis/ classes added by #1034 (issue #1301):
  direction, edgeDetector, schmittTrigger, spikeDetector, zeroCrossing,
  peakTracker, movingAverage, minMax, loudnessMeter, pitchTracker,
  sampleCapture, levelMeter, scopeCapture, spectrumAnalyzer and plotData. The
  engine headers document each analyser's algorithm, clamping and defaults;
  this header documents only what the C mirror adds.

  Every handle is a YseDspProcessor (yse_dsp_processor.h), released with
  yse_dsp_processor_destroy(). The processor conventions apply throughout: a
  NULL handle or one made by another class's constructor makes a setter a
  no-op and a getter or process function return 0; process functions take
  `length` samples from host float arrays and return `length`, or 0 (nothing
  done) for a refused handle or a NULL required array. Every call here but a
  constructor allocates nothing, takes no lock and never sets
  yse_last_error(), so each is safe on a real-time thread.

  Reports over a whole call. Several analysers report what one engine block
  found: an edge detector's rises, a peak tracker's interval reports, a
  meter's finished windows. A process call spans several 128-sample blocks,
  so the bridge gathers them over the whole call:

  - A count (yse_dsp_edge_detector_get_rises, ..._get_reports, ...) covers
    every sample the last process call took.
  - Positions are sample indices into that call, written in order to an
    optional host array up to a `capacity`; the count says how many there
    were in all, so a host can tell when its array was too small. An array
    may be NULL with any capacity.
  - A refused call leaves the reports as they were. A class's reset clears
    them, as the engine's reset clears its own; so does an edge detector's
    set(), which clears the engine report too. A per-sample call leaves them,
    as the engine's leaves its block report.

  Lists. A list setter returns 1 when applied and 0, changing nothing, for a
  refused handle or a NULL array with a count above 0. A list getter returns
  the size of the whole list and writes as many entries as `capacity` holds;
  NULL and 0 return the size.

  Enums. An enum setter ignores a value outside the enum.

  Threads. A processor is called from one thread at a time, and that holds
  here too, with one addition: the four classes that publish something for a
  host to draw — levelMeter, scopeCapture, spectrumAnalyzer and plotData —
  let one other thread read it while the analysing thread goes on. Each
  class's section says which call belongs to which thread:

  - analysing thread: the constructor's caller hands the handle to it; every
    setter, getter and process call not listed below.
  - reader thread: yse_dsp_level_meter_read, yse_dsp_scope_capture_acquire /
    _held, yse_dsp_spectrum_analyzer_acquire / _held and
    yse_dsp_plot_data_acquire / _held / _held_subplot. One thread at a time
    reads; it may be the analysing thread. A reader call copies a published
    frame out without waiting, copying under a lock or allocating, and a
    frame it holds is never written by the analysing thread.
  - either thread: yse_dsp_scope_capture_get_traces,
    yse_dsp_spectrum_analyzer_get_frames, yse_dsp_plot_data_get_frames, and
    the capacities fixed by the constructor (yse_dsp_scope_capture_get_capacity,
    yse_dsp_spectrum_analyzer_get_capacity, yse_dsp_plot_data_get_plots,
    yse_dsp_plot_data_get_capacity).

  yse_dsp_processor_destroy() must not overlap any call on the handle, from
  either thread.

  Not mirrored: the float-setting helpers the patcher uses (scopeCapture::Size
  / Period / Trigger, spectrumAnalyzer::Size / Mode, plotData::Points / Plots
  / FrameSize); the C setters take integers and enums and clamp the same way.
  plotData's sample-buffer call reads a shared sample buffer, whose C surface
  comes with #1303.
*/

#ifndef YSE_C_DSP_ANALYSIS_H_INCLUDED
#define YSE_C_DSP_ANALYSIS_H_INCLUDED

#include "yse_common.h"
#include "yse_dsp_processor.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── direction: the sign of the first difference (#1120) ────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_direction_create(void);
/** One sample: 1 above the previous input, -1 below it, 0 otherwise. */
YSE_C_API float yse_dsp_direction_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_direction_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length);
/** Sets the remembered sample the next input is compared with. */
YSE_C_API void yse_dsp_direction_set(YseDspProcessor* p, float value);
YSE_C_API void yse_dsp_direction_reset(YseDspProcessor* p);
YSE_C_API float yse_dsp_direction_get_previous(YseDspProcessor* p);

/* ─── edgeDetector: zero / non-zero transitions (#1121) ──────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_edge_detector_create(void);
/** One sample: 1 for a rise, -1 for a fall, 0 otherwise. */
YSE_C_API int yse_dsp_edge_detector_process_sample(YseDspProcessor* p, float in);
/** Finds the rises and falls in `in`. Their positions go, in order, to the
   optional `rises` and `falls` arrays up to their capacities. Writes no
   samples: returns `length`, or 0 for a refused handle or a NULL `in`. */
YSE_C_API unsigned int
yse_dsp_edge_detector_process(YseDspProcessor* p, const float* in, unsigned int* rises,
                              unsigned int rise_capacity, unsigned int* falls,
                              unsigned int fall_capacity, unsigned int length);
/** Rises in the last process call. */
YSE_C_API unsigned int yse_dsp_edge_detector_get_rises(YseDspProcessor* p);
/** Falls in the last process call. */
YSE_C_API unsigned int yse_dsp_edge_detector_get_falls(YseDspProcessor* p);
/** 1 while the remembered sample is non-zero. */
YSE_C_API int yse_dsp_edge_detector_is_high(YseDspProcessor* p);
/** Sets the remembered state (0 / 1) and clears the reports. */
YSE_C_API void yse_dsp_edge_detector_set(YseDspProcessor* p, int non_zero);
YSE_C_API void yse_dsp_edge_detector_reset(YseDspProcessor* p);

/* ─── schmittTrigger: a threshold with hysteresis (#1122) ────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_schmitt_trigger_create(void);
/** The low (reset) threshold. 0; NaN is ignored. */
YSE_C_API void yse_dsp_schmitt_trigger_set_low(YseDspProcessor* p, float threshold);
YSE_C_API float yse_dsp_schmitt_trigger_get_low(YseDspProcessor* p);
/** The high (set) threshold. 0; NaN is ignored. */
YSE_C_API void yse_dsp_schmitt_trigger_set_high(YseDspProcessor* p, float threshold);
YSE_C_API float yse_dsp_schmitt_trigger_get_high(YseDspProcessor* p);
/** Sets the output state (0 / 1). */
YSE_C_API void yse_dsp_schmitt_trigger_set(YseDspProcessor* p, int on);
YSE_C_API void yse_dsp_schmitt_trigger_reset(YseDspProcessor* p);
/** 1 while the output is 1. */
YSE_C_API int yse_dsp_schmitt_trigger_is_on(YseDspProcessor* p);
YSE_C_API float yse_dsp_schmitt_trigger_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_schmitt_trigger_process(YseDspProcessor* p, const float* in,
                                                       float* out, unsigned int length);
/** The thresholds read per sample; they also set the getters. */
YSE_C_API unsigned int yse_dsp_schmitt_trigger_process_signal(YseDspProcessor* p, const float* in,
                                                              const float* low, const float* high,
                                                              float* out, unsigned int length);

/* ─── spikeDetector: the time between rises (#1123) ──────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_spike_detector_create(void);
/** One sample: 1 when it is a reported rise (its interval is then
   get_last_interval). */
YSE_C_API int yse_dsp_spike_detector_process_sample(YseDspProcessor* p, float in);
/** Finds the reported rises in `in`. Their positions and intervals (in ms)
   go, in order, to the optional `positions` and `intervals` arrays, both up
   to `capacity` entries. Writes no samples: returns `length`, or 0 for a
   refused handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_spike_detector_process(YseDspProcessor* p, const float* in,
                                                      unsigned int* positions, float* intervals,
                                                      unsigned int capacity, unsigned int length);
/** Reported rises in the last process call. */
YSE_C_API unsigned int yse_dsp_spike_detector_get_spikes(YseDspProcessor* p);
/** The interval of the most recent reported rise in ms, from any call. */
YSE_C_API float yse_dsp_spike_detector_get_last_interval(YseDspProcessor* p);
/** The refractory period in ms. 0; negative or NaN is 0. */
YSE_C_API void yse_dsp_spike_detector_set_refractory(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_spike_detector_get_refractory(YseDspProcessor* p);
/** Starts the refractory period again from the next sample. */
YSE_C_API void yse_dsp_spike_detector_restart(YseDspProcessor* p);
YSE_C_API void yse_dsp_spike_detector_reset(YseDspProcessor* p);

/* ─── zeroCrossing: crossings per block and a click on each (#1124) ──────── */

YSE_C_API YseDspProcessor* yse_dsp_zero_crossing_create(void);
/** One sample: 1 when it is a crossing. */
YSE_C_API int yse_dsp_zero_crossing_process_sample(YseDspProcessor* p, float in);
/** Finds the crossings in `in`. Every output is optional: `counts` gets the
   number of crossings of each 128-sample block on every sample of it (the
   engine's counts()), `clicks` the click volume on each crossing and 0
   elsewhere, and `positions` the crossings' positions in order up to
   `capacity`. Returns `length`, or 0 for a refused handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_zero_crossing_process(YseDspProcessor* p, const float* in,
                                                     float* counts, float* clicks,
                                                     unsigned int* positions, unsigned int capacity,
                                                     unsigned int length);
/** Crossings in the last process call. */
YSE_C_API unsigned int yse_dsp_zero_crossing_get_crossings(YseDspProcessor* p);
/** The click volume. 1; NaN is 0. */
YSE_C_API void yse_dsp_zero_crossing_set_volume(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_zero_crossing_get_volume(YseDspProcessor* p);
YSE_C_API void yse_dsp_zero_crossing_reset(YseDspProcessor* p);

/* ─── peakTracker: the peak since the last report (#1126) ────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_peak_tracker_create(void);
/** One sample: 1 when an interval report falls on it (its peak is then
   get_last_report). */
YSE_C_API int yse_dsp_peak_tracker_process_sample(YseDspProcessor* p, float in);
/** Tracks the peak of `in`. The interval reports' positions and peaks go,
   in order, to the optional `positions` and `values` arrays, both up to
   `capacity` entries. Writes no samples: returns `length`, or 0 for a
   refused handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_peak_tracker_process(YseDspProcessor* p, const float* in,
                                                    unsigned int* positions, float* values,
                                                    unsigned int capacity, unsigned int length);
/** Interval reports in the last process call. */
YSE_C_API unsigned int yse_dsp_peak_tracker_get_reports(YseDspProcessor* p);
/** The most recent report, from take or an interval; 0 before the first. */
YSE_C_API float yse_dsp_peak_tracker_get_last_report(YseDspProcessor* p);
/** The peak of the running window, without ending it. */
YSE_C_API float yse_dsp_peak_tracker_get_peak(YseDspProcessor* p);
/** Reports now: the running window's peak, starting a new window. */
YSE_C_API float yse_dsp_peak_tracker_take(YseDspProcessor* p);
/** The reporting interval in ms; 0 (the default) reports only on take. */
YSE_C_API void yse_dsp_peak_tracker_set_interval(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_peak_tracker_get_interval(YseDspProcessor* p);
/** Signed mode (0 / 1): the peak keeps its sample's sign. Off. */
YSE_C_API void yse_dsp_peak_tracker_set_signed(YseDspProcessor* p, int on);
YSE_C_API int yse_dsp_peak_tracker_is_signed(YseDspProcessor* p);
YSE_C_API void yse_dsp_peak_tracker_reset(YseDspProcessor* p);

/* ─── movingAverage: the average over the last N samples (#1118, #1119) ──── */

/** The capacity the engine's default constructor takes. */
#define YSE_DSP_MOVING_AVERAGE_DEFAULT_CAPACITY 100
/** The largest capacity, in samples. */
#define YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY 1048576

/** A window of up to `capacity` samples, clamped to 1 to
   YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY, starting at the full capacity in
   bipolar mode. */
YSE_C_API YseDspProcessor* yse_dsp_moving_average_create(unsigned int capacity);
YSE_C_API float yse_dsp_moving_average_process_sample(YseDspProcessor* p, float in);
YSE_C_API unsigned int yse_dsp_moving_average_process(YseDspProcessor* p, const float* in,
                                                      float* out, unsigned int length);
/** The average of the window that ends on the last sample taken. */
YSE_C_API float yse_dsp_moving_average_get_value(YseDspProcessor* p);
/** The window in samples, clamped to 1 .. the capacity. */
YSE_C_API void yse_dsp_moving_average_set_length(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_moving_average_get_length(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_moving_average_get_capacity(YseDspProcessor* p);
YSE_C_API void yse_dsp_moving_average_set_mode(YseDspProcessor* p, YseDspAverageMode mode);
YSE_C_API YseDspAverageMode yse_dsp_moving_average_get_mode(YseDspProcessor* p);
/** Feeds `in` to the accumulation only. Returns `length`, or 0 for a refused
   handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_moving_average_accumulate(YseDspProcessor* p, const float* in,
                                                         unsigned int length);
/** The average of every sample since the last take, without starting over. */
YSE_C_API float yse_dsp_moving_average_get_accumulated(YseDspProcessor* p);
/** The number of samples since the last take. */
YSE_C_API double yse_dsp_moving_average_get_accumulated_samples(YseDspProcessor* p);
/** Reports get_accumulated and starts a new accumulation. */
YSE_C_API float yse_dsp_moving_average_take(YseDspProcessor* p);
YSE_C_API void yse_dsp_moving_average_reset(YseDspProcessor* p);

/* ─── minMax: the running minimum and maximum (#1127) ────────────────────── */

YSE_C_API YseDspProcessor* yse_dsp_min_max_create(void);
YSE_C_API void yse_dsp_min_max_process_sample(YseDspProcessor* p, float in);
/** One sample; a `reset` that is not 0 (and not NaN) starts over at `in`. */
YSE_C_API void yse_dsp_min_max_process_sample_reset(YseDspProcessor* p, float in, float reset);
/** Tracks `in`; the minimum and maximum after each sample go to the optional
   `minimum` and `maximum` arrays. Returns `length`, or 0 for a refused handle
   or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_min_max_process(YseDspProcessor* p, const float* in, float* minimum,
                                               float* maximum, unsigned int length);
/** The same, starting over at every sample where `reset` is not 0. */
YSE_C_API unsigned int yse_dsp_min_max_process_reset(YseDspProcessor* p, const float* in,
                                                     const float* reset, float* minimum,
                                                     float* maximum, unsigned int length);
YSE_C_API float yse_dsp_min_max_get_minimum(YseDspProcessor* p);
YSE_C_API float yse_dsp_min_max_get_maximum(YseDspProcessor* p);
/** 1 while no sample has been tracked since the last reset. */
YSE_C_API int yse_dsp_min_max_is_empty(YseDspProcessor* p);
YSE_C_API void yse_dsp_min_max_reset(YseDspProcessor* p);

/* ─── loudnessMeter: BS.1770 / EBU R 128 loudness (#1129, #1237) ─────────── */

/** The most channels a meter measures. */
#define YSE_DSP_LOUDNESS_MAX_CHANNELS 64
/** The reading of silence in LUFS and dBFS, and of an integrated loudness
   with no block past the gates. */
#define YSE_DSP_LOUDNESS_SILENCE (-200.0f)

/** A meter for `channels` channels, clamped to 1 to
   YSE_DSP_LOUDNESS_MAX_CHANNELS, every weight 1. */
YSE_C_API YseDspProcessor* yse_dsp_loudness_meter_create(unsigned int channels);
YSE_C_API unsigned int yse_dsp_loudness_meter_get_channels(YseDspProcessor* p);
/** The weight of `channel` (from 0). Below 0, not finite or a channel out of
   range is ignored. */
YSE_C_API void yse_dsp_loudness_meter_set_weight(YseDspProcessor* p, unsigned int channel,
                                                 float value);
YSE_C_API float yse_dsp_loudness_meter_get_weight(YseDspProcessor* p, unsigned int channel);
/** One channel's samples, channel 0; every other channel reads as silence.
   Returns `length`, or 0 for a refused handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_loudness_meter_process(YseDspProcessor* p, const float* in,
                                                      unsigned int length);
/** `count` channels, `channels[c]` holding channel c's `length` samples. A
   NULL entry, or a channel past `count`, reads as silence; channels past
   get_channels are ignored. Returns `length`, or 0 for a refused handle or a
   NULL `channels`. */
YSE_C_API unsigned int yse_dsp_loudness_meter_process_channels(YseDspProcessor* p,
                                                               const float* const* channels,
                                                               unsigned int count,
                                                               unsigned int length);
/** Interval periods that ended in the last process call. */
YSE_C_API unsigned int yse_dsp_loudness_meter_get_reports(YseDspProcessor* p);
/** The momentary loudness (400 ms) in LUFS. */
YSE_C_API float yse_dsp_loudness_meter_get_momentary(YseDspProcessor* p);
/** The short-term loudness (3 s) in LUFS. */
YSE_C_API float yse_dsp_loudness_meter_get_short_term(YseDspProcessor* p);
YSE_C_API float yse_dsp_loudness_meter_get_max_momentary(YseDspProcessor* p);
YSE_C_API float yse_dsp_loudness_meter_get_max_short_term(YseDspProcessor* p);
/** The gated integrated loudness since the last reset in LUFS. */
YSE_C_API float yse_dsp_loudness_meter_get_integrated(YseDspProcessor* p);
/** The loudness range in LU since the last reset. */
YSE_C_API float yse_dsp_loudness_meter_get_loudness_range(YseDspProcessor* p);
/** The sample peak in dBFS of `channel` counted from 1, or with 0 the
   highest of all channels. */
YSE_C_API float yse_dsp_loudness_meter_get_peak(YseDspProcessor* p, unsigned int channel);
/** The true peak in dBTP, `channel` as for get_peak. */
YSE_C_API float yse_dsp_loudness_meter_get_true_peak(YseDspProcessor* p, unsigned int channel);
/** The 400 ms blocks above the absolute gate since the last reset. */
YSE_C_API unsigned long long yse_dsp_loudness_meter_get_gated_blocks(YseDspProcessor* p);
/** The reporting interval in ms; 0 (the default) turns it off. */
YSE_C_API void yse_dsp_loudness_meter_set_interval(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_loudness_meter_get_interval(YseDspProcessor* p);
YSE_C_API void yse_dsp_loudness_meter_reset(YseDspProcessor* p);
/** The K-weighting high shelf at `sample_rate`, as b0 b1 b2 a1 a2 in `out`
   (five doubles). Returns 1, or 0 for a NULL `out`. */
YSE_C_API int yse_dsp_loudness_meter_shelf(double sample_rate, double* out);
/** The K-weighting high-pass, the same way. */
YSE_C_API int yse_dsp_loudness_meter_high_pass(double sample_rate, double* out);

/* ─── pitchTracker: YIN pitch, amplitude and onsets (#1130) ──────────────── */

#define YSE_DSP_PITCH_TRACKER_MIN_SIZE 128
#define YSE_DSP_PITCH_TRACKER_MAX_SIZE 4096
#define YSE_DSP_PITCH_TRACKER_MIN_PERIOD 16
#define YSE_DSP_PITCH_TRACKER_MAX_PERIOD 4096
#define YSE_DSP_PITCH_TRACKER_DEFAULT_SIZE 2048
#define YSE_DSP_PITCH_TRACKER_DEFAULT_PERIOD 512
#define YSE_DSP_PITCH_TRACKER_DEFAULT_ONSET_PERIOD 2048

/** A tracker with the engine defaults (frequencies 40 to 4000 Hz, threshold
   0.01, tolerance 0.15, onset amplitude 0.1, onset pitch 1 semitone). */
YSE_C_API YseDspProcessor* yse_dsp_pitch_tracker_create(void);
/** The analysis buffer, rounded down to a power of two within the limits. */
YSE_C_API void yse_dsp_pitch_tracker_set_size(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_size(YseDspProcessor* p);
/** The time between analyses, within the limits. */
YSE_C_API void yse_dsp_pitch_tracker_set_period(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_period(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_min_frequency(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_pitch_tracker_get_min_frequency(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_max_frequency(YseDspProcessor* p, float hz);
YSE_C_API float yse_dsp_pitch_tracker_get_max_frequency(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_threshold(YseDspProcessor* p, float amplitude);
YSE_C_API float yse_dsp_pitch_tracker_get_threshold(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_tolerance(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_pitch_tracker_get_tolerance(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_onset_amplitude(YseDspProcessor* p, float amplitude);
YSE_C_API float yse_dsp_pitch_tracker_get_onset_amplitude(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_onset_pitch(YseDspProcessor* p, float semitones);
YSE_C_API float yse_dsp_pitch_tracker_get_onset_pitch(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_set_onset_period(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_onset_period(YseDspProcessor* p);
/** Takes in `length` samples. Returns `length`, or 0 for a refused handle or
   a NULL `in`; get_analyses then says how many analyses ran in them. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_process(YseDspProcessor* p, const float* in,
                                                     unsigned int length);
/** Analyses in the last process call. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_analyses(YseDspProcessor* p);
/** Analyses of the last process call that passed the gate. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_reports(YseDspProcessor* p);
/** Analyses of the last process call that found a pitch. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_pitches(YseDspProcessor* p);
/** Onsets the last process call found. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_onsets(YseDspProcessor* p);
/** The pitch of the last analysis that found one, in Hz; 0 before any. */
YSE_C_API float yse_dsp_pitch_tracker_get_frequency(YseDspProcessor* p);
YSE_C_API float yse_dsp_pitch_tracker_get_clarity(YseDspProcessor* p);
YSE_C_API float yse_dsp_pitch_tracker_get_amplitude(YseDspProcessor* p);
/** 1 when the last analysis found a pitch. */
YSE_C_API int yse_dsp_pitch_tracker_is_voiced(YseDspProcessor* p);
YSE_C_API float yse_dsp_pitch_tracker_get_onset_frequency(YseDspProcessor* p);
YSE_C_API float yse_dsp_pitch_tracker_get_onset_peak(YseDspProcessor* p);
/** size / 2 samples. */
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_latency(YseDspProcessor* p);
YSE_C_API void yse_dsp_pitch_tracker_reset(YseDspProcessor* p);

/* ─── sampleCapture: a fixed-size store of samples (#1128) ───────────────── */

#define YSE_DSP_SAMPLE_CAPTURE_DEFAULT_CAPACITY 4096
#define YSE_DSP_SAMPLE_CAPTURE_MAX_CAPACITY 1048576
/** The most block positions set_indices takes. */
#define YSE_DSP_SAMPLE_CAPTURE_MAX_INDICES 10

/** A store of `capacity` samples, clamped to 1 to
   YSE_DSP_SAMPLE_CAPTURE_MAX_CAPACITY. */
YSE_C_API YseDspProcessor* yse_dsp_sample_capture_create(unsigned int capacity);
YSE_C_API void yse_dsp_sample_capture_set_mode(YseDspProcessor* p, YseDspCaptureMode mode);
YSE_C_API YseDspCaptureMode yse_dsp_sample_capture_get_mode(YseDspProcessor* p);
/** Stores only the samples at these positions of each 128-sample block (the
   first YSE_DSP_SAMPLE_CAPTURE_MAX_INDICES, sorted, duplicates dropped). A
   count of 0 stores every sample again. */
YSE_C_API int yse_dsp_sample_capture_set_indices(YseDspProcessor* p, const unsigned int* list,
                                                 unsigned int count);
/** The block positions set, ascending; a list getter (0: every sample). */
YSE_C_API unsigned int yse_dsp_sample_capture_get_indices(YseDspProcessor* p, unsigned int* out,
                                                          unsigned int capacity);
/** Stores one sample. */
YSE_C_API void yse_dsp_sample_capture_process_sample(YseDspProcessor* p, float in);
/** Stores the samples of `in`, every one or those at the indices of each
   128-sample block. Returns `length`, or 0 for a refused handle or a NULL
   `in`. */
YSE_C_API unsigned int yse_dsp_sample_capture_process(YseDspProcessor* p, const float* in,
                                                      unsigned int length);
YSE_C_API unsigned int yse_dsp_sample_capture_get_size(YseDspProcessor* p);
YSE_C_API unsigned int yse_dsp_sample_capture_get_capacity(YseDspProcessor* p);
YSE_C_API int yse_dsp_sample_capture_is_full(YseDspProcessor* p);
/** Held sample `index`, 0 the oldest; 0 past the size. */
YSE_C_API float yse_dsp_sample_capture_get_sample(YseDspProcessor* p, unsigned int index);
/** The held samples, oldest first; a list getter. */
YSE_C_API unsigned int yse_dsp_sample_capture_get_samples(YseDspProcessor* p, float* out,
                                                          unsigned int capacity);
YSE_C_API void yse_dsp_sample_capture_clear(YseDspProcessor* p);

/* ─── levelMeter: a meter's level, hold and clip (#1166) ─────────────────────
   Threads: yse_dsp_level_meter_read is the reader thread's; every other call
   is the analysing thread's. */

/** The reading of silence in dB, and the lowest one given. */
#define YSE_DSP_LEVEL_METER_SILENCE (-200.0f)

/** What a meter shows, as one consistent snapshot. */
typedef struct YseDspLevelReading {
  float level; /**< the level after the ballistics, an amplitude */
  float level_db; /**< the level in dB, the offset included */
  float peak_hold; /**< the held peak, an amplitude */
  float peak_hold_db; /**< the held peak in dB, the offset included */
  int clipped; /**< 1 after a sample above 1 in magnitude, until clear */
} YseDspLevelReading;

YSE_C_API YseDspProcessor* yse_dsp_level_meter_create(void);
/** One sample: 1 when a window ends on it. */
YSE_C_API int yse_dsp_level_meter_process_sample(YseDspProcessor* p, float in);
/** Measures `in`. Returns `length`, or 0 for a refused handle or a NULL
   `in`; get_reports then says how many windows ended in it. */
YSE_C_API unsigned int yse_dsp_level_meter_process(YseDspProcessor* p, const float* in,
                                                   unsigned int length);
/** Windows that ended in the last process call. */
YSE_C_API unsigned int yse_dsp_level_meter_get_reports(YseDspProcessor* p);
/** Shows `amplitude` now, as if a window with that value had just ended. */
YSE_C_API void yse_dsp_level_meter_set(YseDspProcessor* p, float amplitude);
/** set from a reading in dB, the offset included. */
YSE_C_API void yse_dsp_level_meter_set_db(YseDspProcessor* p, float db);
YSE_C_API float yse_dsp_level_meter_get_level(YseDspProcessor* p);
YSE_C_API float yse_dsp_level_meter_get_level_db(YseDspProcessor* p);
YSE_C_API float yse_dsp_level_meter_get_peak_hold(YseDspProcessor* p);
YSE_C_API float yse_dsp_level_meter_get_peak_hold_db(YseDspProcessor* p);
YSE_C_API int yse_dsp_level_meter_is_clipped(YseDspProcessor* p);
/** Turns the clip indicator off and drops the held peak to the level. */
YSE_C_API void yse_dsp_level_meter_clear(YseDspProcessor* p);
YSE_C_API void yse_dsp_level_meter_reset(YseDspProcessor* p);
YSE_C_API void yse_dsp_level_meter_set_detector(YseDspProcessor* p, YseDspLevelDetector detector);
YSE_C_API YseDspLevelDetector yse_dsp_level_meter_get_detector(YseDspProcessor* p);
/** The window length in ms. 20. */
YSE_C_API void yse_dsp_level_meter_set_interval(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_level_meter_get_interval(YseDspProcessor* p);
YSE_C_API void yse_dsp_level_meter_set_attack(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_level_meter_get_attack(YseDspProcessor* p);
YSE_C_API void yse_dsp_level_meter_set_release(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_level_meter_get_release(YseDspProcessor* p);
YSE_C_API void yse_dsp_level_meter_set_hold(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_level_meter_get_hold(YseDspProcessor* p);
/** The reference level in dBFS that reads 0 dB. 0. */
YSE_C_API void yse_dsp_level_meter_set_offset(YseDspProcessor* p, float db);
YSE_C_API float yse_dsp_level_meter_get_offset(YseDspProcessor* p);
/** Sets the attack, release and offset of a standard meter. */
YSE_C_API void yse_dsp_level_meter_set_ballistics(YseDspProcessor* p,
                                                  YseDspLevelBallistics standard);
/** The attack and release (ms) and offset (dB) of `standard`, each to an
   optional output. Returns 1, or 0 for a value outside the enum. */
YSE_C_API int yse_dsp_level_meter_ballistics_settings(YseDspLevelBallistics standard, float* attack,
                                                      float* release, float* offset);
/** Where `db` lies from `floor_db` (0) to `ceiling_db` (1). */
YSE_C_API float yse_dsp_level_meter_position(float db, float floor_db, float ceiling_db);
/** Reader thread: copies the newest reading to `out`. The analysing thread
   publishes one at the end of every call that can change it (process,
   process_sample, set, set_db, clear, reset, set_offset, set_ballistics),
   and the constructor publishes the first. Returns 1, or 0 (nothing
   written) for a refused handle or a NULL `out`. */
YSE_C_API int yse_dsp_level_meter_read(YseDspProcessor* p, YseDspLevelReading* out);

/* ─── scopeCapture: an oscilloscope's traces (#1169) ─────────────────────────
   Threads: acquire and held are the reader thread's, get_traces and
   get_capacity either thread's, every other call the analysing thread's. */

#define YSE_DSP_SCOPE_DEFAULT_SIZE 128
#define YSE_DSP_SCOPE_DEFAULT_PERIOD 256
#define YSE_DSP_SCOPE_MIN_SIZE 2
#define YSE_DSP_SCOPE_MAX_SIZE 8192
#define YSE_DSP_SCOPE_MIN_PERIOD 2
#define YSE_DSP_SCOPE_MAX_PERIOD 8192

/** A finished trace's description. A serial of 0 is no trace yet. */
typedef struct YseDspScopeTrace {
  YseDspScopeMode mode; /**< what the points hold */
  unsigned int size; /**< the points */
  unsigned int serial; /**< the traces finished up to this one, from 1 */
} YseDspScopeTrace;

/** A capture of up to `capacity` points a trace, clamped to
   YSE_DSP_SCOPE_MIN_SIZE .. YSE_DSP_SCOPE_MAX_SIZE. */
YSE_C_API YseDspProcessor* yse_dsp_scope_capture_create(unsigned int capacity);
YSE_C_API void yse_dsp_scope_capture_set_size(YseDspProcessor* p, unsigned int points);
YSE_C_API unsigned int yse_dsp_scope_capture_get_size(YseDspProcessor* p);
YSE_C_API void yse_dsp_scope_capture_set_period(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_scope_capture_get_period(YseDspProcessor* p);
/** The wait between traces in ms. 0. */
YSE_C_API void yse_dsp_scope_capture_set_delay(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_scope_capture_get_delay(YseDspProcessor* p);
YSE_C_API void yse_dsp_scope_capture_set_trigger(YseDspProcessor* p, YseDspScopeTrigger trigger);
YSE_C_API YseDspScopeTrigger yse_dsp_scope_capture_get_trigger(YseDspProcessor* p);
YSE_C_API void yse_dsp_scope_capture_set_level(YseDspProcessor* p, float value);
YSE_C_API float yse_dsp_scope_capture_get_level(YseDspProcessor* p);
/** Either thread. */
YSE_C_API unsigned int yse_dsp_scope_capture_get_capacity(YseDspProcessor* p);
/** Captures `in` as waveform points. Returns `length`, or 0 for a refused
   handle or a NULL `in`. */
YSE_C_API unsigned int yse_dsp_scope_capture_process(YseDspProcessor* p, const float* in,
                                                     unsigned int length);
/** Captures an X/Y pair as X/Y points. */
YSE_C_API unsigned int yse_dsp_scope_capture_process_xy(YseDspProcessor* p, const float* x,
                                                        const float* y, unsigned int length);
YSE_C_API void yse_dsp_scope_capture_restart(YseDspProcessor* p);
/** Either thread: how many traces have finished. */
YSE_C_API unsigned int yse_dsp_scope_capture_get_traces(YseDspProcessor* p);
/** Reader thread: takes the newest finished trace (or keeps the one held
   when none is newer), describes it in the optional `trace`, and copies its
   points to the optional `first` and `second` arrays (the minima and maxima
   of a waveform, X and Y of an X/Y trace) up to `capacity` each. Returns
   the trace's size, 0 before the first trace or for a refused handle (then
   `trace` gets a serial of 0 and nothing else is written). */
YSE_C_API unsigned int yse_dsp_scope_capture_acquire(YseDspProcessor* p, YseDspScopeTrace* trace,
                                                     float* first, float* second,
                                                     unsigned int capacity);
/** Reader thread: the same for the trace already held, without taking a
   newer one. */
YSE_C_API unsigned int yse_dsp_scope_capture_held(YseDspProcessor* p, YseDspScopeTrace* trace,
                                                  float* first, float* second,
                                                  unsigned int capacity);

/* ─── spectrumAnalyzer: a spectrum every update interval (#1170) ─────────────
   Threads: acquire and held are the reader thread's, get_frames and
   get_capacity either thread's, every other call the analysing thread's. */

#define YSE_DSP_SPECTRUM_DEFAULT_SIZE 4096
#define YSE_DSP_SPECTRUM_MIN_SIZE 16
#define YSE_DSP_SPECTRUM_MAX_SIZE 16384

/** A finished frame's description. A serial of 0 is no frame yet. */
typedef struct YseDspSpectrumFrame {
  YseDspSpectrumMode mode; /**< what the bins hold */
  unsigned int size; /**< the FFT size */
  unsigned int bins; /**< the bins, DC first: bin j is j * rate / size Hz */
  unsigned int serial; /**< the frames finished up to this one, from 1 */
  unsigned int rate; /**< the sample rate the frame was taken at */
} YseDspSpectrumFrame;

/** An analyser of FFT sizes up to `capacity`, made a power of two from
   YSE_DSP_SPECTRUM_MIN_SIZE to YSE_DSP_SPECTRUM_MAX_SIZE. */
YSE_C_API YseDspProcessor* yse_dsp_spectrum_analyzer_create(unsigned int capacity);
/** The FFT size: the power of two at or above `samples`, within the
   capacity. */
YSE_C_API void yse_dsp_spectrum_analyzer_set_size(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_size(YseDspProcessor* p);
/** The update interval in ms, 10 .. 2000. 20; NaN is ignored. */
YSE_C_API void yse_dsp_spectrum_analyzer_set_interval(YseDspProcessor* p, float ms);
YSE_C_API float yse_dsp_spectrum_analyzer_get_interval(YseDspProcessor* p);
YSE_C_API void yse_dsp_spectrum_analyzer_set_mode(YseDspProcessor* p, YseDspSpectrumMode mode);
YSE_C_API YseDspSpectrumMode yse_dsp_spectrum_analyzer_get_mode(YseDspProcessor* p);
/** Either thread. */
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_capacity(YseDspProcessor* p);
/** Takes in `in`. Returns `length`, or 0 for a refused handle or a NULL
   `in`. */
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_process(YseDspProcessor* p, const float* in,
                                                         unsigned int length);
/** Takes in a run of a spectral frame's bins, already transformed: `count`
   real and imaginary parts from bin `first_bin`, of a frame of `bins` bins,
   FFT size `size` and hop `hop` (a `first_bin` of 0 starts a frame). The
   engine's stftFrame call. Returns 1, or 0 for a refused handle. NULL parts
   are taken as a run with no bins. */
YSE_C_API int yse_dsp_spectrum_analyzer_process_bins(YseDspProcessor* p, const float* real,
                                                     const float* imaginary, unsigned int first_bin,
                                                     unsigned int count, unsigned int bins,
                                                     unsigned int size, unsigned int hop);
/** Forgets the samples taken in and starts the interval again. */
YSE_C_API void yse_dsp_spectrum_analyzer_reset(YseDspProcessor* p);
/** Either thread: how many frames have finished. */
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_frames(YseDspProcessor* p);
/** Reader thread: takes the newest finished frame (or keeps the one held
   when none is newer), describes it in the optional `frame`, and copies its
   bins to the optional `values` up to `capacity`. Returns the frame's bins,
   0 before the first frame or for a refused handle (then `frame` gets a
   serial of 0 and nothing else is written). */
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_acquire(YseDspProcessor* p,
                                                         YseDspSpectrumFrame* frame, float* values,
                                                         unsigned int capacity);
/** Reader thread: the same for the frame already held. */
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_held(YseDspProcessor* p,
                                                      YseDspSpectrumFrame* frame, float* values,
                                                      unsigned int capacity);

/* ─── plotData: the data and axes of a 2-D plot (#1171) ──────────────────────
   Threads: acquire, held and held_subplot are the reader thread's,
   get_frames, get_plots and get_capacity either thread's, every other call
   the writing (analysing) thread's. Nothing reaches the reader until the
   writing thread calls publish. */

#define YSE_DSP_PLOT_DEFAULT_POINTS 4
#define YSE_DSP_PLOT_MAX_POINTS 4096
#define YSE_DSP_PLOT_MAX_PLOTS 16
#define YSE_DSP_PLOT_MAX_FRAME_SIZE 4194304

/** One axis of a subplot. */
typedef struct YseDspPlotAxis {
  float start;
  float end;
  YseDspPlotScale scale;
} YseDspPlotAxis;

/** A published subplot's axes and filter. */
typedef struct YseDspPlotSubplot {
  YseDspPlotAxis domain; /**< the horizontal span */
  YseDspPlotAxis range; /**< the vertical span */
  float x_origin;
  float y_origin;
  YseDspPlotFilter filter; /**< applied to the published values */
} YseDspPlotSubplot;

/** A published frame's description. */
typedef struct YseDspPlotFrame {
  unsigned int plots; /**< the subplots */
  unsigned int points; /**< the values of each */
  unsigned int serial; /**< the frames published up to this one, from 1 */
} YseDspPlotFrame;

/** A plot of `plots` subplots (1 .. YSE_DSP_PLOT_MAX_PLOTS), each storing up
   to `capacity` values (1 .. YSE_DSP_PLOT_MAX_POINTS). Publishes the first
   frame. */
YSE_C_API YseDspProcessor* yse_dsp_plot_data_create(unsigned int plots, unsigned int capacity);
/** Either thread. */
YSE_C_API unsigned int yse_dsp_plot_data_get_plots(YseDspProcessor* p);
/** Either thread. */
YSE_C_API unsigned int yse_dsp_plot_data_get_capacity(YseDspProcessor* p);
/** The values each subplot shows, clamped to 1 .. the capacity. */
YSE_C_API void yse_dsp_plot_data_set_points(YseDspProcessor* p, unsigned int count);
YSE_C_API unsigned int yse_dsp_plot_data_get_points(YseDspProcessor* p);
/** The samples a signal frame takes in; 0 (the default) is the points. */
YSE_C_API void yse_dsp_plot_data_set_frame_size(YseDspProcessor* p, unsigned int samples);
YSE_C_API unsigned int yse_dsp_plot_data_get_frame_size(YseDspProcessor* p);
/** Frames a second, 0.1 .. 1000. 15; NaN is ignored. */
YSE_C_API void yse_dsp_plot_data_set_frame_rate(YseDspProcessor* p, float per_second);
YSE_C_API float yse_dsp_plot_data_get_frame_rate(YseDspProcessor* p);
/** Writes `count` values into subplot `plot` from point `from` on; a list
   setter, which also returns 0 for a plot past get_plots. */
YSE_C_API int yse_dsp_plot_data_set(YseDspProcessor* p, unsigned int plot, const float* values,
                                    unsigned int count, unsigned int from);
/** Subplot `plot`'s stored value `index`, unfiltered; 0 outside the plot. */
YSE_C_API float yse_dsp_plot_data_get_value(YseDspProcessor* p, unsigned int plot,
                                            unsigned int index);
/** Subplot `plot`'s domain; NaN leaves an end as it is. */
YSE_C_API void yse_dsp_plot_data_set_domain(YseDspProcessor* p, unsigned int plot, float start,
                                            float end, YseDspPlotScale scale);
/** Copies subplot `plot`'s domain (the default outside the plot) to `out`.
   Returns 1, or 0 for a refused handle or a NULL `out`. */
YSE_C_API int yse_dsp_plot_data_get_domain(YseDspProcessor* p, unsigned int plot,
                                           YseDspPlotAxis* out);
YSE_C_API void yse_dsp_plot_data_set_range(YseDspProcessor* p, unsigned int plot, float start,
                                           float end, YseDspPlotScale scale);
YSE_C_API int yse_dsp_plot_data_get_range(YseDspProcessor* p, unsigned int plot,
                                          YseDspPlotAxis* out);
YSE_C_API void yse_dsp_plot_data_set_x_origin(YseDspProcessor* p, unsigned int plot, float value);
YSE_C_API float yse_dsp_plot_data_get_x_origin(YseDspProcessor* p, unsigned int plot);
YSE_C_API void yse_dsp_plot_data_set_y_origin(YseDspProcessor* p, unsigned int plot, float value);
YSE_C_API float yse_dsp_plot_data_get_y_origin(YseDspProcessor* p, unsigned int plot);
YSE_C_API void yse_dsp_plot_data_set_filter(YseDspProcessor* p, unsigned int plot,
                                            YseDspPlotFilter filter);
YSE_C_API YseDspPlotFilter yse_dsp_plot_data_get_filter(YseDspProcessor* p, unsigned int plot);
/** The region the engine's sample-buffer call reads for subplot `plot`
   (kept for when that call has a C surface, #1303). */
YSE_C_API void yse_dsp_plot_data_set_region(YseDspProcessor* p, unsigned int plot,
                                            unsigned int channel, float start_ms, float end_ms);
YSE_C_API unsigned int yse_dsp_plot_data_get_region_channel(YseDspProcessor* p, unsigned int plot);
YSE_C_API float yse_dsp_plot_data_get_region_start(YseDspProcessor* p, unsigned int plot);
YSE_C_API float yse_dsp_plot_data_get_region_end(YseDspProcessor* p, unsigned int plot);
/** Collects `in` into subplot `plot`'s signal frames. Returns `length`, or 0
   for a refused handle, a NULL `in` or a plot past get_plots. */
YSE_C_API unsigned int yse_dsp_plot_data_process(YseDspProcessor* p, unsigned int plot,
                                                 const float* in, unsigned int length);
/** Drops subplot `plot`'s signal frame being collected. */
YSE_C_API void yse_dsp_plot_data_restart(YseDspProcessor* p, unsigned int plot);
/** Publishes the subplots when anything changed since the last publish.
   Returns 1 when it did. */
YSE_C_API int yse_dsp_plot_data_publish(YseDspProcessor* p);
/** Either thread: how many frames have been published. */
YSE_C_API unsigned int yse_dsp_plot_data_get_frames(YseDspProcessor* p);
/** Reader thread: takes the newest published frame (or keeps the one held
   when none is newer) and describes it in the optional `frame`. Returns its
   serial (at least 1: the constructor publishes one), 0 for a refused
   handle. */
YSE_C_API unsigned int yse_dsp_plot_data_acquire(YseDspProcessor* p, YseDspPlotFrame* frame);
/** Reader thread: describes the frame already held. */
YSE_C_API unsigned int yse_dsp_plot_data_held(YseDspProcessor* p, YseDspPlotFrame* frame);
/** Reader thread: subplot `plot` of the frame held, its axes to the optional
   `subplot` and its filtered values to the optional `values` up to
   `capacity`. Returns the frame's points, 0 for a plot past the frame's
   plots or a refused handle (nothing written then). */
YSE_C_API unsigned int yse_dsp_plot_data_held_subplot(YseDspProcessor* p, unsigned int plot,
                                                      YseDspPlotSubplot* subplot, float* values,
                                                      unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
