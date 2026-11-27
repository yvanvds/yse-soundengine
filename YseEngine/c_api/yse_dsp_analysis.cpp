#include "yse_c/yse_dsp_analysis.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/analysis/direction.hpp"
#include "../dsp/analysis/edgeDetector.hpp"
#include "../dsp/analysis/levelMeter.hpp"
#include "../dsp/analysis/loudnessMeter.hpp"
#include "../dsp/analysis/minMax.hpp"
#include "../dsp/analysis/movingAverage.hpp"
#include "../dsp/analysis/peakTracker.hpp"
#include "../dsp/analysis/pitchTracker.hpp"
#include "../dsp/analysis/plotData.hpp"
#include "../dsp/analysis/sampleCapture.hpp"
#include "../dsp/analysis/schmittTrigger.hpp"
#include "../dsp/analysis/scopeCapture.hpp"
#include "../dsp/analysis/spectrumAnalyzer.hpp"
#include "../dsp/analysis/spikeDetector.hpp"
#include "../dsp/analysis/zeroCrossing.hpp"
#include "../dsp/spectral/stft.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::blocks;
  using yse_c::processor::drive;
  using yse_c::processor::emit;
  using yse_c::processor::get;
  using yse_c::processor::handle;
  using yse_c::processor::in_enum;
  using yse_c::processor::make;
  using yse_c::processor::object;
  using yse_c::processor::one_signal;
  using yse_c::processor::query;
  using yse_c::processor::set_list;
  using yse_c::processor::stage;
  using yse_c::processor::three_signals;
  using yse_c::processor::with;

  // ── Bridge-side state ────────────────────────────────────────────────

  // What an analyser reported over the last process call (#1298's design):
  // the engine reports one 128-sample block, a host call spans several.
  struct edgeReports {
    unsigned rises = 0;
    unsigned falls = 0;
  };
  struct countReports {
    unsigned count = 0;
  };

  // levelMeter has no reader path of its own (the patcher's ~levelmeter
  // copies its readings into atomics after every block). The bridge gives it
  // the triple buffer scopeCapture, spectrumAnalyzer and plotData use: three
  // readings, the analysing thread writes one, the reader holds one and the
  // third is the newest. Publishing and reading are each one atomic
  // exchange, so neither side waits, and the reading a host gets is one
  // snapshot, never fields of two.
  constexpr unsigned FRESH = 4;
  constexpr unsigned INDEX = 3;
  struct levelState {
    unsigned reports = 0;
    YseDspLevelReading slots[3]{};
    // The newest slot, | FRESH while no reader has taken it.
    std::atomic<unsigned> published{1};
    unsigned writeSlot = 0; // the analysing thread's
    unsigned readSlot = 2; // the reader's
  };

  // Clear what a process call gathered: at the start of the next call, and
  // with the class's reset.
  void clear_reports(edgeReports& s) noexcept {
    s = edgeReports{};
  }
  void clear_reports(countReports& s) noexcept {
    s = countReports{};
  }
  void clear_reports(levelState& s) noexcept {
    s.reports = 0;
  }

  // One wrapper type per class; the count is how many host arrays a process
  // call stages. pitchTracker takes a raw pointer, so it stages nothing; a
  // loudness meter stages every channel it can measure.
  using Direction = handle<DSP::direction, 1>;
  using EdgeDetector = handle<DSP::edgeDetector, 1, edgeReports>;
  using SchmittTrigger = handle<DSP::schmittTrigger, 3>;
  using SpikeDetector = handle<DSP::spikeDetector, 1, countReports>;
  using ZeroCrossing = handle<DSP::zeroCrossing, 1, countReports>;
  using PeakTracker = handle<DSP::peakTracker, 1, countReports>;
  using MovingAverage = handle<DSP::movingAverage, 1>;
  using MinMax = handle<DSP::minMax, 2>;
  using LoudnessMeter = handle<DSP::loudnessMeter, DSP::loudnessMeter::MAX_CHANNELS, countReports>;
  using PitchTracker = handle<DSP::pitchTracker, 0, countReports>;
  using SampleCapture = handle<DSP::sampleCapture, 1>;
  using LevelMeter = handle<DSP::levelMeter, 1, levelState>;
  using ScopeCapture = handle<DSP::scopeCapture, 2>;
  using SpectrumAnalyzer = handle<DSP::spectrumAnalyzer, 1>;
  using PlotData = handle<DSP::plotData, 1>;

  // The constants the header spells as macros.
  static_assert(YSE_DSP_MOVING_AVERAGE_DEFAULT_CAPACITY == DSP::movingAverage::DEFAULT_CAPACITY,
                "yse_dsp_analysis.h: YSE_DSP_MOVING_AVERAGE_DEFAULT_CAPACITY drifted");
  static_assert(YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY == DSP::movingAverage::MAXIMUM_CAPACITY,
                "yse_dsp_analysis.h: YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY drifted");
  static_assert(YSE_DSP_LOUDNESS_MAX_CHANNELS == DSP::loudnessMeter::MAX_CHANNELS,
                "yse_dsp_analysis.h: YSE_DSP_LOUDNESS_MAX_CHANNELS drifted");
  static_assert(YSE_DSP_LOUDNESS_SILENCE == DSP::loudnessMeter::SILENCE,
                "yse_dsp_analysis.h: YSE_DSP_LOUDNESS_SILENCE drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_MIN_SIZE == DSP::pitchTracker::MIN_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_MIN_SIZE drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_MAX_SIZE == DSP::pitchTracker::MAX_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_MAX_SIZE drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_MIN_PERIOD == DSP::pitchTracker::MIN_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_MIN_PERIOD drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_MAX_PERIOD == DSP::pitchTracker::MAX_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_MAX_PERIOD drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_DEFAULT_SIZE == DSP::pitchTracker::DEFAULT_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_DEFAULT_SIZE drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_DEFAULT_PERIOD == DSP::pitchTracker::DEFAULT_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_DEFAULT_PERIOD drifted");
  static_assert(YSE_DSP_PITCH_TRACKER_DEFAULT_ONSET_PERIOD ==
                    DSP::pitchTracker::DEFAULT_ONSET_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_PITCH_TRACKER_DEFAULT_ONSET_PERIOD drifted");
  static_assert(YSE_DSP_SAMPLE_CAPTURE_DEFAULT_CAPACITY == DSP::sampleCapture::DEFAULT_CAPACITY,
                "yse_dsp_analysis.h: YSE_DSP_SAMPLE_CAPTURE_DEFAULT_CAPACITY drifted");
  static_assert(YSE_DSP_SAMPLE_CAPTURE_MAX_CAPACITY == DSP::sampleCapture::MAXIMUM_CAPACITY,
                "yse_dsp_analysis.h: YSE_DSP_SAMPLE_CAPTURE_MAX_CAPACITY drifted");
  static_assert(YSE_DSP_SAMPLE_CAPTURE_MAX_INDICES == DSP::sampleCapture::MAX_INDICES,
                "yse_dsp_analysis.h: YSE_DSP_SAMPLE_CAPTURE_MAX_INDICES drifted");
  static_assert(YSE_DSP_LEVEL_METER_SILENCE == DSP::levelMeter::SILENCE,
                "yse_dsp_analysis.h: YSE_DSP_LEVEL_METER_SILENCE drifted");
  static_assert(YSE_DSP_SCOPE_DEFAULT_SIZE == DSP::scopeCapture::DEFAULT_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_DEFAULT_SIZE drifted");
  static_assert(YSE_DSP_SCOPE_DEFAULT_PERIOD == DSP::scopeCapture::DEFAULT_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_DEFAULT_PERIOD drifted");
  static_assert(YSE_DSP_SCOPE_MIN_SIZE == DSP::scopeCapture::MIN_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_MIN_SIZE drifted");
  static_assert(YSE_DSP_SCOPE_MAX_SIZE == DSP::scopeCapture::MAX_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_MAX_SIZE drifted");
  static_assert(YSE_DSP_SCOPE_MIN_PERIOD == DSP::scopeCapture::MIN_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_MIN_PERIOD drifted");
  static_assert(YSE_DSP_SCOPE_MAX_PERIOD == DSP::scopeCapture::MAX_PERIOD,
                "yse_dsp_analysis.h: YSE_DSP_SCOPE_MAX_PERIOD drifted");
  static_assert(YSE_DSP_SPECTRUM_DEFAULT_SIZE == DSP::spectrumAnalyzer::DEFAULT_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SPECTRUM_DEFAULT_SIZE drifted");
  static_assert(YSE_DSP_SPECTRUM_MIN_SIZE == DSP::spectrumAnalyzer::MIN_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SPECTRUM_MIN_SIZE drifted");
  static_assert(YSE_DSP_SPECTRUM_MAX_SIZE == DSP::spectrumAnalyzer::MAX_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_SPECTRUM_MAX_SIZE drifted");
  static_assert(YSE_DSP_PLOT_DEFAULT_POINTS == DSP::plotData::DEFAULT_POINTS,
                "yse_dsp_analysis.h: YSE_DSP_PLOT_DEFAULT_POINTS drifted");
  static_assert(YSE_DSP_PLOT_MAX_POINTS == DSP::plotData::MAX_POINTS,
                "yse_dsp_analysis.h: YSE_DSP_PLOT_MAX_POINTS drifted");
  static_assert(YSE_DSP_PLOT_MAX_PLOTS == DSP::plotData::MAX_PLOTS,
                "yse_dsp_analysis.h: YSE_DSP_PLOT_MAX_PLOTS drifted");
  static_assert(YSE_DSP_PLOT_MAX_FRAME_SIZE == DSP::plotData::MAX_FRAME_SIZE,
                "yse_dsp_analysis.h: YSE_DSP_PLOT_MAX_FRAME_SIZE drifted");
  // Positions and indices cross as unsigned int, the engine's UInt.
  static_assert(std::is_same<unsigned int, UInt>::value,
                "yse_dsp_analysis.h: a position must be the engine's UInt");
  static_assert(sizeof(unsigned long long) >= sizeof(std::uint64_t),
                "yse_dsp_analysis.h: a block count must fit an unsigned long long");

  // ── Helpers ──────────────────────────────────────────────────────────

  // An optional host array at `offset`; NULL stays NULL.
  template <typename T> T* at(T* array, unsigned offset) noexcept {
    return array != nullptr ? array + offset : nullptr;
  }

  // Append one report to an optional host array of `capacity` entries: the
  // count always grows, the array only while it has room.
  template <typename T>
  void record(unsigned& count, T* array, unsigned capacity, T value) noexcept {
    const unsigned slot = count++;
    if (array != nullptr && slot < capacity) array[slot] = value;
  }

  // One sample through the handle's process(), 0 if `p` is not an H.
  template <typename H> float one(YseDspProcessor* p, float in) {
    auto* o = object<H>(p);
    return o != nullptr ? static_cast<float>(o->process(in)) : 0.f;
  }

  // A one-in, one-out block at the current settings.
  template <typename H>
  unsigned int plain(YseDspProcessor* p, const float* in, float* out, unsigned int length) {
    return one_signal<H>(p, in, out, length,
                         [](H& h, DSP::buffer& a) -> DSP::buffer& { return h.object(a); });
  }

  // A block that writes no samples: `run(h, staged, offset)` per block.
  // Clears the bridge-side reports first, so they cover this call.
  template <typename H, typename F>
  unsigned int take_in(YseDspProcessor* p, const float* in, unsigned int length, F&& run) {
    if (in == nullptr) return 0;
    if (H* h = get<H>(p)) clear_reports(h->state);
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      run(h, stage(h.scratch[0], in + o, n), o);
    });
  }

  // A class's reset, which also clears the reports gathered over a call.
  template <typename H, typename F> void reset_with(YseDspProcessor* p, F&& engine_reset) {
    if (H* h = get<H>(p)) {
      engine_reset(h->object);
      clear_reports(h->state);
    }
  }

  // ── levelMeter's reading ─────────────────────────────────────────────

  YseDspLevelReading reading(const DSP::levelMeter& m) noexcept {
    YseDspLevelReading r{};
    r.level = m.level();
    r.level_db = m.levelDb();
    r.peak_hold = m.peakHold();
    r.peak_hold_db = m.peakHoldDb();
    r.clipped = m.clipped() ? 1 : 0;
    return r;
  }

  // The analysing thread: write the reading into the write slot and hand it
  // over, taking back whichever slot the reader let go of.
  void publish(LevelMeter& h) noexcept {
    levelState& s = h.state;
    s.slots[s.writeSlot] = reading(h.object);
    s.writeSlot = s.published.exchange(s.writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
  }

  // A level-meter call that can change the reading, then publishes it.
  template <typename F> void change(YseDspProcessor* p, F&& f) {
    if (LevelMeter* h = get<LevelMeter>(p)) {
      f(h->object);
      publish(*h);
    }
  }

  // ── The reader side of the three published classes ───────────────────

  unsigned copy_trace(const DSP::scopeCapture::trace& t, YseDspScopeTrace* info, float* first,
                      float* second, unsigned capacity) noexcept {
    if (info != nullptr) {
      info->mode = static_cast<YseDspScopeMode>(t.mode);
      info->size = t.size;
      info->serial = t.serial;
    }
    const unsigned n = std::min(t.size, capacity);
    if (first != nullptr && t.first != nullptr) std::memcpy(first, t.first, sizeof(float) * n);
    if (second != nullptr && t.second != nullptr) std::memcpy(second, t.second, sizeof(float) * n);
    return t.size;
  }

  unsigned copy_frame(const DSP::spectrumAnalyzer::frame& f, YseDspSpectrumFrame* info,
                      float* values, unsigned capacity) noexcept {
    if (info != nullptr) {
      info->mode = static_cast<YseDspSpectrumMode>(f.mode);
      info->size = f.size;
      info->bins = f.bins;
      info->serial = f.serial;
      info->rate = f.rate;
    }
    const unsigned n = std::min(f.bins, capacity);
    if (values != nullptr && f.values != nullptr) std::memcpy(values, f.values, sizeof(float) * n);
    return f.bins;
  }

  unsigned describe(const DSP::plotData::frame& f, YseDspPlotFrame* info) noexcept {
    if (info != nullptr) {
      info->plots = f.plots;
      info->points = f.points;
      info->serial = f.serial;
    }
    return f.serial;
  }

  YseDspPlotAxis to_axis(const DSP::plotData::axis& a) noexcept {
    YseDspPlotAxis out{};
    out.start = a.start;
    out.end = a.end;
    out.scale = static_cast<YseDspPlotScale>(a.scale);
    return out;
  }
} // namespace

extern "C" {

// ─── direction ──────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_direction_create(void) {
  return make<Direction>("yse_dsp_direction_create");
}
YSE_C_API float yse_dsp_direction_process_sample(YseDspProcessor* p, float in) {
  return one<Direction>(p, in);
}
YSE_C_API unsigned int yse_dsp_direction_process(YseDspProcessor* p, const float* in, float* out,
                                                 unsigned int length) {
  return plain<Direction>(p, in, out, length);
}
YSE_C_API void yse_dsp_direction_set(YseDspProcessor* p, float value) {
  with<Direction>(p, [&](DSP::direction& f) { f.set(value); });
}
YSE_C_API void yse_dsp_direction_reset(YseDspProcessor* p) {
  with<Direction>(p, [](DSP::direction& f) { f.reset(); });
}
YSE_C_API float yse_dsp_direction_get_previous(YseDspProcessor* p) {
  return query<Direction>(p, 0.f, [](DSP::direction& f) { return f.previous(); });
}

// ─── edgeDetector ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_edge_detector_create(void) {
  return make<EdgeDetector>("yse_dsp_edge_detector_create");
}
YSE_C_API int yse_dsp_edge_detector_process_sample(YseDspProcessor* p, float in) {
  return query<EdgeDetector>(p, 0, [&](DSP::edgeDetector& f) { return f.process(in); });
}
YSE_C_API unsigned int
yse_dsp_edge_detector_process(YseDspProcessor* p, const float* in, unsigned int* rises,
                              unsigned int rise_capacity, unsigned int* falls,
                              unsigned int fall_capacity, unsigned int length) {
  return take_in<EdgeDetector>(p, in, length, [&](EdgeDetector& h, DSP::buffer& a, unsigned o) {
    DSP::edgeDetector& f = h.object;
    f(a);
    for (UInt i = 0; i < f.rises(); ++i)
      record(h.state.rises, rises, rise_capacity, o + f.rise(i));
    for (UInt i = 0; i < f.falls(); ++i)
      record(h.state.falls, falls, fall_capacity, o + f.fall(i));
  });
}
YSE_C_API unsigned int yse_dsp_edge_detector_get_rises(YseDspProcessor* p) {
  EdgeDetector* h = get<EdgeDetector>(p);
  return h != nullptr ? h->state.rises : 0u;
}
YSE_C_API unsigned int yse_dsp_edge_detector_get_falls(YseDspProcessor* p) {
  EdgeDetector* h = get<EdgeDetector>(p);
  return h != nullptr ? h->state.falls : 0u;
}
YSE_C_API int yse_dsp_edge_detector_is_high(YseDspProcessor* p) {
  return query<EdgeDetector>(p, 0, [](DSP::edgeDetector& f) { return f.high() ? 1 : 0; });
}
YSE_C_API void yse_dsp_edge_detector_set(YseDspProcessor* p, int non_zero) {
  // The engine's set() clears its block report; the call's reports go too.
  reset_with<EdgeDetector>(p, [&](DSP::edgeDetector& f) { f.set(non_zero != 0); });
}
YSE_C_API void yse_dsp_edge_detector_reset(YseDspProcessor* p) {
  reset_with<EdgeDetector>(p, [](DSP::edgeDetector& f) { f.reset(); });
}

// ─── schmittTrigger ─────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_schmitt_trigger_create(void) {
  return make<SchmittTrigger>("yse_dsp_schmitt_trigger_create");
}
YSE_C_API void yse_dsp_schmitt_trigger_set_low(YseDspProcessor* p, float threshold) {
  with<SchmittTrigger>(p, [&](DSP::schmittTrigger& f) { f.low(threshold); });
}
YSE_C_API float yse_dsp_schmitt_trigger_get_low(YseDspProcessor* p) {
  return query<SchmittTrigger>(p, 0.f, [](DSP::schmittTrigger& f) { return f.low(); });
}
YSE_C_API void yse_dsp_schmitt_trigger_set_high(YseDspProcessor* p, float threshold) {
  with<SchmittTrigger>(p, [&](DSP::schmittTrigger& f) { f.high(threshold); });
}
YSE_C_API float yse_dsp_schmitt_trigger_get_high(YseDspProcessor* p) {
  return query<SchmittTrigger>(p, 0.f, [](DSP::schmittTrigger& f) { return f.high(); });
}
YSE_C_API void yse_dsp_schmitt_trigger_set(YseDspProcessor* p, int on) {
  with<SchmittTrigger>(p, [&](DSP::schmittTrigger& f) { f.set(on != 0); });
}
YSE_C_API void yse_dsp_schmitt_trigger_reset(YseDspProcessor* p) {
  with<SchmittTrigger>(p, [](DSP::schmittTrigger& f) { f.reset(); });
}
YSE_C_API int yse_dsp_schmitt_trigger_is_on(YseDspProcessor* p) {
  return query<SchmittTrigger>(p, 0, [](DSP::schmittTrigger& f) { return f.on() ? 1 : 0; });
}
YSE_C_API float yse_dsp_schmitt_trigger_process_sample(YseDspProcessor* p, float in) {
  return one<SchmittTrigger>(p, in);
}
YSE_C_API unsigned int yse_dsp_schmitt_trigger_process(YseDspProcessor* p, const float* in,
                                                       float* out, unsigned int length) {
  return plain<SchmittTrigger>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_schmitt_trigger_process_signal(YseDspProcessor* p, const float* in,
                                                              const float* low, const float* high,
                                                              float* out, unsigned int length) {
  return three_signals<SchmittTrigger>(
      p, in, low, high, out, length,
      [](SchmittTrigger& h, DSP::buffer& a, DSP::buffer& lo, DSP::buffer& hi) -> DSP::buffer& {
        return h.object(a, lo, hi);
      });
}

// ─── spikeDetector ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_spike_detector_create(void) {
  return make<SpikeDetector>("yse_dsp_spike_detector_create");
}
YSE_C_API int yse_dsp_spike_detector_process_sample(YseDspProcessor* p, float in) {
  return query<SpikeDetector>(p, 0, [&](DSP::spikeDetector& f) { return f.process(in) ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_spike_detector_process(YseDspProcessor* p, const float* in,
                                                      unsigned int* positions, float* intervals,
                                                      unsigned int capacity, unsigned int length) {
  return take_in<SpikeDetector>(p, in, length, [&](SpikeDetector& h, DSP::buffer& a, unsigned o) {
    DSP::spikeDetector& f = h.object;
    f(a);
    for (UInt i = 0; i < f.spikes(); ++i) {
      const unsigned slot = h.state.count;
      record(h.state.count, positions, capacity, o + f.spike(i));
      if (intervals != nullptr && slot < capacity) intervals[slot] = f.interval(i);
    }
  });
}
YSE_C_API unsigned int yse_dsp_spike_detector_get_spikes(YseDspProcessor* p) {
  SpikeDetector* h = get<SpikeDetector>(p);
  return h != nullptr ? h->state.count : 0u;
}
YSE_C_API float yse_dsp_spike_detector_get_last_interval(YseDspProcessor* p) {
  return query<SpikeDetector>(p, 0.f, [](DSP::spikeDetector& f) { return f.lastInterval(); });
}
YSE_C_API void yse_dsp_spike_detector_set_refractory(YseDspProcessor* p, float ms) {
  with<SpikeDetector>(p, [&](DSP::spikeDetector& f) { f.refractory(ms); });
}
YSE_C_API float yse_dsp_spike_detector_get_refractory(YseDspProcessor* p) {
  return query<SpikeDetector>(p, 0.f, [](DSP::spikeDetector& f) { return f.refractory(); });
}
YSE_C_API void yse_dsp_spike_detector_restart(YseDspProcessor* p) {
  with<SpikeDetector>(p, [](DSP::spikeDetector& f) { f.restart(); });
}
YSE_C_API void yse_dsp_spike_detector_reset(YseDspProcessor* p) {
  reset_with<SpikeDetector>(p, [](DSP::spikeDetector& f) { f.reset(); });
}

// ─── zeroCrossing ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_zero_crossing_create(void) {
  return make<ZeroCrossing>("yse_dsp_zero_crossing_create");
}
YSE_C_API int yse_dsp_zero_crossing_process_sample(YseDspProcessor* p, float in) {
  return query<ZeroCrossing>(p, 0, [&](DSP::zeroCrossing& f) { return f.process(in) ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_zero_crossing_process(YseDspProcessor* p, const float* in,
                                                     float* counts, float* clicks,
                                                     unsigned int* positions, unsigned int capacity,
                                                     unsigned int length) {
  return take_in<ZeroCrossing>(p, in, length, [&](ZeroCrossing& h, DSP::buffer& a, unsigned o) {
    DSP::zeroCrossing& f = h.object;
    f(a);
    const unsigned n = a.getLength();
    emit(f.counts(), at(counts, o), n);
    emit(f.clicks(), at(clicks, o), n);
    for (UInt i = 0; i < f.crossings(); ++i)
      record(h.state.count, positions, capacity, o + f.crossing(i));
  });
}
YSE_C_API unsigned int yse_dsp_zero_crossing_get_crossings(YseDspProcessor* p) {
  ZeroCrossing* h = get<ZeroCrossing>(p);
  return h != nullptr ? h->state.count : 0u;
}
YSE_C_API void yse_dsp_zero_crossing_set_volume(YseDspProcessor* p, float value) {
  with<ZeroCrossing>(p, [&](DSP::zeroCrossing& f) { f.volume(value); });
}
YSE_C_API float yse_dsp_zero_crossing_get_volume(YseDspProcessor* p) {
  return query<ZeroCrossing>(p, 0.f, [](DSP::zeroCrossing& f) { return f.volume(); });
}
YSE_C_API void yse_dsp_zero_crossing_reset(YseDspProcessor* p) {
  reset_with<ZeroCrossing>(p, [](DSP::zeroCrossing& f) { f.reset(); });
}

// ─── peakTracker ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_peak_tracker_create(void) {
  return make<PeakTracker>("yse_dsp_peak_tracker_create");
}
YSE_C_API int yse_dsp_peak_tracker_process_sample(YseDspProcessor* p, float in) {
  return query<PeakTracker>(p, 0, [&](DSP::peakTracker& f) { return f.process(in) ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_peak_tracker_process(YseDspProcessor* p, const float* in,
                                                    unsigned int* positions, float* values,
                                                    unsigned int capacity, unsigned int length) {
  return take_in<PeakTracker>(p, in, length, [&](PeakTracker& h, DSP::buffer& a, unsigned o) {
    DSP::peakTracker& f = h.object;
    f(a);
    for (UInt i = 0; i < f.reports(); ++i) {
      const unsigned slot = h.state.count;
      record(h.state.count, positions, capacity, o + f.report(i));
      if (values != nullptr && slot < capacity) values[slot] = f.value(i);
    }
  });
}
YSE_C_API unsigned int yse_dsp_peak_tracker_get_reports(YseDspProcessor* p) {
  PeakTracker* h = get<PeakTracker>(p);
  return h != nullptr ? h->state.count : 0u;
}
YSE_C_API float yse_dsp_peak_tracker_get_last_report(YseDspProcessor* p) {
  return query<PeakTracker>(p, 0.f, [](DSP::peakTracker& f) { return f.lastReport(); });
}
YSE_C_API float yse_dsp_peak_tracker_get_peak(YseDspProcessor* p) {
  return query<PeakTracker>(p, 0.f, [](DSP::peakTracker& f) { return f.peak(); });
}
YSE_C_API float yse_dsp_peak_tracker_take(YseDspProcessor* p) {
  return query<PeakTracker>(p, 0.f, [](DSP::peakTracker& f) { return f.take(); });
}
YSE_C_API void yse_dsp_peak_tracker_set_interval(YseDspProcessor* p, float ms) {
  with<PeakTracker>(p, [&](DSP::peakTracker& f) { f.interval(ms); });
}
YSE_C_API float yse_dsp_peak_tracker_get_interval(YseDspProcessor* p) {
  return query<PeakTracker>(p, 0.f, [](DSP::peakTracker& f) { return f.interval(); });
}
YSE_C_API void yse_dsp_peak_tracker_set_signed(YseDspProcessor* p, int on) {
  with<PeakTracker>(p, [&](DSP::peakTracker& f) { f.signedPeak(on != 0); });
}
YSE_C_API int yse_dsp_peak_tracker_is_signed(YseDspProcessor* p) {
  return query<PeakTracker>(p, 0, [](DSP::peakTracker& f) { return f.signedPeak() ? 1 : 0; });
}
YSE_C_API void yse_dsp_peak_tracker_reset(YseDspProcessor* p) {
  reset_with<PeakTracker>(p, [](DSP::peakTracker& f) { f.reset(); });
}

// ─── movingAverage ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_moving_average_create(unsigned int capacity) {
  return make<MovingAverage>("yse_dsp_moving_average_create", capacity);
}
YSE_C_API float yse_dsp_moving_average_process_sample(YseDspProcessor* p, float in) {
  return one<MovingAverage>(p, in);
}
YSE_C_API unsigned int yse_dsp_moving_average_process(YseDspProcessor* p, const float* in,
                                                      float* out, unsigned int length) {
  return plain<MovingAverage>(p, in, out, length);
}
YSE_C_API float yse_dsp_moving_average_get_value(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0.f, [](DSP::movingAverage& f) { return f.value(); });
}
YSE_C_API void yse_dsp_moving_average_set_length(YseDspProcessor* p, unsigned int samples) {
  with<MovingAverage>(p, [&](DSP::movingAverage& f) { f.length(samples); });
}
YSE_C_API unsigned int yse_dsp_moving_average_get_length(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0u, [](DSP::movingAverage& f) { return f.length(); });
}
YSE_C_API unsigned int yse_dsp_moving_average_get_capacity(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0u, [](DSP::movingAverage& f) { return f.capacity(); });
}
YSE_C_API void yse_dsp_moving_average_set_mode(YseDspProcessor* p, YseDspAverageMode mode) {
  if (!in_enum(mode, YSE_DSP_AVERAGE_RMS)) return;
  with<MovingAverage>(
      p, [&](DSP::movingAverage& f) { f.mode(static_cast<DSP::movingAverage::MODE>(mode)); });
}
YSE_C_API YseDspAverageMode yse_dsp_moving_average_get_mode(YseDspProcessor* p) {
  return query<MovingAverage>(p, YSE_DSP_AVERAGE_BIPOLAR, [](DSP::movingAverage& f) {
    return static_cast<YseDspAverageMode>(f.mode());
  });
}
YSE_C_API unsigned int yse_dsp_moving_average_accumulate(YseDspProcessor* p, const float* in,
                                                         unsigned int length) {
  if (in == nullptr) return 0;
  return drive<MovingAverage>(p, length, [&](MovingAverage& h, unsigned o, unsigned n) {
    h.object.accumulate(stage(h.scratch[0], in + o, n));
  });
}
YSE_C_API float yse_dsp_moving_average_get_accumulated(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0.f, [](DSP::movingAverage& f) { return f.accumulated(); });
}
YSE_C_API double yse_dsp_moving_average_get_accumulated_samples(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0.0, [](DSP::movingAverage& f) { return f.accumulatedSamples(); });
}
YSE_C_API float yse_dsp_moving_average_take(YseDspProcessor* p) {
  return query<MovingAverage>(p, 0.f, [](DSP::movingAverage& f) { return f.take(); });
}
YSE_C_API void yse_dsp_moving_average_reset(YseDspProcessor* p) {
  with<MovingAverage>(p, [](DSP::movingAverage& f) { f.reset(); });
}

// ─── minMax ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_min_max_create(void) {
  return make<MinMax>("yse_dsp_min_max_create");
}
YSE_C_API void yse_dsp_min_max_process_sample(YseDspProcessor* p, float in) {
  with<MinMax>(p, [&](DSP::minMax& f) { f.process(in); });
}
YSE_C_API void yse_dsp_min_max_process_sample_reset(YseDspProcessor* p, float in, float reset) {
  with<MinMax>(p, [&](DSP::minMax& f) { f.process(in, reset); });
}
YSE_C_API unsigned int yse_dsp_min_max_process(YseDspProcessor* p, const float* in, float* minimum,
                                               float* maximum, unsigned int length) {
  if (in == nullptr) return 0;
  return drive<MinMax>(p, length, [&](MinMax& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
    emit(h.object.minimumSignal(), at(minimum, o), n);
    emit(h.object.maximumSignal(), at(maximum, o), n);
  });
}
YSE_C_API unsigned int yse_dsp_min_max_process_reset(YseDspProcessor* p, const float* in,
                                                     const float* reset, float* minimum,
                                                     float* maximum, unsigned int length) {
  if (in == nullptr || reset == nullptr) return 0;
  return drive<MinMax>(p, length, [&](MinMax& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], reset + o, n));
    emit(h.object.minimumSignal(), at(minimum, o), n);
    emit(h.object.maximumSignal(), at(maximum, o), n);
  });
}
YSE_C_API float yse_dsp_min_max_get_minimum(YseDspProcessor* p) {
  return query<MinMax>(p, 0.f, [](DSP::minMax& f) { return f.minimum(); });
}
YSE_C_API float yse_dsp_min_max_get_maximum(YseDspProcessor* p) {
  return query<MinMax>(p, 0.f, [](DSP::minMax& f) { return f.maximum(); });
}
YSE_C_API int yse_dsp_min_max_is_empty(YseDspProcessor* p) {
  return query<MinMax>(p, 0, [](DSP::minMax& f) { return f.empty() ? 1 : 0; });
}
YSE_C_API void yse_dsp_min_max_reset(YseDspProcessor* p) {
  with<MinMax>(p, [](DSP::minMax& f) { f.reset(); });
}

// ─── loudnessMeter ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_loudness_meter_create(unsigned int channels) {
  return make<LoudnessMeter>("yse_dsp_loudness_meter_create", channels);
}
YSE_C_API unsigned int yse_dsp_loudness_meter_get_channels(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0u, [](DSP::loudnessMeter& f) { return f.channels(); });
}
YSE_C_API void yse_dsp_loudness_meter_set_weight(YseDspProcessor* p, unsigned int channel,
                                                 float value) {
  with<LoudnessMeter>(p, [&](DSP::loudnessMeter& f) { f.weight(channel, value); });
}
YSE_C_API float yse_dsp_loudness_meter_get_weight(YseDspProcessor* p, unsigned int channel) {
  return query<LoudnessMeter>(p, 0.f, [&](DSP::loudnessMeter& f) { return f.weight(channel); });
}
YSE_C_API unsigned int yse_dsp_loudness_meter_process(YseDspProcessor* p, const float* in,
                                                      unsigned int length) {
  return take_in<LoudnessMeter>(p, in, length, [](LoudnessMeter& h, DSP::buffer& a, unsigned) {
    h.state.count += h.object(a);
  });
}
YSE_C_API unsigned int yse_dsp_loudness_meter_process_channels(YseDspProcessor* p,
                                                               const float* const* channels,
                                                               unsigned int count,
                                                               unsigned int length) {
  if (channels == nullptr) return 0;
  LoudnessMeter* h = get<LoudnessMeter>(p);
  if (h == nullptr) return 0;
  clear_reports(h->state);
  // Every channel the meter measures is staged: a NULL entry, or one past
  // `count`, as silence. The engine takes its block length from channel 0,
  // so channel 0 must be there even when the host left it out.
  const unsigned measured = h->object.channels();
  std::array<DSP::buffer*, DSP::loudnessMeter::MAX_CHANNELS> in{};
  blocks(length, [&](unsigned o, unsigned n) {
    for (unsigned c = 0; c < measured; ++c) {
      DSP::buffer& b = h->scratch[c];
      const float* src = c < count ? channels[c] : nullptr;
      if (src != nullptr) {
        stage(b, src + o, n);
      } else {
        b.resize(n);
        std::memset(b.getPtr(), 0, sizeof(float) * n);
      }
      in[c] = &b;
    }
    h->state.count += h->object(in.data(), measured);
  });
  return length;
}
YSE_C_API unsigned int yse_dsp_loudness_meter_get_reports(YseDspProcessor* p) {
  LoudnessMeter* h = get<LoudnessMeter>(p);
  return h != nullptr ? h->state.count : 0u;
}
YSE_C_API float yse_dsp_loudness_meter_get_momentary(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.momentary(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_short_term(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.shortTerm(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_max_momentary(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.maxMomentary(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_max_short_term(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.maxShortTerm(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_integrated(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.integrated(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_loudness_range(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.loudnessRange(); });
}
YSE_C_API float yse_dsp_loudness_meter_get_peak(YseDspProcessor* p, unsigned int channel) {
  return query<LoudnessMeter>(p, 0.f, [&](DSP::loudnessMeter& f) { return f.peak(channel); });
}
YSE_C_API float yse_dsp_loudness_meter_get_true_peak(YseDspProcessor* p, unsigned int channel) {
  return query<LoudnessMeter>(p, 0.f, [&](DSP::loudnessMeter& f) { return f.truePeak(channel); });
}
YSE_C_API unsigned long long yse_dsp_loudness_meter_get_gated_blocks(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0ull, [](DSP::loudnessMeter& f) { return f.gatedBlocks(); });
}
YSE_C_API void yse_dsp_loudness_meter_set_interval(YseDspProcessor* p, float ms) {
  with<LoudnessMeter>(p, [&](DSP::loudnessMeter& f) { f.interval(ms); });
}
YSE_C_API float yse_dsp_loudness_meter_get_interval(YseDspProcessor* p) {
  return query<LoudnessMeter>(p, 0.f, [](DSP::loudnessMeter& f) { return f.interval(); });
}
YSE_C_API void yse_dsp_loudness_meter_reset(YseDspProcessor* p) {
  reset_with<LoudnessMeter>(p, [](DSP::loudnessMeter& f) { f.reset(); });
}
YSE_C_API int yse_dsp_loudness_meter_shelf(double sample_rate, double* out) {
  if (out == nullptr) return 0;
  const DSP::loudnessMeter::section s = DSP::loudnessMeter::Shelf(sample_rate);
  out[0] = s.b0;
  out[1] = s.b1;
  out[2] = s.b2;
  out[3] = s.a1;
  out[4] = s.a2;
  return 1;
}
YSE_C_API int yse_dsp_loudness_meter_high_pass(double sample_rate, double* out) {
  if (out == nullptr) return 0;
  const DSP::loudnessMeter::section s = DSP::loudnessMeter::HighPass(sample_rate);
  out[0] = s.b0;
  out[1] = s.b1;
  out[2] = s.b2;
  out[3] = s.a1;
  out[4] = s.a2;
  return 1;
}

// ─── pitchTracker ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_pitch_tracker_create(void) {
  return make<PitchTracker>("yse_dsp_pitch_tracker_create");
}
YSE_C_API void yse_dsp_pitch_tracker_set_size(YseDspProcessor* p, unsigned int samples) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.size(samples); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_size(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.size(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_period(YseDspProcessor* p, unsigned int samples) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.period(samples); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_period(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.period(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_min_frequency(YseDspProcessor* p, float hz) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.minFrequency(hz); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_min_frequency(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.minFrequency(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_max_frequency(YseDspProcessor* p, float hz) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.maxFrequency(hz); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_max_frequency(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.maxFrequency(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_threshold(YseDspProcessor* p, float amplitude) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.threshold(amplitude); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_threshold(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.threshold(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_tolerance(YseDspProcessor* p, float value) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.tolerance(value); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_tolerance(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.tolerance(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_onset_amplitude(YseDspProcessor* p, float amplitude) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.onsetAmplitude(amplitude); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_onset_amplitude(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.onsetAmplitude(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_onset_pitch(YseDspProcessor* p, float semitones) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.onsetPitch(semitones); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_onset_pitch(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.onsetPitch(); });
}
YSE_C_API void yse_dsp_pitch_tracker_set_onset_period(YseDspProcessor* p, unsigned int samples) {
  with<PitchTracker>(p, [&](DSP::pitchTracker& f) { f.onsetPeriod(samples); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_onset_period(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.onsetPeriod(); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_process(YseDspProcessor* p, const float* in,
                                                     unsigned int length) {
  // The engine takes a raw pointer of any length, and its counts already
  // cover the whole call, so there is nothing to stage or gather but the
  // analyses its call returns.
  if (in == nullptr) return 0;
  PitchTracker* h = get<PitchTracker>(p);
  if (h == nullptr) return 0;
  h->state.count = h->object(in, length);
  return length;
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_analyses(YseDspProcessor* p) {
  PitchTracker* h = get<PitchTracker>(p);
  return h != nullptr ? h->state.count : 0u;
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_reports(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.reports(); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_pitches(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.pitches(); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_onsets(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.onsets(); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_frequency(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.frequency(); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_clarity(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.clarity(); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_amplitude(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.amplitude(); });
}
YSE_C_API int yse_dsp_pitch_tracker_is_voiced(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0, [](DSP::pitchTracker& f) { return f.voiced() ? 1 : 0; });
}
YSE_C_API float yse_dsp_pitch_tracker_get_onset_frequency(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.onsetFrequency(); });
}
YSE_C_API float yse_dsp_pitch_tracker_get_onset_peak(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0.f, [](DSP::pitchTracker& f) { return f.onsetPeak(); });
}
YSE_C_API unsigned int yse_dsp_pitch_tracker_get_latency(YseDspProcessor* p) {
  return query<PitchTracker>(p, 0u, [](DSP::pitchTracker& f) { return f.latency(); });
}
YSE_C_API void yse_dsp_pitch_tracker_reset(YseDspProcessor* p) {
  reset_with<PitchTracker>(p, [](DSP::pitchTracker& f) { f.reset(); });
}

// ─── sampleCapture ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_sample_capture_create(unsigned int capacity) {
  return make<SampleCapture>("yse_dsp_sample_capture_create", capacity);
}
YSE_C_API void yse_dsp_sample_capture_set_mode(YseDspProcessor* p, YseDspCaptureMode mode) {
  if (!in_enum(mode, YSE_DSP_CAPTURE_FIRST)) return;
  with<SampleCapture>(
      p, [&](DSP::sampleCapture& f) { f.mode(static_cast<DSP::sampleCapture::MODE>(mode)); });
}
YSE_C_API YseDspCaptureMode yse_dsp_sample_capture_get_mode(YseDspProcessor* p) {
  return query<SampleCapture>(p, YSE_DSP_CAPTURE_LAST, [](DSP::sampleCapture& f) {
    return static_cast<YseDspCaptureMode>(f.mode());
  });
}
YSE_C_API int yse_dsp_sample_capture_set_indices(YseDspProcessor* p, const unsigned int* list,
                                                 unsigned int count) {
  return set_list<SampleCapture>(p, list, count,
                                 [&](DSP::sampleCapture& f) { f.indices(list, count); });
}
YSE_C_API unsigned int yse_dsp_sample_capture_get_indices(YseDspProcessor* p, unsigned int* out,
                                                          unsigned int capacity) {
  DSP::sampleCapture* f = object<SampleCapture>(p);
  if (f == nullptr) return 0;
  const unsigned n = f->indexCount();
  if (out != nullptr) {
    for (unsigned i = 0; i < n && i < capacity; ++i)
      out[i] = f->index(i);
  }
  return n;
}
YSE_C_API void yse_dsp_sample_capture_process_sample(YseDspProcessor* p, float in) {
  with<SampleCapture>(p, [&](DSP::sampleCapture& f) { f.process(in); });
}
YSE_C_API unsigned int yse_dsp_sample_capture_process(YseDspProcessor* p, const float* in,
                                                      unsigned int length) {
  if (in == nullptr) return 0;
  return drive<SampleCapture>(p, length, [&](SampleCapture& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
  });
}
YSE_C_API unsigned int yse_dsp_sample_capture_get_size(YseDspProcessor* p) {
  return query<SampleCapture>(p, 0u, [](DSP::sampleCapture& f) { return f.size(); });
}
YSE_C_API unsigned int yse_dsp_sample_capture_get_capacity(YseDspProcessor* p) {
  return query<SampleCapture>(p, 0u, [](DSP::sampleCapture& f) { return f.capacity(); });
}
YSE_C_API int yse_dsp_sample_capture_is_full(YseDspProcessor* p) {
  return query<SampleCapture>(p, 0, [](DSP::sampleCapture& f) { return f.full() ? 1 : 0; });
}
YSE_C_API float yse_dsp_sample_capture_get_sample(YseDspProcessor* p, unsigned int index) {
  return query<SampleCapture>(p, 0.f, [&](DSP::sampleCapture& f) { return f.sample(index); });
}
YSE_C_API unsigned int yse_dsp_sample_capture_get_samples(YseDspProcessor* p, float* out,
                                                          unsigned int capacity) {
  DSP::sampleCapture* f = object<SampleCapture>(p);
  if (f == nullptr) return 0;
  if (out != nullptr && capacity > 0) f->copy(out, capacity);
  return f->size();
}
YSE_C_API void yse_dsp_sample_capture_clear(YseDspProcessor* p) {
  with<SampleCapture>(p, [](DSP::sampleCapture& f) { f.clear(); });
}

// ─── levelMeter ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_level_meter_create(void) {
  YseDspProcessor* p = make<LevelMeter>("yse_dsp_level_meter_create");
  // The first reading, in every slot, so a reader that comes before the
  // first publish reads silence rather than zeroes.
  if (LevelMeter* h = get<LevelMeter>(p)) {
    const YseDspLevelReading first = reading(h->object);
    for (YseDspLevelReading& s : h->state.slots)
      s = first;
  }
  return p;
}
YSE_C_API int yse_dsp_level_meter_process_sample(YseDspProcessor* p, float in) {
  LevelMeter* h = get<LevelMeter>(p);
  if (h == nullptr) return 0;
  const bool ended = h->object.process(in);
  publish(*h);
  return ended ? 1 : 0;
}
YSE_C_API unsigned int yse_dsp_level_meter_process(YseDspProcessor* p, const float* in,
                                                   unsigned int length) {
  if (in == nullptr) return 0;
  LevelMeter* h = get<LevelMeter>(p);
  if (h == nullptr) return 0;
  take_in<LevelMeter>(p, in, length, [](LevelMeter& m, DSP::buffer& a, unsigned) {
    m.state.reports += m.object(a);
  });
  publish(*h);
  return length;
}
YSE_C_API unsigned int yse_dsp_level_meter_get_reports(YseDspProcessor* p) {
  LevelMeter* h = get<LevelMeter>(p);
  return h != nullptr ? h->state.reports : 0u;
}
YSE_C_API void yse_dsp_level_meter_set(YseDspProcessor* p, float amplitude) {
  change(p, [&](DSP::levelMeter& f) { f.set(amplitude); });
}
YSE_C_API void yse_dsp_level_meter_set_db(YseDspProcessor* p, float db) {
  change(p, [&](DSP::levelMeter& f) { f.setDb(db); });
}
YSE_C_API float yse_dsp_level_meter_get_level(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.level(); });
}
YSE_C_API float yse_dsp_level_meter_get_level_db(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.levelDb(); });
}
YSE_C_API float yse_dsp_level_meter_get_peak_hold(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.peakHold(); });
}
YSE_C_API float yse_dsp_level_meter_get_peak_hold_db(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.peakHoldDb(); });
}
YSE_C_API int yse_dsp_level_meter_is_clipped(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0, [](DSP::levelMeter& f) { return f.clipped() ? 1 : 0; });
}
YSE_C_API void yse_dsp_level_meter_clear(YseDspProcessor* p) {
  change(p, [](DSP::levelMeter& f) { f.clear(); });
}
YSE_C_API void yse_dsp_level_meter_reset(YseDspProcessor* p) {
  if (LevelMeter* h = get<LevelMeter>(p)) {
    h->object.reset();
    clear_reports(h->state);
    publish(*h);
  }
}
YSE_C_API void yse_dsp_level_meter_set_detector(YseDspProcessor* p, YseDspLevelDetector detector) {
  if (!in_enum(detector, YSE_DSP_LEVEL_RMS)) return;
  with<LevelMeter>(
      p, [&](DSP::levelMeter& f) { f.detector(static_cast<DSP::levelMeter::DETECTOR>(detector)); });
}
YSE_C_API YseDspLevelDetector yse_dsp_level_meter_get_detector(YseDspProcessor* p) {
  return query<LevelMeter>(p, YSE_DSP_LEVEL_PEAK, [](DSP::levelMeter& f) {
    return static_cast<YseDspLevelDetector>(f.detector());
  });
}
YSE_C_API void yse_dsp_level_meter_set_interval(YseDspProcessor* p, float ms) {
  with<LevelMeter>(p, [&](DSP::levelMeter& f) { f.interval(ms); });
}
YSE_C_API float yse_dsp_level_meter_get_interval(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.interval(); });
}
YSE_C_API void yse_dsp_level_meter_set_attack(YseDspProcessor* p, float ms) {
  with<LevelMeter>(p, [&](DSP::levelMeter& f) { f.attack(ms); });
}
YSE_C_API float yse_dsp_level_meter_get_attack(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.attack(); });
}
YSE_C_API void yse_dsp_level_meter_set_release(YseDspProcessor* p, float ms) {
  with<LevelMeter>(p, [&](DSP::levelMeter& f) { f.release(ms); });
}
YSE_C_API float yse_dsp_level_meter_get_release(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.release(); });
}
YSE_C_API void yse_dsp_level_meter_set_hold(YseDspProcessor* p, float ms) {
  with<LevelMeter>(p, [&](DSP::levelMeter& f) { f.hold(ms); });
}
YSE_C_API float yse_dsp_level_meter_get_hold(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.hold(); });
}
YSE_C_API void yse_dsp_level_meter_set_offset(YseDspProcessor* p, float db) {
  change(p, [&](DSP::levelMeter& f) { f.offset(db); });
}
YSE_C_API float yse_dsp_level_meter_get_offset(YseDspProcessor* p) {
  return query<LevelMeter>(p, 0.f, [](DSP::levelMeter& f) { return f.offset(); });
}
YSE_C_API void yse_dsp_level_meter_set_ballistics(YseDspProcessor* p,
                                                  YseDspLevelBallistics standard) {
  if (!in_enum(standard, YSE_DSP_BALLISTICS_EBU)) return;
  change(p, [&](DSP::levelMeter& f) {
    f.ballistics(static_cast<DSP::levelMeter::BALLISTICS>(standard));
  });
}
YSE_C_API int yse_dsp_level_meter_ballistics_settings(YseDspLevelBallistics standard, float* attack,
                                                      float* release, float* offset) {
  if (!in_enum(standard, YSE_DSP_BALLISTICS_EBU)) return 0;
  const DSP::levelMeter::ballisticsSettings s =
      DSP::levelMeter::settings(static_cast<DSP::levelMeter::BALLISTICS>(standard));
  if (attack != nullptr) *attack = s.attack;
  if (release != nullptr) *release = s.release;
  if (offset != nullptr) *offset = s.offset;
  return 1;
}
YSE_C_API float yse_dsp_level_meter_position(float db, float floor_db, float ceiling_db) {
  return DSP::levelMeter::position(db, floor_db, ceiling_db);
}
YSE_C_API int yse_dsp_level_meter_read(YseDspProcessor* p, YseDspLevelReading* out) {
  // The reader thread. It touches only `published`, its own slot index and
  // the slot it holds, which the analysing thread never writes.
  LevelMeter* h = get<LevelMeter>(p);
  if (h == nullptr || out == nullptr) return 0;
  levelState& s = h->state;
  if ((s.published.load(std::memory_order_acquire) & FRESH) != 0)
    s.readSlot = s.published.exchange(s.readSlot, std::memory_order_acq_rel) & INDEX;
  *out = s.slots[s.readSlot];
  return 1;
}

// ─── scopeCapture ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_scope_capture_create(unsigned int capacity) {
  return make<ScopeCapture>("yse_dsp_scope_capture_create", capacity);
}
YSE_C_API void yse_dsp_scope_capture_set_size(YseDspProcessor* p, unsigned int points) {
  with<ScopeCapture>(p, [&](DSP::scopeCapture& f) { f.size(points); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_get_size(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0u, [](DSP::scopeCapture& f) { return f.size(); });
}
YSE_C_API void yse_dsp_scope_capture_set_period(YseDspProcessor* p, unsigned int samples) {
  with<ScopeCapture>(p, [&](DSP::scopeCapture& f) { f.period(samples); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_get_period(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0u, [](DSP::scopeCapture& f) { return f.period(); });
}
YSE_C_API void yse_dsp_scope_capture_set_delay(YseDspProcessor* p, float ms) {
  with<ScopeCapture>(p, [&](DSP::scopeCapture& f) { f.delay(ms); });
}
YSE_C_API float yse_dsp_scope_capture_get_delay(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0.f, [](DSP::scopeCapture& f) { return f.delay(); });
}
YSE_C_API void yse_dsp_scope_capture_set_trigger(YseDspProcessor* p, YseDspScopeTrigger trigger) {
  if (!in_enum(trigger, YSE_DSP_SCOPE_TRIGGER_DOWN)) return;
  with<ScopeCapture>(p, [&](DSP::scopeCapture& f) {
    f.trigger(static_cast<DSP::scopeCapture::TRIGGER>(trigger));
  });
}
YSE_C_API YseDspScopeTrigger yse_dsp_scope_capture_get_trigger(YseDspProcessor* p) {
  return query<ScopeCapture>(p, YSE_DSP_SCOPE_TRIGGER_NONE, [](DSP::scopeCapture& f) {
    return static_cast<YseDspScopeTrigger>(f.trigger());
  });
}
YSE_C_API void yse_dsp_scope_capture_set_level(YseDspProcessor* p, float value) {
  with<ScopeCapture>(p, [&](DSP::scopeCapture& f) { f.level(value); });
}
YSE_C_API float yse_dsp_scope_capture_get_level(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0.f, [](DSP::scopeCapture& f) { return f.level(); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_get_capacity(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0u, [](DSP::scopeCapture& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_process(YseDspProcessor* p, const float* in,
                                                     unsigned int length) {
  if (in == nullptr) return 0;
  return drive<ScopeCapture>(p, length, [&](ScopeCapture& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
  });
}
YSE_C_API unsigned int yse_dsp_scope_capture_process_xy(YseDspProcessor* p, const float* x,
                                                        const float* y, unsigned int length) {
  if (x == nullptr || y == nullptr) return 0;
  return drive<ScopeCapture>(p, length, [&](ScopeCapture& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], x + o, n), stage(h.scratch[1], y + o, n));
  });
}
YSE_C_API void yse_dsp_scope_capture_restart(YseDspProcessor* p) {
  with<ScopeCapture>(p, [](DSP::scopeCapture& f) { f.restart(); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_get_traces(YseDspProcessor* p) {
  return query<ScopeCapture>(p, 0u, [](DSP::scopeCapture& f) { return f.traces(); });
}
YSE_C_API unsigned int yse_dsp_scope_capture_acquire(YseDspProcessor* p, YseDspScopeTrace* trace,
                                                     float* first, float* second,
                                                     unsigned int capacity) {
  DSP::scopeCapture* f = object<ScopeCapture>(p);
  return copy_trace(f != nullptr ? f->acquire() : DSP::scopeCapture::trace{}, trace, first, second,
                    capacity);
}
YSE_C_API unsigned int yse_dsp_scope_capture_held(YseDspProcessor* p, YseDspScopeTrace* trace,
                                                  float* first, float* second,
                                                  unsigned int capacity) {
  DSP::scopeCapture* f = object<ScopeCapture>(p);
  return copy_trace(f != nullptr ? f->held() : DSP::scopeCapture::trace{}, trace, first, second,
                    capacity);
}

// ─── spectrumAnalyzer ───────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_spectrum_analyzer_create(unsigned int capacity) {
  return make<SpectrumAnalyzer>("yse_dsp_spectrum_analyzer_create", capacity);
}
YSE_C_API void yse_dsp_spectrum_analyzer_set_size(YseDspProcessor* p, unsigned int samples) {
  with<SpectrumAnalyzer>(p, [&](DSP::spectrumAnalyzer& f) { f.size(samples); });
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_size(YseDspProcessor* p) {
  return query<SpectrumAnalyzer>(p, 0u, [](DSP::spectrumAnalyzer& f) { return f.size(); });
}
YSE_C_API void yse_dsp_spectrum_analyzer_set_interval(YseDspProcessor* p, float ms) {
  with<SpectrumAnalyzer>(p, [&](DSP::spectrumAnalyzer& f) { f.interval(ms); });
}
YSE_C_API float yse_dsp_spectrum_analyzer_get_interval(YseDspProcessor* p) {
  return query<SpectrumAnalyzer>(p, 0.f, [](DSP::spectrumAnalyzer& f) { return f.interval(); });
}
YSE_C_API void yse_dsp_spectrum_analyzer_set_mode(YseDspProcessor* p, YseDspSpectrumMode mode) {
  if (!in_enum(mode, YSE_DSP_SPECTRUM_PHASE)) return;
  with<SpectrumAnalyzer>(
      p, [&](DSP::spectrumAnalyzer& f) { f.mode(static_cast<DSP::spectrumAnalyzer::MODE>(mode)); });
}
YSE_C_API YseDspSpectrumMode yse_dsp_spectrum_analyzer_get_mode(YseDspProcessor* p) {
  return query<SpectrumAnalyzer>(p, YSE_DSP_SPECTRUM_AMPLITUDE, [](DSP::spectrumAnalyzer& f) {
    return static_cast<YseDspSpectrumMode>(f.mode());
  });
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_capacity(YseDspProcessor* p) {
  return query<SpectrumAnalyzer>(p, 0u, [](DSP::spectrumAnalyzer& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_process(YseDspProcessor* p, const float* in,
                                                         unsigned int length) {
  if (in == nullptr) return 0;
  return drive<SpectrumAnalyzer>(p, length, [&](SpectrumAnalyzer& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
  });
}
YSE_C_API int yse_dsp_spectrum_analyzer_process_bins(YseDspProcessor* p, const float* real,
                                                     const float* imaginary, unsigned int first_bin,
                                                     unsigned int count, unsigned int bins,
                                                     unsigned int size, unsigned int hop) {
  DSP::spectrumAnalyzer* f = object<SpectrumAnalyzer>(p);
  if (f == nullptr) return 0;
  DSP::stftFrame run;
  // stftFrame's parts are non-const because a ~pfft subpatch writes them;
  // the analyser only reads them.
  run.real = const_cast<float*>(real); // NOLINT(cppcoreguidelines-pro-type-const-cast)
  run.imaginary = const_cast<float*>(imaginary); // NOLINT(cppcoreguidelines-pro-type-const-cast)
  run.firstBin = first_bin;
  run.count = count;
  run.bins = bins;
  run.size = size;
  run.hop = hop;
  (*f)(run);
  return 1;
}
YSE_C_API void yse_dsp_spectrum_analyzer_reset(YseDspProcessor* p) {
  with<SpectrumAnalyzer>(p, [](DSP::spectrumAnalyzer& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_get_frames(YseDspProcessor* p) {
  return query<SpectrumAnalyzer>(p, 0u, [](DSP::spectrumAnalyzer& f) { return f.frames(); });
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_acquire(YseDspProcessor* p,
                                                         YseDspSpectrumFrame* frame, float* values,
                                                         unsigned int capacity) {
  DSP::spectrumAnalyzer* f = object<SpectrumAnalyzer>(p);
  return copy_frame(f != nullptr ? f->acquire() : DSP::spectrumAnalyzer::frame{}, frame, values,
                    capacity);
}
YSE_C_API unsigned int yse_dsp_spectrum_analyzer_held(YseDspProcessor* p,
                                                      YseDspSpectrumFrame* frame, float* values,
                                                      unsigned int capacity) {
  DSP::spectrumAnalyzer* f = object<SpectrumAnalyzer>(p);
  return copy_frame(f != nullptr ? f->held() : DSP::spectrumAnalyzer::frame{}, frame, values,
                    capacity);
}

// ─── plotData ───────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_plot_data_create(unsigned int plots, unsigned int capacity) {
  return make<PlotData>("yse_dsp_plot_data_create", plots, capacity);
}
YSE_C_API unsigned int yse_dsp_plot_data_get_plots(YseDspProcessor* p) {
  return query<PlotData>(p, 0u, [](DSP::plotData& f) { return f.plots(); });
}
YSE_C_API unsigned int yse_dsp_plot_data_get_capacity(YseDspProcessor* p) {
  return query<PlotData>(p, 0u, [](DSP::plotData& f) { return f.capacity(); });
}
YSE_C_API void yse_dsp_plot_data_set_points(YseDspProcessor* p, unsigned int count) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.points(count); });
}
YSE_C_API unsigned int yse_dsp_plot_data_get_points(YseDspProcessor* p) {
  return query<PlotData>(p, 0u, [](DSP::plotData& f) { return f.points(); });
}
YSE_C_API void yse_dsp_plot_data_set_frame_size(YseDspProcessor* p, unsigned int samples) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.frameSize(samples); });
}
YSE_C_API unsigned int yse_dsp_plot_data_get_frame_size(YseDspProcessor* p) {
  return query<PlotData>(p, 0u, [](DSP::plotData& f) { return f.frameSize(); });
}
YSE_C_API void yse_dsp_plot_data_set_frame_rate(YseDspProcessor* p, float per_second) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.frameRate(per_second); });
}
YSE_C_API float yse_dsp_plot_data_get_frame_rate(YseDspProcessor* p) {
  return query<PlotData>(p, 0.f, [](DSP::plotData& f) { return f.frameRate(); });
}
YSE_C_API int yse_dsp_plot_data_set(YseDspProcessor* p, unsigned int plot, const float* values,
                                    unsigned int count, unsigned int from) {
  DSP::plotData* f = object<PlotData>(p);
  if (f == nullptr || plot >= f->plots()) return 0;
  return set_list<PlotData>(p, values, count,
                            [&](DSP::plotData& d) { d.set(plot, values, count, from); });
}
YSE_C_API float yse_dsp_plot_data_get_value(YseDspProcessor* p, unsigned int plot,
                                            unsigned int index) {
  return query<PlotData>(p, 0.f, [&](DSP::plotData& f) { return f.value(plot, index); });
}
YSE_C_API void yse_dsp_plot_data_set_domain(YseDspProcessor* p, unsigned int plot, float start,
                                            float end, YseDspPlotScale scale) {
  if (!in_enum(scale, YSE_DSP_PLOT_LOG)) return;
  with<PlotData>(p, [&](DSP::plotData& f) {
    f.domain(plot, start, end, static_cast<DSP::plotData::SCALE>(scale));
  });
}
YSE_C_API int yse_dsp_plot_data_get_domain(YseDspProcessor* p, unsigned int plot,
                                           YseDspPlotAxis* out) {
  DSP::plotData* f = object<PlotData>(p);
  if (f == nullptr || out == nullptr) return 0;
  *out = to_axis(f->domain(plot));
  return 1;
}
YSE_C_API void yse_dsp_plot_data_set_range(YseDspProcessor* p, unsigned int plot, float start,
                                           float end, YseDspPlotScale scale) {
  if (!in_enum(scale, YSE_DSP_PLOT_LOG)) return;
  with<PlotData>(p, [&](DSP::plotData& f) {
    f.range(plot, start, end, static_cast<DSP::plotData::SCALE>(scale));
  });
}
YSE_C_API int yse_dsp_plot_data_get_range(YseDspProcessor* p, unsigned int plot,
                                          YseDspPlotAxis* out) {
  DSP::plotData* f = object<PlotData>(p);
  if (f == nullptr || out == nullptr) return 0;
  *out = to_axis(f->range(plot));
  return 1;
}
YSE_C_API void yse_dsp_plot_data_set_x_origin(YseDspProcessor* p, unsigned int plot, float value) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.xOrigin(plot, value); });
}
YSE_C_API float yse_dsp_plot_data_get_x_origin(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, 0.f, [&](DSP::plotData& f) { return f.xOrigin(plot); });
}
YSE_C_API void yse_dsp_plot_data_set_y_origin(YseDspProcessor* p, unsigned int plot, float value) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.yOrigin(plot, value); });
}
YSE_C_API float yse_dsp_plot_data_get_y_origin(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, 0.f, [&](DSP::plotData& f) { return f.yOrigin(plot); });
}
YSE_C_API void yse_dsp_plot_data_set_filter(YseDspProcessor* p, unsigned int plot,
                                            YseDspPlotFilter filter) {
  if (!in_enum(filter, YSE_DSP_PLOT_FILTER_ATODB)) return;
  with<PlotData>(
      p, [&](DSP::plotData& f) { f.filter(plot, static_cast<DSP::plotData::FILTER>(filter)); });
}
YSE_C_API YseDspPlotFilter yse_dsp_plot_data_get_filter(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, YSE_DSP_PLOT_FILTER_NONE, [&](DSP::plotData& f) {
    return static_cast<YseDspPlotFilter>(f.filter(plot));
  });
}
YSE_C_API void yse_dsp_plot_data_set_region(YseDspProcessor* p, unsigned int plot,
                                            unsigned int channel, float start_ms, float end_ms) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.region(plot, channel, start_ms, end_ms); });
}
YSE_C_API unsigned int yse_dsp_plot_data_get_region_channel(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, 0u, [&](DSP::plotData& f) { return f.regionChannel(plot); });
}
YSE_C_API float yse_dsp_plot_data_get_region_start(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, 0.f, [&](DSP::plotData& f) { return f.regionStart(plot); });
}
YSE_C_API float yse_dsp_plot_data_get_region_end(YseDspProcessor* p, unsigned int plot) {
  return query<PlotData>(p, 0.f, [&](DSP::plotData& f) { return f.regionEnd(plot); });
}
YSE_C_API unsigned int yse_dsp_plot_data_process(YseDspProcessor* p, unsigned int plot,
                                                 const float* in, unsigned int length) {
  if (in == nullptr) return 0;
  DSP::plotData* f = object<PlotData>(p);
  if (f == nullptr || plot >= f->plots()) return 0;
  return drive<PlotData>(p, length, [&](PlotData& h, unsigned o, unsigned n) {
    h.object(plot, stage(h.scratch[0], in + o, n));
  });
}
YSE_C_API void yse_dsp_plot_data_restart(YseDspProcessor* p, unsigned int plot) {
  with<PlotData>(p, [&](DSP::plotData& f) { f.restart(plot); });
}
YSE_C_API int yse_dsp_plot_data_publish(YseDspProcessor* p) {
  return query<PlotData>(p, 0, [](DSP::plotData& f) { return f.publish() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_plot_data_get_frames(YseDspProcessor* p) {
  return query<PlotData>(p, 0u, [](DSP::plotData& f) { return f.frames(); });
}
YSE_C_API unsigned int yse_dsp_plot_data_acquire(YseDspProcessor* p, YseDspPlotFrame* frame) {
  DSP::plotData* f = object<PlotData>(p);
  return describe(f != nullptr ? f->acquire() : DSP::plotData::frame{}, frame);
}
YSE_C_API unsigned int yse_dsp_plot_data_held(YseDspProcessor* p, YseDspPlotFrame* frame) {
  DSP::plotData* f = object<PlotData>(p);
  return describe(f != nullptr ? f->held() : DSP::plotData::frame{}, frame);
}
YSE_C_API unsigned int yse_dsp_plot_data_held_subplot(YseDspProcessor* p, unsigned int plot,
                                                      YseDspPlotSubplot* subplot, float* values,
                                                      unsigned int capacity) {
  DSP::plotData* f = object<PlotData>(p);
  if (f == nullptr) return 0;
  const DSP::plotData::frame held = f->held();
  if (plot >= held.plots || held.subplots == nullptr) return 0;
  const DSP::plotData::subplot& s = held.subplots[plot];
  if (subplot != nullptr) {
    subplot->domain = to_axis(s.domain);
    subplot->range = to_axis(s.range);
    subplot->x_origin = s.xOrigin;
    subplot->y_origin = s.yOrigin;
    subplot->filter = static_cast<YseDspPlotFilter>(s.filter);
  }
  const unsigned n = std::min(held.points, capacity);
  if (values != nullptr && s.values != nullptr) std::memcpy(values, s.values, sizeof(float) * n);
  return held.points;
}

} // extern "C"
