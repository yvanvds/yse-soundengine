// C-API tests for the phasor toolkit and clock followers of the MSP port
// (issue #1299): yse_c/yse_dsp_phasor_toolkit.h (phasorRate, phasorSubdiv,
// phasorStepDiv, phasorSwing, phasorTwist, phasorWhere, phasorWhat,
// stepCounter, stepFunction, sampleHoldMemory, signalStash, snowfall,
// beatSpan, clockFollower), on the shared YseDspProcessor handle of #1173.
//
// The DSP itself is covered by each class's own suite (test_phasor_rate.cpp,
// ...). What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another toolkit class, or
//     a filter) is treated like NULL, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair the engine has, the list
//     setters' return values, and enum setters ignoring a value out of range.
//   * Parity — every process entry point produces exactly what the engine
//     class produces when driven directly in 128-sample blocks, for a length
//     that is not a multiple of 128, side outputs (index, remaining, count,
//     step, every snowfall dimension) included; the per-sample calls match
//     the engine's per-sample calls.
//   * Gathered reports — the clockFollower's taps, clicks, ticks, tempo
//     change and MIDI clock bytes cover the whole process call, the bytes at
//     their block's offset, and a refused call leaves them as they were.
//   * The beat clock — a beat span follows the engine's beatSpan, and the
//     clockFollower's span call matches the engine's.
//   * Real-time safety — process, sample, setter and list calls allocate
//     nothing.
//
// No engine session is needed: the classes are plain objects that read the
// global SAMPLERATE, which the C and C++ sides here share.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "support/alloc_probe.hpp"

#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_phasor_toolkit.h"
#include "yse_c/yse_dsp_processor.h"

#include "dsp/buffer.hpp"
#include "dsp/generators/beatSpan.hpp"
#include "dsp/generators/clockFollower.hpp"
#include "dsp/generators/phasorRate.hpp"
#include "dsp/generators/phasorStepDiv.hpp"
#include "dsp/generators/phasorSubdiv.hpp"
#include "dsp/generators/phasorSwing.hpp"
#include "dsp/generators/phasorTwist.hpp"
#include "dsp/generators/phasorWhat.hpp"
#include "dsp/generators/phasorWhere.hpp"
#include "dsp/generators/sampleHoldMemory.hpp"
#include "dsp/generators/signalStash.hpp"
#include "dsp/generators/snowfall.hpp"
#include "dsp/generators/stepCounter.hpp"
#include "dsp/generators/stepFunction.hpp"
#include "headers/constants.hpp"

namespace {
  namespace DSP = YSE::DSP;
  constexpr unsigned BLOCK = YSE::STANDARD_BUFFERSIZE;
  // Not a multiple of the block size: the last block is short.
  constexpr unsigned LENGTH = 3 * BLOCK + 37;

  // A deterministic, broadband test signal around `offset`.
  std::vector<float> Signal(unsigned length, float scale = 1.f, float offset = 0.f) {
    std::vector<float> v(length);
    unsigned state = 12345u;
    for (unsigned i = 0; i < length; ++i) {
      state = state * 1103515245u + 12345u;
      const float noise = static_cast<float>((state >> 9) & 0xFFFF) / 32768.f - 1.f;
      v[i] = offset + scale * (0.5f * std::sin(0.05f * static_cast<float>(i)) + 0.5f * noise);
    }
    return v;
  }

  // A 0..1 ramp with `period` samples per cycle, as a phasor would make; a
  // negative period falls.
  std::vector<float> Ramp(unsigned length, float period) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i) {
      const float x = std::fmod(static_cast<float>(i) / std::fabs(period), 1.f);
      v[i] = period > 0.f ? x : 1.f - x;
    }
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

  void CheckEqual(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) {
        INFO("sample " << i);
        CHECK(a[i] == b[i]);
        return;
      }
    }
  }

  // A signal that is not constant: a parity check against a flat line proves
  // little.
  bool Moves(const std::vector<float>& v) {
    return std::any_of(v.begin(), v.end(), [&](float x) { return x != v[0]; });
  }

  using Ctor = std::function<YseDspProcessor*()>;

  struct Named {
    const char* name;
    Ctor make;
  };

  std::vector<Named> AllCtors() {
    return {
        {"phasor_rate", [] { return yse_dsp_phasor_rate_create(); }},
        {"phasor_subdiv", [] { return yse_dsp_phasor_subdiv_create(); }},
        {"phasor_step_div", [] { return yse_dsp_phasor_step_div_create(); }},
        {"phasor_swing", [] { return yse_dsp_phasor_swing_create(); }},
        {"phasor_twist", [] { return yse_dsp_phasor_twist_create(); }},
        {"phasor_where", [] { return yse_dsp_phasor_where_create(); }},
        {"phasor_what", [] { return yse_dsp_phasor_what_create(); }},
        {"step_counter", [] { return yse_dsp_step_counter_create(); }},
        {"step_function", [] { return yse_dsp_step_function_create(); }},
        {"sample_hold_memory", [] { return yse_dsp_sample_hold_memory_create(0); }},
        {"signal_stash", [] { return yse_dsp_signal_stash_create(0); }},
        {"snowfall", [] { return yse_dsp_snowfall_create(0); }},
        {"beat_span", [] { return yse_dsp_beat_span_create(); }},
        {"clock_follower", [] { return yse_dsp_clock_follower_create(); }},
    };
  }

  // One parity case: the C run and the engine reference, each a list of
  // outputs (the main output first, then the side outputs).
  using Outputs = std::vector<std::vector<float>>;
  struct Parity {
    const char* name;
    std::function<Outputs()> c;
    std::function<Outputs()> engine;
  };

  // A fast clock for the span cases: a 128-sample block moves about 0.13
  // beats at 48 kHz. Steady blocks, a short one, a tempo change, a gap the
  // span jumps over, a lost reading, a stop and a backwards run.
  struct Reading {
    double beat;
    float bpm;
    unsigned length;
    bool lose;
  };
  std::vector<Reading> Readings() {
    std::vector<Reading> r;
    double beat = 0.0;
    auto add = [&](float bpm, unsigned length) {
      beat += static_cast<double>(bpm) * length / (60.0 * YSE::SAMPLERATE);
      r.push_back({beat, bpm, length, false});
    };
    for (int i = 0; i < 4; ++i)
      add(3000.f, BLOCK);
    add(3000.f, 37);
    for (int i = 0; i < 3; ++i)
      add(4500.f, BLOCK);
    beat += 2.5;
    add(4500.f, BLOCK);
    r.push_back({0.0, 0.f, BLOCK, true});
    add(4500.f, BLOCK);
    add(0.f, BLOCK);
    add(0.f, BLOCK);
    add(-2000.f, 100);
    add(3000.f, BLOCK);
    return r;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api phasor toolkit: every constructor pairs with destroy and frees what it "
            "allocated") {
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

  TEST_CASE("c api phasor toolkit: a handle of another class is refused like NULL") {
    YseDspProcessor* pr = yse_dsp_phasor_rate_create();
    REQUIRE(pr != nullptr);
    yse_dsp_phasor_rate_set_multiplier(pr, 0.25f);
    yse_dsp_phasor_rate_set_oneshot(pr, 1);
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(lp != nullptr);
    YseDspProcessor* span = yse_dsp_beat_span_create();
    yse_dsp_beat_span_read(span, 1.0, 120.f, BLOCK);

    const std::vector<float> in = Ramp(LENGTH, 61.f);
    const float list[3] = {1.f, 0.f, 0.5f};
    const int steps[2] = {1, 2};
    std::vector<float> out(LENGTH, 7.f);
    std::vector<float> extra(LENGTH, 7.f);
    float* outs[2] = {out.data(), extra.data()};
    float one = 7.f;
    YseDspClockByte clock[2];
    std::memset(clock, 0x5A, sizeof clock);

    for (YseDspProcessor* wrong : {pr, lp}) {
      // Setters and list edits of every other class do nothing ...
      yse_dsp_phasor_subdiv_set_div(wrong, 4);
      yse_dsp_phasor_step_div_start(wrong);
      yse_dsp_phasor_swing_set_swing(wrong, 0.7f);
      yse_dsp_phasor_twist_set_curve(wrong, 0.5f);
      yse_dsp_phasor_what_set_trigger_mode(wrong, YSE_DSP_PHASOR_WHAT_EQUALS);
      yse_dsp_step_counter_set_threshold(wrong, 0.1f);
      yse_dsp_step_function_step(wrong, 2);
      yse_dsp_sample_hold_memory_set_size(wrong, 3);
      yse_dsp_sample_hold_memory_set_max_size(wrong, 5);
      yse_dsp_signal_stash_write(wrong, 1.f);
      yse_dsp_signal_stash_set_max_size(wrong, 5);
      yse_dsp_snowfall_set_dimensions(wrong, 3);
      yse_dsp_beat_span_read(wrong, 2.0, 60.f, BLOCK);
      yse_dsp_clock_follower_tap(wrong);
      CHECK(yse_dsp_phasor_subdiv_set_pattern(wrong, steps, 2) == 0);
      CHECK(yse_dsp_phasor_step_div_set_pattern(wrong, list, 3) == 0);
      CHECK(yse_dsp_phasor_what_set_matches(wrong, list, 3) == 0);
      CHECK(yse_dsp_step_counter_set_sequence(wrong, list, 3) == 0);
      CHECK(yse_dsp_step_function_set_breakpoints(wrong, list, 2, 0, nullptr) == 0);
      CHECK(yse_dsp_sample_hold_memory_load(wrong, list, 3) == 0);
      CHECK(yse_dsp_signal_stash_load(wrong, list, 3) == 0);
      // ... getters return 0 ...
      CHECK(yse_dsp_phasor_subdiv_get_div(wrong) == 0);
      CHECK(yse_dsp_phasor_subdiv_get_sync_update(wrong) == YSE_DSP_SUBDIV_UPDATE_NOW);
      CHECK(yse_dsp_phasor_step_div_get_pattern_length(wrong) == 0);
      CHECK(yse_dsp_phasor_swing_get_swing(wrong) == 0.f);
      CHECK(yse_dsp_phasor_twist_get_shape_mode(wrong) == 0);
      CHECK(yse_dsp_phasor_what_get_count(wrong) == 0);
      CHECK(yse_dsp_step_counter_get_length(wrong) == 0);
      CHECK(yse_dsp_step_function_get_last_step(wrong) == 0);
      CHECK(yse_dsp_sample_hold_memory_get_max_size(wrong) == 0);
      CHECK(yse_dsp_signal_stash_get_value(wrong, 0) == 0.f);
      CHECK(yse_dsp_snowfall_get_dimensions(wrong) == 0);
      CHECK(yse_dsp_beat_span_get_end(wrong) == 0.0);
      CHECK(yse_dsp_clock_follower_get_tempo(wrong) == 0.f);
      CHECK(yse_dsp_clock_follower_get_clock_bytes(wrong) == 0);
      // ... and sample and process calls return 0 and write nothing.
      CHECK(yse_dsp_phasor_subdiv_process_sample(wrong, 0.5f, &one) == 0.f);
      CHECK(yse_dsp_phasor_where_process_sample(wrong, 0.5f, &one) == 0.f);
      CHECK(yse_dsp_step_counter_process_sample(wrong, 0.5f, &one, &one) == 0.f);
      CHECK(yse_dsp_snowfall_process_sample(wrong, 0.5f, out.data(), 4) == 0);
      CHECK(yse_dsp_phasor_subdiv_process(wrong, in.data(), out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_phasor_step_div_process_free(wrong, out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_phasor_swing_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_phasor_twist_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_phasor_where_process(wrong, in.data(), out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_phasor_what_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_step_counter_process(wrong, in.data(), out.data(), extra.data(), extra.data(),
                                         LENGTH) == 0);
      CHECK(yse_dsp_step_function_process_signal(wrong, in.data(), in.data(), out.data(), LENGTH) ==
            0);
      CHECK(yse_dsp_sample_hold_memory_process(wrong, in.data(), in.data(), in.data(), out.data(),
                                               LENGTH) == 0);
      CHECK(yse_dsp_signal_stash_process(wrong, in.data(), in.data(), in.data(), in.data(),
                                         out.data(), LENGTH) == 0);
      CHECK(yse_dsp_snowfall_process(wrong, in.data(), outs, 2, LENGTH) == 0);
      CHECK(yse_dsp_clock_follower_process(wrong, out.data(), clock, 2, LENGTH) == 0);
      CHECK(yse_dsp_clock_follower_process_click(wrong, in.data(), out.data(), clock, 2, LENGTH) ==
            0);
      CHECK(yse_dsp_clock_follower_process_span(wrong, span, out.data(), BLOCK, clock, 2) == 0);
    }
    // The phasorRate's own functions refuse the filter too.
    yse_dsp_phasor_rate_set_multiplier(lp, 3.f);
    CHECK(yse_dsp_phasor_rate_get_multiplier(lp) == 0.f);
    CHECK(yse_dsp_phasor_rate_get_sync(lp) == YSE_DSP_RATE_SYNC_CYCLE);
    CHECK(yse_dsp_phasor_rate_process_sample(lp, 0.5f) == 0.f);
    CHECK(yse_dsp_phasor_rate_process(lp, in.data(), out.data(), LENGTH) == 0);
    // A clock follower refuses a span of another class, the filter included.
    YseDspProcessor* cf = yse_dsp_clock_follower_create();
    CHECK(yse_dsp_clock_follower_process_span(cf, pr, out.data(), BLOCK, clock, 2) == 0);
    CHECK(yse_dsp_clock_follower_process_span(cf, lp, out.data(), BLOCK, clock, 2) == 0);
    CHECK(yse_dsp_clock_follower_is_following(cf) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);
    CHECK(one == 7.f);
    for (const YseDspClockByte& b : clock)
      REQUIRE(b.offset == 0x5A5A5A5Au);

    // The phasorRate's and the span's own state is what it was.
    CHECK(yse_dsp_phasor_rate_get_multiplier(pr) == 0.25f);
    CHECK(yse_dsp_phasor_rate_get_oneshot(pr) == 1);
    CHECK(yse_dsp_beat_span_get_end(span) == 1.0);
    CHECK(yse_dsp_phasor_rate_process(pr, in.data(), out.data(), LENGTH) == LENGTH);
    for (YseDspProcessor* p : {pr, lp, span, cf})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api phasor toolkit: NULL handles and NULL arrays are no-ops that write nothing") {
    const std::vector<float> in = Ramp(LENGTH, 61.f);
    const float list[2] = {1.f, 0.f};
    std::vector<float> out(LENGTH, 7.f);
    float one = 7.f;

    // NULL handle.
    yse_dsp_phasor_rate_jump(nullptr, 0.5f);
    yse_dsp_phasor_subdiv_seed(nullptr, 3);
    yse_dsp_phasor_step_div_step(nullptr, 1);
    yse_dsp_phasor_what_clear(nullptr);
    yse_dsp_step_counter_reset(nullptr);
    yse_dsp_step_function_reset(nullptr);
    yse_dsp_sample_hold_memory_set_max_size(nullptr, 4);
    yse_dsp_signal_stash_capture(nullptr);
    yse_dsp_snowfall_seed(nullptr, 3);
    yse_dsp_beat_span_lose(nullptr);
    yse_dsp_clock_follower_reset(nullptr, 100.f);
    yse_dsp_clock_follower_midi(nullptr, YSE_DSP_MIDI_CLOCK_TICK);
    CHECK(yse_dsp_phasor_rate_is_stopped(nullptr) == 0);
    CHECK(yse_dsp_phasor_subdiv_get_silent_mode(nullptr) == YSE_DSP_SUBDIV_SILENT_HOLD);
    CHECK(yse_dsp_phasor_step_div_get_time(nullptr, 0) == 0.f);
    CHECK(yse_dsp_phasor_swing_get_sync_update(nullptr) == 0);
    CHECK(yse_dsp_phasor_twist_get_curve(nullptr) == 0.f);
    CHECK(yse_dsp_phasor_what_set_matches(nullptr, list, 2) == 0);
    CHECK(yse_dsp_step_function_get_length(nullptr) == 0);
    CHECK(yse_dsp_sample_hold_memory_get_read_position(nullptr) == 0);
    CHECK(yse_dsp_signal_stash_get_size(nullptr) == 0);
    CHECK(yse_dsp_snowfall_get_velocity(nullptr, 0) == 0.f);
    CHECK(yse_dsp_beat_span_is_valid(nullptr) == 0);
    CHECK(yse_dsp_beat_span_get_at(nullptr, 3) == 0.0);
    CHECK(yse_dsp_clock_follower_get_taps(nullptr) == 0);
    CHECK(yse_dsp_clock_follower_get_tempo_changed(nullptr) == 0);
    CHECK(yse_dsp_phasor_rate_process_sample_signal(nullptr, 0.5f, 2.f) == 0.f);
    CHECK(yse_dsp_phasor_step_div_process_free_sample(nullptr, &one) == 0.f);
    CHECK(yse_dsp_signal_stash_process_sample(nullptr, 1.f, 1.f, 1.f, 0.f) == 0.f);
    CHECK(yse_dsp_phasor_rate_process_signal(nullptr, in.data(), in.data(), out.data(), LENGTH) ==
          0);
    CHECK(yse_dsp_phasor_step_div_process(nullptr, in.data(), out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_step_function_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_clock_follower_process(nullptr, out.data(), nullptr, 0, LENGTH) == 0);
    yse_dsp_processor_destroy(nullptr);
    CHECK(one == 7.f);

    // A live handle with a NULL required array.
    auto* pr = yse_dsp_phasor_rate_create();
    auto* sd = yse_dsp_phasor_subdiv_create();
    auto* st = yse_dsp_phasor_step_div_create();
    auto* sw = yse_dsp_phasor_swing_create();
    auto* tw = yse_dsp_phasor_twist_create();
    auto* wh = yse_dsp_phasor_where_create();
    auto* wt = yse_dsp_phasor_what_create();
    auto* sc = yse_dsp_step_counter_create();
    auto* sf = yse_dsp_step_function_create();
    auto* sh = yse_dsp_sample_hold_memory_create(8);
    auto* ss = yse_dsp_signal_stash_create(8);
    auto* sn = yse_dsp_snowfall_create(2);
    auto* bs = yse_dsp_beat_span_create();
    auto* cf = yse_dsp_clock_follower_create();
    float* outs[2] = {out.data(), out.data()};
    CHECK(yse_dsp_phasor_rate_process(pr, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_phasor_rate_process_signal(pr, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_phasor_subdiv_process(sd, in.data(), nullptr, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_phasor_step_div_process(st, nullptr, out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_phasor_step_div_process_free(st, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_phasor_swing_process(sw, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_phasor_twist_process(tw, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_phasor_where_process(wh, nullptr, out.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_phasor_what_process(wt, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_step_counter_process(sc, nullptr, out.data(), nullptr, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_step_function_process_signal(sf, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_sample_hold_memory_process(sh, in.data(), nullptr, in.data(), out.data(),
                                             LENGTH) == 0);
    CHECK(yse_dsp_signal_stash_process(ss, in.data(), in.data(), in.data(), nullptr, out.data(),
                                       LENGTH) == 0);
    CHECK(yse_dsp_snowfall_process(sn, nullptr, outs, 2, LENGTH) == 0);
    CHECK(yse_dsp_snowfall_process(sn, in.data(), nullptr, 2, LENGTH) == 0);
    CHECK(yse_dsp_snowfall_process_sample(sn, 0.5f, nullptr, 2) == 0);
    CHECK(yse_dsp_clock_follower_process(cf, nullptr, nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_clock_follower_process_click(cf, nullptr, out.data(), nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_clock_follower_process_span(cf, nullptr, out.data(), BLOCK, nullptr, 0) == 0);
    CHECK(yse_dsp_clock_follower_process_span(cf, bs, nullptr, BLOCK, nullptr, 0) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);

    // NULL list arrays that hold values change nothing.
    const int steps[2] = {1, 2};
    REQUIRE(yse_dsp_phasor_subdiv_set_pattern(sd, steps, 2) == 1);
    CHECK(yse_dsp_phasor_subdiv_set_pattern(sd, nullptr, 2) == 0);
    CHECK(yse_dsp_phasor_subdiv_get_pattern_length(sd) == 2);
    CHECK(yse_dsp_phasor_subdiv_set_prob(sd, nullptr, 1) == 0);
    REQUIRE(yse_dsp_phasor_step_div_set_pattern(st, list, 2) == 1);
    CHECK(yse_dsp_phasor_step_div_set_pattern(st, nullptr, 2) == 0);
    CHECK(yse_dsp_phasor_step_div_set_breakpoints(st, nullptr, 2, 0) == 0);
    CHECK(yse_dsp_phasor_step_div_set_onsets(st, nullptr, 2) == 0);
    CHECK(yse_dsp_phasor_step_div_get_pattern_length(st) == 2);
    CHECK(yse_dsp_phasor_what_set_matches(wt, nullptr, 2) == 0);
    CHECK(yse_dsp_step_counter_set_sequence(sc, nullptr, 2) == 0);
    CHECK(yse_dsp_step_function_set_pattern(sf, nullptr, 2) == 0);
    CHECK(yse_dsp_step_function_set_curve_pattern(sf, nullptr, 2) == 0);
    CHECK(yse_dsp_step_function_set_breakpoints(sf, nullptr, 2, 0, nullptr) == 0);
    REQUIRE(yse_dsp_sample_hold_memory_load(sh, list, 2) == 1);
    CHECK(yse_dsp_sample_hold_memory_load(sh, nullptr, 2) == 0);
    CHECK(yse_dsp_sample_hold_memory_get_size(sh) == 2);
    CHECK(yse_dsp_signal_stash_load(ss, nullptr, 2) == 0);
    CHECK(yse_dsp_signal_stash_get_size(ss) == 1);
    // A count of 0 clears, and the array may be NULL then.
    CHECK(yse_dsp_phasor_subdiv_set_pattern(sd, nullptr, 0) == 1);
    CHECK(yse_dsp_phasor_subdiv_get_pattern_length(sd) == 0);
    CHECK(yse_dsp_phasor_step_div_set_pattern(st, nullptr, 0) == 1);
    CHECK(yse_dsp_phasor_step_div_get_pattern_length(st) == 0);

    // The optional outputs may be NULL: the call still runs.
    CHECK(yse_dsp_phasor_subdiv_process(sd, in.data(), out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_phasor_step_div_process_free(st, out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_phasor_where_process(wh, in.data(), out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_step_counter_process(sc, in.data(), out.data(), nullptr, nullptr, LENGTH) ==
          LENGTH);
    CHECK(yse_dsp_phasor_subdiv_process_sample(sd, 0.5f, nullptr) >= 0.f);
    CHECK(yse_dsp_step_counter_process_sample(sc, 0.5f, nullptr, nullptr) >= 0.f);
    float* holes[2] = {nullptr, out.data()};
    CHECK(yse_dsp_snowfall_process(sn, in.data(), holes, 2, LENGTH) == LENGTH);
    CHECK(yse_dsp_clock_follower_process(cf, out.data(), nullptr, 0, LENGTH) == LENGTH);
    // A NULL clock array with a capacity is read as a capacity of 0.
    CHECK(yse_dsp_clock_follower_process(cf, out.data(), nullptr, 8, LENGTH) == LENGTH);
    // A zero length is a valid, empty call.
    CHECK(yse_dsp_phasor_swing_process(sw, in.data(), out.data(), 0) == 0);

    for (YseDspProcessor* p : {pr, sd, st, sw, tw, wh, wt, sc, sf, sh, ss, sn, bs, cf})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api phasor toolkit: setters and getters round-trip") {
    const float nan = std::numeric_limits<float>::quiet_NaN();

    SUBCASE("phasor_rate") {
      auto* p = yse_dsp_phasor_rate_create();
      CHECK(yse_dsp_phasor_rate_get_multiplier(p) == 1.f);
      CHECK(yse_dsp_phasor_rate_get_sync(p) == YSE_DSP_RATE_SYNC_OFF);
      CHECK(yse_dsp_phasor_rate_get_oneshot(p) == 0);
      yse_dsp_phasor_rate_set_multiplier(p, 0.5f);
      yse_dsp_phasor_rate_set_multiplier(p, 0.f); // ignored
      CHECK(yse_dsp_phasor_rate_get_multiplier(p) == 0.5f);
      yse_dsp_phasor_rate_set_sync(p, YSE_DSP_RATE_SYNC_LOCK);
      CHECK(yse_dsp_phasor_rate_get_sync(p) == YSE_DSP_RATE_SYNC_LOCK);
      yse_dsp_phasor_rate_set_sync(p, static_cast<YseDspRateSync>(3));
      yse_dsp_phasor_rate_set_sync(p, static_cast<YseDspRateSync>(-1));
      CHECK(yse_dsp_phasor_rate_get_sync(p) == YSE_DSP_RATE_SYNC_LOCK);
      yse_dsp_phasor_rate_set_oneshot(p, 1);
      CHECK(yse_dsp_phasor_rate_get_oneshot(p) == 1);
      // A oneshot ramp at twice the input's speed ends within one cycle.
      yse_dsp_phasor_rate_set_multiplier(p, 0.5f);
      for (int i = 0; i < 100; ++i)
        (void)yse_dsp_phasor_rate_process_sample(p, static_cast<float>(i) / 100.f);
      CHECK(yse_dsp_phasor_rate_is_stopped(p) == 1);
      yse_dsp_phasor_rate_jump(p, 0.f);
      CHECK(yse_dsp_phasor_rate_process_sample(p, 0.f) == 0.f);
      CHECK(yse_dsp_phasor_rate_is_stopped(p) == 0);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("phasor_subdiv") {
      auto* p = yse_dsp_phasor_subdiv_create();
      CHECK(yse_dsp_phasor_subdiv_get_div(p) == 1);
      yse_dsp_phasor_subdiv_set_div(p, 5);
      CHECK(yse_dsp_phasor_subdiv_get_div(p) == 5);
      yse_dsp_phasor_subdiv_set_div(p, 1 << 20);
      CHECK(yse_dsp_phasor_subdiv_get_div(p) == YSE_DSP_PHASOR_SUBDIV_MAX_DIV);
      std::vector<int> steps(YSE_DSP_PHASOR_SUBDIV_MAX_PATTERN + 5, 1);
      CHECK(yse_dsp_phasor_subdiv_set_pattern(p, steps.data(),
                                              static_cast<unsigned>(steps.size())) == 1);
      CHECK(yse_dsp_phasor_subdiv_get_pattern_length(p) == YSE_DSP_PHASOR_SUBDIV_MAX_PATTERN);
      const float chances[3] = {1.f, 0.5f, 0.f};
      CHECK(yse_dsp_phasor_subdiv_set_prob(p, chances, 3) == 1);
      CHECK(yse_dsp_phasor_subdiv_get_prob_length(p) == 3);
      yse_dsp_phasor_subdiv_set_lock_prob(p, 1);
      CHECK(yse_dsp_phasor_subdiv_get_lock_prob(p) == 1);
      yse_dsp_phasor_subdiv_set_silent_mode(p, YSE_DSP_SUBDIV_SILENT_MINUS_ONE);
      yse_dsp_phasor_subdiv_set_silent_mode(p, static_cast<YseDspSubdivSilent>(2));
      CHECK(yse_dsp_phasor_subdiv_get_silent_mode(p) == YSE_DSP_SUBDIV_SILENT_MINUS_ONE);
      yse_dsp_phasor_subdiv_set_sync_update(p, YSE_DSP_SUBDIV_UPDATE_INPUT);
      yse_dsp_phasor_subdiv_set_sync_update(p, static_cast<YseDspSubdivUpdate>(3));
      CHECK(yse_dsp_phasor_subdiv_get_sync_update(p) == YSE_DSP_SUBDIV_UPDATE_INPUT);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("phasor_step_div") {
      auto* p = yse_dsp_phasor_step_div_create();
      const float times[3] = {50.f, 25.f, -3.f};
      CHECK(yse_dsp_phasor_step_div_set_pattern(p, times, 3) == 1);
      CHECK(yse_dsp_phasor_step_div_get_pattern_length(p) == 3);
      CHECK(yse_dsp_phasor_step_div_get_time(p, 1) == 25.f);
      CHECK(yse_dsp_phasor_step_div_get_time(p, 2) == 0.f); // negative counts as 0
      CHECK(yse_dsp_phasor_step_div_get_time(p, 9) == 0.f);
      const float line[6] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f};
      CHECK(yse_dsp_phasor_step_div_set_breakpoints(p, line, 6, 0) == 1);
      CHECK(yse_dsp_phasor_step_div_get_pattern_length(p) == 3);
      CHECK(yse_dsp_phasor_step_div_get_time(p, 1) == 500.f);
      const float onsets[3] = {0.25f, 0.5f, 0.75f};
      CHECK(yse_dsp_phasor_step_div_set_onsets(p, onsets, 3) == 1);
      CHECK(yse_dsp_phasor_step_div_get_pattern_length(p) == 4);
      CHECK(yse_dsp_phasor_step_div_get_time(p, 3) == doctest::Approx(0.25f));
      yse_dsp_phasor_step_div_set_loop(p, 1);
      CHECK(yse_dsp_phasor_step_div_get_loop(p) == 1);
      yse_dsp_phasor_step_div_set_sync_update(p, 1);
      CHECK(yse_dsp_phasor_step_div_get_sync_update(p) == 1);
      CHECK(yse_dsp_phasor_step_div_is_running(p) == 0);
      yse_dsp_phasor_step_div_start(p);
      (void)yse_dsp_phasor_step_div_process_free_sample(p, nullptr);
      CHECK(yse_dsp_phasor_step_div_is_running(p) == 1);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("phasor_swing and phasor_twist") {
      auto* s = yse_dsp_phasor_swing_create();
      CHECK(yse_dsp_phasor_swing_get_swing(s) == 0.5f);
      yse_dsp_phasor_swing_set_swing(s, 0.99f);
      CHECK(yse_dsp_phasor_swing_get_swing(s) == YSE_DSP_PHASOR_SWING_MAX);
      yse_dsp_phasor_swing_set_swing(s, 0.f);
      CHECK(yse_dsp_phasor_swing_get_swing(s) == YSE_DSP_PHASOR_SWING_MIN);
      yse_dsp_phasor_swing_set_swing(s, nan);
      CHECK(yse_dsp_phasor_swing_get_swing(s) == YSE_DSP_PHASOR_SWING_MIN);
      yse_dsp_phasor_swing_set_sync_update(s, 1);
      CHECK(yse_dsp_phasor_swing_get_sync_update(s) == 1);
      auto* t = yse_dsp_phasor_twist_create();
      CHECK(yse_dsp_phasor_twist_get_shape_mode(t) == 1);
      yse_dsp_phasor_twist_set_curve(t, -2.f);
      CHECK(yse_dsp_phasor_twist_get_curve(t) == -1.f);
      yse_dsp_phasor_twist_set_shape_mode(t, 0);
      CHECK(yse_dsp_phasor_twist_get_shape_mode(t) == 0);
      yse_dsp_phasor_twist_set_sync_update(t, 1);
      CHECK(yse_dsp_phasor_twist_get_sync_update(t) == 1);
      yse_dsp_processor_destroy(s);
      yse_dsp_processor_destroy(t);
    }

    SUBCASE("phasor_what, step_counter and step_function") {
      auto* w = yse_dsp_phasor_what_create();
      const float values[3] = {0.f, nan, 0.5f};
      CHECK(yse_dsp_phasor_what_set_matches(w, values, 3) == 1);
      CHECK(yse_dsp_phasor_what_get_count(w) == 2);
      yse_dsp_phasor_what_clear(w);
      CHECK(yse_dsp_phasor_what_get_count(w) == 0);
      // An out-of-range trigger mode is ignored: BOTH still fires on a fall.
      yse_dsp_phasor_what_set_matches(w, values + 2, 1);
      yse_dsp_phasor_what_set_trigger_mode(w, static_cast<YseDspPhasorWhatMode>(9));
      (void)yse_dsp_phasor_what_process_sample(w, 0.6f);
      CHECK(yse_dsp_phasor_what_process_sample(w, 0.4f) == 1.f);

      auto* c = yse_dsp_step_counter_create();
      const float counts[3] = {2.f, 1.f, 3.f};
      CHECK(yse_dsp_step_counter_set_sequence(c, counts, 3) == 1);
      CHECK(yse_dsp_step_counter_get_length(c) == 3);
      yse_dsp_step_counter_set_direction(c, static_cast<YseDspStepCounterDirection>(3));
      yse_dsp_step_counter_set_sync_update(c, static_cast<YseDspStepCounterSync>(3));
      CHECK(yse_dsp_step_counter_set_sequence(c, nullptr, 0) == 1);
      CHECK(yse_dsp_step_counter_get_length(c) == 0);

      auto* f = yse_dsp_step_function_create();
      const float pattern[3] = {0.f, 1.f, 0.5f};
      CHECK(yse_dsp_step_function_set_pattern(f, pattern, 3) == 1);
      CHECK(yse_dsp_step_function_get_length(f) == 3);
      const float line[6] = {0.f, 10.f, 1.f, 10.f, 0.5f, 10.f};
      const float initial = 0.25f;
      CHECK(yse_dsp_step_function_set_breakpoints(f, line, 6, 0, &initial) == 1);
      CHECK(yse_dsp_step_function_get_length(f) == 4);
      yse_dsp_step_function_step(f, -1);
      (void)yse_dsp_step_function_process_sample(f, 0.1f);
      CHECK(yse_dsp_step_function_get_last_step(f) == 3);
      yse_dsp_step_function_reset(f);
      (void)yse_dsp_step_function_process_sample(f, 0.2f);
      CHECK(yse_dsp_step_function_get_last_step(f) == 0);
      yse_dsp_step_function_set_sync_update(f, static_cast<YseDspStepFunctionSync>(3));
      for (YseDspProcessor* p : {w, c, f})
        yse_dsp_processor_destroy(p);
    }

    SUBCASE("sample_hold_memory and signal_stash") {
      auto* h = yse_dsp_sample_hold_memory_create(0);
      CHECK(yse_dsp_sample_hold_memory_get_max_size(h) == YSE_DSP_MEMORY_DEFAULT_MAX_SIZE);
      CHECK(yse_dsp_sample_hold_memory_get_size(h) == 1);
      yse_dsp_sample_hold_memory_set_size(h, 4);
      CHECK(yse_dsp_sample_hold_memory_get_size(h) == 4);
      const float values[3] = {0.1f, 0.2f, 0.3f};
      CHECK(yse_dsp_sample_hold_memory_load(h, values, 3) == 1);
      CHECK(yse_dsp_sample_hold_memory_get_size(h) == 3);
      CHECK(yse_dsp_sample_hold_memory_get_value(h, 2) == 0.3f);
      CHECK(yse_dsp_sample_hold_memory_get_value(h, 5000) == 0.f);
      CHECK(yse_dsp_sample_hold_memory_get_write_position(h) == 0);
      // A new capacity clears the memory and keeps the size that fits.
      yse_dsp_sample_hold_memory_set_max_size(h, 2);
      CHECK(yse_dsp_sample_hold_memory_get_max_size(h) == 2);
      CHECK(yse_dsp_sample_hold_memory_get_size(h) == 2);
      CHECK(yse_dsp_sample_hold_memory_get_value(h, 0) == 0.f);
      yse_dsp_sample_hold_memory_set_max_size(h, 0);
      CHECK(yse_dsp_sample_hold_memory_get_max_size(h) == 1);
      yse_dsp_sample_hold_memory_set_max_size(h, 16);
      CHECK(yse_dsp_sample_hold_memory_get_max_size(h) == 16);
      CHECK(yse_dsp_sample_hold_memory_get_size(h) == 1);
      // The handle still processes after the capacity moved.
      CHECK(yse_dsp_sample_hold_memory_process_sample(h, 0.7f, 1.f, 1.f) == 0.7f);
      yse_dsp_sample_hold_memory_set_direction(h, static_cast<YseDspMemoryDirection>(4));
      yse_dsp_sample_hold_memory_set_mode(h, static_cast<YseDspSashMode>(3));

      auto* s = yse_dsp_signal_stash_create(5);
      CHECK(yse_dsp_signal_stash_get_max_size(s) == 5);
      CHECK(yse_dsp_signal_stash_load(s, values, 3) == 1);
      CHECK(yse_dsp_signal_stash_get_size(s) == 3);
      yse_dsp_signal_stash_set_write_position(s, 2.f);
      CHECK(yse_dsp_signal_stash_get_write_position(s) == 2);
      yse_dsp_signal_stash_write(s, 0.9f);
      CHECK(yse_dsp_signal_stash_get_value(s, 2) == 0.9f);
      yse_dsp_signal_stash_advance_write(s);
      CHECK(yse_dsp_signal_stash_get_write_position(s) == 0);
      yse_dsp_signal_stash_advance(s);
      CHECK(yse_dsp_signal_stash_get_read_position(s) == 1);
      yse_dsp_signal_stash_set_size(s, 99);
      CHECK(yse_dsp_signal_stash_get_size(s) == 5);
      yse_dsp_signal_stash_set_max_size(s, 3);
      CHECK(yse_dsp_signal_stash_get_max_size(s) == 3);
      CHECK(yse_dsp_signal_stash_get_size(s) == 3);
      CHECK(yse_dsp_signal_stash_get_value(s, 2) == 0.f);
      // Out-of-range enums are ignored.
      yse_dsp_signal_stash_set_sample_trigger_mode(s, static_cast<YseDspStashTrigger>(7));
      yse_dsp_signal_stash_set_advance_trigger_mode(s, static_cast<YseDspStashTrigger>(-1));
      yse_dsp_signal_stash_set_mode(s, static_cast<YseDspStashMode>(3));
      yse_dsp_signal_stash_set_write_mode(s, static_cast<YseDspStashMode>(3));
      yse_dsp_signal_stash_set_range(s, static_cast<YseDspStashRange>(4));
      yse_dsp_signal_stash_set_interpolation(s, static_cast<YseDspStashInterpolation>(3));
      yse_dsp_signal_stash_set_direction(s, static_cast<YseDspMemoryDirection>(4));
      yse_dsp_processor_destroy(h);
      yse_dsp_processor_destroy(s);
    }

    SUBCASE("snowfall") {
      auto* p = yse_dsp_snowfall_create(0);
      CHECK(yse_dsp_snowfall_get_dimensions(p) == 1);
      yse_dsp_snowfall_set_dimensions(p, 9);
      CHECK(yse_dsp_snowfall_get_dimensions(p) == YSE_DSP_SNOWFALL_MAX_DIMENSIONS);
      yse_dsp_snowfall_set_dimensions(p, 2);
      CHECK(yse_dsp_snowfall_get_dimensions(p) == 2);
      CHECK(yse_dsp_snowfall_is_alive(p) == 0);
      yse_dsp_snowfall_set_initial(p, 1, 0.4f);
      yse_dsp_snowfall_set_direction(p, 1, -0.5f);
      yse_dsp_snowfall_set_direction(p, 9, 5.f); // past the dimensions: ignored
      float out[4] = {7.f, 7.f, 7.f, 7.f};
      CHECK(yse_dsp_snowfall_process_sample(p, 0.5f, out, 4) == 2);
      CHECK(yse_dsp_snowfall_is_alive(p) == 1);
      CHECK(yse_dsp_snowfall_get_velocity(p, 1) == -0.5f);
      CHECK(yse_dsp_snowfall_get_value(p, 1) < 0.4f);
      CHECK(out[2] == 7.f);
      // A capacity shorter than the dimensions is not overrun.
      out[1] = 7.f;
      CHECK(yse_dsp_snowfall_process_sample(p, 0.6f, out, 1) == 2);
      CHECK(out[1] == 7.f);
      yse_dsp_snowfall_set_boundary_mode(p, static_cast<YseDspSnowfallBoundary>(4));
      yse_dsp_snowfall_set_end_mode(p, static_cast<YseDspSnowfallEnd>(2));
      yse_dsp_snowfall_set_start_mode(p, static_cast<YseDspSnowfallStart>(2));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("beat_span") {
      auto* p = yse_dsp_beat_span_create();
      DSP::beatSpan s;
      CHECK(yse_dsp_beat_span_is_valid(p) == 0);
      CHECK(yse_dsp_beat_span_get_length(p) == 0);
      yse_dsp_beat_span_read(p, 2.0, 120.f, 64);
      s.read(2.0, 120.f, 64);
      CHECK(yse_dsp_beat_span_is_valid(p) == 1);
      CHECK(yse_dsp_beat_span_is_moving(p) == 1);
      CHECK(yse_dsp_beat_span_get_end(p) == 2.0);
      CHECK(yse_dsp_beat_span_get_start(p) == s.start());
      CHECK(yse_dsp_beat_span_get_tempo(p) == 120.f);
      CHECK(yse_dsp_beat_span_get_length(p) == 64);
      CHECK(yse_dsp_beat_span_get_step(p) == s.step());
      CHECK(yse_dsp_beat_span_get_at(p, 10) == s.at(10));
      yse_dsp_beat_span_read(p, 2.0, 120.f, 1000);
      CHECK(yse_dsp_beat_span_get_length(p) == BLOCK);
      yse_dsp_beat_span_read(p, 2.0, 120.f, 0);
      CHECK(yse_dsp_beat_span_get_length(p) == 1);
      yse_dsp_beat_span_read(p, 2.0, nan, 64);
      CHECK(yse_dsp_beat_span_is_valid(p) == 0);
      yse_dsp_beat_span_read(p, 3.0, 0.f, 64);
      yse_dsp_beat_span_read(p, 3.0, 0.f, 64);
      CHECK(yse_dsp_beat_span_is_moving(p) == 0);
      yse_dsp_beat_span_lose(p);
      CHECK(yse_dsp_beat_span_is_valid(p) == 0);
      CHECK(yse_dsp_beat_span_is_moving(p) == 0);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("clock_follower") {
      auto* p = yse_dsp_clock_follower_create();
      CHECK(yse_dsp_clock_follower_get_tempo(p) == YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO);
      CHECK(yse_dsp_clock_follower_get_ppq(p) == YSE_DSP_CLOCK_FOLLOWER_DEFAULT_PPQ);
      CHECK(yse_dsp_clock_follower_is_running(p) == 1);
      yse_dsp_clock_follower_set_tempo(p, 5000.f);
      CHECK(yse_dsp_clock_follower_get_tempo(p) == YSE_DSP_CLOCK_FOLLOWER_MAX_TEMPO);
      yse_dsp_clock_follower_set_tempo(p, nan);
      CHECK(yse_dsp_clock_follower_get_tempo(p) == YSE_DSP_CLOCK_FOLLOWER_MAX_TEMPO);
      yse_dsp_clock_follower_set_offset(p, 1.25f);
      CHECK(yse_dsp_clock_follower_get_offset(p) == doctest::Approx(0.25f));
      yse_dsp_clock_follower_set_clock_offset(p, -0.25f);
      CHECK(yse_dsp_clock_follower_get_clock_offset(p) == doctest::Approx(0.75f));
      yse_dsp_clock_follower_set_ppq(p, 0);
      CHECK(yse_dsp_clock_follower_get_ppq(p) == 1);
      yse_dsp_clock_follower_set_ppq(p, 1000);
      CHECK(yse_dsp_clock_follower_get_ppq(p) == YSE_DSP_CLOCK_FOLLOWER_MAX_PPQ);
      yse_dsp_clock_follower_stop(p);
      CHECK(yse_dsp_clock_follower_is_running(p) == 0);
      yse_dsp_clock_follower_start(p);
      CHECK(yse_dsp_clock_follower_is_running(p) == 1);
      CHECK(yse_dsp_clock_follower_get_phase(p) == 0.f);
      yse_dsp_clock_follower_reset(p, 90.f);
      CHECK(yse_dsp_clock_follower_get_tempo(p) == 90.f);
      CHECK(yse_dsp_clock_follower_get_offset(p) == doctest::Approx(0.25f)); // kept
      yse_dsp_clock_follower_reset(p, nan);
      CHECK(yse_dsp_clock_follower_get_tempo(p) == YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api phasor toolkit: every process entry point matches the engine class") {
    const std::vector<float> ramp = Ramp(LENGTH, 61.f);
    const std::vector<float> slow = Ramp(LENGTH, 150.f);
    const std::vector<float> fall = Ramp(LENGTH, -97.f);
    const std::vector<float> knob = Signal(LENGTH, 0.5f, 1.f);
    const std::vector<float> stepper = Signal(LENGTH, 4.f, 2.f);
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> gateA = Gate(LENGTH, 23, 9);
    const std::vector<float> gateB = Gate(LENGTH, 41, 13);
    const std::vector<float> index = Signal(LENGTH, 6.f, 3.f);
    const std::vector<float> clicks = Gate(LENGTH, 150, 1);
    const int subdivPattern[2] = {1, 2};
    const float chances[3] = {1.f, 0.5f, 0.f};
    const float stepTimes[4] = {50.f, 25.f, 0.f, 40.f};
    const float freeTimes[3] = {2.f, 1.5f, 3.f};
    const float matches[3] = {0.f, 0.25f, 0.6f};
    const float counts[3] = {2.f, 1.f, 3.f};
    const float pattern[3] = {0.f, 1.f, 0.5f};
    const float curves[2] = {0.5f, -0.3f};

    // The C side of a single-output call.
    auto one = [](YseDspProcessor* p, std::function<unsigned(float*)> call) {
      std::vector<float> out(LENGTH, 7.f);
      REQUIRE(call(out.data()) == LENGTH);
      yse_dsp_processor_destroy(p);
      return Outputs{out};
    };
    // The C side of a call with side outputs.
    auto many = [](YseDspProcessor* p, unsigned outputs,
                   std::function<unsigned(std::vector<std::vector<float>>&)> call) {
      Outputs out(outputs, std::vector<float>(LENGTH, 7.f));
      REQUIRE(call(out) == LENGTH);
      yse_dsp_processor_destroy(p);
      return out;
    };

    const std::vector<Parity> cases = {
        {"phasor_rate",
         [&] {
           auto* p = yse_dsp_phasor_rate_create();
           yse_dsp_phasor_rate_set_multiplier(p, 0.25f);
           yse_dsp_phasor_rate_set_sync(p, YSE_DSP_RATE_SYNC_CYCLE);
           return one(
               p, [&](float* o) { return yse_dsp_phasor_rate_process(p, ramp.data(), o, LENGTH); });
         },
         [&] {
           DSP::phasorRate f;
           f.multiplier(0.25f).sync(DSP::phasorRate::syncMode::cycle);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, ramp, o, n)), r[0], o, n); });
           return r;
         }},
        {"phasor_rate signal",
         [&] {
           auto* p = yse_dsp_phasor_rate_create();
           return one(p, [&](float* o) {
             return yse_dsp_phasor_rate_process_signal(p, ramp.data(), knob.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::phasorRate f;
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a, b;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, ramp, o, n), Stage(b, knob, o, n)), r[0], o, n);
           });
           return r;
         }},
        {"phasor_subdiv",
         [&] {
           auto* p = yse_dsp_phasor_subdiv_create();
           yse_dsp_phasor_subdiv_set_div(p, 3);
           yse_dsp_phasor_subdiv_set_pattern(p, subdivPattern, 2);
           yse_dsp_phasor_subdiv_set_prob(p, chances, 3);
           yse_dsp_phasor_subdiv_set_silent_mode(p, YSE_DSP_SUBDIV_SILENT_MINUS_ONE);
           yse_dsp_phasor_subdiv_seed(p, 7);
           return many(p, 2, [&](Outputs& o) {
             return yse_dsp_phasor_subdiv_process(p, slow.data(), o[0].data(), o[1].data(), LENGTH);
           });
         },
         [&] {
           DSP::phasorSubdiv f;
           f.div(3).pattern(subdivPattern, 2).prob(chances, 3);
           f.silentMode(DSP::phasorSubdiv::silent::minusOne).seed(7);
           Outputs r(2, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, slow, o, n)), r[0], o, n);
             Put(f.index(), r[1], o, n);
           });
           return r;
         }},
        {"phasor_step_div",
         [&] {
           auto* p = yse_dsp_phasor_step_div_create();
           yse_dsp_phasor_step_div_set_pattern(p, stepTimes, 4);
           return many(p, 2, [&](Outputs& o) {
             return yse_dsp_phasor_step_div_process(p, slow.data(), o[0].data(), o[1].data(),
                                                    LENGTH);
           });
         },
         [&] {
           DSP::phasorStepDiv f;
           f.pattern(stepTimes, 4);
           Outputs r(2, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, slow, o, n)), r[0], o, n);
             Put(f.index(), r[1], o, n);
           });
           return r;
         }},
        {"phasor_step_div free",
         [&] {
           auto* p = yse_dsp_phasor_step_div_create();
           yse_dsp_phasor_step_div_set_pattern(p, freeTimes, 3);
           yse_dsp_phasor_step_div_set_loop(p, 1);
           yse_dsp_phasor_step_div_start(p);
           return many(p, 2, [&](Outputs& o) {
             return yse_dsp_phasor_step_div_process_free(p, o[0].data(), o[1].data(), LENGTH);
           });
         },
         [&] {
           // Full engine blocks, then the short tail one sample at a time.
           DSP::phasorStepDiv f;
           f.pattern(freeTimes, 3).loop(true).start();
           Outputs r(2, std::vector<float>(LENGTH));
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             if (n == BLOCK) {
               Put(f(), r[0], o, n);
               Put(f.index(), r[1], o, n);
               return;
             }
             for (unsigned i = 0; i < n; ++i) {
               r[0][o + i] = f.process();
               r[1][o + i] = f.lastIndex();
             }
           });
           return r;
         }},
        {"phasor_swing",
         [&] {
           auto* p = yse_dsp_phasor_swing_create();
           yse_dsp_phasor_swing_set_swing(p, 0.66f);
           return one(p, [&](float* o) {
             return yse_dsp_phasor_swing_process(p, fall.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::phasorSwing f;
           f.swing(0.66f);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, fall, o, n)), r[0], o, n); });
           return r;
         }},
        {"phasor_twist",
         [&] {
           auto* p = yse_dsp_phasor_twist_create();
           yse_dsp_phasor_twist_set_curve(p, 0.6f);
           yse_dsp_phasor_twist_set_shape_mode(p, 0);
           return one(p, [&](float* o) {
             return yse_dsp_phasor_twist_process(p, fall.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::phasorTwist f;
           f.curve(0.6f).shapeMode(false);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, fall, o, n)), r[0], o, n); });
           return r;
         }},
        {"phasor_where",
         [&] {
           auto* p = yse_dsp_phasor_where_create();
           return many(p, 2, [&](Outputs& o) {
             return yse_dsp_phasor_where_process(p, ramp.data(), o[0].data(), o[1].data(), LENGTH);
           });
         },
         [&] {
           DSP::phasorWhere f;
           Outputs r(2, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, ramp, o, n)), r[0], o, n);
             Put(f.remaining(), r[1], o, n);
           });
           return r;
         }},
        {"phasor_what",
         [&] {
           auto* p = yse_dsp_phasor_what_create();
           yse_dsp_phasor_what_set_matches(p, matches, 3);
           yse_dsp_phasor_what_set_trigger_mode(p, YSE_DSP_PHASOR_WHAT_ASCENDING);
           return one(
               p, [&](float* o) { return yse_dsp_phasor_what_process(p, ramp.data(), o, LENGTH); });
         },
         [&] {
           DSP::phasorWhat f;
           f.matches(matches, 3).triggerMode(DSP::phasorWhat::ASCENDING);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, ramp, o, n)), r[0], o, n); });
           return r;
         }},
        {"step_counter",
         [&] {
           auto* p = yse_dsp_step_counter_create();
           yse_dsp_step_counter_set_sequence(p, counts, 3);
           yse_dsp_step_counter_set_direction(p, YSE_DSP_STEP_COUNTER_DOWN);
           yse_dsp_step_counter_set_start_mode(p, 0);
           return many(p, 3, [&](Outputs& o) {
             return yse_dsp_step_counter_process(p, ramp.data(), o[0].data(), o[1].data(),
                                                 o[2].data(), LENGTH);
           });
         },
         [&] {
           DSP::stepCounter f;
           f.sequence(counts, 3).direction(DSP::stepCounter::DOWN).startMode(false);
           Outputs r(3, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, ramp, o, n)), r[0], o, n);
             Put(f.counts(), r[1], o, n);
             Put(f.steps(), r[2], o, n);
           });
           return r;
         }},
        {"step_function",
         [&] {
           auto* p = yse_dsp_step_function_create();
           yse_dsp_step_function_set_pattern(p, pattern, 3);
           yse_dsp_step_function_set_curve_pattern(p, curves, 2);
           return one(p, [&](float* o) {
             return yse_dsp_step_function_process(p, ramp.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::stepFunction f;
           f.pattern(pattern, 3).curvePattern(curves, 2);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, ramp, o, n)), r[0], o, n); });
           return r;
         }},
        {"step_function signal",
         [&] {
           auto* p = yse_dsp_step_function_create();
           yse_dsp_step_function_set_pattern(p, pattern, 3);
           return one(p, [&](float* o) {
             return yse_dsp_step_function_process_signal(p, ramp.data(), stepper.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::stepFunction f;
           f.pattern(pattern, 3);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a, b;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, ramp, o, n), Stage(b, stepper, o, n)), r[0], o, n);
           });
           return r;
         }},
        {"sample_hold_memory",
         [&] {
           auto* p = yse_dsp_sample_hold_memory_create(16);
           yse_dsp_sample_hold_memory_set_size(p, 4);
           yse_dsp_sample_hold_memory_set_direction(p, YSE_DSP_MEMORY_URN);
           yse_dsp_sample_hold_memory_seed(p, 3);
           return one(p, [&](float* o) {
             return yse_dsp_sample_hold_memory_process(p, noise.data(), gateA.data(), gateB.data(),
                                                       o, LENGTH);
           });
         },
         [&] {
           DSP::sampleHoldMemory f(16);
           f.size(4).direction(DSP::sampleHoldMemory::URN).seed(3);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a, b, c;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, noise, o, n), Stage(b, gateA, o, n), Stage(c, gateB, o, n)), r[0], o,
                 n);
           });
           return r;
         }},
        {"signal_stash",
         [&] {
           auto* p = yse_dsp_signal_stash_create(16);
           yse_dsp_signal_stash_set_size(p, 6);
           yse_dsp_signal_stash_set_direction(p, YSE_DSP_MEMORY_RANDOM);
           yse_dsp_signal_stash_set_write_mode(p, YSE_DSP_STASH_INDEX);
           yse_dsp_signal_stash_set_interpolation(p, YSE_DSP_STASH_LINEAR);
           yse_dsp_signal_stash_set_duration(p, 10);
           yse_dsp_signal_stash_seed(p, 11);
           return one(p, [&](float* o) {
             return yse_dsp_signal_stash_process(p, noise.data(), gateA.data(), gateB.data(),
                                                 index.data(), o, LENGTH);
           });
         },
         [&] {
           DSP::signalStash f(16);
           f.size(6).direction(DSP::signalStash::RANDOM).writeMode(DSP::signalStash::INDEX);
           f.interpolation(DSP::signalStash::LINEAR).duration(10).seed(11);
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a, b, c, d;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             Put(f(Stage(a, noise, o, n), Stage(b, gateA, o, n), Stage(c, gateB, o, n),
                   Stage(d, index, o, n)),
                 r[0], o, n);
           });
           return r;
         }},
        {"snowfall",
         [&] {
           auto* p = yse_dsp_snowfall_create(3);
           yse_dsp_snowfall_set_direction(p, 0, 4.f);
           yse_dsp_snowfall_set_direction_deviation(p, 1, 3.f);
           yse_dsp_snowfall_set_initial(p, 2, 0.5f);
           yse_dsp_snowfall_set_initial_deviation(p, 2, 0.2f);
           yse_dsp_snowfall_set_wander_probability(p, 1, 0.5f);
           yse_dsp_snowfall_set_energy_ramp(p, 0, 1.f);
           yse_dsp_snowfall_set_interval(p, 1.f);
           yse_dsp_snowfall_set_interval_deviation(p, 0.5f);
           yse_dsp_snowfall_set_scale_maximum(p, 2, 10.f);
           yse_dsp_snowfall_seed(p, 5);
           // Four outputs for three dimensions: the last gets zeroes.
           return many(p, 4, [&](Outputs& o) {
             float* outs[4] = {o[0].data(), o[1].data(), o[2].data(), o[3].data()};
             return yse_dsp_snowfall_process(p, slow.data(), outs, 4, LENGTH);
           });
         },
         [&] {
           DSP::snowfall f(3);
           f.direction(0, 4.f).directionDeviation(1, 3.f).initial(2, 0.5f);
           f.initialDeviation(2, 0.2f).wanderProbability(1, 0.5f).energyRamp(0, 1.f);
           f.interval(1.f).intervalDeviation(0.5f).scaleMaximum(2, 10.f).seed(5);
           Outputs r(4, std::vector<float>(LENGTH, 0.f));
           DSP::buffer a;
           Blocks(LENGTH, [&](unsigned o, unsigned n) {
             f(Stage(a, slow, o, n));
             for (unsigned d = 0; d < 3; ++d)
               Put(f.output(d), r[d], o, n);
           });
           return r;
         }},
        {"clock_follower",
         [&] {
           auto* p = yse_dsp_clock_follower_create();
           yse_dsp_clock_follower_set_tempo(p, 700.f);
           yse_dsp_clock_follower_set_offset(p, 0.1f);
           return one(p, [&](float* o) {
             return yse_dsp_clock_follower_process(p, o, nullptr, 0, LENGTH);
           });
         },
         [&] {
           DSP::clockFollower f;
           f.tempo(700.f).offset(0.1f);
           Outputs r(1, std::vector<float>(LENGTH));
           Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(f(n), r[0], o, n); });
           return r;
         }},
        {"clock_follower click",
         [&] {
           auto* p = yse_dsp_clock_follower_create();
           return one(p, [&](float* o) {
             return yse_dsp_clock_follower_process_click(p, clicks.data(), o, nullptr, 0, LENGTH);
           });
         },
         [&] {
           DSP::clockFollower f;
           Outputs r(1, std::vector<float>(LENGTH));
           DSP::buffer a;
           Blocks(LENGTH,
                  [&](unsigned o, unsigned n) { Put(f(Stage(a, clicks, o, n)), r[0], o, n); });
           return r;
         }},
    };

    for (const Parity& c : cases) {
      INFO("case: " << std::string(c.name));
      const Outputs expected = c.engine();
      REQUIRE(Moves(expected[0]));
      const Outputs got = c.c();
      REQUIRE(got.size() == expected.size());
      for (std::size_t i = 0; i < got.size(); ++i) {
        INFO("output " << i);
        CheckEqual(got[i], expected[i]);
      }
    }
  }

  TEST_CASE("c api phasor toolkit: the per-sample calls match the engine's") {
    const std::vector<float> ramp = Ramp(LENGTH, 61.f);
    const std::vector<float> knob = Signal(LENGTH, 0.5f, 1.f);
    const std::vector<float> noise = Signal(LENGTH);
    const std::vector<float> gateA = Gate(LENGTH, 23, 9);
    const std::vector<float> gateB = Gate(LENGTH, 41, 13);
    const float counts[2] = {2.f, 3.f};
    const float pattern[3] = {0.f, 1.f, 0.5f};
    const float matches[2] = {0.2f, 0.7f};
    const float times[2] = {1.f, 2.f};
    const int div[1] = {2};

    auto* pr = yse_dsp_phasor_rate_create();
    auto* sd = yse_dsp_phasor_subdiv_create();
    auto* st = yse_dsp_phasor_step_div_create();
    auto* fr = yse_dsp_phasor_step_div_create();
    auto* sw = yse_dsp_phasor_swing_create();
    auto* tw = yse_dsp_phasor_twist_create();
    auto* wh = yse_dsp_phasor_where_create();
    auto* wt = yse_dsp_phasor_what_create();
    auto* sc = yse_dsp_step_counter_create();
    auto* sf = yse_dsp_step_function_create();
    auto* sh = yse_dsp_sample_hold_memory_create(8);
    auto* ss = yse_dsp_signal_stash_create(8);
    auto* sn = yse_dsp_snowfall_create(2);
    DSP::phasorRate epr;
    DSP::phasorSubdiv esd;
    DSP::phasorStepDiv est, efr;
    DSP::phasorSwing esw;
    DSP::phasorTwist etw;
    DSP::phasorWhere ewh;
    DSP::phasorWhat ewt;
    DSP::stepCounter esc;
    DSP::stepFunction esf;
    DSP::sampleHoldMemory esh(8);
    DSP::signalStash ess(8);
    DSP::snowfall esn(2);

    yse_dsp_phasor_rate_set_multiplier(pr, 0.5f);
    epr.multiplier(0.5f);
    yse_dsp_phasor_subdiv_set_div(sd, 3);
    yse_dsp_phasor_subdiv_set_pattern(sd, div, 1);
    esd.div(3).pattern(div, 1);
    yse_dsp_phasor_step_div_set_pattern(st, times, 2);
    est.pattern(times, 2);
    yse_dsp_phasor_step_div_set_pattern(fr, times, 2);
    yse_dsp_phasor_step_div_set_loop(fr, 1);
    yse_dsp_phasor_step_div_start(fr);
    efr.pattern(times, 2).loop(true).start();
    yse_dsp_phasor_swing_set_swing(sw, 0.3f);
    esw.swing(0.3f);
    yse_dsp_phasor_twist_set_curve(tw, -0.4f);
    etw.curve(-0.4f);
    yse_dsp_phasor_what_set_matches(wt, matches, 2);
    ewt.matches(matches, 2);
    yse_dsp_step_counter_set_sequence(sc, counts, 2);
    esc.sequence(counts, 2);
    yse_dsp_step_function_set_pattern(sf, pattern, 3);
    esf.pattern(pattern, 3);
    yse_dsp_sample_hold_memory_set_size(sh, 3);
    esh.size(3);
    yse_dsp_signal_stash_set_size(ss, 3);
    yse_dsp_signal_stash_set_mode(ss, YSE_DSP_STASH_NORMALIZED);
    ess.size(3).mode(DSP::signalStash::NORMALIZED);
    yse_dsp_snowfall_set_direction(sn, 1, -2.f);
    esn.direction(1, -2.f);

    for (unsigned i = 0; i < LENGTH; ++i) {
      INFO("sample " << i);
      const float x = ramp[i];
      float a = 0.f, b = 0.f;
      REQUIRE(yse_dsp_phasor_rate_process_sample(pr, x) == epr.process(x));
      REQUIRE(yse_dsp_phasor_rate_process_sample_signal(pr, x, knob[i]) == epr.process(x, knob[i]));
      REQUIRE(yse_dsp_phasor_subdiv_process_sample(sd, x, &a) == esd.process(x));
      REQUIRE(a == esd.lastIndex());
      REQUIRE(yse_dsp_phasor_step_div_process_sample(st, x, &a) == est.process(x));
      REQUIRE(a == est.lastIndex());
      REQUIRE(yse_dsp_phasor_step_div_process_free_sample(fr, &a) == efr.process());
      REQUIRE(a == efr.lastIndex());
      REQUIRE(yse_dsp_phasor_swing_process_sample(sw, x) == esw.process(x));
      REQUIRE(yse_dsp_phasor_twist_process_sample(tw, x) == etw.process(x));
      REQUIRE(yse_dsp_phasor_where_process_sample(wh, x, &a) == ewh.process(x));
      REQUIRE(a == ewh.lastRemaining());
      REQUIRE(yse_dsp_phasor_what_process_sample(wt, x) == ewt.process(x));
      REQUIRE(yse_dsp_step_counter_process_sample(sc, x, &a, &b) == esc.process(x));
      REQUIRE(a == esc.lastCount());
      REQUIRE(b == esc.lastStep());
      REQUIRE(yse_dsp_step_function_process_sample(sf, x) == esf.process(x));
      REQUIRE(yse_dsp_step_function_process_sample_signal(sf, x, knob[i] * 3.f) ==
              esf.process(x, knob[i] * 3.f));
      REQUIRE(yse_dsp_sample_hold_memory_process_sample(sh, noise[i], gateA[i], gateB[i]) ==
              esh.process(noise[i], gateA[i], gateB[i]));
      REQUIRE(yse_dsp_signal_stash_process_sample(ss, noise[i], gateA[i], x, 0.f) ==
              ess.process(noise[i], gateA[i], x, 0.f));
      float c[4] = {}, e[4] = {};
      REQUIRE(yse_dsp_snowfall_process_sample(sn, x, c, 4) == 2);
      esn.process(x, e);
      REQUIRE(c[0] == e[0]);
      REQUIRE(c[1] == e[1]);
    }
    CHECK(yse_dsp_step_function_get_last_step(sf) == esf.lastStep());
    CHECK(yse_dsp_sample_hold_memory_get_read_position(sh) == esh.readPosition());
    CHECK(yse_dsp_signal_stash_get_write_position(ss) == ess.writePosition());
    for (YseDspProcessor* p : {pr, sd, st, fr, sw, tw, wh, wt, sc, sf, sh, ss, sn})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api phasor toolkit: in-place processing matches out-of-place") {
    const std::vector<float> ramp = Ramp(LENGTH, 61.f);
    std::vector<float> separate(LENGTH), side(LENGTH);
    std::vector<float> shared = ramp;
    auto* a = yse_dsp_phasor_where_create();
    auto* b = yse_dsp_phasor_where_create();
    CHECK(yse_dsp_phasor_where_process(a, ramp.data(), separate.data(), side.data(), LENGTH) ==
          LENGTH);
    // The input array becomes the side output, and the main output another.
    std::vector<float> main(LENGTH);
    CHECK(yse_dsp_phasor_where_process(b, shared.data(), main.data(), shared.data(), LENGTH) ==
          LENGTH);
    CheckEqual(main, separate);
    CheckEqual(shared, side);

    std::vector<float> twisted = ramp;
    auto* t1 = yse_dsp_phasor_twist_create();
    auto* t2 = yse_dsp_phasor_twist_create();
    yse_dsp_phasor_twist_set_curve(t1, 0.4f);
    yse_dsp_phasor_twist_set_curve(t2, 0.4f);
    CHECK(yse_dsp_phasor_twist_process(t1, ramp.data(), separate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_phasor_twist_process(t2, twisted.data(), twisted.data(), LENGTH) == LENGTH);
    CheckEqual(twisted, separate);
    for (YseDspProcessor* p : {a, b, t1, t2})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api phasor toolkit: the clock follower's reports cover the whole call") {
    // A fast MIDI clock out: at 1000 BPM and 192 ticks a beat, a tick every
    // 15 samples, so every block sends several.
    const std::vector<float> clicks = [] {
      std::vector<float> v(LENGTH, 0.f);
      v[BLOCK + 20] = 1.f; // one click, in a block that is not the last
      return v;
    }();
    auto* p = yse_dsp_clock_follower_create();
    DSP::clockFollower f;
    yse_dsp_clock_follower_set_tempo(p, 1000.f);
    yse_dsp_clock_follower_set_ppq(p, 192);
    f.tempo(1000.f).ppq(192);

    // Calls made before the process call land in its first block.
    yse_dsp_clock_follower_tap(p);
    yse_dsp_clock_follower_start(p);
    f.tap();
    f.start();

    std::vector<YseDspClockByte> clock(256);
    std::memset(clock.data(), 0x5A, sizeof(YseDspClockByte) * clock.size());
    std::vector<float> out(LENGTH);
    REQUIRE(yse_dsp_clock_follower_process_click(p, clicks.data(), out.data(), clock.data(),
                                                 static_cast<unsigned>(clock.size()),
                                                 LENGTH) == LENGTH);

    // The engine, block by block.
    std::vector<YseDspClockByte> expected;
    unsigned taps = 0, clickCount = 0, ticks = 0;
    bool tempo = false;
    std::vector<float> ref(LENGTH);
    DSP::buffer a;
    Blocks(LENGTH, [&](unsigned o, unsigned n) {
      Put(f(Stage(a, clicks, o, n)), ref, o, n);
      taps += f.taps();
      clickCount += f.clicks();
      ticks += f.ticks();
      tempo = tempo || f.tempoChanged();
      for (unsigned i = 0; i < f.clockBytes(); ++i)
        expected.push_back({o, f.clockByte(i)});
    });
    CheckEqual(out, ref);
    REQUIRE(expected.size() > 20);
    REQUIRE(expected.size() < clock.size());
    CHECK(expected.front().status == YSE_DSP_MIDI_CLOCK_START);
    CHECK(expected.back().offset == 3 * BLOCK); // bytes from the short last block too
    CHECK(clickCount == 1);
    CHECK(yse_dsp_clock_follower_get_taps(p) == taps);
    CHECK(yse_dsp_clock_follower_get_taps(p) == 1);
    CHECK(yse_dsp_clock_follower_get_clicks(p) == 1);
    CHECK(yse_dsp_clock_follower_get_ticks(p) == ticks);
    CHECK(yse_dsp_clock_follower_get_tempo_changed(p) == (tempo ? 1 : 0));
    REQUIRE(yse_dsp_clock_follower_get_clock_bytes(p) == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      INFO("byte " << i);
      CHECK(clock[i].offset == expected[i].offset);
      CHECK(clock[i].status == expected[i].status);
    }
    CHECK(clock[expected.size()].offset == 0x5A5A5A5Au);

    // MIDI ticks before the next call, a capacity smaller than the bytes.
    for (int i = 0; i < 5; ++i) {
      yse_dsp_clock_follower_midi(p, YSE_DSP_MIDI_CLOCK_TICK);
      f.midi(DSP::clockFollower::MIDI_TICK);
    }
    std::memset(clock.data(), 0x5A, sizeof(YseDspClockByte) * clock.size());
    REQUIRE(yse_dsp_clock_follower_process(p, out.data(), clock.data(), 3, LENGTH) == LENGTH);
    expected.clear();
    ticks = 0;
    Blocks(LENGTH, [&](unsigned o, unsigned n) {
      Put(f(n), ref, o, n);
      ticks += f.ticks();
      for (unsigned i = 0; i < f.clockBytes(); ++i)
        expected.push_back({o, f.clockByte(i)});
    });
    CheckEqual(out, ref);
    CHECK(ticks == 5);
    CHECK(yse_dsp_clock_follower_get_ticks(p) == 5);
    CHECK(yse_dsp_clock_follower_get_taps(p) == 0); // the last call's, not the one before
    CHECK(yse_dsp_clock_follower_get_clicks(p) == 0);
    REQUIRE(yse_dsp_clock_follower_get_clock_bytes(p) == expected.size());
    REQUIRE(expected.size() > 3);
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(clock[i].offset == expected[i].offset);
      CHECK(clock[i].status == expected[i].status);
    }
    CHECK(clock[3].offset == 0x5A5A5A5Au);

    // A refused call leaves the reports as they were; reset clears them.
    const unsigned bytes = yse_dsp_clock_follower_get_clock_bytes(p);
    CHECK(yse_dsp_clock_follower_process(p, nullptr, clock.data(), 3, LENGTH) == 0);
    CHECK(yse_dsp_clock_follower_process_click(p, nullptr, out.data(), clock.data(), 3, LENGTH) ==
          0);
    CHECK(yse_dsp_clock_follower_get_clock_bytes(p) == bytes);
    CHECK(yse_dsp_clock_follower_get_ticks(p) == 5);
    yse_dsp_clock_follower_reset(p, 120.f);
    CHECK(yse_dsp_clock_follower_get_clock_bytes(p) == 0);
    CHECK(yse_dsp_clock_follower_get_ticks(p) == 0);
    CHECK(yse_dsp_clock_follower_get_tempo_changed(p) == 0);
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api phasor toolkit: the clock follower follows a beat span like the engine") {
    auto* p = yse_dsp_clock_follower_create();
    auto* span = yse_dsp_beat_span_create();
    DSP::clockFollower f;
    DSP::beatSpan s;
    yse_dsp_clock_follower_set_ppq(p, 48);
    yse_dsp_clock_follower_set_clock_offset(p, 0.1f);
    f.ppq(48).clockOffset(0.1f);

    std::vector<float> all_c, all_engine;
    unsigned sent = 0;
    for (const Reading& r : Readings()) {
      if (r.lose) {
        yse_dsp_beat_span_lose(span);
        s.lose();
      } else {
        yse_dsp_beat_span_read(span, r.beat, r.bpm, r.length);
        s.read(r.beat, r.bpm, r.length);
      }
      std::vector<float> out(BLOCK, 7.f);
      YseDspClockByte clock[YSE_DSP_CLOCK_FOLLOWER_MAX_CLOCK_BYTES];
      const unsigned n = yse_dsp_clock_follower_process_span(
          p, span, out.data(), BLOCK, clock, YSE_DSP_CLOCK_FOLLOWER_MAX_CLOCK_BYTES);
      DSP::buffer& e = f(s);
      REQUIRE(n == e.getLength());
      all_c.insert(all_c.end(), out.begin(), out.begin() + n);
      all_engine.insert(all_engine.end(), e.getPtr(), e.getPtr() + n);
      for (unsigned i = n; i < BLOCK; ++i)
        REQUIRE(out[i] == 7.f);
      REQUIRE(yse_dsp_clock_follower_get_clock_bytes(p) == f.clockBytes());
      for (unsigned i = 0; i < f.clockBytes(); ++i) {
        CHECK(clock[i].offset == 0);
        CHECK(clock[i].status == f.clockByte(i));
      }
      sent += f.clockBytes();
      CHECK(yse_dsp_clock_follower_get_tempo_changed(p) == (f.tempoChanged() ? 1 : 0));
    }
    REQUIRE(Moves(all_engine));
    REQUIRE(sent > 0);
    CheckEqual(all_c, all_engine);
    CHECK(yse_dsp_clock_follower_is_following(p) == 1);
    CHECK(yse_dsp_clock_follower_get_tempo(p) == f.tempo());
    // While following, taps and tempo are ignored, as in the engine.
    yse_dsp_clock_follower_set_tempo(p, 60.f);
    CHECK(yse_dsp_clock_follower_get_tempo(p) == f.tempo());

    // Room for fewer samples than the span covers: refused, nothing written.
    yse_dsp_beat_span_read(span, 100.0, 120.f, BLOCK);
    std::vector<float> out(BLOCK, 7.f);
    CHECK(yse_dsp_clock_follower_process_span(p, span, out.data(), BLOCK - 1, nullptr, 0) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    yse_dsp_clock_follower_reset(p, 120.f);
    CHECK(yse_dsp_clock_follower_is_following(p) == 0);
    yse_dsp_processor_destroy(p);
    yse_dsp_processor_destroy(span);
  }

  TEST_CASE("c api phasor toolkit: process, sample, setter and list calls allocate nothing") {
    const std::vector<float> ramp = Ramp(LENGTH, 100.f);
    const std::vector<float> gate = Gate(LENGTH, 50, 20);
    const std::vector<float> knob = Signal(LENGTH, 0.3f, 1.f);
    const float list[3] = {1.f, 0.5f, 0.25f};
    const int steps[2] = {1, 2};
    std::vector<float> o1(LENGTH), o2(LENGTH), o3(LENGTH), o4(LENGTH);
    float* outs[4] = {o1.data(), o2.data(), o3.data(), o4.data()};
    std::vector<YseDspClockByte> clock(64);
    float one = 0.f, two = 0.f;
    float values[4];

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* pr = all[0];
    YseDspProcessor* sd = all[1];
    YseDspProcessor* st = all[2];
    YseDspProcessor* sw = all[3];
    YseDspProcessor* tw = all[4];
    YseDspProcessor* wh = all[5];
    YseDspProcessor* wt = all[6];
    YseDspProcessor* sc = all[7];
    YseDspProcessor* sf = all[8];
    YseDspProcessor* sh = all[9];
    YseDspProcessor* ss = all[10];
    YseDspProcessor* sn = all[11];
    YseDspProcessor* bs = all[12];
    YseDspProcessor* cf = all[13];

    {
      TestHelpers::ProbeScope probe;
      yse_dsp_phasor_rate_set_multiplier(pr, 0.5f);
      yse_dsp_phasor_rate_set_sync(pr, YSE_DSP_RATE_SYNC_LOCK);
      yse_dsp_phasor_rate_jump_at(pr, 0.2f, 0.5f);
      yse_dsp_phasor_rate_reset(pr);
      yse_dsp_phasor_rate_process(pr, ramp.data(), o1.data(), LENGTH);
      yse_dsp_phasor_rate_process_signal(pr, ramp.data(), knob.data(), o1.data(), LENGTH);
      (void)yse_dsp_phasor_rate_process_sample(pr, 0.5f);
      yse_dsp_phasor_subdiv_set_div(sd, 4);
      yse_dsp_phasor_subdiv_set_pattern(sd, steps, 2);
      yse_dsp_phasor_subdiv_set_prob(sd, list, 3);
      yse_dsp_phasor_subdiv_seed(sd, 9);
      yse_dsp_phasor_subdiv_process(sd, ramp.data(), o1.data(), o2.data(), LENGTH);
      (void)yse_dsp_phasor_subdiv_process_sample(sd, 0.5f, &one);
      yse_dsp_phasor_step_div_set_breakpoints(st, list, 3, 1);
      yse_dsp_phasor_step_div_set_onsets(st, list, 3);
      yse_dsp_phasor_step_div_set_pattern(st, list, 3);
      yse_dsp_phasor_step_div_process(st, ramp.data(), o1.data(), o2.data(), LENGTH);
      yse_dsp_phasor_step_div_start(st);
      yse_dsp_phasor_step_div_step(st, 1);
      yse_dsp_phasor_step_div_process_free(st, o1.data(), o2.data(), LENGTH);
      (void)yse_dsp_phasor_step_div_process_free_sample(st, &one);
      yse_dsp_phasor_swing_set_swing(sw, 0.6f);
      yse_dsp_phasor_swing_process(sw, ramp.data(), o1.data(), LENGTH);
      yse_dsp_phasor_twist_set_curve(tw, 0.6f);
      yse_dsp_phasor_twist_process(tw, ramp.data(), o1.data(), LENGTH);
      yse_dsp_phasor_where_process(wh, ramp.data(), o1.data(), o2.data(), LENGTH);
      (void)yse_dsp_phasor_where_process_sample(wh, 0.5f, &one);
      yse_dsp_phasor_what_set_matches(wt, list, 3);
      yse_dsp_phasor_what_set_trigger_mode(wt, YSE_DSP_PHASOR_WHAT_DESCENDING);
      yse_dsp_phasor_what_process(wt, ramp.data(), o1.data(), LENGTH);
      yse_dsp_phasor_what_clear(wt);
      yse_dsp_step_counter_set_sequence(sc, list, 3);
      yse_dsp_step_counter_set_sync_update(sc, YSE_DSP_STEP_COUNTER_AFTER_STEP);
      yse_dsp_step_counter_process(sc, ramp.data(), o1.data(), o2.data(), o3.data(), LENGTH);
      (void)yse_dsp_step_counter_process_sample(sc, 0.5f, &one, &two);
      yse_dsp_step_counter_reset(sc);
      yse_dsp_step_function_set_pattern(sf, list, 3);
      yse_dsp_step_function_set_curve_pattern(sf, list, 3);
      yse_dsp_step_function_set_breakpoints(sf, list, 3, 0, list);
      yse_dsp_step_function_process(sf, ramp.data(), o1.data(), LENGTH);
      yse_dsp_step_function_process_signal(sf, ramp.data(), knob.data(), o1.data(), LENGTH);
      yse_dsp_sample_hold_memory_set_size(sh, 3);
      yse_dsp_sample_hold_memory_set_direction(sh, YSE_DSP_MEMORY_URN);
      yse_dsp_sample_hold_memory_load(sh, list, 3);
      yse_dsp_sample_hold_memory_process(sh, knob.data(), gate.data(), gate.data(), o1.data(),
                                         LENGTH);
      yse_dsp_signal_stash_set_size(ss, 3);
      yse_dsp_signal_stash_set_direction(ss, YSE_DSP_MEMORY_URN);
      yse_dsp_signal_stash_load(ss, list, 3);
      yse_dsp_signal_stash_write(ss, 0.5f);
      yse_dsp_signal_stash_advance(ss);
      yse_dsp_signal_stash_capture(ss);
      yse_dsp_signal_stash_process(ss, knob.data(), gate.data(), gate.data(), ramp.data(),
                                   o1.data(), LENGTH);
      yse_dsp_snowfall_set_dimensions(sn, 4);
      yse_dsp_snowfall_set_direction(sn, 2, 3.f);
      yse_dsp_snowfall_set_boundary_mode(sn, YSE_DSP_SNOWFALL_WRAP);
      yse_dsp_snowfall_process(sn, ramp.data(), outs, 4, LENGTH);
      (void)yse_dsp_snowfall_process_sample(sn, 0.5f, values, 4);
      yse_dsp_beat_span_read(bs, 1.0, 120.f, BLOCK);
      (void)yse_dsp_beat_span_get_at(bs, 5);
      yse_dsp_clock_follower_tap(cf);
      yse_dsp_clock_follower_midi(cf, YSE_DSP_MIDI_CLOCK_TICK);
      yse_dsp_clock_follower_set_ppq(cf, 96);
      yse_dsp_clock_follower_process(cf, o1.data(), clock.data(), 64, LENGTH);
      yse_dsp_clock_follower_process_click(cf, gate.data(), o1.data(), clock.data(), 64, LENGTH);
      yse_dsp_clock_follower_process_span(cf, bs, o1.data(), BLOCK, clock.data(), 64);
      yse_dsp_clock_follower_reset(cf, 100.f);
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_phasor_rate_process(cf, ramp.data(), o1.data(), LENGTH);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
