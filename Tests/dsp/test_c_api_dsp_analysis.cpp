// C-API tests for the signal analysers and meters of the MSP port (issue
// #1301): yse_c/yse_dsp_analysis.h (direction, edgeDetector, schmittTrigger,
// spikeDetector, zeroCrossing, peakTracker, movingAverage, minMax,
// loudnessMeter, pitchTracker, sampleCapture, levelMeter, scopeCapture,
// spectrumAnalyzer, plotData), on the shared YseDspProcessor handle of #1173.
//
// The DSP itself is covered by each class's own suite (test_direction.cpp,
// ...). What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another analyser, or a
//     filter) is treated like NULL, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair, list setters and getters, and
//     enum setters given a value out of range.
//   * Parity — every process entry point produces exactly what the engine
//     class produces when driven directly in 128-sample blocks, for a length
//     that is not a multiple of 128: samples, the positions and values of
//     what each block reported (offset into the whole call), and the getters
//     after the call. The per-sample calls match the engine's.
//   * Reports over a whole call — a report in a block that is not the last,
//     a host array smaller than the count, refused calls, reset and set.
//   * The reader path — levelMeter's reading, scopeCapture's traces,
//     spectrumAnalyzer's frames and plotData's frames, copied out on the
//     analysing thread and on another thread while the analysing thread runs.
//   * Real-time safety — every call but a constructor allocates nothing.
//
// No engine session is needed: the classes are plain objects that read the
// global SAMPLERATE, which the C and C++ sides here share. Every case pins it
// to 48 kHz, which the expected positions and intervals assume: a session
// another suite opened in the same process may have left it elsewhere.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

#include "yse_c/yse_dsp_analysis.h"
#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_processor.h"

#include "dsp/analysis/direction.hpp"
#include "dsp/analysis/edgeDetector.hpp"
#include "dsp/analysis/levelMeter.hpp"
#include "dsp/analysis/loudnessMeter.hpp"
#include "dsp/analysis/minMax.hpp"
#include "dsp/analysis/movingAverage.hpp"
#include "dsp/analysis/peakTracker.hpp"
#include "dsp/analysis/pitchTracker.hpp"
#include "dsp/analysis/plotData.hpp"
#include "dsp/analysis/sampleCapture.hpp"
#include "dsp/analysis/schmittTrigger.hpp"
#include "dsp/analysis/scopeCapture.hpp"
#include "dsp/analysis/spectrumAnalyzer.hpp"
#include "dsp/analysis/spikeDetector.hpp"
#include "dsp/analysis/zeroCrossing.hpp"
#include "dsp/buffer.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"

namespace {
  namespace DSP = YSE::DSP;
  constexpr unsigned BLOCK = YSE::STANDARD_BUFFERSIZE;
  // Not a multiple of the block size: the last block is short.
  constexpr unsigned LENGTH = 3 * BLOCK + 37;
  // Long enough for the classes that measure in 100 ms steps or analyse
  // thousands of samples: a second and a bit at 48 kHz.
  constexpr unsigned LONG = 375 * BLOCK + 37;
  const float NaN = std::numeric_limits<float>::quiet_NaN();

  // A deterministic, broadband test signal around `offset`.
  std::vector<float> Signal(unsigned length, float scale = 1.f, float offset = 0.f,
                            unsigned seed = 12345u) {
    std::vector<float> v(length);
    unsigned state = seed;
    for (unsigned i = 0; i < length; ++i) {
      state = state * 1103515245u + 12345u;
      const float noise = static_cast<float>((state >> 9) & 0xFFFF) / 32768.f - 1.f;
      v[i] = offset + scale * (0.5f * std::sin(0.05f * static_cast<float>(i)) + 0.5f * noise);
    }
    return v;
  }

  // A sine of `hz` at 48 kHz.
  std::vector<float> Sine(unsigned length, float hz, float amplitude = 0.5f) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i)
      v[i] = amplitude * std::sin(2.f * 3.14159265f * hz * static_cast<float>(i) / 48000.f);
    return v;
  }

  // A 0/1 gate that is high for `on` of every `period` samples.
  std::vector<float> Gate(unsigned length, unsigned period, unsigned on) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i)
      v[i] = (i % period) < on ? 1.f : 0.f;
    return v;
  }

  // Drives an engine object the way the bridge must: consecutive blocks of
  // at most BLOCK samples, `body(offset, count)` per block.
  template <typename F> void Blocks(unsigned length, F&& body) {
    for (unsigned offset = 0; offset < length; offset += BLOCK)
      body(offset, std::min(BLOCK, length - offset));
  }

  DSP::buffer& Stage(DSP::buffer& b, const std::vector<float>& src, unsigned offset, unsigned n) {
    b.resize(n);
    std::memcpy(b.getPtr(), src.data() + offset, sizeof(float) * n);
    return b;
  }

  // Copy an engine block into the reference run at `offset`.
  void Put(DSP::buffer& b, std::vector<float>& dst, unsigned offset, unsigned n) {
    REQUIRE(b.getLength() == n);
    std::memcpy(dst.data() + offset, b.getPtr(), sizeof(float) * n);
  }

  // Bit for bit: a NaN matches a NaN, and 0 does not match -0.
  bool Same(float a, float b) {
    std::uint32_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
  }

  void CheckSame(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (!Same(a[i], b[i])) {
        INFO("sample " << i << ": " << a[i] << " vs " << b[i]);
        CHECK(Same(a[i], b[i]));
        return;
      }
    }
  }

  bool Moves(const std::vector<float>& v) {
    return std::any_of(v.begin(), v.end(), [&](float x) { return !Same(x, v[0]); });
  }

  // Positions or values that fall in more than one block: a check that the
  // bridge adds each block's offset must see a report past the first block.
  bool PastFirstBlock(const std::vector<unsigned>& positions) {
    return std::any_of(positions.begin(), positions.end(), [](unsigned i) { return i >= BLOCK; });
  }

  using Ctor = std::function<YseDspProcessor*()>;
  struct Named {
    const char* name;
    Ctor make;
  };

  std::vector<Named> AllCtors() {
    return {
        {"direction", [] { return yse_dsp_direction_create(); }},
        {"edge_detector", [] { return yse_dsp_edge_detector_create(); }},
        {"schmitt_trigger", [] { return yse_dsp_schmitt_trigger_create(); }},
        {"spike_detector", [] { return yse_dsp_spike_detector_create(); }},
        {"zero_crossing", [] { return yse_dsp_zero_crossing_create(); }},
        {"peak_tracker", [] { return yse_dsp_peak_tracker_create(); }},
        {"moving_average", [] { return yse_dsp_moving_average_create(64); }},
        {"min_max", [] { return yse_dsp_min_max_create(); }},
        {"loudness_meter", [] { return yse_dsp_loudness_meter_create(2); }},
        {"pitch_tracker", [] { return yse_dsp_pitch_tracker_create(); }},
        {"sample_capture", [] { return yse_dsp_sample_capture_create(300); }},
        {"level_meter", [] { return yse_dsp_level_meter_create(); }},
        {"scope_capture", [] { return yse_dsp_scope_capture_create(64); }},
        {"spectrum_analyzer", [] { return yse_dsp_spectrum_analyzer_create(256); }},
        {"plot_data", [] { return yse_dsp_plot_data_create(2, 16); }},
    };
  }

  // The positions an engine block call reported, offset into the whole run.
  template <typename Count, typename At>
  void Collect(std::vector<unsigned>& to, unsigned offset, Count&& count, At&& at) {
    for (UInt i = 0; i < count(); ++i)
      to.push_back(offset + at(i));
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api analysis: every constructor pairs with destroy and frees what it "
            "allocated") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (const Named& c : AllCtors()) {
      INFO("constructor: " << std::string(c.name));
      TestHelpers::AllocWatch watch;
      YseDspProcessor* p = c.make();
      REQUIRE(p != nullptr);
      const std::size_t allocated = watch.newSize();
      yse_dsp_processor_destroy(p);
      if (allocated == 0) {
        MESSAGE("alloc probe watch inactive (sanitizer owns new/delete): size check skipped");
        continue;
      }
      if (watch.sawSizedDelete()) CHECK(watch.deleteSize() == allocated);
    }
  }

  TEST_CASE("c api analysis: a handle of another class is refused like NULL") {
    TestHelpers::ScopedSampleRate rate(48000);
    YseDspProcessor* dir = yse_dsp_direction_create();
    yse_dsp_direction_set(dir, 0.75f);
    YseDspProcessor* lv = yse_dsp_level_meter_create();
    yse_dsp_level_meter_set_offset(lv, -14.f);
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(dir != nullptr);
    REQUIRE(lv != nullptr);
    REQUIRE(lp != nullptr);

    const std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f), extra(LENGTH, 7.f);
    std::vector<unsigned> pos(LENGTH, 7u);
    const float* chans[2] = {in.data(), in.data()};
    YseDspLevelReading reading{};
    reading.level = 7.f;
    YseDspScopeTrace trace{};
    trace.serial = 7u;
    YseDspSpectrumFrame frame{};
    frame.serial = 7u;
    YseDspPlotFrame plot{};
    plot.serial = 7u;
    YseDspPlotSubplot sub{};
    sub.x_origin = 7.f;
    YseDspPlotAxis axis{};
    axis.start = 7.f;

    for (YseDspProcessor* wrong : {lv, lp}) {
      // Setters of every other class do nothing ...
      yse_dsp_direction_set(wrong, 1.f);
      yse_dsp_direction_reset(wrong);
      CHECK(yse_dsp_direction_get_previous(wrong) == 0.f);
      CHECK(yse_dsp_direction_process_sample(wrong, 1.f) == 0.f);
      CHECK(yse_dsp_direction_process(wrong, in.data(), out.data(), LENGTH) == 0);
    }
    for (YseDspProcessor* wrong : {dir, lp}) {
      // levelMeter, refused: every call, the reader's too.
      yse_dsp_level_meter_set(wrong, 0.5f);
      yse_dsp_level_meter_set_db(wrong, -6.f);
      yse_dsp_level_meter_clear(wrong);
      yse_dsp_level_meter_reset(wrong);
      yse_dsp_level_meter_set_detector(wrong, YSE_DSP_LEVEL_RMS);
      yse_dsp_level_meter_set_interval(wrong, 5.f);
      yse_dsp_level_meter_set_attack(wrong, 5.f);
      yse_dsp_level_meter_set_release(wrong, 5.f);
      yse_dsp_level_meter_set_hold(wrong, 5.f);
      yse_dsp_level_meter_set_offset(wrong, 3.f);
      yse_dsp_level_meter_set_ballistics(wrong, YSE_DSP_BALLISTICS_VU);
      CHECK(yse_dsp_level_meter_process_sample(wrong, 2.f) == 0);
      CHECK(yse_dsp_level_meter_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_level_meter_get_reports(wrong) == 0u);
      CHECK(yse_dsp_level_meter_get_level(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_level_db(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_peak_hold(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_peak_hold_db(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_is_clipped(wrong) == 0);
      CHECK(yse_dsp_level_meter_get_detector(wrong) == YSE_DSP_LEVEL_PEAK);
      CHECK(yse_dsp_level_meter_get_interval(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_attack(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_release(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_hold(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_get_offset(wrong) == 0.f);
      CHECK(yse_dsp_level_meter_read(wrong, &reading) == 0);
      CHECK(reading.level == 7.f);
    }
    for (YseDspProcessor* wrong : {dir, lv, lp}) {
      // ... the other analysers, refused by a direction, a level meter and
      // a filter alike ...
      yse_dsp_edge_detector_set(wrong, 1);
      yse_dsp_edge_detector_reset(wrong);
      CHECK(yse_dsp_edge_detector_process_sample(wrong, 1.f) == 0);
      CHECK(yse_dsp_edge_detector_process(wrong, in.data(), pos.data(), LENGTH, pos.data(), LENGTH,
                                          LENGTH) == 0);
      CHECK(yse_dsp_edge_detector_get_rises(wrong) == 0u);
      CHECK(yse_dsp_edge_detector_get_falls(wrong) == 0u);
      CHECK(yse_dsp_edge_detector_is_high(wrong) == 0);

      yse_dsp_schmitt_trigger_set_low(wrong, 0.2f);
      yse_dsp_schmitt_trigger_set_high(wrong, 0.4f);
      yse_dsp_schmitt_trigger_set(wrong, 1);
      yse_dsp_schmitt_trigger_reset(wrong);
      CHECK(yse_dsp_schmitt_trigger_get_low(wrong) == 0.f);
      CHECK(yse_dsp_schmitt_trigger_get_high(wrong) == 0.f);
      CHECK(yse_dsp_schmitt_trigger_is_on(wrong) == 0);
      CHECK(yse_dsp_schmitt_trigger_process_sample(wrong, 1.f) == 0.f);
      CHECK(yse_dsp_schmitt_trigger_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_schmitt_trigger_process_signal(wrong, in.data(), in.data(), in.data(),
                                                   out.data(), LENGTH) == 0);

      yse_dsp_spike_detector_set_refractory(wrong, 3.f);
      yse_dsp_spike_detector_restart(wrong);
      yse_dsp_spike_detector_reset(wrong);
      CHECK(yse_dsp_spike_detector_process_sample(wrong, 1.f) == 0);
      CHECK(yse_dsp_spike_detector_process(wrong, in.data(), pos.data(), out.data(), LENGTH,
                                           LENGTH) == 0);
      CHECK(yse_dsp_spike_detector_get_spikes(wrong) == 0u);
      CHECK(yse_dsp_spike_detector_get_last_interval(wrong) == 0.f);
      CHECK(yse_dsp_spike_detector_get_refractory(wrong) == 0.f);

      yse_dsp_zero_crossing_set_volume(wrong, 0.5f);
      yse_dsp_zero_crossing_reset(wrong);
      CHECK(yse_dsp_zero_crossing_process_sample(wrong, 1.f) == 0);
      CHECK(yse_dsp_zero_crossing_process(wrong, in.data(), out.data(), extra.data(), pos.data(),
                                          LENGTH, LENGTH) == 0);
      CHECK(yse_dsp_zero_crossing_get_crossings(wrong) == 0u);
      CHECK(yse_dsp_zero_crossing_get_volume(wrong) == 0.f);

      yse_dsp_peak_tracker_set_interval(wrong, 2.f);
      yse_dsp_peak_tracker_set_signed(wrong, 1);
      yse_dsp_peak_tracker_reset(wrong);
      CHECK(yse_dsp_peak_tracker_process_sample(wrong, 1.f) == 0);
      CHECK(yse_dsp_peak_tracker_process(wrong, in.data(), pos.data(), out.data(), LENGTH,
                                         LENGTH) == 0);
      CHECK(yse_dsp_peak_tracker_get_reports(wrong) == 0u);
      CHECK(yse_dsp_peak_tracker_get_last_report(wrong) == 0.f);
      CHECK(yse_dsp_peak_tracker_get_peak(wrong) == 0.f);
      CHECK(yse_dsp_peak_tracker_take(wrong) == 0.f);
      CHECK(yse_dsp_peak_tracker_get_interval(wrong) == 0.f);
      CHECK(yse_dsp_peak_tracker_is_signed(wrong) == 0);

      yse_dsp_moving_average_set_length(wrong, 3);
      yse_dsp_moving_average_set_mode(wrong, YSE_DSP_AVERAGE_RMS);
      yse_dsp_moving_average_reset(wrong);
      CHECK(yse_dsp_moving_average_process_sample(wrong, 1.f) == 0.f);
      CHECK(yse_dsp_moving_average_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_moving_average_accumulate(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_moving_average_get_value(wrong) == 0.f);
      CHECK(yse_dsp_moving_average_get_length(wrong) == 0u);
      CHECK(yse_dsp_moving_average_get_capacity(wrong) == 0u);
      CHECK(yse_dsp_moving_average_get_mode(wrong) == YSE_DSP_AVERAGE_BIPOLAR);
      CHECK(yse_dsp_moving_average_get_accumulated(wrong) == 0.f);
      CHECK(yse_dsp_moving_average_get_accumulated_samples(wrong) == 0.0);
      CHECK(yse_dsp_moving_average_take(wrong) == 0.f);

      yse_dsp_min_max_process_sample(wrong, 1.f);
      yse_dsp_min_max_process_sample_reset(wrong, 1.f, 1.f);
      yse_dsp_min_max_reset(wrong);
      CHECK(yse_dsp_min_max_process(wrong, in.data(), out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_min_max_process_reset(wrong, in.data(), in.data(), out.data(), extra.data(),
                                          LENGTH) == 0);
      CHECK(yse_dsp_min_max_get_minimum(wrong) == 0.f);
      CHECK(yse_dsp_min_max_get_maximum(wrong) == 0.f);
      CHECK(yse_dsp_min_max_is_empty(wrong) == 0);

      yse_dsp_loudness_meter_set_weight(wrong, 0, 2.f);
      yse_dsp_loudness_meter_set_interval(wrong, 5.f);
      yse_dsp_loudness_meter_reset(wrong);
      CHECK(yse_dsp_loudness_meter_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_loudness_meter_process_channels(wrong, chans, 2, LENGTH) == 0);
      CHECK(yse_dsp_loudness_meter_get_channels(wrong) == 0u);
      CHECK(yse_dsp_loudness_meter_get_weight(wrong, 0) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_reports(wrong) == 0u);
      CHECK(yse_dsp_loudness_meter_get_momentary(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_short_term(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_max_momentary(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_max_short_term(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_integrated(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_loudness_range(wrong) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_peak(wrong, 0) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_true_peak(wrong, 0) == 0.f);
      CHECK(yse_dsp_loudness_meter_get_gated_blocks(wrong) == 0ull);
      CHECK(yse_dsp_loudness_meter_get_interval(wrong) == 0.f);

      yse_dsp_pitch_tracker_set_size(wrong, 256);
      yse_dsp_pitch_tracker_set_period(wrong, 64);
      yse_dsp_pitch_tracker_set_min_frequency(wrong, 100.f);
      yse_dsp_pitch_tracker_set_max_frequency(wrong, 900.f);
      yse_dsp_pitch_tracker_set_threshold(wrong, 0.2f);
      yse_dsp_pitch_tracker_set_tolerance(wrong, 0.3f);
      yse_dsp_pitch_tracker_set_onset_amplitude(wrong, 0.3f);
      yse_dsp_pitch_tracker_set_onset_pitch(wrong, 2.f);
      yse_dsp_pitch_tracker_set_onset_period(wrong, 100);
      yse_dsp_pitch_tracker_reset(wrong);
      CHECK(yse_dsp_pitch_tracker_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_pitch_tracker_get_size(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_period(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_min_frequency(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_max_frequency(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_threshold(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_tolerance(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_onset_amplitude(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_onset_pitch(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_onset_period(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_analyses(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_reports(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_pitches(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_onsets(wrong) == 0u);
      CHECK(yse_dsp_pitch_tracker_get_frequency(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_clarity(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_amplitude(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_is_voiced(wrong) == 0);
      CHECK(yse_dsp_pitch_tracker_get_onset_frequency(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_onset_peak(wrong) == 0.f);
      CHECK(yse_dsp_pitch_tracker_get_latency(wrong) == 0u);

      const unsigned idx[2] = {1, 2};
      yse_dsp_sample_capture_set_mode(wrong, YSE_DSP_CAPTURE_FIRST);
      yse_dsp_sample_capture_process_sample(wrong, 1.f);
      yse_dsp_sample_capture_clear(wrong);
      CHECK(yse_dsp_sample_capture_set_indices(wrong, idx, 2) == 0);
      CHECK(yse_dsp_sample_capture_get_indices(wrong, pos.data(), LENGTH) == 0u);
      CHECK(yse_dsp_sample_capture_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_sample_capture_get_mode(wrong) == YSE_DSP_CAPTURE_LAST);
      CHECK(yse_dsp_sample_capture_get_size(wrong) == 0u);
      CHECK(yse_dsp_sample_capture_get_capacity(wrong) == 0u);
      CHECK(yse_dsp_sample_capture_is_full(wrong) == 0);
      CHECK(yse_dsp_sample_capture_get_sample(wrong, 0) == 0.f);
      CHECK(yse_dsp_sample_capture_get_samples(wrong, out.data(), LENGTH) == 0u);

      yse_dsp_scope_capture_set_size(wrong, 8);
      yse_dsp_scope_capture_set_period(wrong, 4);
      yse_dsp_scope_capture_set_delay(wrong, 1.f);
      yse_dsp_scope_capture_set_trigger(wrong, YSE_DSP_SCOPE_TRIGGER_UP);
      yse_dsp_scope_capture_set_level(wrong, 0.5f);
      yse_dsp_scope_capture_restart(wrong);
      CHECK(yse_dsp_scope_capture_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_scope_capture_process_xy(wrong, in.data(), in.data(), LENGTH) == 0);
      CHECK(yse_dsp_scope_capture_get_size(wrong) == 0u);
      CHECK(yse_dsp_scope_capture_get_period(wrong) == 0u);
      CHECK(yse_dsp_scope_capture_get_delay(wrong) == 0.f);
      CHECK(yse_dsp_scope_capture_get_trigger(wrong) == YSE_DSP_SCOPE_TRIGGER_NONE);
      CHECK(yse_dsp_scope_capture_get_level(wrong) == 0.f);
      CHECK(yse_dsp_scope_capture_get_capacity(wrong) == 0u);
      CHECK(yse_dsp_scope_capture_get_traces(wrong) == 0u);
      CHECK(yse_dsp_scope_capture_acquire(wrong, &trace, out.data(), extra.data(), LENGTH) == 0u);
      CHECK(trace.serial == 0u);
      trace.serial = 7u;
      CHECK(yse_dsp_scope_capture_held(wrong, &trace, out.data(), extra.data(), LENGTH) == 0u);
      CHECK(trace.serial == 0u);

      yse_dsp_spectrum_analyzer_set_size(wrong, 32);
      yse_dsp_spectrum_analyzer_set_interval(wrong, 50.f);
      yse_dsp_spectrum_analyzer_set_mode(wrong, YSE_DSP_SPECTRUM_PHASE);
      yse_dsp_spectrum_analyzer_reset(wrong);
      CHECK(yse_dsp_spectrum_analyzer_process(wrong, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_spectrum_analyzer_process_bins(wrong, in.data(), in.data(), 0, 8, 8, 14, 7) ==
            0);
      CHECK(yse_dsp_spectrum_analyzer_get_size(wrong) == 0u);
      CHECK(yse_dsp_spectrum_analyzer_get_interval(wrong) == 0.f);
      CHECK(yse_dsp_spectrum_analyzer_get_mode(wrong) == YSE_DSP_SPECTRUM_AMPLITUDE);
      CHECK(yse_dsp_spectrum_analyzer_get_capacity(wrong) == 0u);
      CHECK(yse_dsp_spectrum_analyzer_get_frames(wrong) == 0u);
      CHECK(yse_dsp_spectrum_analyzer_acquire(wrong, &frame, out.data(), LENGTH) == 0u);
      CHECK(frame.serial == 0u);
      frame.serial = 7u;
      CHECK(yse_dsp_spectrum_analyzer_held(wrong, &frame, out.data(), LENGTH) == 0u);
      CHECK(frame.serial == 0u);

      const float values[2] = {1.f, 2.f};
      yse_dsp_plot_data_set_points(wrong, 2);
      yse_dsp_plot_data_set_frame_size(wrong, 4);
      yse_dsp_plot_data_set_frame_rate(wrong, 30.f);
      yse_dsp_plot_data_set_domain(wrong, 0, 1.f, 2.f, YSE_DSP_PLOT_LOG);
      yse_dsp_plot_data_set_range(wrong, 0, 1.f, 2.f, YSE_DSP_PLOT_LOG);
      yse_dsp_plot_data_set_x_origin(wrong, 0, 1.f);
      yse_dsp_plot_data_set_y_origin(wrong, 0, 1.f);
      yse_dsp_plot_data_set_filter(wrong, 0, YSE_DSP_PLOT_FILTER_ATODB);
      yse_dsp_plot_data_set_region(wrong, 0, 1, 2.f, 3.f);
      yse_dsp_plot_data_restart(wrong, 0);
      CHECK(yse_dsp_plot_data_set(wrong, 0, values, 2, 0) == 0);
      CHECK(yse_dsp_plot_data_process(wrong, 0, in.data(), LENGTH) == 0);
      CHECK(yse_dsp_plot_data_publish(wrong) == 0);
      CHECK(yse_dsp_plot_data_get_plots(wrong) == 0u);
      CHECK(yse_dsp_plot_data_get_capacity(wrong) == 0u);
      CHECK(yse_dsp_plot_data_get_points(wrong) == 0u);
      CHECK(yse_dsp_plot_data_get_frame_size(wrong) == 0u);
      CHECK(yse_dsp_plot_data_get_frame_rate(wrong) == 0.f);
      CHECK(yse_dsp_plot_data_get_value(wrong, 0, 0) == 0.f);
      CHECK(yse_dsp_plot_data_get_domain(wrong, 0, &axis) == 0);
      CHECK(yse_dsp_plot_data_get_range(wrong, 0, &axis) == 0);
      CHECK(axis.start == 7.f);
      CHECK(yse_dsp_plot_data_get_x_origin(wrong, 0) == 0.f);
      CHECK(yse_dsp_plot_data_get_y_origin(wrong, 0) == 0.f);
      CHECK(yse_dsp_plot_data_get_filter(wrong, 0) == YSE_DSP_PLOT_FILTER_NONE);
      CHECK(yse_dsp_plot_data_get_region_channel(wrong, 0) == 0u);
      CHECK(yse_dsp_plot_data_get_region_start(wrong, 0) == 0.f);
      CHECK(yse_dsp_plot_data_get_region_end(wrong, 0) == 0.f);
      CHECK(yse_dsp_plot_data_get_frames(wrong) == 0u);
      CHECK(yse_dsp_plot_data_acquire(wrong, &plot) == 0u);
      CHECK(yse_dsp_plot_data_held(wrong, &plot) == 0u);
      CHECK(yse_dsp_plot_data_held_subplot(wrong, 0, &sub, out.data(), LENGTH) == 0u);
      CHECK(sub.x_origin == 7.f);
    }
    // ... and nothing was written.
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);
    for (unsigned v : pos)
      REQUIRE(v == 7u);

    // The direction's and the level meter's own state is what it was.
    CHECK(yse_dsp_direction_get_previous(dir) == 0.75f);
    CHECK(yse_dsp_level_meter_get_offset(lv) == -14.f);
    CHECK(yse_dsp_level_meter_get_interval(lv) == 20.f);
    CHECK(yse_dsp_direction_process(dir, in.data(), out.data(), LENGTH) == LENGTH);
    for (YseDspProcessor* p : {dir, lv, lp})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api analysis: NULL handles and NULL arrays are no-ops that write nothing") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f);

    // NULL handle: a representative call of every shape per class.
    yse_dsp_direction_set(nullptr, 1.f);
    CHECK(yse_dsp_direction_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    yse_dsp_edge_detector_set(nullptr, 1);
    CHECK(yse_dsp_edge_detector_get_rises(nullptr) == 0u);
    CHECK(yse_dsp_edge_detector_process(nullptr, in.data(), nullptr, 0, nullptr, 0, LENGTH) == 0);
    yse_dsp_schmitt_trigger_set_low(nullptr, 1.f);
    CHECK(yse_dsp_schmitt_trigger_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    yse_dsp_spike_detector_reset(nullptr);
    CHECK(yse_dsp_spike_detector_get_spikes(nullptr) == 0u);
    yse_dsp_zero_crossing_reset(nullptr);
    CHECK(yse_dsp_zero_crossing_get_crossings(nullptr) == 0u);
    yse_dsp_peak_tracker_reset(nullptr);
    CHECK(yse_dsp_peak_tracker_take(nullptr) == 0.f);
    yse_dsp_moving_average_reset(nullptr);
    CHECK(yse_dsp_moving_average_accumulate(nullptr, in.data(), LENGTH) == 0);
    yse_dsp_min_max_reset(nullptr);
    CHECK(yse_dsp_min_max_process(nullptr, in.data(), out.data(), nullptr, LENGTH) == 0);
    yse_dsp_loudness_meter_reset(nullptr);
    CHECK(yse_dsp_loudness_meter_process(nullptr, in.data(), LENGTH) == 0);
    yse_dsp_pitch_tracker_reset(nullptr);
    CHECK(yse_dsp_pitch_tracker_process(nullptr, in.data(), LENGTH) == 0);
    yse_dsp_sample_capture_clear(nullptr);
    CHECK(yse_dsp_sample_capture_get_samples(nullptr, out.data(), LENGTH) == 0u);
    yse_dsp_level_meter_reset(nullptr);
    YseDspLevelReading reading{};
    CHECK(yse_dsp_level_meter_read(nullptr, &reading) == 0);
    yse_dsp_scope_capture_restart(nullptr);
    CHECK(yse_dsp_scope_capture_acquire(nullptr, nullptr, out.data(), out.data(), LENGTH) == 0u);
    yse_dsp_spectrum_analyzer_reset(nullptr);
    CHECK(yse_dsp_spectrum_analyzer_acquire(nullptr, nullptr, out.data(), LENGTH) == 0u);
    yse_dsp_plot_data_restart(nullptr, 0);
    CHECK(yse_dsp_plot_data_acquire(nullptr, nullptr) == 0u);
    CHECK(yse_dsp_plot_data_held_subplot(nullptr, 0, nullptr, out.data(), LENGTH) == 0u);
    for (float v : out)
      REQUIRE(v == 7.f);

    // NULL required arrays: 0, nothing written, no state changed.
    YseDspProcessor* dir = yse_dsp_direction_create();
    YseDspProcessor* ed = yse_dsp_edge_detector_create();
    YseDspProcessor* st = yse_dsp_schmitt_trigger_create();
    YseDspProcessor* sp = yse_dsp_spike_detector_create();
    YseDspProcessor* zc = yse_dsp_zero_crossing_create();
    YseDspProcessor* pk = yse_dsp_peak_tracker_create();
    YseDspProcessor* ma = yse_dsp_moving_average_create(16);
    YseDspProcessor* mm = yse_dsp_min_max_create();
    YseDspProcessor* ld = yse_dsp_loudness_meter_create(2);
    YseDspProcessor* pt = yse_dsp_pitch_tracker_create();
    YseDspProcessor* sc = yse_dsp_sample_capture_create(64);
    YseDspProcessor* lv = yse_dsp_level_meter_create();
    YseDspProcessor* sco = yse_dsp_scope_capture_create(64);
    YseDspProcessor* sa = yse_dsp_spectrum_analyzer_create(64);
    YseDspProcessor* pd = yse_dsp_plot_data_create(1, 8);

    CHECK(yse_dsp_direction_process(dir, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_direction_process(dir, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_edge_detector_process(ed, nullptr, nullptr, 0, nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_schmitt_trigger_process_signal(st, in.data(), nullptr, in.data(), out.data(),
                                                 LENGTH) == 0);
    CHECK(yse_dsp_schmitt_trigger_process_signal(st, in.data(), in.data(), nullptr, out.data(),
                                                 LENGTH) == 0);
    CHECK(yse_dsp_spike_detector_process(sp, nullptr, nullptr, nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_zero_crossing_process(zc, nullptr, out.data(), out.data(), nullptr, 0, LENGTH) ==
          0);
    CHECK(yse_dsp_peak_tracker_process(pk, nullptr, nullptr, nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_moving_average_process(ma, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_moving_average_accumulate(ma, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_moving_average_get_accumulated_samples(ma) == 0.0);
    CHECK(yse_dsp_min_max_process(mm, nullptr, out.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_min_max_process_reset(mm, in.data(), nullptr, out.data(), out.data(), LENGTH) ==
          0);
    CHECK(yse_dsp_min_max_is_empty(mm) == 1);
    CHECK(yse_dsp_loudness_meter_process(ld, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_loudness_meter_process_channels(ld, nullptr, 2, LENGTH) == 0);
    CHECK(yse_dsp_pitch_tracker_process(pt, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_sample_capture_process(sc, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_sample_capture_get_size(sc) == 0u);
    CHECK(yse_dsp_level_meter_process(lv, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_level_meter_read(lv, nullptr) == 0);
    CHECK(yse_dsp_scope_capture_process(sco, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_scope_capture_process_xy(sco, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_spectrum_analyzer_process(sa, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_plot_data_process(pd, 0, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_plot_data_process(pd, 1, in.data(), LENGTH) == 0); // past the plots
    for (float v : out)
      REQUIRE(v == 7.f);

    // NULL optional outputs: the call runs and returns the length.
    CHECK(yse_dsp_edge_detector_process(ed, in.data(), nullptr, 5, nullptr, 5, LENGTH) == LENGTH);
    CHECK(yse_dsp_spike_detector_process(sp, in.data(), nullptr, nullptr, 5, LENGTH) == LENGTH);
    CHECK(yse_dsp_zero_crossing_process(zc, in.data(), nullptr, nullptr, nullptr, 5, LENGTH) ==
          LENGTH);
    CHECK(yse_dsp_zero_crossing_get_crossings(zc) > 0u);
    CHECK(yse_dsp_peak_tracker_process(pk, in.data(), nullptr, nullptr, 5, LENGTH) == LENGTH);
    CHECK(yse_dsp_min_max_process(mm, in.data(), nullptr, nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_min_max_is_empty(mm) == 0);
    CHECK(yse_dsp_scope_capture_acquire(sco, nullptr, nullptr, nullptr, 0) == 0u);
    CHECK(yse_dsp_plot_data_acquire(pd, nullptr) == 1u);
    CHECK(yse_dsp_plot_data_held_subplot(pd, 0, nullptr, nullptr, 0) == 4u);
    // A NULL list with a count is refused, a NULL `out` of the static helpers
    // too.
    CHECK(yse_dsp_sample_capture_set_indices(sc, nullptr, 2) == 0);
    CHECK(yse_dsp_plot_data_set(pd, 0, nullptr, 2, 0) == 0);
    CHECK(yse_dsp_loudness_meter_shelf(48000.0, nullptr) == 0);
    CHECK(yse_dsp_loudness_meter_high_pass(48000.0, nullptr) == 0);
    YseDspPlotAxis* noAxis = nullptr;
    CHECK(yse_dsp_plot_data_get_domain(pd, 0, noAxis) == 0);

    for (YseDspProcessor* p : {dir, ed, st, sp, zc, pk, ma, mm, ld, pt, sc, lv, sco, sa, pd})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api analysis: setters and getters round-trip") {
    TestHelpers::ScopedSampleRate rate(48000);
    YseDspProcessor* p = yse_dsp_direction_create();
    yse_dsp_direction_set(p, -0.25f);
    CHECK(yse_dsp_direction_get_previous(p) == -0.25f);
    yse_dsp_direction_reset(p);
    CHECK(yse_dsp_direction_get_previous(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_edge_detector_create();
    CHECK(yse_dsp_edge_detector_is_high(p) == 0);
    yse_dsp_edge_detector_set(p, 1);
    CHECK(yse_dsp_edge_detector_is_high(p) == 1);
    yse_dsp_edge_detector_reset(p);
    CHECK(yse_dsp_edge_detector_is_high(p) == 0);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_schmitt_trigger_create();
    CHECK(yse_dsp_schmitt_trigger_get_low(p) == 0.f);
    CHECK(yse_dsp_schmitt_trigger_get_high(p) == 0.f);
    yse_dsp_schmitt_trigger_set_low(p, -0.1f);
    yse_dsp_schmitt_trigger_set_high(p, 0.4f);
    yse_dsp_schmitt_trigger_set_high(p, NaN); // ignored
    CHECK(yse_dsp_schmitt_trigger_get_low(p) == -0.1f);
    CHECK(yse_dsp_schmitt_trigger_get_high(p) == 0.4f);
    yse_dsp_schmitt_trigger_set(p, 1);
    CHECK(yse_dsp_schmitt_trigger_is_on(p) == 1);
    yse_dsp_schmitt_trigger_reset(p);
    CHECK(yse_dsp_schmitt_trigger_is_on(p) == 0);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_spike_detector_create();
    yse_dsp_spike_detector_set_refractory(p, 12.5f);
    CHECK(yse_dsp_spike_detector_get_refractory(p) == 12.5f);
    yse_dsp_spike_detector_set_refractory(p, -3.f);
    CHECK(yse_dsp_spike_detector_get_refractory(p) == 0.f);
    CHECK(yse_dsp_spike_detector_get_last_interval(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_zero_crossing_create();
    CHECK(yse_dsp_zero_crossing_get_volume(p) == 1.f);
    yse_dsp_zero_crossing_set_volume(p, -0.5f);
    CHECK(yse_dsp_zero_crossing_get_volume(p) == -0.5f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_peak_tracker_create();
    CHECK(yse_dsp_peak_tracker_get_interval(p) == 0.f);
    yse_dsp_peak_tracker_set_interval(p, 7.5f);
    CHECK(yse_dsp_peak_tracker_get_interval(p) == 7.5f);
    CHECK(yse_dsp_peak_tracker_is_signed(p) == 0);
    yse_dsp_peak_tracker_set_signed(p, 1);
    CHECK(yse_dsp_peak_tracker_is_signed(p) == 1);
    yse_dsp_peak_tracker_process_sample(p, -0.8f);
    yse_dsp_peak_tracker_process_sample(p, 0.5f);
    CHECK(yse_dsp_peak_tracker_get_peak(p) == -0.8f);
    CHECK(yse_dsp_peak_tracker_take(p) == -0.8f);
    CHECK(yse_dsp_peak_tracker_get_last_report(p) == -0.8f);
    CHECK(yse_dsp_peak_tracker_get_peak(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_moving_average_create(0); // clamped to 1
    CHECK(yse_dsp_moving_average_get_capacity(p) == 1u);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_moving_average_create(YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY + 1);
    CHECK(yse_dsp_moving_average_get_capacity(p) == YSE_DSP_MOVING_AVERAGE_MAX_CAPACITY);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_moving_average_create(32);
    CHECK(yse_dsp_moving_average_get_capacity(p) == 32u);
    CHECK(yse_dsp_moving_average_get_length(p) == 32u);
    yse_dsp_moving_average_set_length(p, 10);
    CHECK(yse_dsp_moving_average_get_length(p) == 10u);
    yse_dsp_moving_average_set_length(p, 100);
    CHECK(yse_dsp_moving_average_get_length(p) == 32u);
    CHECK(yse_dsp_moving_average_get_mode(p) == YSE_DSP_AVERAGE_BIPOLAR);
    yse_dsp_moving_average_set_mode(p, YSE_DSP_AVERAGE_RMS);
    CHECK(yse_dsp_moving_average_get_mode(p) == YSE_DSP_AVERAGE_RMS);
    yse_dsp_moving_average_set_mode(p, static_cast<YseDspAverageMode>(3));
    yse_dsp_moving_average_set_mode(p, static_cast<YseDspAverageMode>(-1));
    CHECK(yse_dsp_moving_average_get_mode(p) == YSE_DSP_AVERAGE_RMS);
    yse_dsp_moving_average_process_sample(p, 2.f);
    yse_dsp_moving_average_process_sample(p, 2.f);
    CHECK(yse_dsp_moving_average_get_accumulated_samples(p) == 2.0);
    CHECK(yse_dsp_moving_average_take(p) == 2.f);
    CHECK(yse_dsp_moving_average_get_accumulated_samples(p) == 0.0);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_min_max_create();
    CHECK(yse_dsp_min_max_is_empty(p) == 1);
    yse_dsp_min_max_process_sample(p, 0.5f);
    yse_dsp_min_max_process_sample(p, -0.25f);
    yse_dsp_min_max_process_sample(p, NaN);
    CHECK(yse_dsp_min_max_get_minimum(p) == -0.25f);
    CHECK(yse_dsp_min_max_get_maximum(p) == 0.5f);
    yse_dsp_min_max_process_sample_reset(p, 0.1f, 1.f);
    CHECK(yse_dsp_min_max_get_minimum(p) == 0.1f);
    CHECK(yse_dsp_min_max_get_maximum(p) == 0.1f);
    yse_dsp_min_max_reset(p);
    CHECK(yse_dsp_min_max_is_empty(p) == 1);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_loudness_meter_create(0); // clamped to 1
    CHECK(yse_dsp_loudness_meter_get_channels(p) == 1u);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_loudness_meter_create(YSE_DSP_LOUDNESS_MAX_CHANNELS + 5);
    CHECK(yse_dsp_loudness_meter_get_channels(p) == YSE_DSP_LOUDNESS_MAX_CHANNELS);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_loudness_meter_create(3);
    CHECK(yse_dsp_loudness_meter_get_weight(p, 2) == 1.f);
    yse_dsp_loudness_meter_set_weight(p, 2, 1.41f);
    yse_dsp_loudness_meter_set_weight(p, 1, -1.f); // ignored
    yse_dsp_loudness_meter_set_weight(p, 70, 2.f); // out of range
    CHECK(yse_dsp_loudness_meter_get_weight(p, 2) == 1.41f);
    CHECK(yse_dsp_loudness_meter_get_weight(p, 1) == 1.f);
    CHECK(yse_dsp_loudness_meter_get_weight(p, 70) == 0.f);
    yse_dsp_loudness_meter_set_interval(p, 100.f);
    CHECK(yse_dsp_loudness_meter_get_interval(p) == 100.f);
    CHECK(yse_dsp_loudness_meter_get_momentary(p) == YSE_DSP_LOUDNESS_SILENCE);
    CHECK(yse_dsp_loudness_meter_get_integrated(p) == YSE_DSP_LOUDNESS_SILENCE);
    CHECK(yse_dsp_loudness_meter_get_gated_blocks(p) == 0ull);
    {
      double shelf[5] = {}, high[5] = {};
      REQUIRE(yse_dsp_loudness_meter_shelf(44100.0, shelf) == 1);
      REQUIRE(yse_dsp_loudness_meter_high_pass(44100.0, high) == 1);
      const DSP::loudnessMeter::section s = DSP::loudnessMeter::Shelf(44100.0);
      const DSP::loudnessMeter::section h = DSP::loudnessMeter::HighPass(44100.0);
      CHECK(shelf[0] == s.b0);
      CHECK(shelf[1] == s.b1);
      CHECK(shelf[2] == s.b2);
      CHECK(shelf[3] == s.a1);
      CHECK(shelf[4] == s.a2);
      CHECK(high[0] == h.b0);
      CHECK(high[1] == h.b1);
      CHECK(high[2] == h.b2);
      CHECK(high[3] == h.a1);
      CHECK(high[4] == h.a2);
    }
    yse_dsp_processor_destroy(p);

    p = yse_dsp_pitch_tracker_create();
    CHECK(yse_dsp_pitch_tracker_get_size(p) == YSE_DSP_PITCH_TRACKER_DEFAULT_SIZE);
    CHECK(yse_dsp_pitch_tracker_get_period(p) == YSE_DSP_PITCH_TRACKER_DEFAULT_PERIOD);
    CHECK(yse_dsp_pitch_tracker_get_onset_period(p) == YSE_DSP_PITCH_TRACKER_DEFAULT_ONSET_PERIOD);
    CHECK(yse_dsp_pitch_tracker_get_min_frequency(p) == 40.f);
    CHECK(yse_dsp_pitch_tracker_get_max_frequency(p) == 4000.f);
    CHECK(yse_dsp_pitch_tracker_get_threshold(p) == 0.01f);
    CHECK(yse_dsp_pitch_tracker_get_tolerance(p) == 0.15f);
    CHECK(yse_dsp_pitch_tracker_get_onset_amplitude(p) == 0.1f);
    CHECK(yse_dsp_pitch_tracker_get_onset_pitch(p) == 1.f);
    yse_dsp_pitch_tracker_set_size(p, 1000); // a power of two below
    CHECK(yse_dsp_pitch_tracker_get_size(p) == 512u);
    CHECK(yse_dsp_pitch_tracker_get_latency(p) == 256u);
    yse_dsp_pitch_tracker_set_size(p, 1);
    CHECK(yse_dsp_pitch_tracker_get_size(p) == YSE_DSP_PITCH_TRACKER_MIN_SIZE);
    yse_dsp_pitch_tracker_set_period(p, 99999);
    CHECK(yse_dsp_pitch_tracker_get_period(p) == YSE_DSP_PITCH_TRACKER_MAX_PERIOD);
    yse_dsp_pitch_tracker_set_period(p, 1);
    CHECK(yse_dsp_pitch_tracker_get_period(p) == YSE_DSP_PITCH_TRACKER_MIN_PERIOD);
    yse_dsp_pitch_tracker_set_min_frequency(p, 100.f);
    yse_dsp_pitch_tracker_set_max_frequency(p, 900.f);
    yse_dsp_pitch_tracker_set_threshold(p, 0.2f);
    yse_dsp_pitch_tracker_set_tolerance(p, 0.3f);
    yse_dsp_pitch_tracker_set_onset_amplitude(p, 0.3f);
    yse_dsp_pitch_tracker_set_onset_pitch(p, 2.f);
    yse_dsp_pitch_tracker_set_onset_period(p, 100);
    CHECK(yse_dsp_pitch_tracker_get_min_frequency(p) == 100.f);
    CHECK(yse_dsp_pitch_tracker_get_max_frequency(p) == 900.f);
    CHECK(yse_dsp_pitch_tracker_get_threshold(p) == 0.2f);
    CHECK(yse_dsp_pitch_tracker_get_tolerance(p) == 0.3f);
    CHECK(yse_dsp_pitch_tracker_get_onset_amplitude(p) == 0.3f);
    CHECK(yse_dsp_pitch_tracker_get_onset_pitch(p) == 2.f);
    CHECK(yse_dsp_pitch_tracker_get_onset_period(p) == 100u);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_sample_capture_create(0); // clamped to 1
    CHECK(yse_dsp_sample_capture_get_capacity(p) == 1u);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_sample_capture_create(5);
    CHECK(yse_dsp_sample_capture_get_mode(p) == YSE_DSP_CAPTURE_LAST);
    yse_dsp_sample_capture_set_mode(p, YSE_DSP_CAPTURE_FIRST);
    yse_dsp_sample_capture_set_mode(p, static_cast<YseDspCaptureMode>(2)); // ignored
    CHECK(yse_dsp_sample_capture_get_mode(p) == YSE_DSP_CAPTURE_FIRST);
    {
      const unsigned list[12] = {9, 3, 3, 0, 100, 4, 5, 6, 7, 8, 1, 2};
      REQUIRE(yse_dsp_sample_capture_set_indices(p, list, 12) == 1);
      // The first ten, sorted and without the duplicate 3.
      CHECK(yse_dsp_sample_capture_get_indices(p, nullptr, 0) == 9u);
      unsigned got[4] = {77, 77, 77, 77};
      CHECK(yse_dsp_sample_capture_get_indices(p, got, 3) == 9u);
      CHECK(got[0] == 0u);
      CHECK(got[1] == 3u);
      CHECK(got[2] == 4u);
      CHECK(got[3] == 77u);
      CHECK(yse_dsp_sample_capture_set_indices(p, nullptr, 3) == 0); // refused: unchanged
      CHECK(yse_dsp_sample_capture_get_indices(p, nullptr, 0) == 9u);
      CHECK(yse_dsp_sample_capture_set_indices(p, nullptr, 0) == 1); // every sample again
      CHECK(yse_dsp_sample_capture_get_indices(p, got, 4) == 0u);
    }
    for (int i = 1; i <= 7; ++i)
      yse_dsp_sample_capture_process_sample(p, static_cast<float>(i));
    CHECK(yse_dsp_sample_capture_is_full(p) == 1);
    CHECK(yse_dsp_sample_capture_get_size(p) == 5u);
    CHECK(yse_dsp_sample_capture_get_sample(p, 0) == 1.f); // FIRST keeps 1 .. 5
    CHECK(yse_dsp_sample_capture_get_sample(p, 9) == 0.f);
    {
      float held[3] = {77.f, 77.f, 77.f};
      CHECK(yse_dsp_sample_capture_get_samples(p, nullptr, 0) == 5u);
      CHECK(yse_dsp_sample_capture_get_samples(p, held, 2) == 5u);
      CHECK(held[0] == 1.f);
      CHECK(held[1] == 2.f);
      CHECK(held[2] == 77.f);
    }
    yse_dsp_sample_capture_clear(p);
    CHECK(yse_dsp_sample_capture_get_size(p) == 0u);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_level_meter_create();
    CHECK(yse_dsp_level_meter_get_interval(p) == 20.f);
    CHECK(yse_dsp_level_meter_get_detector(p) == YSE_DSP_LEVEL_PEAK);
    yse_dsp_level_meter_set_detector(p, YSE_DSP_LEVEL_RMS);
    yse_dsp_level_meter_set_detector(p, static_cast<YseDspLevelDetector>(2)); // ignored
    CHECK(yse_dsp_level_meter_get_detector(p) == YSE_DSP_LEVEL_RMS);
    yse_dsp_level_meter_set_interval(p, 5.f);
    yse_dsp_level_meter_set_attack(p, 3.f);
    yse_dsp_level_meter_set_release(p, 30.f);
    yse_dsp_level_meter_set_hold(p, 300.f);
    yse_dsp_level_meter_set_offset(p, -6.f);
    CHECK(yse_dsp_level_meter_get_interval(p) == 5.f);
    CHECK(yse_dsp_level_meter_get_attack(p) == 3.f);
    CHECK(yse_dsp_level_meter_get_release(p) == 30.f);
    CHECK(yse_dsp_level_meter_get_hold(p) == 300.f);
    CHECK(yse_dsp_level_meter_get_offset(p) == -6.f);
    yse_dsp_level_meter_set_ballistics(p, YSE_DSP_BALLISTICS_BBC);
    CHECK(yse_dsp_level_meter_get_attack(p) == 10.f);
    CHECK(yse_dsp_level_meter_get_release(p) == 3000.f);
    CHECK(yse_dsp_level_meter_get_offset(p) == -14.f);
    yse_dsp_level_meter_set_ballistics(p, static_cast<YseDspLevelBallistics>(6)); // ignored
    CHECK(yse_dsp_level_meter_get_release(p) == 3000.f);
    for (int standard = 0; standard <= 5; ++standard) {
      float a = 0.f, r = 0.f, o = 0.f;
      const auto s = static_cast<YseDspLevelBallistics>(standard);
      REQUIRE(yse_dsp_level_meter_ballistics_settings(s, &a, &r, &o) == 1);
      const DSP::levelMeter::ballisticsSettings e =
          DSP::levelMeter::settings(static_cast<DSP::levelMeter::BALLISTICS>(standard));
      CHECK(a == e.attack);
      CHECK(r == e.release);
      CHECK(o == e.offset);
      CHECK(yse_dsp_level_meter_ballistics_settings(s, nullptr, nullptr, nullptr) == 1);
    }
    CHECK(yse_dsp_level_meter_ballistics_settings(static_cast<YseDspLevelBallistics>(6), nullptr,
                                                  nullptr, nullptr) == 0);
    CHECK(yse_dsp_level_meter_position(-30.f, -60.f, 0.f) ==
          DSP::levelMeter::position(-30.f, -60.f, 0.f));
    CHECK(yse_dsp_level_meter_position(NaN, -60.f, 0.f) == 0.f);
    yse_dsp_level_meter_set(p, 2.f);
    CHECK(yse_dsp_level_meter_is_clipped(p) == 1);
    yse_dsp_level_meter_clear(p);
    CHECK(yse_dsp_level_meter_is_clipped(p) == 0);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_scope_capture_create(1); // clamped to the minimum
    CHECK(yse_dsp_scope_capture_get_capacity(p) == YSE_DSP_SCOPE_MIN_SIZE);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_scope_capture_create(64);
    CHECK(yse_dsp_scope_capture_get_capacity(p) == 64u);
    CHECK(yse_dsp_scope_capture_get_size(p) == 64u); // the default, held to the capacity
    CHECK(yse_dsp_scope_capture_get_period(p) == YSE_DSP_SCOPE_DEFAULT_PERIOD);
    yse_dsp_scope_capture_set_size(p, 1000);
    CHECK(yse_dsp_scope_capture_get_size(p) == 64u);
    yse_dsp_scope_capture_set_size(p, 10);
    yse_dsp_scope_capture_set_period(p, 3);
    yse_dsp_scope_capture_set_delay(p, 2.5f);
    yse_dsp_scope_capture_set_level(p, 0.25f);
    yse_dsp_scope_capture_set_trigger(p, YSE_DSP_SCOPE_TRIGGER_DOWN);
    yse_dsp_scope_capture_set_trigger(p, static_cast<YseDspScopeTrigger>(3)); // ignored
    CHECK(yse_dsp_scope_capture_get_size(p) == 10u);
    CHECK(yse_dsp_scope_capture_get_period(p) == 3u);
    CHECK(yse_dsp_scope_capture_get_delay(p) == 2.5f);
    CHECK(yse_dsp_scope_capture_get_level(p) == 0.25f);
    CHECK(yse_dsp_scope_capture_get_trigger(p) == YSE_DSP_SCOPE_TRIGGER_DOWN);
    CHECK(yse_dsp_scope_capture_get_traces(p) == 0u);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_spectrum_analyzer_create(1000); // a power of two above
    CHECK(yse_dsp_spectrum_analyzer_get_capacity(p) == 1024u);
    CHECK(yse_dsp_spectrum_analyzer_get_size(p) == 1024u);
    yse_dsp_spectrum_analyzer_set_size(p, 100);
    CHECK(yse_dsp_spectrum_analyzer_get_size(p) == 128u);
    CHECK(yse_dsp_spectrum_analyzer_get_interval(p) == 20.f);
    yse_dsp_spectrum_analyzer_set_interval(p, 1.f);
    CHECK(yse_dsp_spectrum_analyzer_get_interval(p) == 10.f);
    yse_dsp_spectrum_analyzer_set_interval(p, NaN);
    CHECK(yse_dsp_spectrum_analyzer_get_interval(p) == 10.f);
    yse_dsp_spectrum_analyzer_set_mode(p, YSE_DSP_SPECTRUM_PHASE);
    yse_dsp_spectrum_analyzer_set_mode(p, static_cast<YseDspSpectrumMode>(2)); // ignored
    CHECK(yse_dsp_spectrum_analyzer_get_mode(p) == YSE_DSP_SPECTRUM_PHASE);
    CHECK(yse_dsp_spectrum_analyzer_get_frames(p) == 0u);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_plot_data_create(0, 0); // clamped to 1 and 1
    CHECK(yse_dsp_plot_data_get_plots(p) == 1u);
    CHECK(yse_dsp_plot_data_get_capacity(p) == 1u);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_plot_data_create(2, 16);
    CHECK(yse_dsp_plot_data_get_plots(p) == 2u);
    CHECK(yse_dsp_plot_data_get_capacity(p) == 16u);
    CHECK(yse_dsp_plot_data_get_points(p) == YSE_DSP_PLOT_DEFAULT_POINTS);
    CHECK(yse_dsp_plot_data_get_frames(p) == 1u);
    yse_dsp_plot_data_set_points(p, 100);
    CHECK(yse_dsp_plot_data_get_points(p) == 16u);
    yse_dsp_plot_data_set_frame_size(p, 40);
    CHECK(yse_dsp_plot_data_get_frame_size(p) == 40u);
    yse_dsp_plot_data_set_frame_rate(p, 5000.f);
    CHECK(yse_dsp_plot_data_get_frame_rate(p) == 1000.f);
    {
      const float values[3] = {1.f, NaN, 3.f};
      CHECK(yse_dsp_plot_data_set(p, 1, values, 3, 14) == 1); // cut at the capacity
      CHECK(yse_dsp_plot_data_get_value(p, 1, 14) == 1.f);
      CHECK(yse_dsp_plot_data_get_value(p, 1, 15) == 0.f); // NaN is 0
      CHECK(yse_dsp_plot_data_set(p, 2, values, 3, 0) == 0); // past the plots
      CHECK(yse_dsp_plot_data_set(p, 0, nullptr, 0, 0) == 1);
    }
    YseDspPlotAxis axis{};
    CHECK(yse_dsp_plot_data_get_domain(p, 0, &axis) == 1);
    CHECK(axis.start == 0.f);
    CHECK(axis.end == 1.f);
    CHECK(axis.scale == YSE_DSP_PLOT_LINEAR);
    yse_dsp_plot_data_set_domain(p, 1, 20.f, 20000.f, YSE_DSP_PLOT_LOG);
    yse_dsp_plot_data_set_range(p, 1, -60.f, NaN, YSE_DSP_PLOT_LINEAR);
    yse_dsp_plot_data_set_range(p, 1, 5.f, 6.f, static_cast<YseDspPlotScale>(2)); // ignored
    CHECK(yse_dsp_plot_data_get_domain(p, 1, &axis) == 1);
    CHECK(axis.start == 20.f);
    CHECK(axis.end == 20000.f);
    CHECK(axis.scale == YSE_DSP_PLOT_LOG);
    CHECK(yse_dsp_plot_data_get_range(p, 1, &axis) == 1);
    CHECK(axis.start == -60.f);
    CHECK(axis.end == 1.f); // NaN left it
    CHECK(axis.scale == YSE_DSP_PLOT_LINEAR);
    yse_dsp_plot_data_set_x_origin(p, 1, 100.f);
    yse_dsp_plot_data_set_y_origin(p, 1, -6.f);
    yse_dsp_plot_data_set_filter(p, 1, YSE_DSP_PLOT_FILTER_ATODB);
    yse_dsp_plot_data_set_filter(p, 1, static_cast<YseDspPlotFilter>(2)); // ignored
    CHECK(yse_dsp_plot_data_get_x_origin(p, 1) == 100.f);
    CHECK(yse_dsp_plot_data_get_y_origin(p, 1) == -6.f);
    CHECK(yse_dsp_plot_data_get_filter(p, 1) == YSE_DSP_PLOT_FILTER_ATODB);
    yse_dsp_plot_data_set_region(p, 1, 3, 10.f, 250.f);
    CHECK(yse_dsp_plot_data_get_region_channel(p, 1) == 3u);
    CHECK(yse_dsp_plot_data_get_region_start(p, 1) == 10.f);
    CHECK(yse_dsp_plot_data_get_region_end(p, 1) == 250.f);
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api analysis: every process entry point matches the engine class") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Signal(LENGTH);
    const std::vector<float> low = Signal(LENGTH, 0.2f, -0.2f, 3u);
    const std::vector<float> high = Signal(LENGTH, 0.2f, 0.25f, 4u);
    const std::vector<float> gate = Gate(LENGTH, 50, 20);
    const std::vector<float> resets = Gate(LENGTH, 97, 1);

    SUBCASE("direction") {
      YseDspProcessor* p = yse_dsp_direction_create();
      DSP::direction e;
      std::vector<float> c(LENGTH), r(LENGTH);
      DSP::buffer a;
      REQUIRE(yse_dsp_direction_process(p, in.data(), c.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(e(Stage(a, in, o, n)), r, o, n); });
      CHECK(Moves(c));
      CheckSame(c, r);
      CHECK(yse_dsp_direction_get_previous(p) == e.previous());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("schmittTrigger, at the thresholds and per sample") {
      YseDspProcessor* p = yse_dsp_schmitt_trigger_create();
      yse_dsp_schmitt_trigger_set_low(p, -0.2f);
      yse_dsp_schmitt_trigger_set_high(p, 0.3f);
      DSP::schmittTrigger e;
      e.low(-0.2f).high(0.3f);
      std::vector<float> c(LENGTH), r(LENGTH);
      DSP::buffer a, b, d;
      REQUIRE(yse_dsp_schmitt_trigger_process(p, in.data(), c.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(e(Stage(a, in, o, n)), r, o, n); });
      CHECK(Moves(c));
      CheckSame(c, r);
      REQUIRE(yse_dsp_schmitt_trigger_process_signal(p, in.data(), low.data(), high.data(),
                                                     c.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        Put(e(Stage(a, in, o, n), Stage(b, low, o, n), Stage(d, high, o, n)), r, o, n);
      });
      CHECK(Moves(c));
      CheckSame(c, r);
      CHECK(yse_dsp_schmitt_trigger_get_low(p) == e.low());
      CHECK(yse_dsp_schmitt_trigger_get_high(p) == e.high());
      CHECK(yse_dsp_schmitt_trigger_is_on(p) == (e.on() ? 1 : 0));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("edgeDetector: the rises and falls of every block") {
      YseDspProcessor* p = yse_dsp_edge_detector_create();
      DSP::edgeDetector e;
      std::vector<unsigned> rises(LENGTH, 0u), falls(LENGTH, 0u), er, ef;
      REQUIRE(yse_dsp_edge_detector_process(p, gate.data(), rises.data(), LENGTH, falls.data(),
                                            LENGTH, LENGTH) == LENGTH);
      DSP::buffer a;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, gate, o, n));
        Collect(er, o, [&] { return e.rises(); }, [&](UInt i) { return e.rise(i); });
        Collect(ef, o, [&] { return e.falls(); }, [&](UInt i) { return e.fall(i); });
      });
      REQUIRE(yse_dsp_edge_detector_get_rises(p) == er.size());
      REQUIRE(yse_dsp_edge_detector_get_falls(p) == ef.size());
      CHECK(PastFirstBlock(er));
      CHECK(PastFirstBlock(ef));
      rises.resize(er.size());
      falls.resize(ef.size());
      CHECK(rises == er);
      CHECK(falls == ef);
      CHECK(yse_dsp_edge_detector_is_high(p) == (e.high() ? 1 : 0));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("spikeDetector: positions and intervals") {
      YseDspProcessor* p = yse_dsp_spike_detector_create();
      yse_dsp_spike_detector_set_refractory(p, 0.9f); // 43.2 samples: every other rise
      DSP::spikeDetector e;
      e.refractory(0.9f);
      const std::vector<float> pulses = Gate(LENGTH, 30, 3);
      std::vector<unsigned> pos(LENGTH, 0u), ep;
      std::vector<float> iv(LENGTH, 0.f), ei;
      REQUIRE(yse_dsp_spike_detector_process(p, pulses.data(), pos.data(), iv.data(), LENGTH,
                                             LENGTH) == LENGTH);
      DSP::buffer a;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, pulses, o, n));
        for (UInt i = 0; i < e.spikes(); ++i) {
          ep.push_back(o + e.spike(i));
          ei.push_back(e.interval(i));
        }
      });
      REQUIRE(yse_dsp_spike_detector_get_spikes(p) == ep.size());
      CHECK(PastFirstBlock(ep));
      pos.resize(ep.size());
      iv.resize(ei.size());
      CHECK(pos == ep);
      CheckSame(iv, ei);
      CHECK(yse_dsp_spike_detector_get_last_interval(p) == e.lastInterval());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("zeroCrossing: counts, clicks and positions") {
      YseDspProcessor* p = yse_dsp_zero_crossing_create();
      yse_dsp_zero_crossing_set_volume(p, 0.5f);
      DSP::zeroCrossing e;
      e.volume(0.5f);
      std::vector<float> counts(LENGTH), clicks(LENGTH), rc(LENGTH), rk(LENGTH);
      std::vector<unsigned> pos(LENGTH, 0u), ep;
      REQUIRE(yse_dsp_zero_crossing_process(p, in.data(), counts.data(), clicks.data(), pos.data(),
                                            LENGTH, LENGTH) == LENGTH);
      DSP::buffer a;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, in, o, n));
        Put(e.counts(), rc, o, n);
        Put(e.clicks(), rk, o, n);
        Collect(ep, o, [&] { return e.crossings(); }, [&](UInt i) { return e.crossing(i); });
      });
      CHECK(Moves(counts));
      CHECK(Moves(clicks));
      CheckSame(counts, rc);
      CheckSame(clicks, rk);
      REQUIRE(yse_dsp_zero_crossing_get_crossings(p) == ep.size());
      CHECK(PastFirstBlock(ep));
      pos.resize(ep.size());
      CHECK(pos == ep);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("peakTracker: interval reports, signed") {
      YseDspProcessor* p = yse_dsp_peak_tracker_create();
      yse_dsp_peak_tracker_set_interval(p, 1.1f); // 52.8 samples
      yse_dsp_peak_tracker_set_signed(p, 1);
      DSP::peakTracker e;
      e.interval(1.1f).signedPeak(true);
      std::vector<unsigned> pos(LENGTH, 0u), ep;
      std::vector<float> values(LENGTH, 0.f), ev;
      REQUIRE(yse_dsp_peak_tracker_process(p, in.data(), pos.data(), values.data(), LENGTH,
                                           LENGTH) == LENGTH);
      DSP::buffer a;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, in, o, n));
        for (UInt i = 0; i < e.reports(); ++i) {
          ep.push_back(o + e.report(i));
          ev.push_back(e.value(i));
        }
      });
      REQUIRE(yse_dsp_peak_tracker_get_reports(p) == ep.size());
      CHECK(PastFirstBlock(ep));
      pos.resize(ep.size());
      values.resize(ev.size());
      CHECK(pos == ep);
      CheckSame(values, ev);
      CHECK(Moves(values));
      CHECK(yse_dsp_peak_tracker_get_last_report(p) == e.lastReport());
      CHECK(yse_dsp_peak_tracker_get_peak(p) == e.peak());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("movingAverage: the window and the accumulation") {
      for (int mode = 0; mode <= 2; ++mode) {
        INFO("mode " << mode);
        YseDspProcessor* p = yse_dsp_moving_average_create(64);
        yse_dsp_moving_average_set_length(p, 20);
        yse_dsp_moving_average_set_mode(p, static_cast<YseDspAverageMode>(mode));
        DSP::movingAverage e(64);
        e.length(20).mode(static_cast<DSP::movingAverage::MODE>(mode));
        std::vector<float> c(LENGTH), r(LENGTH);
        DSP::buffer a;
        REQUIRE(yse_dsp_moving_average_process(p, in.data(), c.data(), LENGTH) == LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(e(Stage(a, in, o, n)), r, o, n); });
        CHECK(Moves(c));
        CheckSame(c, r);
        CHECK(yse_dsp_moving_average_get_value(p) == e.value());
        REQUIRE(yse_dsp_moving_average_accumulate(p, high.data(), LENGTH) == LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) { e.accumulate(Stage(a, high, o, n)); });
        CHECK(yse_dsp_moving_average_get_accumulated_samples(p) == e.accumulatedSamples());
        CHECK(yse_dsp_moving_average_get_accumulated(p) == e.accumulated());
        CHECK(yse_dsp_moving_average_take(p) == e.take());
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("minMax: both outputs, with and without a reset signal") {
      YseDspProcessor* p = yse_dsp_min_max_create();
      DSP::minMax e;
      std::vector<float> lo(LENGTH), hi(LENGTH), rl(LENGTH), rh(LENGTH);
      DSP::buffer a, b;
      REQUIRE(yse_dsp_min_max_process(p, in.data(), lo.data(), hi.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, in, o, n));
        Put(e.minimumSignal(), rl, o, n);
        Put(e.maximumSignal(), rh, o, n);
      });
      CHECK(Moves(lo));
      CHECK(Moves(hi));
      CheckSame(lo, rl);
      CheckSame(hi, rh);
      REQUIRE(yse_dsp_min_max_process_reset(p, in.data(), resets.data(), lo.data(), hi.data(),
                                            LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        e(Stage(a, in, o, n), Stage(b, resets, o, n));
        Put(e.minimumSignal(), rl, o, n);
        Put(e.maximumSignal(), rh, o, n);
      });
      CheckSame(lo, rl);
      CheckSame(hi, rh);
      CHECK(yse_dsp_min_max_get_minimum(p) == e.minimum());
      CHECK(yse_dsp_min_max_get_maximum(p) == e.maximum());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("loudnessMeter: one channel and several") {
      const std::vector<float> left = Sine(LONG, 997.f, 0.4f);
      const std::vector<float> right = Signal(LONG, 0.3f, 0.f, 9u);
      YseDspProcessor* p = yse_dsp_loudness_meter_create(3);
      yse_dsp_loudness_meter_set_weight(p, 1, 1.41f);
      yse_dsp_loudness_meter_set_interval(p, 7.f);
      DSP::loudnessMeter e(3);
      e.weight(1, 1.41f).interval(7.f);
      // Two channels given, the third reads as silence.
      const float* chans[2] = {left.data(), right.data()};
      REQUIRE(yse_dsp_loudness_meter_process_channels(p, chans, 2, LONG) == LONG);
      DSP::buffer a, b;
      unsigned reports = 0;
      Blocks(LONG, [&](unsigned o, unsigned n) {
        DSP::buffer* ins[2] = {&Stage(a, left, o, n), &Stage(b, right, o, n)};
        reports += e(ins, 2);
      });
      CHECK(reports > 1u);
      CHECK(yse_dsp_loudness_meter_get_reports(p) == reports);
      CHECK(yse_dsp_loudness_meter_get_momentary(p) == e.momentary());
      CHECK(yse_dsp_loudness_meter_get_short_term(p) == e.shortTerm());
      CHECK(yse_dsp_loudness_meter_get_max_momentary(p) == e.maxMomentary());
      CHECK(yse_dsp_loudness_meter_get_max_short_term(p) == e.maxShortTerm());
      CHECK(yse_dsp_loudness_meter_get_integrated(p) == e.integrated());
      CHECK(yse_dsp_loudness_meter_get_loudness_range(p) == e.loudnessRange());
      CHECK(yse_dsp_loudness_meter_get_gated_blocks(p) == e.gatedBlocks());
      CHECK(yse_dsp_loudness_meter_get_gated_blocks(p) > 0ull);
      CHECK(yse_dsp_loudness_meter_get_integrated(p) > YSE_DSP_LOUDNESS_SILENCE);
      for (unsigned ch = 0; ch <= 3; ++ch) {
        CHECK(yse_dsp_loudness_meter_get_peak(p, ch) == e.peak(ch));
        CHECK(yse_dsp_loudness_meter_get_true_peak(p, ch) == e.truePeak(ch));
      }
      // A NULL channel 0 reads as silence, as a missing channel does.
      const float* gap[2] = {nullptr, right.data()};
      REQUIRE(yse_dsp_loudness_meter_process_channels(p, gap, 2, LENGTH) == LENGTH);
      const std::vector<float> zeros(LENGTH, 0.f);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        DSP::buffer* ins[2] = {&Stage(a, zeros, o, n), &Stage(b, right, o, n)};
        e(ins, 2);
      });
      CHECK(yse_dsp_loudness_meter_get_momentary(p) == e.momentary());
      CHECK(yse_dsp_loudness_meter_get_peak(p, 2) == e.peak(2));
      yse_dsp_processor_destroy(p);

      // The one-channel call.
      p = yse_dsp_loudness_meter_create(1);
      DSP::loudnessMeter one(1);
      REQUIRE(yse_dsp_loudness_meter_process(p, left.data(), LONG) == LONG);
      Blocks(LONG, [&](unsigned o, unsigned n) { one(Stage(a, left, o, n)); });
      CHECK(yse_dsp_loudness_meter_get_momentary(p) == one.momentary());
      CHECK(yse_dsp_loudness_meter_get_integrated(p) == one.integrated());
      CHECK(yse_dsp_loudness_meter_get_true_peak(p, 0) == one.truePeak(0));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("pitchTracker: a tone, its counts over the whole call") {
      std::vector<float> tone = Sine(LONG, 440.f, 0.5f);
      // A second note half-way, for an onset in a block that is not the last.
      for (unsigned i = LONG / 2; i < LONG; ++i)
        tone[i] = 0.5f * std::sin(2.f * 3.14159265f * 660.f * static_cast<float>(i) / 48000.f);
      YseDspProcessor* p = yse_dsp_pitch_tracker_create();
      DSP::pitchTracker e;
      REQUIRE(yse_dsp_pitch_tracker_process(p, tone.data(), LONG) == LONG);
      unsigned analyses = 0, reports = 0, pitches = 0, onsets = 0;
      Blocks(LONG, [&](unsigned o, unsigned n) {
        analyses += e(tone.data() + o, n);
        reports += e.reports();
        pitches += e.pitches();
        onsets += e.onsets();
      });
      CHECK(analyses > 10u);
      CHECK(onsets > 1u);
      CHECK(yse_dsp_pitch_tracker_get_analyses(p) == analyses);
      CHECK(yse_dsp_pitch_tracker_get_reports(p) == reports);
      CHECK(yse_dsp_pitch_tracker_get_pitches(p) == pitches);
      CHECK(yse_dsp_pitch_tracker_get_onsets(p) == onsets);
      CHECK(yse_dsp_pitch_tracker_get_frequency(p) == e.frequency());
      CHECK(yse_dsp_pitch_tracker_get_frequency(p) == doctest::Approx(660.f).epsilon(0.01));
      CHECK(yse_dsp_pitch_tracker_get_clarity(p) == e.clarity());
      CHECK(yse_dsp_pitch_tracker_get_amplitude(p) == e.amplitude());
      CHECK(yse_dsp_pitch_tracker_is_voiced(p) == (e.voiced() ? 1 : 0));
      CHECK(yse_dsp_pitch_tracker_get_onset_frequency(p) == e.onsetFrequency());
      CHECK(yse_dsp_pitch_tracker_get_onset_peak(p) == e.onsetPeak());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("sampleCapture: every sample and the indices of each block") {
      for (int mode = 0; mode <= 1; ++mode) {
        INFO("mode " << mode);
        const unsigned idx[6] = {3, 0, 3, 100, 127, 200};
        YseDspProcessor* p = yse_dsp_sample_capture_create(300);
        yse_dsp_sample_capture_set_mode(p, static_cast<YseDspCaptureMode>(mode));
        DSP::sampleCapture e(300);
        e.mode(static_cast<DSP::sampleCapture::MODE>(mode));
        DSP::buffer a;
        REQUIRE(yse_dsp_sample_capture_process(p, in.data(), LENGTH) == LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) { e(Stage(a, in, o, n)); });
        std::vector<float> c(300), r(300);
        CHECK(yse_dsp_sample_capture_get_samples(p, c.data(), 300) == e.size());
        e.copy(r.data(), 300);
        CheckSame(c, r);
        // With block positions: four of them in every full block, two in
        // the short one (100 and 127 are past it), 200 in none.
        yse_dsp_sample_capture_clear(p);
        e.clear();
        REQUIRE(yse_dsp_sample_capture_set_indices(p, idx, 6) == 1);
        e.indices(idx, 6);
        REQUIRE(yse_dsp_sample_capture_process(p, in.data(), LENGTH) == LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) { e(Stage(a, in, o, n)); });
        CHECK(yse_dsp_sample_capture_get_size(p) == 14u);
        CHECK(yse_dsp_sample_capture_get_samples(p, c.data(), 300) == e.size());
        e.copy(r.data(), 300);
        CheckSame(c, r);
        CHECK(yse_dsp_sample_capture_get_sample(p, 4) == in[BLOCK]); // the next block's first
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("levelMeter: windows, ballistics, hold and the reading") {
      const std::vector<float> loud = Signal(LENGTH, 2.5f);
      YseDspProcessor* p = yse_dsp_level_meter_create();
      yse_dsp_level_meter_set_detector(p, YSE_DSP_LEVEL_RMS);
      yse_dsp_level_meter_set_interval(p, 1.3f);
      yse_dsp_level_meter_set_attack(p, 2.f);
      yse_dsp_level_meter_set_release(p, 20.f);
      yse_dsp_level_meter_set_hold(p, 4.f);
      yse_dsp_level_meter_set_offset(p, -3.f);
      DSP::levelMeter e;
      e.detector(DSP::levelMeter::RMS)
          .interval(1.3f)
          .attack(2.f)
          .release(20.f)
          .hold(4.f)
          .offset(-3.f);
      REQUIRE(yse_dsp_level_meter_process(p, loud.data(), LENGTH) == LENGTH);
      DSP::buffer a;
      unsigned reports = 0;
      Blocks(LENGTH, [&](unsigned o, unsigned n) { reports += e(Stage(a, loud, o, n)); });
      CHECK(reports > 3u);
      CHECK(yse_dsp_level_meter_get_reports(p) == reports);
      CHECK(yse_dsp_level_meter_get_level(p) == e.level());
      CHECK(yse_dsp_level_meter_get_level_db(p) == e.levelDb());
      CHECK(yse_dsp_level_meter_get_peak_hold(p) == e.peakHold());
      CHECK(yse_dsp_level_meter_get_peak_hold_db(p) == e.peakHoldDb());
      CHECK(yse_dsp_level_meter_is_clipped(p) == (e.clipped() ? 1 : 0));
      CHECK(yse_dsp_level_meter_is_clipped(p) == 1);
      YseDspLevelReading r{};
      REQUIRE(yse_dsp_level_meter_read(p, &r) == 1);
      CHECK(r.level == e.level());
      CHECK(r.level_db == e.levelDb());
      CHECK(r.peak_hold == e.peakHold());
      CHECK(r.peak_hold_db == e.peakHoldDb());
      CHECK(r.clipped == 1);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("scopeCapture: waveform and X/Y traces, triggered") {
      YseDspProcessor* p = yse_dsp_scope_capture_create(16);
      yse_dsp_scope_capture_set_size(p, 8);
      yse_dsp_scope_capture_set_period(p, 5);
      yse_dsp_scope_capture_set_trigger(p, YSE_DSP_SCOPE_TRIGGER_UP);
      yse_dsp_scope_capture_set_level(p, 0.1f);
      DSP::scopeCapture e(16);
      e.size(8).period(5).trigger(DSP::scopeCapture::UP).level(0.1f);
      DSP::buffer a, b;
      REQUIRE(yse_dsp_scope_capture_process(p, in.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { e(Stage(a, in, o, n)); });
      CHECK(yse_dsp_scope_capture_get_traces(p) == e.traces());
      CHECK(e.traces() > 2u);
      std::vector<float> lo(16, 77.f), hi(16, 77.f);
      YseDspScopeTrace t{};
      REQUIRE(yse_dsp_scope_capture_acquire(p, &t, lo.data(), hi.data(), 16) == 8u);
      const DSP::scopeCapture::trace et = e.acquire();
      CHECK(t.serial == et.serial);
      CHECK(t.size == et.size);
      CHECK(t.mode == YSE_DSP_SCOPE_WAVEFORM);
      for (unsigned i = 0; i < 8; ++i) {
        CHECK(Same(lo[i], et.first[i]));
        CHECK(Same(hi[i], et.second[i]));
      }
      CHECK(lo[8] == 77.f);

      // X/Y, a switch that drops the trace being captured, as the engine's.
      const std::vector<float> y = Signal(LENGTH, 1.f, 0.f, 99u);
      REQUIRE(yse_dsp_scope_capture_process_xy(p, in.data(), y.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { e(Stage(a, in, o, n), Stage(b, y, o, n)); });
      CHECK(yse_dsp_scope_capture_get_traces(p) == e.traces());
      REQUIRE(yse_dsp_scope_capture_acquire(p, &t, lo.data(), hi.data(), 3) == 8u);
      const DSP::scopeCapture::trace ex = e.acquire();
      CHECK(t.serial == ex.serial);
      CHECK(t.mode == YSE_DSP_SCOPE_XY);
      for (unsigned i = 0; i < 3; ++i) {
        CHECK(Same(lo[i], ex.first[i]));
        CHECK(Same(hi[i], ex.second[i]));
      }
      CHECK(Same(lo[3], et.first[3])); // past the capacity: the earlier copy is untouched
      // held() describes the same trace without taking a newer one.
      YseDspScopeTrace h{};
      std::vector<float> l2(16), h2(16);
      REQUIRE(yse_dsp_scope_capture_held(p, &h, l2.data(), h2.data(), 16) == 8u);
      CHECK(h.serial == ex.serial);
      for (unsigned i = 0; i < 8; ++i)
        CHECK(Same(l2[i], ex.first[i]));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("spectrumAnalyzer: amplitude and phase frames, and spectral bins") {
      const std::vector<float> tone = Sine(LONG, 3000.f, 0.5f);
      for (int mode = 0; mode <= 1; ++mode) {
        INFO("mode " << mode);
        YseDspProcessor* p = yse_dsp_spectrum_analyzer_create(256);
        yse_dsp_spectrum_analyzer_set_size(p, 64);
        yse_dsp_spectrum_analyzer_set_interval(p, 11.f);
        yse_dsp_spectrum_analyzer_set_mode(p, static_cast<YseDspSpectrumMode>(mode));
        DSP::spectrumAnalyzer e(256);
        e.size(64).interval(11.f).mode(static_cast<DSP::spectrumAnalyzer::MODE>(mode));
        DSP::buffer a;
        REQUIRE(yse_dsp_spectrum_analyzer_process(p, tone.data(), LONG) == LONG);
        Blocks(LONG, [&](unsigned o, unsigned n) { e(Stage(a, tone, o, n)); });
        CHECK(yse_dsp_spectrum_analyzer_get_frames(p) == e.frames());
        CHECK(e.frames() > 50u);
        std::vector<float> bins(256, 77.f);
        YseDspSpectrumFrame f{};
        REQUIRE(yse_dsp_spectrum_analyzer_acquire(p, &f, bins.data(), 256) == 33u);
        const DSP::spectrumAnalyzer::frame ef = e.acquire();
        CHECK(f.serial == ef.serial);
        CHECK(f.size == 64u);
        CHECK(f.bins == ef.bins);
        CHECK(f.rate == ef.rate);
        CHECK(static_cast<int>(f.mode) == mode);
        for (unsigned i = 0; i < 33; ++i)
          CHECK(Same(bins[i], ef.values[i]));
        CHECK(bins[33] == 77.f);
        yse_dsp_processor_destroy(p);
      }

      // Spectral bins in two runs: the frame publishes at its last bin. Phase
      // mode too, where swapping the real and imaginary parts would show.
      for (int mode = 0; mode <= 1; ++mode) {
        INFO("bins, mode " << mode);
        YseDspProcessor* p = yse_dsp_spectrum_analyzer_create(64);
        yse_dsp_spectrum_analyzer_set_mode(p, static_cast<YseDspSpectrumMode>(mode));
        DSP::spectrumAnalyzer e(64);
        e.mode(static_cast<DSP::spectrumAnalyzer::MODE>(mode));
        std::vector<float> re(17), im(17);
        for (unsigned i = 0; i < 17; ++i) {
          re[i] = 0.1f * static_cast<float>(i);
          im[i] = -0.05f * static_cast<float>(i) + 0.3f;
        }
        for (int frame = 0; frame < 40; ++frame) {
          CHECK(yse_dsp_spectrum_analyzer_process_bins(p, re.data(), im.data(), 0, 9, 17, 32,
                                                       480) == 1);
          CHECK(yse_dsp_spectrum_analyzer_process_bins(p, re.data() + 9, im.data() + 9, 9, 8, 17,
                                                       32, 480) == 1);
          DSP::stftFrame run;
          run.real = re.data();
          run.imaginary = im.data();
          run.firstBin = 0;
          run.count = 9;
          run.bins = 17;
          run.size = 32;
          run.hop = 480;
          e(run);
          run.real = re.data() + 9;
          run.imaginary = im.data() + 9;
          run.firstBin = 9;
          run.count = 8;
          e(run);
        }
        CHECK(yse_dsp_spectrum_analyzer_get_frames(p) == e.frames());
        CHECK(e.frames() > 10u);
        std::vector<float> bins(17);
        YseDspSpectrumFrame f{};
        REQUIRE(yse_dsp_spectrum_analyzer_acquire(p, &f, bins.data(), 17) == 17u);
        const DSP::spectrumAnalyzer::frame ef = e.acquire();
        CHECK(f.serial == ef.serial);
        CHECK(f.size == 32u);
        for (unsigned i = 0; i < 17; ++i)
          CHECK(Same(bins[i], ef.values[i]));
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("plotData: signal frames, set values, the filter and the published frame") {
      YseDspProcessor* p = yse_dsp_plot_data_create(2, 16);
      yse_dsp_plot_data_set_points(p, 8);
      yse_dsp_plot_data_set_frame_rate(p, 1000.f); // a frame every 48 samples
      yse_dsp_plot_data_set_filter(p, 0, YSE_DSP_PLOT_FILTER_ATODB);
      yse_dsp_plot_data_set_range(p, 1, -2.f, 2.f, YSE_DSP_PLOT_LINEAR);
      DSP::plotData e(2, 16);
      e.points(8).frameRate(1000.f).filter(0, DSP::plotData::ATODB);
      e.range(1, -2.f, 2.f, DSP::plotData::LINEAR);
      const float set[5] = {0.5f, 0.25f, 1.f, 2.f, 0.125f};
      REQUIRE(yse_dsp_plot_data_set(p, 0, set, 5, 1) == 1);
      e.set(0, set, 5, 1);
      DSP::buffer a;
      REQUIRE(yse_dsp_plot_data_process(p, 1, in.data(), LENGTH) == LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { e(1, Stage(a, in, o, n)); });
      for (unsigned i = 0; i < 16; ++i) {
        CHECK(Same(yse_dsp_plot_data_get_value(p, 0, i), e.value(0, i)));
        CHECK(Same(yse_dsp_plot_data_get_value(p, 1, i), e.value(1, i)));
      }
      CHECK(yse_dsp_plot_data_publish(p) == 1);
      CHECK(e.publish());
      CHECK(yse_dsp_plot_data_publish(p) == 0); // nothing changed since
      CHECK(yse_dsp_plot_data_get_frames(p) == e.frames());
      YseDspPlotFrame f{};
      REQUIRE(yse_dsp_plot_data_acquire(p, &f) == 2u);
      const DSP::plotData::frame ef = e.acquire();
      CHECK(f.serial == ef.serial);
      CHECK(f.plots == 2u);
      CHECK(f.points == 8u);
      for (unsigned plot = 0; plot < 2; ++plot) {
        INFO("plot " << plot);
        YseDspPlotSubplot s{};
        std::vector<float> values(16, 77.f);
        REQUIRE(yse_dsp_plot_data_held_subplot(p, plot, &s, values.data(), 16) == 8u);
        const DSP::plotData::subplot& es = ef.subplots[plot];
        CHECK(s.domain.start == es.domain.start);
        CHECK(s.domain.end == es.domain.end);
        CHECK(s.range.start == es.range.start);
        CHECK(s.range.end == es.range.end);
        CHECK(static_cast<int>(s.range.scale) == static_cast<int>(es.range.scale));
        CHECK(static_cast<int>(s.filter) == static_cast<int>(es.filter));
        CHECK(s.x_origin == es.xOrigin);
        CHECK(s.y_origin == es.yOrigin);
        for (unsigned i = 0; i < 8; ++i)
          CHECK(Same(values[i], es.values[i]));
        CHECK(values[8] == 77.f);
      }
      CHECK(yse_dsp_plot_data_held_subplot(p, 2, nullptr, nullptr, 0) == 0u);
      YseDspPlotFrame h{};
      CHECK(yse_dsp_plot_data_held(p, &h) == 2u);
      CHECK(h.points == 8u);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api analysis: the per-sample calls match the engine's") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Signal(300);
    const std::vector<float> gate = Gate(300, 23, 7);

    YseDspProcessor* dir = yse_dsp_direction_create();
    YseDspProcessor* ed = yse_dsp_edge_detector_create();
    YseDspProcessor* st = yse_dsp_schmitt_trigger_create();
    YseDspProcessor* sp = yse_dsp_spike_detector_create();
    YseDspProcessor* zc = yse_dsp_zero_crossing_create();
    YseDspProcessor* pk = yse_dsp_peak_tracker_create();
    YseDspProcessor* ma = yse_dsp_moving_average_create(16);
    YseDspProcessor* mm = yse_dsp_min_max_create();
    YseDspProcessor* sc = yse_dsp_sample_capture_create(64);
    YseDspProcessor* lv = yse_dsp_level_meter_create();
    yse_dsp_schmitt_trigger_set_low(st, -0.1f);
    yse_dsp_schmitt_trigger_set_high(st, 0.2f);
    yse_dsp_peak_tracker_set_interval(pk, 0.3f);
    yse_dsp_level_meter_set_interval(lv, 0.5f);
    yse_dsp_level_meter_set_release(lv, 3.f);

    DSP::direction edir;
    DSP::edgeDetector eed;
    DSP::schmittTrigger est;
    est.low(-0.1f).high(0.2f);
    DSP::spikeDetector esp;
    DSP::zeroCrossing ezc;
    DSP::peakTracker epk;
    epk.interval(0.3f);
    DSP::movingAverage ema(16);
    DSP::minMax emm;
    DSP::sampleCapture esc(64);
    DSP::levelMeter elv;
    elv.interval(0.5f).release(3.f);

    unsigned events = 0;
    for (unsigned i = 0; i < 300; ++i) {
      const float x = in[i];
      const float g = gate[i];
      CHECK(Same(yse_dsp_direction_process_sample(dir, x), edir.process(x)));
      const int edge = yse_dsp_edge_detector_process_sample(ed, g);
      CHECK(edge == eed.process(g));
      CHECK(Same(yse_dsp_schmitt_trigger_process_sample(st, x), est.process(x)));
      const int spike = yse_dsp_spike_detector_process_sample(sp, g);
      CHECK(spike == (esp.process(g) ? 1 : 0));
      CHECK(yse_dsp_zero_crossing_process_sample(zc, x) == (ezc.process(x) ? 1 : 0));
      const int report = yse_dsp_peak_tracker_process_sample(pk, x);
      CHECK(report == (epk.process(x) ? 1 : 0));
      CHECK(Same(yse_dsp_moving_average_process_sample(ma, x), ema.process(x)));
      yse_dsp_min_max_process_sample_reset(mm, x, g);
      emm.process(x, g);
      yse_dsp_sample_capture_process_sample(sc, x);
      esc.process(x);
      const int window = yse_dsp_level_meter_process_sample(lv, x);
      CHECK(window == (elv.process(x) ? 1 : 0));
      events += static_cast<unsigned>(edge != 0) + static_cast<unsigned>(spike) +
                static_cast<unsigned>(report) + static_cast<unsigned>(window);
    }
    CHECK(events > 20u);
    CHECK(yse_dsp_spike_detector_get_last_interval(sp) == esp.lastInterval());
    CHECK(yse_dsp_peak_tracker_get_last_report(pk) == epk.lastReport());
    CHECK(yse_dsp_min_max_get_minimum(mm) == emm.minimum());
    CHECK(yse_dsp_min_max_get_maximum(mm) == emm.maximum());
    CHECK(yse_dsp_sample_capture_get_sample(sc, 63) == esc.sample(63));
    CHECK(yse_dsp_level_meter_get_level(lv) == elv.level());
    // A per-sample call publishes its reading too.
    YseDspLevelReading r{};
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level == elv.level());
    // ... and leaves the reports of the last process call, as the engine's
    // leaves its block report.
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 0u);
    CHECK(yse_dsp_level_meter_get_reports(lv) == 0u);

    for (YseDspProcessor* p : {dir, ed, st, sp, zc, pk, ma, mm, sc, lv})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api analysis: reports cover the whole call") {
    TestHelpers::ScopedSampleRate rate(48000);
    // A gate that rises once, at sample 200: in the second block, which is
    // not the last.
    std::vector<float> gate(LENGTH, 0.f);
    for (unsigned i = 200; i < 260; ++i)
      gate[i] = 1.f;

    YseDspProcessor* ed = yse_dsp_edge_detector_create();
    unsigned rises[4] = {77, 77, 77, 77}, falls[4] = {77, 77, 77, 77};
    REQUIRE(yse_dsp_edge_detector_process(ed, gate.data(), rises, 4, falls, 4, LENGTH) == LENGTH);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 1u);
    CHECK(yse_dsp_edge_detector_get_falls(ed) == 1u);
    CHECK(rises[0] == 200u);
    CHECK(falls[0] == 260u);
    CHECK(rises[1] == 77u);
    // A refused call leaves the reports as they were.
    CHECK(yse_dsp_edge_detector_process(ed, nullptr, rises, 4, falls, 4, LENGTH) == 0);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 1u);
    // The next call starts over.
    std::vector<float> zeros(LENGTH, 0.f);
    REQUIRE(yse_dsp_edge_detector_process(ed, zeros.data(), rises, 4, falls, 4, LENGTH) == LENGTH);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 0u);
    CHECK(yse_dsp_edge_detector_get_falls(ed) == 0u);
    // set() and reset() clear them.
    REQUIRE(yse_dsp_edge_detector_process(ed, gate.data(), nullptr, 0, nullptr, 0, LENGTH) ==
            LENGTH);
    yse_dsp_edge_detector_set(ed, 0);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 0u);
    REQUIRE(yse_dsp_edge_detector_process(ed, gate.data(), nullptr, 0, nullptr, 0, LENGTH) ==
            LENGTH);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 1u);
    yse_dsp_edge_detector_reset(ed);
    CHECK(yse_dsp_edge_detector_get_falls(ed) == 0u);

    // A host array smaller than the count: whole count, the first entries.
    const std::vector<float> pulses = Gate(LENGTH, 40, 2);
    unsigned few[3] = {77, 77, 77};
    REQUIRE(yse_dsp_edge_detector_process(ed, pulses.data(), few, 2, nullptr, 0, LENGTH) == LENGTH);
    CHECK(yse_dsp_edge_detector_get_rises(ed) == 11u);
    CHECK(few[0] == 0u);
    CHECK(few[1] == 40u);
    CHECK(few[2] == 77u);
    yse_dsp_processor_destroy(ed);

    YseDspProcessor* sp = yse_dsp_spike_detector_create();
    unsigned spos[3] = {77, 77, 77};
    float sint[3] = {77.f, 77.f, 77.f};
    REQUIRE(yse_dsp_spike_detector_process(sp, pulses.data(), spos, sint, 2, LENGTH) == LENGTH);
    CHECK(yse_dsp_spike_detector_get_spikes(sp) == 11u);
    CHECK(spos[1] == 40u);
    CHECK(sint[1] == doctest::Approx(40.f * 1000.f / 48000.f));
    CHECK(spos[2] == 77u);
    CHECK(sint[2] == 77.f);
    yse_dsp_spike_detector_reset(sp);
    CHECK(yse_dsp_spike_detector_get_spikes(sp) == 0u);
    yse_dsp_processor_destroy(sp);

    YseDspProcessor* pk = yse_dsp_peak_tracker_create();
    yse_dsp_peak_tracker_set_interval(pk, 2.f); // 96 samples
    std::vector<unsigned> ppos(8, 77u);
    std::vector<float> pval(8, 77.f);
    REQUIRE(yse_dsp_peak_tracker_process(pk, gate.data(), ppos.data(), pval.data(), 8, LENGTH) ==
            LENGTH);
    CHECK(yse_dsp_peak_tracker_get_reports(pk) == 4u);
    CHECK(ppos[0] == 95u);
    CHECK(ppos[2] == 287u); // a report in the third block
    CHECK(pval[0] == 0.f);
    CHECK(pval[2] == 1.f);
    yse_dsp_peak_tracker_reset(pk);
    CHECK(yse_dsp_peak_tracker_get_reports(pk) == 0u);
    yse_dsp_processor_destroy(pk);

    YseDspProcessor* zc = yse_dsp_zero_crossing_create();
    std::vector<float> wave(LENGTH, 0.5f);
    for (unsigned i = 300; i < LENGTH; ++i)
      wave[i] = -0.5f;
    unsigned zpos[2] = {77, 77};
    REQUIRE(yse_dsp_zero_crossing_process(zc, wave.data(), nullptr, nullptr, zpos, 2, LENGTH) ==
            LENGTH);
    CHECK(yse_dsp_zero_crossing_get_crossings(zc) == 1u);
    CHECK(zpos[0] == 300u);
    yse_dsp_zero_crossing_reset(zc);
    CHECK(yse_dsp_zero_crossing_get_crossings(zc) == 0u);
    yse_dsp_processor_destroy(zc);

    YseDspProcessor* lv = yse_dsp_level_meter_create();
    yse_dsp_level_meter_set_interval(lv, 2.f);
    REQUIRE(yse_dsp_level_meter_process(lv, gate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_level_meter_get_reports(lv) == 4u);
    yse_dsp_level_meter_reset(lv);
    CHECK(yse_dsp_level_meter_get_reports(lv) == 0u);
    yse_dsp_processor_destroy(lv);

    YseDspProcessor* ld = yse_dsp_loudness_meter_create(1);
    yse_dsp_loudness_meter_set_interval(ld, 2.f);
    REQUIRE(yse_dsp_loudness_meter_process(ld, gate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_loudness_meter_get_reports(ld) == 4u);
    yse_dsp_loudness_meter_reset(ld);
    CHECK(yse_dsp_loudness_meter_get_reports(ld) == 0u);
    yse_dsp_processor_destroy(ld);

    YseDspProcessor* pt = yse_dsp_pitch_tracker_create();
    yse_dsp_pitch_tracker_set_period(pt, 100);
    REQUIRE(yse_dsp_pitch_tracker_process(pt, gate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_pitch_tracker_get_analyses(pt) == 4u);
    yse_dsp_pitch_tracker_reset(pt);
    CHECK(yse_dsp_pitch_tracker_get_analyses(pt) == 0u);
    yse_dsp_processor_destroy(pt);
  }

  TEST_CASE("c api analysis: the reader calls copy out a published frame") {
    TestHelpers::ScopedSampleRate rate(48000);
    // levelMeter: the constructor publishes the first reading, every call
    // that changes it publishes the next.
    YseDspProcessor* lv = yse_dsp_level_meter_create();
    YseDspLevelReading r{};
    r.level = 7.f;
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level == 0.f);
    CHECK(r.level_db == YSE_DSP_LEVEL_METER_SILENCE);
    CHECK(r.peak_hold_db == YSE_DSP_LEVEL_METER_SILENCE);
    CHECK(r.clipped == 0);
    yse_dsp_level_meter_set(lv, 0.5f);
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level == 0.5f);
    CHECK(r.peak_hold == 0.5f);
    CHECK(r.level_db == doctest::Approx(-6.0206f));
    yse_dsp_level_meter_set_offset(lv, -20.f);
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level_db == doctest::Approx(13.9794f));
    yse_dsp_level_meter_set_ballistics(lv, YSE_DSP_BALLISTICS_EBU); // offset -14
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level_db == doctest::Approx(7.9794f));
    yse_dsp_level_meter_set_db(lv, 20.f); // 6 dBFS with the -14 dB offset: clips
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.clipped == 1);
    yse_dsp_level_meter_clear(lv);
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.clipped == 0);
    yse_dsp_level_meter_reset(lv);
    REQUIRE(yse_dsp_level_meter_read(lv, &r) == 1);
    CHECK(r.level == 0.f);
    // No publish in between: the reader keeps the reading it holds.
    YseDspLevelReading again{};
    REQUIRE(yse_dsp_level_meter_read(lv, &again) == 1);
    CHECK(Same(again.level, r.level));
    CHECK(Same(again.level_db, r.level_db));
    CHECK(Same(again.peak_hold, r.peak_hold));
    CHECK(Same(again.peak_hold_db, r.peak_hold_db));
    CHECK(again.clipped == r.clipped);
    yse_dsp_processor_destroy(lv);

    // scopeCapture: no trace before the first finishes.
    YseDspProcessor* sco = yse_dsp_scope_capture_create(8);
    yse_dsp_scope_capture_set_size(sco, 4);
    yse_dsp_scope_capture_set_period(sco, 2);
    YseDspScopeTrace t{};
    t.serial = 7u;
    float lo[4] = {77.f, 77.f, 77.f, 77.f}, hi[4] = {77.f, 77.f, 77.f, 77.f};
    CHECK(yse_dsp_scope_capture_acquire(sco, &t, lo, hi, 4) == 0u);
    CHECK(t.serial == 0u);
    CHECK(lo[0] == 77.f);
    const float ramp[8] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    REQUIRE(yse_dsp_scope_capture_process(sco, ramp, 8) == 8u);
    CHECK(yse_dsp_scope_capture_get_traces(sco) == 1u);
    REQUIRE(yse_dsp_scope_capture_acquire(sco, &t, lo, hi, 4) == 4u);
    CHECK(t.serial == 1u);
    for (unsigned i = 0; i < 4; ++i) {
      CHECK(lo[i] == ramp[2 * i]);
      CHECK(hi[i] == ramp[2 * i + 1]);
    }
    yse_dsp_processor_destroy(sco);

    // spectrumAnalyzer: no frame before the first interval.
    YseDspProcessor* sa = yse_dsp_spectrum_analyzer_create(16);
    YseDspSpectrumFrame f{};
    f.serial = 7u;
    float bins[9] = {};
    CHECK(yse_dsp_spectrum_analyzer_acquire(sa, &f, bins, 9) == 0u);
    CHECK(f.serial == 0u);
    yse_dsp_processor_destroy(sa);

    // plotData: the constructor's frame, then nothing new until publish.
    YseDspProcessor* pd = yse_dsp_plot_data_create(1, 4);
    YseDspPlotFrame pf{};
    CHECK(yse_dsp_plot_data_acquire(pd, &pf) == 1u);
    CHECK(pf.plots == 1u);
    CHECK(pf.points == 4u);
    const float four[4] = {1.f, 2.f, 3.f, 4.f};
    REQUIRE(yse_dsp_plot_data_set(pd, 0, four, 4, 0) == 1);
    CHECK(yse_dsp_plot_data_acquire(pd, &pf) == 1u); // not published yet
    float shown[4] = {};
    CHECK(yse_dsp_plot_data_held_subplot(pd, 0, nullptr, shown, 4) == 4u);
    CHECK(shown[3] == 0.f);
    REQUIRE(yse_dsp_plot_data_publish(pd) == 1);
    CHECK(yse_dsp_plot_data_acquire(pd, &pf) == 2u);
    CHECK(yse_dsp_plot_data_held_subplot(pd, 0, nullptr, shown, 4) == 4u);
    CHECK(shown[3] == 4.f);
    yse_dsp_processor_destroy(pd);
  }

  TEST_CASE("c api analysis: a reader thread copies frames while the analysing thread runs") {
    TestHelpers::ScopedSampleRate rate(48000);
    // One analysing thread writes, one reader thread copies out. Each frame
    // the writer publishes is uniform (every value of it the same, a value
    // tied to its serial), so a frame copied while the writer overwrote it
    // would show. The writer waits for the reader to have started, and the
    // reader runs until it has seen the writer's last frame: no deadline,
    // a hang is the ctest timeout's to report. Run under TSan too.
    constexpr unsigned FRAMES = 400;

    SUBCASE("levelMeter readings") {
      YseDspProcessor* lv = yse_dsp_level_meter_create();
      std::atomic<unsigned> reads{0}, torn{0}, fresh{0};
      std::atomic<bool> last{false};
      std::thread reader([&] {
        float previous = 0.f;
        for (;;) {
          YseDspLevelReading r{};
          if (yse_dsp_level_meter_read(lv, &r) != 1) torn.fetch_add(1);
          // With no hold the held peak is the level; a reading mixed from
          // two publishes would not match.
          if (!Same(r.peak_hold, r.level) || !Same(r.peak_hold_db, r.level_db)) torn.fetch_add(1);
          if (r.level < previous) torn.fetch_add(1); // the writer only rises
          if (r.level != previous) fresh.fetch_add(1);
          previous = r.level;
          reads.fetch_add(1, std::memory_order_relaxed);
          if (r.level == static_cast<float>(FRAMES)) break;
        }
        last.store(true);
      });
      while (reads.load(std::memory_order_relaxed) == 0)
        std::this_thread::yield();
      for (unsigned k = 1; k <= FRAMES; ++k) {
        yse_dsp_level_meter_set(lv, static_cast<float>(k));
        if (k == FRAMES / 2) {
          // Half-way, let the reader catch up with at least one new reading.
          while (fresh.load() == 0)
            std::this_thread::yield();
        }
      }
      reader.join();
      CHECK(last.load());
      CHECK(torn.load() == 0u);
      CHECK(fresh.load() > 0u);
      yse_dsp_processor_destroy(lv);
    }

    SUBCASE("scopeCapture traces") {
      YseDspProcessor* sco = yse_dsp_scope_capture_create(16);
      yse_dsp_scope_capture_set_size(sco, 16);
      yse_dsp_scope_capture_set_period(sco, 2);
      // Trace k covers 32 samples, all of value k + 1: its serial.
      std::vector<float> block(32);
      std::atomic<unsigned> reads{0}, torn{0}, seen{0};
      std::thread reader([&] {
        unsigned previous = 0;
        float lo[16], hi[16];
        for (;;) {
          YseDspScopeTrace t{};
          const unsigned n = yse_dsp_scope_capture_acquire(sco, &t, lo, hi, 16);
          if (t.serial < previous) torn.fetch_add(1);
          if (t.serial > 0) {
            if (n != 16) torn.fetch_add(1);
            for (unsigned i = 0; i < 16; ++i)
              if (lo[i] != static_cast<float>(t.serial) || hi[i] != lo[i]) torn.fetch_add(1);
            if (t.serial != previous) seen.fetch_add(1);
          }
          previous = t.serial;
          reads.fetch_add(1, std::memory_order_relaxed);
          if (t.serial == FRAMES) break;
        }
      });
      while (reads.load(std::memory_order_relaxed) == 0)
        std::this_thread::yield();
      for (unsigned k = 1; k <= FRAMES; ++k) {
        std::fill(block.begin(), block.end(), static_cast<float>(k));
        yse_dsp_scope_capture_process(sco, block.data(), 32);
        if (k == FRAMES / 2) {
          while (seen.load() == 0)
            std::this_thread::yield();
        }
      }
      reader.join();
      CHECK(yse_dsp_scope_capture_get_traces(sco) == FRAMES);
      CHECK(torn.load() == 0u);
      CHECK(seen.load() > 0u);
      yse_dsp_processor_destroy(sco);
    }

    SUBCASE("spectrumAnalyzer frames") {
      YseDspProcessor* sa = yse_dsp_spectrum_analyzer_create(32);
      yse_dsp_spectrum_analyzer_set_interval(sa, 10.f); // 480 samples
      // Frame s is taken after 480 s samples, over the last 32: all of value
      // s, written by the writer's call s. A constant reads its value at DC
      // and (but for the window's lobe, bin 1) nothing elsewhere.
      std::vector<float> block(480);
      std::atomic<unsigned> reads{0}, torn{0}, seen{0};
      std::thread reader([&] {
        unsigned previous = 0;
        float bins[17];
        for (;;) {
          YseDspSpectrumFrame f{};
          const unsigned n = yse_dsp_spectrum_analyzer_acquire(sa, &f, bins, 17);
          if (f.serial < previous) torn.fetch_add(1);
          if (f.serial > 0) {
            const auto s = static_cast<float>(f.serial);
            if (n != 17 || f.size != 32) torn.fetch_add(1);
            if (std::fabs(bins[0] - s) > 1e-3f * s) torn.fetch_add(1);
            for (unsigned i = 2; i < 17; ++i)
              if (std::fabs(bins[i]) > 1e-3f * s) torn.fetch_add(1);
            if (f.serial != previous) seen.fetch_add(1);
          }
          previous = f.serial;
          reads.fetch_add(1, std::memory_order_relaxed);
          if (f.serial == FRAMES) break;
        }
      });
      while (reads.load(std::memory_order_relaxed) == 0)
        std::this_thread::yield();
      for (unsigned k = 1; k <= FRAMES; ++k) {
        std::fill(block.begin(), block.end(), static_cast<float>(k));
        yse_dsp_spectrum_analyzer_process(sa, block.data(), 480);
        if (k == FRAMES / 2) {
          while (seen.load() == 0)
            std::this_thread::yield();
        }
      }
      reader.join();
      CHECK(yse_dsp_spectrum_analyzer_get_frames(sa) == FRAMES);
      CHECK(torn.load() == 0u);
      CHECK(seen.load() > 0u);
      yse_dsp_processor_destroy(sa);
    }

    SUBCASE("plotData frames") {
      YseDspProcessor* pd = yse_dsp_plot_data_create(2, 32);
      yse_dsp_plot_data_set_points(pd, 32);
      // The constructor's frame shows the default 4 points; this one, serial
      // 2, the 32 the reader expects.
      REQUIRE(yse_dsp_plot_data_publish(pd) == 1);
      std::vector<float> values(32);
      std::atomic<unsigned> reads{0}, torn{0}, seen{0};
      std::thread reader([&] {
        unsigned previous = 0;
        float shown[32];
        for (;;) {
          YseDspPlotFrame f{};
          const unsigned serial = yse_dsp_plot_data_acquire(pd, &f);
          if (serial < previous || f.plots != 2 || f.points != 32) torn.fetch_add(1);
          // Frame s holds s - 2 in every value of both subplots.
          for (unsigned plot = 0; plot < 2; ++plot) {
            YseDspPlotSubplot s{};
            if (yse_dsp_plot_data_held_subplot(pd, plot, &s, shown, 32) != 32) torn.fetch_add(1);
            for (float v : shown)
              if (v != static_cast<float>(serial - 2)) torn.fetch_add(1);
          }
          if (serial > 2 && serial != previous) seen.fetch_add(1);
          previous = serial;
          reads.fetch_add(1, std::memory_order_relaxed);
          if (serial == FRAMES + 2) break;
        }
      });
      while (reads.load(std::memory_order_relaxed) == 0)
        std::this_thread::yield();
      for (unsigned k = 1; k <= FRAMES; ++k) {
        std::fill(values.begin(), values.end(), static_cast<float>(k));
        yse_dsp_plot_data_set(pd, 0, values.data(), 32, 0);
        yse_dsp_plot_data_set(pd, 1, values.data(), 32, 0);
        yse_dsp_plot_data_publish(pd);
        if (k == FRAMES / 2) {
          while (seen.load() == 0)
            std::this_thread::yield();
        }
      }
      reader.join();
      CHECK(yse_dsp_plot_data_get_frames(pd) == FRAMES + 2);
      CHECK(torn.load() == 0u);
      CHECK(seen.load() > 0u);
      yse_dsp_processor_destroy(pd);
    }
  }

  TEST_CASE("c api analysis: every call but a constructor allocates nothing") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> in = Signal(LONG);
    const std::vector<float> two = Signal(LONG, 0.3f, 0.2f, 77u);
    std::vector<float> o1(LONG), o2(LONG);
    std::vector<unsigned> u1(LONG), u2(LONG);
    const float* chans[2] = {in.data(), nullptr};
    const unsigned idx[3] = {5, 1, 9};
    float bins[300];
    float lo[64], hi[64];
    double coefficients[5];
    YseDspLevelReading reading{};
    YseDspScopeTrace trace{};
    YseDspSpectrumFrame frame{};
    YseDspPlotFrame plot{};
    YseDspPlotSubplot sub{};
    YseDspPlotAxis axis{};
    float a = 0.f, r = 0.f, o = 0.f;

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* dir = all[0];
    YseDspProcessor* ed = all[1];
    YseDspProcessor* st = all[2];
    YseDspProcessor* sp = all[3];
    YseDspProcessor* zc = all[4];
    YseDspProcessor* pk = all[5];
    YseDspProcessor* ma = all[6];
    YseDspProcessor* mm = all[7];
    YseDspProcessor* ld = all[8];
    YseDspProcessor* pt = all[9];
    YseDspProcessor* sc = all[10];
    YseDspProcessor* lv = all[11];
    YseDspProcessor* sco = all[12];
    YseDspProcessor* sa = all[13];
    YseDspProcessor* pd = all[14];

    {
      TestHelpers::ProbeScope probe;
      yse_dsp_direction_set(dir, 0.1f);
      yse_dsp_direction_process(dir, in.data(), o1.data(), LONG);
      (void)yse_dsp_direction_process_sample(dir, 0.3f);
      yse_dsp_direction_reset(dir);

      yse_dsp_edge_detector_process(ed, in.data(), u1.data(), LONG, u2.data(), LONG, LONG);
      (void)yse_dsp_edge_detector_process_sample(ed, 1.f);
      yse_dsp_edge_detector_set(ed, 1);
      yse_dsp_edge_detector_reset(ed);

      yse_dsp_schmitt_trigger_set_low(st, -0.1f);
      yse_dsp_schmitt_trigger_set_high(st, 0.2f);
      yse_dsp_schmitt_trigger_process(st, in.data(), o1.data(), LONG);
      yse_dsp_schmitt_trigger_process_signal(st, in.data(), two.data(), two.data(), o1.data(),
                                             LONG);
      (void)yse_dsp_schmitt_trigger_process_sample(st, 0.5f);
      yse_dsp_schmitt_trigger_set(st, 1);
      yse_dsp_schmitt_trigger_reset(st);

      yse_dsp_spike_detector_set_refractory(sp, 1.f);
      yse_dsp_spike_detector_process(sp, in.data(), u1.data(), o1.data(), LONG, LONG);
      (void)yse_dsp_spike_detector_process_sample(sp, 1.f);
      yse_dsp_spike_detector_restart(sp);
      yse_dsp_spike_detector_reset(sp);

      yse_dsp_zero_crossing_set_volume(zc, 0.5f);
      yse_dsp_zero_crossing_process(zc, in.data(), o1.data(), o2.data(), u1.data(), LONG, LONG);
      (void)yse_dsp_zero_crossing_process_sample(zc, -1.f);
      yse_dsp_zero_crossing_reset(zc);

      yse_dsp_peak_tracker_set_interval(pk, 1.f);
      yse_dsp_peak_tracker_set_signed(pk, 1);
      yse_dsp_peak_tracker_process(pk, in.data(), u1.data(), o1.data(), LONG, LONG);
      (void)yse_dsp_peak_tracker_process_sample(pk, 1.f);
      (void)yse_dsp_peak_tracker_take(pk);
      yse_dsp_peak_tracker_reset(pk);

      yse_dsp_moving_average_set_length(ma, 10);
      yse_dsp_moving_average_set_mode(ma, YSE_DSP_AVERAGE_RMS);
      yse_dsp_moving_average_process(ma, in.data(), o1.data(), LONG);
      yse_dsp_moving_average_accumulate(ma, in.data(), LONG);
      (void)yse_dsp_moving_average_process_sample(ma, 1.f);
      (void)yse_dsp_moving_average_take(ma);
      yse_dsp_moving_average_reset(ma);

      yse_dsp_min_max_process(mm, in.data(), o1.data(), o2.data(), LONG);
      yse_dsp_min_max_process_reset(mm, in.data(), two.data(), o1.data(), o2.data(), LONG);
      yse_dsp_min_max_process_sample(mm, 1.f);
      yse_dsp_min_max_process_sample_reset(mm, 1.f, 1.f);
      yse_dsp_min_max_reset(mm);

      yse_dsp_loudness_meter_set_weight(ld, 1, 1.41f);
      yse_dsp_loudness_meter_set_interval(ld, 10.f);
      yse_dsp_loudness_meter_process(ld, in.data(), LONG);
      yse_dsp_loudness_meter_process_channels(ld, chans, 2, LONG);
      (void)yse_dsp_loudness_meter_get_integrated(ld);
      (void)yse_dsp_loudness_meter_get_loudness_range(ld);
      (void)yse_dsp_loudness_meter_get_true_peak(ld, 0);
      yse_dsp_loudness_meter_shelf(44100.0, coefficients);
      yse_dsp_loudness_meter_high_pass(44100.0, coefficients);
      yse_dsp_loudness_meter_reset(ld);

      yse_dsp_pitch_tracker_set_size(pt, 1024);
      yse_dsp_pitch_tracker_set_period(pt, 256);
      yse_dsp_pitch_tracker_set_min_frequency(pt, 60.f);
      yse_dsp_pitch_tracker_set_max_frequency(pt, 2000.f);
      yse_dsp_pitch_tracker_set_threshold(pt, 0.02f);
      yse_dsp_pitch_tracker_set_tolerance(pt, 0.2f);
      yse_dsp_pitch_tracker_set_onset_amplitude(pt, 0.2f);
      yse_dsp_pitch_tracker_set_onset_pitch(pt, 2.f);
      yse_dsp_pitch_tracker_set_onset_period(pt, 1000);
      yse_dsp_pitch_tracker_process(pt, in.data(), LONG);
      yse_dsp_pitch_tracker_reset(pt);

      yse_dsp_sample_capture_set_mode(sc, YSE_DSP_CAPTURE_FIRST);
      yse_dsp_sample_capture_set_indices(sc, idx, 3);
      (void)yse_dsp_sample_capture_get_indices(sc, u1.data(), 10);
      yse_dsp_sample_capture_process(sc, in.data(), LONG);
      yse_dsp_sample_capture_process_sample(sc, 1.f);
      (void)yse_dsp_sample_capture_get_samples(sc, o1.data(), 300);
      yse_dsp_sample_capture_clear(sc);

      yse_dsp_level_meter_set_detector(lv, YSE_DSP_LEVEL_RMS);
      yse_dsp_level_meter_set_interval(lv, 5.f);
      yse_dsp_level_meter_set_attack(lv, 1.f);
      yse_dsp_level_meter_set_release(lv, 10.f);
      yse_dsp_level_meter_set_hold(lv, 10.f);
      yse_dsp_level_meter_set_offset(lv, -3.f);
      yse_dsp_level_meter_set_ballistics(lv, YSE_DSP_BALLISTICS_VU);
      yse_dsp_level_meter_process(lv, in.data(), LONG);
      (void)yse_dsp_level_meter_process_sample(lv, 1.f);
      yse_dsp_level_meter_set(lv, 0.5f);
      yse_dsp_level_meter_set_db(lv, -6.f);
      yse_dsp_level_meter_read(lv, &reading);
      yse_dsp_level_meter_clear(lv);
      yse_dsp_level_meter_reset(lv);
      yse_dsp_level_meter_ballistics_settings(YSE_DSP_BALLISTICS_DIN, &a, &r, &o);
      (void)yse_dsp_level_meter_position(-6.f, -60.f, 0.f);

      yse_dsp_scope_capture_set_size(sco, 32);
      yse_dsp_scope_capture_set_period(sco, 4);
      yse_dsp_scope_capture_set_delay(sco, 1.f);
      yse_dsp_scope_capture_set_trigger(sco, YSE_DSP_SCOPE_TRIGGER_UP);
      yse_dsp_scope_capture_set_level(sco, 0.f);
      yse_dsp_scope_capture_process(sco, in.data(), LONG);
      yse_dsp_scope_capture_process_xy(sco, in.data(), two.data(), LONG);
      yse_dsp_scope_capture_acquire(sco, &trace, lo, hi, 64);
      yse_dsp_scope_capture_held(sco, &trace, lo, hi, 64);
      (void)yse_dsp_scope_capture_get_traces(sco);
      yse_dsp_scope_capture_restart(sco);

      yse_dsp_spectrum_analyzer_set_size(sa, 128);
      yse_dsp_spectrum_analyzer_set_interval(sa, 10.f);
      yse_dsp_spectrum_analyzer_set_mode(sa, YSE_DSP_SPECTRUM_PHASE);
      yse_dsp_spectrum_analyzer_process(sa, in.data(), LONG);
      yse_dsp_spectrum_analyzer_process_bins(sa, in.data(), two.data(), 0, 33, 33, 64, 480);
      yse_dsp_spectrum_analyzer_acquire(sa, &frame, bins, 300);
      yse_dsp_spectrum_analyzer_held(sa, &frame, bins, 300);
      (void)yse_dsp_spectrum_analyzer_get_frames(sa);
      yse_dsp_spectrum_analyzer_reset(sa);

      yse_dsp_plot_data_set_points(pd, 8);
      yse_dsp_plot_data_set_frame_size(pd, 16);
      yse_dsp_plot_data_set_frame_rate(pd, 500.f);
      yse_dsp_plot_data_set(pd, 0, in.data(), 16, 0);
      yse_dsp_plot_data_set_domain(pd, 0, 1.f, 10.f, YSE_DSP_PLOT_LOG);
      yse_dsp_plot_data_set_range(pd, 0, -1.f, 1.f, YSE_DSP_PLOT_LINEAR);
      yse_dsp_plot_data_set_x_origin(pd, 0, 1.f);
      yse_dsp_plot_data_set_y_origin(pd, 0, 0.5f);
      yse_dsp_plot_data_set_filter(pd, 0, YSE_DSP_PLOT_FILTER_ATODB);
      yse_dsp_plot_data_set_region(pd, 1, 0, 0.f, 10.f);
      yse_dsp_plot_data_get_domain(pd, 0, &axis);
      yse_dsp_plot_data_get_range(pd, 0, &axis);
      yse_dsp_plot_data_process(pd, 1, in.data(), LONG);
      yse_dsp_plot_data_restart(pd, 1);
      yse_dsp_plot_data_publish(pd);
      yse_dsp_plot_data_acquire(pd, &plot);
      yse_dsp_plot_data_held(pd, &plot);
      yse_dsp_plot_data_held_subplot(pd, 0, &sub, lo, 16);
      (void)yse_dsp_plot_data_get_frames(pd);

      // A refused call (wrong class) is allocation-free too.
      yse_dsp_direction_process(sco, in.data(), o1.data(), LONG);
      yse_dsp_level_meter_read(pd, &reading);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
