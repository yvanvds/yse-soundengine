// C-API tests for the oscillators, noise and phase shapers of the MSP port
// (issue #1297): yse_c/yse_dsp_oscillators.h (phasor, cycle, blSaw, blRect,
// blTri, pinkNoise, bandLimitedRandom, click, pulseTrain, kink,
// oscillatorBank, triangleShape, trapezoidShape, sampleCounter), on the
// shared YseDspProcessor handle of #1173.
//
// The DSP itself is covered by each class's own suite (test_pulse_train.cpp,
// ...). What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another generator, or a
//     filter) is treated like NULL, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair.
//   * Parity — every process entry point produces exactly what the engine
//     class produces when driven directly in 128-sample blocks, for a length
//     that is not a multiple of 128, in place where there is an input; the
//     pulseTrain onsets and the sampleCounter carry follow the engine's
//     per-block lists across the bridge's block split; the handle-free value
//     calls match the static formulas.
//   * Seeds — the noise generators are repeatable from C.
//   * Real-time safety — process, value and setter calls allocate nothing.
//
// No engine session is needed: the classes are plain objects that read the
// global SAMPLERATE, which the C and C++ sides here share.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "support/alloc_probe.hpp"

#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_oscillators.h"
#include "yse_c/yse_dsp_processor.h"

#include "dsp/buffer.hpp"
#include "dsp/generators/bandLimitedOscillators.hpp"
#include "dsp/generators/bandLimitedRandom.hpp"
#include "dsp/generators/click.hpp"
#include "dsp/generators/cycle.hpp"
#include "dsp/generators/kink.hpp"
#include "dsp/generators/oscillatorBank.hpp"
#include "dsp/generators/phaseShapes.hpp"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/pinkNoise.hpp"
#include "dsp/generators/pulseTrain.hpp"
#include "dsp/generators/sampleCounter.hpp"
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

  // A 0..1 ramp with `period` samples per cycle, as a phasor would make.
  std::vector<float> Ramp(unsigned length, float period) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i)
      v[i] = std::fmod(static_cast<float>(i) / period, 1.f);
    return v;
  }

  // A 0/1 gate that is high for `on` of every `period` samples.
  std::vector<float> Gate(unsigned length, unsigned period, unsigned on) {
    std::vector<float> v(length);
    for (unsigned i = 0; i < length; ++i)
      v[i] = (i % period) < on ? 1.f : 0.f;
    return v;
  }

  // Drives an engine object directly the way the bridge must: consecutive
  // blocks of at most BLOCK samples. `block` stages the inputs for one block
  // (scratch buffers, offset, count) and returns the engine output buffer;
  // `after` sees each block's object state (onsets, carry).
  using BlockFn = std::function<DSP::buffer&(std::vector<DSP::buffer>&, unsigned, unsigned)>;

  std::vector<float> Reference(unsigned inputs, unsigned length, const BlockFn& block,
                               const std::function<void(unsigned, unsigned)>& after = {}) {
    std::vector<float> out(length, 0.f);
    std::vector<DSP::buffer> scratch(inputs);
    for (unsigned offset = 0; offset < length; offset += BLOCK) {
      const unsigned n = std::min(BLOCK, length - offset);
      DSP::buffer& r = block(scratch, offset, n);
      REQUIRE(r.getLength() == n);
      std::memcpy(out.data() + offset, r.getPtr(), sizeof(float) * n);
      if (after) after(offset, n);
    }
    return out;
  }

  DSP::buffer& Stage(DSP::buffer& b, const std::vector<float>& src, unsigned offset, unsigned n) {
    b.resize(n);
    std::memcpy(b.getPtr(), src.data() + offset, sizeof(float) * n);
    return b;
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

  // A signal that is not silent: a parity check against all zeroes proves
  // nothing.
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
        {"phasor", [] { return yse_dsp_phasor_create(); }},
        {"cycle", [] { return yse_dsp_cycle_create(); }},
        {"bl_saw", [] { return yse_dsp_bl_saw_create(); }},
        {"bl_rect", [] { return yse_dsp_bl_rect_create(); }},
        {"bl_tri", [] { return yse_dsp_bl_tri_create(); }},
        {"pink_noise", [] { return yse_dsp_pink_noise_create(); }},
        {"band_limited_random", [] { return yse_dsp_band_limited_random_create(); }},
        {"click", [] { return yse_dsp_click_create(); }},
        {"pulse_train", [] { return yse_dsp_pulse_train_create(); }},
        {"kink", [] { return yse_dsp_kink_create(); }},
        {"oscillator_bank", [] { return yse_dsp_oscillator_bank_create(0, 0, 0); }},
        {"triangle_shape", [] { return yse_dsp_triangle_shape_create(); }},
        {"trapezoid_shape", [] { return yse_dsp_trapezoid_shape_create(); }},
        {"sample_counter", [] { return yse_dsp_sample_counter_create(); }},
    };
  }

  // Runs `call(p, out)` on a fresh handle from `make`, then destroys it.
  std::vector<float> RunC(const Ctor& make,
                          const std::function<unsigned(YseDspProcessor*, float*)>& call) {
    std::vector<float> out(LENGTH, 7.f);
    YseDspProcessor* p = make();
    REQUIRE(p != nullptr);
    CHECK(call(p, out.data()) == LENGTH);
    yse_dsp_processor_destroy(p);
    return out;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api oscillators: every constructor pairs with destroy and frees what it allocated") {
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

  TEST_CASE("c api oscillators: a handle of another class is refused like NULL") {
    YseDspProcessor* ph = yse_dsp_phasor_create();
    REQUIRE(ph != nullptr);
    yse_dsp_phasor_set_phase(ph, 0.25f);
    yse_dsp_phasor_set_sync_update(ph, 1);
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(lp != nullptr);

    const std::vector<float> in = Signal(LENGTH, 0.5f, 0.5f);
    std::vector<float> out(LENGTH, 7.f);
    std::vector<float> extra(LENGTH, 7.f);

    for (YseDspProcessor* wrong : {ph, lp}) {
      // Setters of every other class do nothing ...
      yse_dsp_cycle_reset(wrong, 0.5f);
      yse_dsp_bl_saw_set_phase(wrong, 0.5f);
      yse_dsp_bl_rect_set_width(wrong, 0.1f);
      yse_dsp_bl_tri_set_duty(wrong, 0.1f);
      yse_dsp_pink_noise_seed(wrong, 1u);
      yse_dsp_band_limited_random_seed(wrong, 1u);
      yse_dsp_click_trigger(wrong, 1.f);
      yse_dsp_pulse_train_set_interval(wrong, 5.f);
      yse_dsp_oscillator_bank_set(wrong, 0, 440.f, 1.f);
      yse_dsp_triangle_shape_set_range(wrong, 0.f, 1.f);
      yse_dsp_trapezoid_shape_set_wrap(wrong, 0);
      yse_dsp_sample_counter_start(wrong);
      // ... getters return 0 ...
      CHECK(yse_dsp_cycle_get_table_size(wrong) == 0);
      CHECK(yse_dsp_bl_rect_get_width(wrong) == 0.f);
      CHECK(yse_dsp_click_get_length(wrong) == 0);
      CHECK(yse_dsp_pulse_train_get_interval(wrong) == 0.f);
      CHECK(yse_dsp_oscillator_bank_get_capacity(wrong) == 0);
      CHECK(yse_dsp_triangle_shape_get_lo(wrong) == 0.f);
      CHECK(yse_dsp_trapezoid_shape_get_wrap(wrong) == 0);
      CHECK(yse_dsp_sample_counter_get_count(wrong) == 0);
      // ... and process calls return 0 and write nothing.
      CHECK(yse_dsp_cycle_process(wrong, 440.f, 0.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_bl_saw_process_sync(wrong, in.data(), in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_pink_noise_process(wrong, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_pulse_train_process(wrong, out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_kink_process(wrong, in.data(), 2.f, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_oscillator_bank_process(wrong, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_sample_counter_process(wrong, out.data(), extra.data(), LENGTH) == 0);
    }
    // The phasor's own functions refuse the filter too.
    yse_dsp_phasor_set_phase(lp, 0.9f);
    CHECK(yse_dsp_phasor_get_phase(lp) == 0.f);
    CHECK(yse_dsp_phasor_process(lp, 440.f, out.data(), LENGTH) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);

    // The phasor's own state is what it was.
    CHECK(yse_dsp_phasor_get_phase(ph) == 0.25f);
    CHECK(yse_dsp_phasor_get_sync_update(ph) == 1);
    CHECK(yse_dsp_phasor_process(ph, 440.f, out.data(), LENGTH) == LENGTH);
    yse_dsp_processor_destroy(ph);
    yse_dsp_processor_destroy(lp);
  }

  TEST_CASE("c api oscillators: NULL handles and NULL arrays are no-ops that write nothing") {
    const std::vector<float> in = Signal(LENGTH, 0.5f, 0.5f);
    std::vector<float> out(LENGTH, 7.f);

    // NULL handle.
    yse_dsp_phasor_update(nullptr);
    yse_dsp_cycle_set_table(nullptr, in.data(), 16);
    yse_dsp_click_set(nullptr, in.data(), 4);
    yse_dsp_oscillator_bank_set_table(nullptr, in.data(), 16);
    yse_dsp_oscillator_bank_set_pairs(nullptr, in.data(), 2);
    yse_dsp_sample_counter_start_at(nullptr, 3);
    CHECK(yse_dsp_phasor_get_phase(nullptr) == 0.f);
    CHECK(yse_dsp_cycle_get_phase(nullptr) == 0.f);
    CHECK(yse_dsp_bl_saw_get_sync_threshold(nullptr) == 0.f);
    CHECK(yse_dsp_bl_tri_get_duty(nullptr) == 0.f);
    CHECK(yse_dsp_click_get_value(nullptr, 0) == 0.f);
    CHECK(yse_dsp_pulse_train_get_defer_interval(nullptr) == 0);
    CHECK(yse_dsp_oscillator_bank_get_frequency(nullptr, 0) == 0.f);
    CHECK(yse_dsp_oscillator_bank_get_interpolating(nullptr) == 0);
    CHECK(yse_dsp_trapezoid_shape_get_hi(nullptr) == 0.f);
    CHECK(yse_dsp_sample_counter_get_carries(nullptr) == 0);
    CHECK(yse_dsp_sample_counter_is_counting(nullptr) == 0);
    CHECK(yse_dsp_phasor_process_signal(nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bl_rect_process_sync(nullptr, in.data(), in.data(), in.data(), out.data(),
                                       LENGTH) == 0);
    CHECK(yse_dsp_click_process(nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_triangle_shape_process(nullptr, in.data(), 0.5f, out.data(), LENGTH) == 0);
    yse_dsp_processor_destroy(nullptr);

    // A live handle with a NULL required array.
    auto* ph = yse_dsp_phasor_create();
    auto* cy = yse_dsp_cycle_create();
    auto* rc = yse_dsp_bl_rect_create();
    auto* pt = yse_dsp_pulse_train_create();
    auto* ob = yse_dsp_oscillator_bank_create(4, 0, 0);
    auto* tz = yse_dsp_trapezoid_shape_create();
    auto* sc = yse_dsp_sample_counter_create();
    auto* nz = yse_dsp_pink_noise_create();
    CHECK(yse_dsp_phasor_process(ph, 440.f, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_phasor_process_signal(ph, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_cycle_process_signal(cy, in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_cycle_process_phase(cy, 440.f, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_bl_rect_process_sync(rc, in.data(), in.data(), nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_pulse_train_process(pt, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_pulse_train_process_signal(pt, in.data(), nullptr, in.data(), out.data(), nullptr,
                                             LENGTH) == 0);
    CHECK(yse_dsp_oscillator_bank_process_signal(ob, in.data(), in.data(), in.data(), nullptr,
                                                 out.data(), LENGTH) == 0);
    CHECK(yse_dsp_trapezoid_shape_process_signal(tz, in.data(), nullptr, in.data(), out.data(),
                                                 LENGTH) == 0);
    CHECK(yse_dsp_sample_counter_process_signal(sc, nullptr, out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_pink_noise_process(nz, nullptr, LENGTH) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);

    // The optional outputs may be NULL: the call still runs.
    CHECK(yse_dsp_pulse_train_process(pt, out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_sample_counter_process(sc, out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_oscillator_bank_process_signal(ob, in.data(), in.data(), nullptr, in.data(),
                                                 out.data(), LENGTH) == LENGTH);
    // A zero length is a valid, empty call.
    CHECK(yse_dsp_phasor_process(ph, 440.f, out.data(), 0) == 0);

    for (YseDspProcessor* p : {ph, cy, rc, pt, ob, tz, sc, nz})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api oscillators: setters and getters round-trip") {
    SUBCASE("phasor") {
      auto* p = yse_dsp_phasor_create();
      CHECK(yse_dsp_phasor_get_sync_update(p) == 0);
      yse_dsp_phasor_set_sync_update(p, 1);
      CHECK(yse_dsp_phasor_get_sync_update(p) == 1);
      yse_dsp_phasor_set_phase(p, 1.25f);
      CHECK(yse_dsp_phasor_get_phase(p) == doctest::Approx(0.25f));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("cycle") {
      auto* p = yse_dsp_cycle_create();
      CHECK(yse_dsp_cycle_get_table_size(p) == YSE_DSP_CYCLE_TABLE_SIZE);
      const float table[8] = {0.f, 1.f, 0.f, -1.f, 0.f, 1.f, 0.f, -1.f};
      yse_dsp_cycle_set_table(p, table, 8);
      CHECK(yse_dsp_cycle_get_table_size(p) == 8);
      yse_dsp_cycle_set_table(p, nullptr, 8);
      CHECK(yse_dsp_cycle_get_table_size(p) == 0);
      yse_dsp_cycle_cosine(p);
      CHECK(yse_dsp_cycle_get_table_size(p) == YSE_DSP_CYCLE_TABLE_SIZE);
      yse_dsp_cycle_reset(p, -0.25f);
      CHECK(yse_dsp_cycle_get_phase(p) == doctest::Approx(0.75f));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("band-limited oscillators") {
      auto* s = yse_dsp_bl_saw_create();
      auto* r = yse_dsp_bl_rect_create();
      auto* t = yse_dsp_bl_tri_create();
      CHECK(yse_dsp_bl_saw_get_sync_threshold(s) == 0.5f);
      yse_dsp_bl_saw_set_sync_threshold(s, 0.2f);
      CHECK(yse_dsp_bl_saw_get_sync_threshold(s) == 0.2f);
      yse_dsp_bl_saw_set_phase(s, 0.3f);
      CHECK(yse_dsp_bl_saw_get_phase(s) == doctest::Approx(0.3f));
      CHECK(yse_dsp_bl_rect_get_width(r) == 0.5f);
      yse_dsp_bl_rect_set_width(r, 1.25f);
      // The engine keeps the value as set and wraps it where it reads it.
      CHECK(yse_dsp_bl_rect_get_width(r) == DSP::blRect().width(1.25f).width());
      yse_dsp_bl_rect_set_phase(r, 0.4f);
      CHECK(yse_dsp_bl_rect_get_phase(r) == doctest::Approx(0.4f));
      yse_dsp_bl_rect_set_sync_threshold(r, 0.1f);
      CHECK(yse_dsp_bl_rect_get_sync_threshold(r) == 0.1f);
      CHECK(yse_dsp_bl_tri_get_duty(t) == 0.5f);
      yse_dsp_bl_tri_set_duty(t, 0.99f);
      CHECK(yse_dsp_bl_tri_get_duty(t) == DSP::blTri().duty(0.99f).duty());
      yse_dsp_bl_tri_set_phase(t, 0.6f);
      CHECK(yse_dsp_bl_tri_get_phase(t) == doctest::Approx(0.6f));
      yse_dsp_bl_tri_set_sync_threshold(t, 0.7f);
      CHECK(yse_dsp_bl_tri_get_sync_threshold(t) == 0.7f);
      for (YseDspProcessor* p : {s, r, t})
        yse_dsp_processor_destroy(p);
    }
    SUBCASE("click") {
      auto* p = yse_dsp_click_create();
      CHECK(yse_dsp_click_get_length(p) == 1);
      CHECK(yse_dsp_click_get_value(p, 0) == 1.f);
      std::vector<float> wave(YSE_DSP_CLICK_MAX_LENGTH + 10);
      for (unsigned i = 0; i < wave.size(); ++i)
        wave[i] = static_cast<float>(i) * 0.01f;
      yse_dsp_click_set(p, wave.data(), static_cast<unsigned>(wave.size()));
      CHECK(yse_dsp_click_get_length(p) == YSE_DSP_CLICK_MAX_LENGTH);
      CHECK(yse_dsp_click_get_value(p, 7) == wave[7]);
      CHECK(yse_dsp_click_get_value(p, YSE_DSP_CLICK_MAX_LENGTH) == 0.f);
      yse_dsp_click_set(p, nullptr, 5);
      CHECK(yse_dsp_click_get_length(p) == 1);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("pulseTrain") {
      auto* p = yse_dsp_pulse_train_create();
      CHECK(yse_dsp_pulse_train_get_interval(p) == 1000.f);
      CHECK(yse_dsp_pulse_train_get_width(p) == 0.5f);
      CHECK(yse_dsp_pulse_train_get_phase(p) == 0.f);
      CHECK(yse_dsp_pulse_train_get_defer_interval(p) == 0);
      yse_dsp_pulse_train_set_interval(p, 12.5f);
      yse_dsp_pulse_train_set_width(p, 0.25f);
      yse_dsp_pulse_train_set_phase(p, 1.5f);
      yse_dsp_pulse_train_set_defer_interval(p, 1);
      CHECK(yse_dsp_pulse_train_get_interval(p) == 12.5f);
      CHECK(yse_dsp_pulse_train_get_width(p) == 0.25f);
      CHECK(yse_dsp_pulse_train_get_phase(p) == doctest::Approx(0.5f));
      CHECK(yse_dsp_pulse_train_get_defer_interval(p) == 1);
      yse_dsp_pulse_train_set_interval(p, -3.f);
      CHECK(yse_dsp_pulse_train_get_interval(p) == 0.f);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("oscillatorBank") {
      auto* d = yse_dsp_oscillator_bank_create(0, 0, 0);
      CHECK(yse_dsp_oscillator_bank_get_capacity(d) == YSE_DSP_OSCILLATOR_BANK_DEFAULT_OSCILLATORS);
      CHECK(yse_dsp_oscillator_bank_get_table_size(d) ==
            YSE_DSP_OSCILLATOR_BANK_DEFAULT_TABLE_SIZE);
      CHECK(yse_dsp_oscillator_bank_get_interpolating(d) == 0);
      yse_dsp_processor_destroy(d);

      auto* p = yse_dsp_oscillator_bank_create(8, 1, 1000);
      CHECK(yse_dsp_oscillator_bank_get_capacity(p) == 8);
      CHECK(yse_dsp_oscillator_bank_get_size(p) == 8);
      CHECK(yse_dsp_oscillator_bank_get_interpolating(p) == 1);
      CHECK(yse_dsp_oscillator_bank_get_table_size(p) == 1024);
      yse_dsp_oscillator_bank_set_size(p, 3);
      CHECK(yse_dsp_oscillator_bank_get_size(p) == 3);
      yse_dsp_oscillator_bank_set_size(p, 99);
      CHECK(yse_dsp_oscillator_bank_get_size(p) == 8);
      yse_dsp_oscillator_bank_set_frequency_smoothing(p, 0);
      yse_dsp_oscillator_bank_set_magnitude_smoothing(p, 32);
      CHECK(yse_dsp_oscillator_bank_get_frequency_smoothing(p) == 0);
      CHECK(yse_dsp_oscillator_bank_get_magnitude_smoothing(p) == 32);
      yse_dsp_oscillator_bank_set_frame_sync(p, 1);
      CHECK(yse_dsp_oscillator_bank_get_frame_sync(p) == 1);
      yse_dsp_oscillator_bank_set_table_size(p, 1u << 20);
      CHECK(yse_dsp_oscillator_bank_get_table_size(p) == YSE_DSP_OSCILLATOR_BANK_MAX_TABLE_SIZE);
      yse_dsp_oscillator_bank_set_table_size(p, 0);
      CHECK(yse_dsp_oscillator_bank_get_table_size(p) == YSE_DSP_OSCILLATOR_BANK_MIN_TABLE_SIZE);
      CHECK(yse_dsp_oscillator_bank_get_custom_table(p) == 0);
      const float table[2] = {0.25f, -0.25f};
      yse_dsp_oscillator_bank_set_table(p, table, 2);
      CHECK(yse_dsp_oscillator_bank_get_custom_table(p) == 1);
      yse_dsp_oscillator_bank_set_table(p, nullptr, 0);
      CHECK(yse_dsp_oscillator_bank_get_custom_table(p) == 0);

      yse_dsp_oscillator_bank_set_magnitude_smoothing(p, 0);
      yse_dsp_oscillator_bank_set(p, 2, 330.f, 0.5f);
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 2) == 330.f);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 2) == 0.5f);
      yse_dsp_oscillator_bank_set_frequency(p, 1, 220.f);
      yse_dsp_oscillator_bank_set_magnitude(p, 1, 0.25f);
      yse_dsp_oscillator_bank_set_phase(p, 1, 1.75f);
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 1) == 220.f);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 1) == 0.25f);
      CHECK(yse_dsp_oscillator_bank_get_phase(p, 1) == doctest::Approx(0.75f));
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 8) == 0.f);
      const float pairs[4] = {100.f, 0.1f, 200.f, 0.2f};
      yse_dsp_oscillator_bank_set_pairs(p, pairs, 2);
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 1) == 200.f);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 2) == 0.f);
      yse_dsp_oscillator_bank_fade_from(p, 1);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 1) == 0.f);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 0) == 0.1f);
      yse_dsp_oscillator_bank_silence(p);
      CHECK(yse_dsp_oscillator_bank_get_magnitude(p, 0) == 0.f);
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 0) == 100.f);
      yse_dsp_oscillator_bank_clear(p);
      CHECK(yse_dsp_oscillator_bank_get_frequency(p, 0) == 0.f);
      CHECK(yse_dsp_oscillator_bank_get_phase(p, 1) == doctest::Approx(0.75f));
      yse_dsp_oscillator_bank_reset(p);
      CHECK(yse_dsp_oscillator_bank_get_phase(p, 1) == 0.f);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("phase shapes") {
      auto* t = yse_dsp_triangle_shape_create();
      CHECK(yse_dsp_triangle_shape_get_lo(t) == -1.f);
      CHECK(yse_dsp_triangle_shape_get_hi(t) == 1.f);
      yse_dsp_triangle_shape_set_range(t, 2.f, -3.f);
      CHECK(yse_dsp_triangle_shape_get_lo(t) == 2.f);
      CHECK(yse_dsp_triangle_shape_get_hi(t) == -3.f);
      auto* z = yse_dsp_trapezoid_shape_create();
      CHECK(yse_dsp_trapezoid_shape_get_lo(z) == 0.f);
      CHECK(yse_dsp_trapezoid_shape_get_hi(z) == 1.f);
      CHECK(yse_dsp_trapezoid_shape_get_wrap(z) == 1);
      yse_dsp_trapezoid_shape_set_range(z, -1.f, 4.f);
      yse_dsp_trapezoid_shape_set_wrap(z, 0);
      CHECK(yse_dsp_trapezoid_shape_get_lo(z) == -1.f);
      CHECK(yse_dsp_trapezoid_shape_get_hi(z) == 4.f);
      CHECK(yse_dsp_trapezoid_shape_get_wrap(z) == 0);
      yse_dsp_processor_destroy(t);
      yse_dsp_processor_destroy(z);
    }
    SUBCASE("sampleCounter") {
      auto* p = yse_dsp_sample_counter_create();
      CHECK(yse_dsp_sample_counter_is_counting(p) == 0);
      yse_dsp_sample_counter_set_minimum(p, 5);
      yse_dsp_sample_counter_set_limit(p, 9);
      CHECK(yse_dsp_sample_counter_get_minimum(p) == 5);
      CHECK(yse_dsp_sample_counter_get_limit(p) == 9);
      CHECK(yse_dsp_sample_counter_get_count(p) == 5);
      yse_dsp_sample_counter_start_at(p, 6);
      CHECK(yse_dsp_sample_counter_get_minimum(p) == 6);
      CHECK(yse_dsp_sample_counter_is_counting(p) == 1);
      std::vector<float> out(10);
      yse_dsp_sample_counter_process(p, out.data(), nullptr, 10);
      // 6 7 8 | 6 7 8 | 6 7 8 | 6: three wraps.
      CHECK(yse_dsp_sample_counter_get_count(p) == 7);
      CHECK(yse_dsp_sample_counter_get_carries(p) == 3);
      yse_dsp_sample_counter_stop(p);
      CHECK(yse_dsp_sample_counter_is_counting(p) == 0);
      yse_dsp_sample_counter_start(p);
      CHECK(yse_dsp_sample_counter_get_carries(p) == 0);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api oscillators: every process entry point matches the engine class") {
    const std::vector<float> freq = Signal(LENGTH, 300.f, 500.f);
    const std::vector<float> phase = Signal(LENGTH, 0.8f);
    const std::vector<float> width = Signal(LENGTH, 0.3f, 0.5f);
    const std::vector<float> sync = Gate(LENGTH, 97, 11);
    const std::vector<float> ramp = Ramp(LENGTH, 61.f);
    const std::vector<float> knob = Signal(LENGTH, 0.4f, 0.5f);

    struct Case {
      const char* name;
      Ctor make;
      std::function<unsigned(YseDspProcessor*, float*)> call;
      std::function<std::vector<float>()> reference;
    };

    const std::vector<Case> cases = {
        {"phasor",
         [] {
           auto* p = yse_dsp_phasor_create();
           yse_dsp_phasor_set_phase(p, 0.2f);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_phasor_process(p, 333.f, o, LENGTH); },
         [&] {
           DSP::phasor f;
           f.phase(0.2f);
           return Reference(
               0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(333.f, n); });
         }},
        {"phasor signal", [] { return yse_dsp_phasor_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_phasor_process_signal(p, freq.data(), o, LENGTH);
         },
         [&] {
           DSP::phasor f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n));
           });
         }},
        {"cycle", [] { return yse_dsp_cycle_create(); },
         [](YseDspProcessor* p, float* o) {
           return yse_dsp_cycle_process(p, 220.f, 0.1f, o, LENGTH);
         },
         [&] {
           DSP::cycle f;
           return Reference(0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& {
             return f(220.f, 0.1f, n);
           });
         }},
        {"cycle frequency", [] { return yse_dsp_cycle_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_cycle_process_frequency(p, freq.data(), 0.3f, o, LENGTH);
         },
         [&] {
           DSP::cycle f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), 0.3f);
           });
         }},
        {"cycle phase", [] { return yse_dsp_cycle_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_cycle_process_phase(p, 150.f, phase.data(), o, LENGTH);
         },
         [&] {
           DSP::cycle f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(150.f, Stage(s[0], phase, o, n));
           });
         }},
        {"cycle signal", [] { return yse_dsp_cycle_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_cycle_process_signal(p, freq.data(), phase.data(), o, LENGTH);
         },
         [&] {
           DSP::cycle f;
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], phase, o, n));
           });
         }},
        {"bl_saw", [] { return yse_dsp_bl_saw_create(); },
         [](YseDspProcessor* p, float* o) { return yse_dsp_bl_saw_process(p, 1234.f, o, LENGTH); },
         [&] {
           DSP::blSaw f;
           return Reference(0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& {
             return f(1234.f, n);
           });
         }},
        {"bl_saw signal", [] { return yse_dsp_bl_saw_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_saw_process_signal(p, freq.data(), o, LENGTH);
         },
         [&] {
           DSP::blSaw f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n));
           });
         }},
        {"bl_saw sync",
         [] {
           auto* p = yse_dsp_bl_saw_create();
           yse_dsp_bl_saw_set_sync_threshold(p, 0.4f);
           return p;
         },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_saw_process_sync(p, freq.data(), sync.data(), o, LENGTH);
         },
         [&] {
           DSP::blSaw f;
           f.syncThreshold(0.4f);
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], sync, o, n));
           });
         }},
        {"bl_rect",
         [] {
           auto* p = yse_dsp_bl_rect_create();
           yse_dsp_bl_rect_set_width(p, 0.3f);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_bl_rect_process(p, 900.f, o, LENGTH); },
         [&] {
           DSP::blRect f;
           f.width(0.3f);
           return Reference(
               0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(900.f, n); });
         }},
        {"bl_rect frequency",
         [] {
           auto* p = yse_dsp_bl_rect_create();
           yse_dsp_bl_rect_set_width(p, 0.2f);
           return p;
         },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_rect_process_frequency(p, freq.data(), o, LENGTH);
         },
         [&] {
           DSP::blRect f;
           f.width(0.2f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n));
           });
         }},
        {"bl_rect signal", [] { return yse_dsp_bl_rect_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_rect_process_signal(p, freq.data(), width.data(), o, LENGTH);
         },
         [&] {
           DSP::blRect f;
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], width, o, n));
           });
         }},
        {"bl_rect sync", [] { return yse_dsp_bl_rect_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_rect_process_sync(p, freq.data(), width.data(), sync.data(), o,
                                               LENGTH);
         },
         [&] {
           DSP::blRect f;
           return Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], width, o, n), Stage(s[2], sync, o, n));
           });
         }},
        {"bl_tri",
         [] {
           auto* p = yse_dsp_bl_tri_create();
           yse_dsp_bl_tri_set_duty(p, 0.7f);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_bl_tri_process(p, 700.f, o, LENGTH); },
         [&] {
           DSP::blTri f;
           f.duty(0.7f);
           return Reference(
               0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(700.f, n); });
         }},
        {"bl_tri frequency", [] { return yse_dsp_bl_tri_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_tri_process_frequency(p, freq.data(), o, LENGTH);
         },
         [&] {
           DSP::blTri f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n));
           });
         }},
        {"bl_tri signal", [] { return yse_dsp_bl_tri_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_tri_process_signal(p, freq.data(), width.data(), o, LENGTH);
         },
         [&] {
           DSP::blTri f;
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], width, o, n));
           });
         }},
        {"bl_tri sync", [] { return yse_dsp_bl_tri_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_bl_tri_process_sync(p, freq.data(), width.data(), sync.data(), o, LENGTH);
         },
         [&] {
           DSP::blTri f;
           return Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n), Stage(s[1], width, o, n), Stage(s[2], sync, o, n));
           });
         }},
        {"pink_noise", [] { return yse_dsp_pink_noise_create(); },
         [](YseDspProcessor* p, float* o) { return yse_dsp_pink_noise_process(p, o, LENGTH); },
         [&] {
           DSP::pinkNoise f;
           return Reference(0, LENGTH,
                            [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(n); });
         }},
        {"pink_noise seeded",
         [] {
           auto* p = yse_dsp_pink_noise_create();
           yse_dsp_pink_noise_seed(p, 777u);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_pink_noise_process(p, o, LENGTH); },
         [&] {
           DSP::pinkNoise f;
           f.seed(777u);
           return Reference(0, LENGTH,
                            [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(n); });
         }},
        {"band_limited_random",
         [] {
           auto* p = yse_dsp_band_limited_random_create();
           yse_dsp_band_limited_random_seed(p, 99u);
           return p;
         },
         [](YseDspProcessor* p, float* o) {
           return yse_dsp_band_limited_random_process(p, 2000.f, o, LENGTH);
         },
         [&] {
           DSP::bandLimitedRandom f;
           f.seed(99u);
           return Reference(0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& {
             return f(2000.f, n);
           });
         }},
        {"band_limited_random signal", [] { return yse_dsp_band_limited_random_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_band_limited_random_process_signal(p, freq.data(), o, LENGTH);
         },
         [&] {
           DSP::bandLimitedRandom f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], freq, o, n));
           });
         }},
        {"click",
         [] {
           auto* p = yse_dsp_click_create();
           const float wave[3] = {1.f, -0.5f, 0.25f};
           yse_dsp_click_set(p, wave, 3);
           yse_dsp_click_trigger(p, 0.8f);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_click_process(p, o, LENGTH); },
         [&] {
           DSP::click f;
           const float wave[3] = {1.f, -0.5f, 0.25f};
           f.set(wave, 3).trigger(0.8f);
           return Reference(0, LENGTH,
                            [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(n); });
         }},
        {"click signal", [] { return yse_dsp_click_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_click_process_signal(p, sync.data(), o, LENGTH);
         },
         [&] {
           DSP::click f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], sync, o, n));
           });
         }},
        {"kink", [] { return yse_dsp_kink_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_kink_process(p, ramp.data(), 2.5f, o, LENGTH);
         },
         [&] {
           DSP::kink f;
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], ramp, o, n), 2.5f);
           });
         }},
        {"kink signal", [] { return yse_dsp_kink_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_kink_process_signal(p, ramp.data(), freq.data(), o, LENGTH);
         },
         [&] {
           DSP::kink f;
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], ramp, o, n), Stage(s[1], freq, o, n));
           });
         }},
        {"oscillator_bank",
         [] {
           auto* p = yse_dsp_oscillator_bank_create(4, 1, 512);
           yse_dsp_oscillator_bank_set_magnitude_smoothing(p, 50);
           const float pairs[6] = {220.f, 0.5f, 330.f, 0.25f, 445.f, 0.1f};
           yse_dsp_oscillator_bank_set_pairs(p, pairs, 3);
           return p;
         },
         [](YseDspProcessor* p, float* o) { return yse_dsp_oscillator_bank_process(p, o, LENGTH); },
         [&] {
           DSP::oscillatorBank f(4, true, 512);
           f.magnitudeSmoothing(50);
           const float pairs[6] = {220.f, 0.5f, 330.f, 0.25f, 445.f, 0.1f};
           f.setPairs(pairs, 3);
           return Reference(0, LENGTH,
                            [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return f(n); });
         }},
        {"triangle_shape",
         [] {
           auto* p = yse_dsp_triangle_shape_create();
           yse_dsp_triangle_shape_set_range(p, -0.5f, 2.f);
           return p;
         },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_triangle_shape_process(p, ramp.data(), 0.3f, o, LENGTH);
         },
         [&] {
           DSP::triangleShape f;
           f.range(-0.5f, 2.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], ramp, o, n), 0.3f);
           });
         }},
        {"triangle_shape signal", [] { return yse_dsp_triangle_shape_create(); },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_triangle_shape_process_signal(p, ramp.data(), knob.data(), o, LENGTH);
         },
         [&] {
           DSP::triangleShape f;
           return Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], ramp, o, n), Stage(s[1], knob, o, n));
           });
         }},
        {"trapezoid_shape",
         [] {
           auto* p = yse_dsp_trapezoid_shape_create();
           yse_dsp_trapezoid_shape_set_wrap(p, 0);
           return p;
         },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_trapezoid_shape_process(p, phase.data(), YSE_DSP_TRAPEZOID_DEFAULT_UP,
                                                  YSE_DSP_TRAPEZOID_DEFAULT_DOWN, o, LENGTH);
         },
         [&] {
           DSP::trapezoidShape f;
           f.wrap(false);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], phase, o, n));
           });
         }},
        {"trapezoid_shape signal",
         [] {
           auto* p = yse_dsp_trapezoid_shape_create();
           yse_dsp_trapezoid_shape_set_range(p, 1.f, -1.f);
           return p;
         },
         [&](YseDspProcessor* p, float* o) {
           return yse_dsp_trapezoid_shape_process_signal(p, ramp.data(), width.data(), knob.data(),
                                                         o, LENGTH);
         },
         [&] {
           DSP::trapezoidShape f;
           f.range(1.f, -1.f);
           return Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], ramp, o, n), Stage(s[1], width, o, n), Stage(s[2], knob, o, n));
           });
         }},
    };

    for (const Case& c : cases) {
      INFO("case: " << std::string(c.name));
      const std::vector<float> expected = c.reference();
      REQUIRE(Moves(expected));
      CheckEqual(RunC(c.make, c.call), expected);
    }
  }

  TEST_CASE("c api oscillators: in-place processing matches out-of-place") {
    const std::vector<float> freq = Signal(LENGTH, 300.f, 500.f);
    const std::vector<float> ramp = Ramp(LENGTH, 61.f);

    // A signal-driven generator: the frequency array becomes the output.
    std::vector<float> separate(LENGTH);
    std::vector<float> shared = freq;
    auto* a = yse_dsp_bl_saw_create();
    auto* b = yse_dsp_bl_saw_create();
    CHECK(yse_dsp_bl_saw_process_signal(a, freq.data(), separate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_bl_saw_process_signal(b, shared.data(), shared.data(), LENGTH) == LENGTH);
    CheckEqual(shared, separate);

    // A phase shaper: the phase array becomes the output.
    std::vector<float> phase = ramp;
    auto* k1 = yse_dsp_kink_create();
    auto* k2 = yse_dsp_kink_create();
    CHECK(yse_dsp_kink_process(k1, ramp.data(), 3.f, separate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_kink_process(k2, phase.data(), 3.f, phase.data(), LENGTH) == LENGTH);
    CheckEqual(phase, separate);

    for (YseDspProcessor* p : {a, b, k1, k2})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api oscillators: pulseTrain onsets cover the whole call") {
    const std::vector<float> interval = Signal(LENGTH, 0.5f, 1.2f);
    const std::vector<float> width = Signal(LENGTH, 0.3f, 0.5f);
    const std::vector<float> phase = Signal(LENGTH, 0.2f, 0.3f);

    // The engine's per-block onset lists, laid out over the whole run.
    auto onsetsOf = [](DSP::pulseTrain& t, std::vector<float>& marks) {
      return [&t, &marks](unsigned o, unsigned) {
        for (unsigned i = 0; i < t.onsets(); ++i)
          marks[o + t.onset(i)] = 1.f;
      };
    };

    SUBCASE("set interval, with a trigger") {
      auto* p = yse_dsp_pulse_train_create();
      yse_dsp_pulse_train_set_interval(p, 1.f); // 48 samples at 48 kHz
      yse_dsp_pulse_train_set_width(p, 0.25f);
      std::vector<float> out(LENGTH), marks(LENGTH, 7.f);
      CHECK(yse_dsp_pulse_train_process(p, out.data(), marks.data(), LENGTH) == LENGTH);
      yse_dsp_pulse_train_trigger(p);
      std::vector<float> out2(LENGTH), marks2(LENGTH, 7.f);
      CHECK(yse_dsp_pulse_train_process(p, out2.data(), marks2.data(), LENGTH) == LENGTH);

      DSP::pulseTrain t;
      t.interval(1.f).width(0.25f);
      std::vector<float> want(LENGTH, 0.f), want2(LENGTH, 0.f);
      const auto ref = Reference(
          0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return t(n); },
          onsetsOf(t, want));
      t.trigger();
      const auto ref2 = Reference(
          0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return t(n); },
          onsetsOf(t, want2));
      CheckEqual(out, ref);
      CheckEqual(marks, want);
      CheckEqual(out2, ref2);
      CheckEqual(marks2, want2);
      // Onsets in more than one engine block, and the trigger's own onset.
      CHECK(std::count(marks.begin(), marks.end(), 1.f) > 3);
      CHECK(std::count(marks.begin() + BLOCK, marks.end(), 1.f) > 0);
      CHECK(marks2[0] == 1.f);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("per-sample interval") {
      auto* p = yse_dsp_pulse_train_create();
      std::vector<float> out(LENGTH), marks(LENGTH, 7.f);
      CHECK(yse_dsp_pulse_train_process_interval(p, interval.data(), out.data(), marks.data(),
                                                 LENGTH) == LENGTH);
      DSP::pulseTrain t;
      std::vector<float> want(LENGTH, 0.f);
      const auto ref = Reference(
          1, LENGTH,
          [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
            return t(Stage(s[0], interval, o, n));
          },
          onsetsOf(t, want));
      CheckEqual(out, ref);
      CheckEqual(marks, want);
      CHECK(std::count(marks.begin() + 2 * BLOCK, marks.end(), 1.f) > 0);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("every setting per sample") {
      auto* p = yse_dsp_pulse_train_create();
      std::vector<float> out(LENGTH), marks(LENGTH, 7.f);
      CHECK(yse_dsp_pulse_train_process_signal(p, interval.data(), width.data(), phase.data(),
                                               out.data(), marks.data(), LENGTH) == LENGTH);
      DSP::pulseTrain t;
      std::vector<float> want(LENGTH, 0.f);
      const auto ref = Reference(
          3, LENGTH,
          [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
            return t(Stage(s[0], interval, o, n), Stage(s[1], width, o, n),
                     Stage(s[2], phase, o, n));
          },
          onsetsOf(t, want));
      CheckEqual(out, ref);
      CheckEqual(marks, want);
      REQUIRE(Moves(ref));
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api oscillators: sampleCounter output and carry match the engine class") {
    const std::vector<float> enable = Gate(LENGTH, 150, 120);

    SUBCASE("started") {
      auto* p = yse_dsp_sample_counter_create();
      yse_dsp_sample_counter_set_limit(p, 50);
      yse_dsp_sample_counter_start_at(p, 3);
      std::vector<float> out(LENGTH), carry(LENGTH, 7.f);
      CHECK(yse_dsp_sample_counter_process(p, out.data(), carry.data(), LENGTH) == LENGTH);

      DSP::sampleCounter c;
      c.limit(50).start(3);
      std::vector<float> want(LENGTH);
      const auto ref = Reference(
          0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& { return c(n); },
          [&](unsigned o, unsigned n) {
            std::memcpy(want.data() + o, c.carry().getPtr(), sizeof(float) * n);
          });
      CheckEqual(out, ref);
      CheckEqual(carry, want);
      CHECK(carry[LENGTH - 1] > carry[BLOCK]);
      CHECK(yse_dsp_sample_counter_get_count(p) == c.count());
      CHECK(yse_dsp_sample_counter_get_carries(p) == c.carries());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("enable signal") {
      auto* p = yse_dsp_sample_counter_create();
      yse_dsp_sample_counter_set_minimum(p, 10);
      yse_dsp_sample_counter_set_limit(p, 40);
      std::vector<float> out(LENGTH), carry(LENGTH, 7.f);
      CHECK(yse_dsp_sample_counter_process_signal(p, enable.data(), out.data(), carry.data(),
                                                  LENGTH) == LENGTH);

      DSP::sampleCounter c;
      c.minimum(10).limit(40);
      std::vector<float> want(LENGTH);
      const auto ref = Reference(
          1, LENGTH,
          [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
            return c(Stage(s[0], enable, o, n));
          },
          [&](unsigned o, unsigned n) {
            std::memcpy(want.data() + o, c.carry().getPtr(), sizeof(float) * n);
          });
      CheckEqual(out, ref);
      CheckEqual(carry, want);
      REQUIRE(Moves(want));
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api oscillators: oscillatorBank signal input matches the engine class") {
    std::vector<float> freq(LENGTH), mag(LENGTH), phase(LENGTH), index(LENGTH);
    for (unsigned i = 0; i < LENGTH; ++i) {
      // A frame of six bins, one oscillator per sample, as an analysis
      // stream drives the bank.
      index[i] = static_cast<float>(i % 6);
      freq[i] =
          110.f * static_cast<float>(1 + i % 6) + std::floor(static_cast<float>(i) / 6.f) * 0.5f;
      mag[i] = 0.1f + 0.05f * static_cast<float>(i % 3);
      phase[i] = 0.1f * static_cast<float>(i % 6);
    }

    for (bool withPhase : {true, false}) {
      INFO("phase signal: " << withPhase);
      auto* p = yse_dsp_oscillator_bank_create(6, 0, 0);
      yse_dsp_oscillator_bank_set_frame_sync(p, 1);
      std::vector<float> out(LENGTH);
      CHECK(yse_dsp_oscillator_bank_process_signal(p, freq.data(), mag.data(),
                                                   withPhase ? phase.data() : nullptr, index.data(),
                                                   out.data(), LENGTH) == LENGTH);
      DSP::oscillatorBank b(6);
      b.frameSync(true);
      const auto ref = Reference(4, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
        if (withPhase)
          return b(Stage(s[0], freq, o, n), Stage(s[1], mag, o, n), Stage(s[2], phase, o, n),
                   Stage(s[3], index, o, n));
        return b(Stage(s[0], freq, o, n), Stage(s[1], mag, o, n), Stage(s[3], index, o, n));
      });
      REQUIRE(Moves(ref));
      CheckEqual(out, ref);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api oscillators: cycle reads a host table in place") {
    std::vector<float> table(64);
    for (unsigned i = 0; i < table.size(); ++i)
      table[i] = i < 32 ? 1.f : -1.f;
    auto* p = yse_dsp_cycle_create();
    yse_dsp_cycle_set_table(p, table.data(), 64);
    std::vector<float> a(LENGTH), b(LENGTH);
    CHECK(yse_dsp_cycle_process(p, 440.f, 0.f, a.data(), LENGTH) == LENGTH);

    DSP::cycle c;
    c.table(table.data(), 64);
    CheckEqual(a, Reference(0, LENGTH, [&](auto&, unsigned, unsigned n) -> DSP::buffer& {
                 return c(440.f, 0.f, n);
               }));

    // The table is borrowed, not copied: editing it changes the output.
    std::fill(table.begin(), table.end(), 0.25f);
    CHECK(yse_dsp_cycle_process(p, 440.f, 0.f, b.data(), LENGTH) == LENGTH);
    CHECK(std::all_of(b.begin(), b.end(), [](float v) { return v == 0.25f; }));

    // An empty table is silence; cosine() goes back to the built-in table.
    yse_dsp_cycle_set_table(p, table.data(), 0);
    CHECK(yse_dsp_cycle_process(p, 440.f, 0.f, b.data(), LENGTH) == LENGTH);
    CHECK(std::all_of(b.begin(), b.end(), [](float v) { return v == 0.f; }));
    yse_dsp_cycle_cosine(p);
    yse_dsp_cycle_reset(p, 0.f);
    CHECK(yse_dsp_cycle_process(p, 440.f, 0.f, b.data(), 1) == 1);
    CHECK(b[0] == doctest::Approx(1.f));
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api oscillators: reset returns click and pulseTrain to their start") {
    std::vector<float> a(LENGTH), b(LENGTH);

    auto* c = yse_dsp_click_create();
    yse_dsp_click_trigger(c, 1.f);
    yse_dsp_click_reset(c); // forgets the pending trigger
    CHECK(yse_dsp_click_process(c, a.data(), LENGTH) == LENGTH);
    CHECK(std::all_of(a.begin(), a.end(), [](float v) { return v == 0.f; }));
    CHECK(yse_dsp_click_get_length(c) == 1); // the waveform stays
    yse_dsp_processor_destroy(c);

    auto* fresh = yse_dsp_pulse_train_create();
    auto* used = yse_dsp_pulse_train_create();
    for (YseDspProcessor* p : {fresh, used})
      yse_dsp_pulse_train_set_interval(p, 3.f);
    CHECK(yse_dsp_pulse_train_process(used, b.data(), nullptr, 101) == 101);
    yse_dsp_pulse_train_reset(used);
    CHECK(yse_dsp_pulse_train_get_interval(used) == 3.f); // keeps the settings
    CHECK(yse_dsp_pulse_train_process(fresh, a.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_pulse_train_process(used, b.data(), nullptr, LENGTH) == LENGTH);
    REQUIRE(Moves(a));
    CheckEqual(b, a);
    yse_dsp_processor_destroy(fresh);
    yse_dsp_processor_destroy(used);
  }

  TEST_CASE("c api oscillators: the noise generators are repeatable from a seed") {
    std::vector<float> a(LENGTH), b(LENGTH), c(LENGTH);
    auto* p = yse_dsp_pink_noise_create();
    auto* q = yse_dsp_pink_noise_create();
    yse_dsp_pink_noise_process(p, a.data(), LENGTH);
    yse_dsp_pink_noise_seed(q, YSE_DSP_NOISE_DEFAULT_SEED);
    yse_dsp_pink_noise_process(q, b.data(), LENGTH);
    CheckEqual(a, b); // a new generator starts from the default seed
    yse_dsp_pink_noise_seed(p, 42u);
    yse_dsp_pink_noise_process(p, a.data(), LENGTH);
    yse_dsp_pink_noise_seed(q, 42u);
    yse_dsp_pink_noise_process(q, b.data(), LENGTH);
    CheckEqual(a, b);
    yse_dsp_pink_noise_seed(q, 43u);
    yse_dsp_pink_noise_process(q, c.data(), LENGTH);
    CHECK(a != c);

    auto* r = yse_dsp_band_limited_random_create();
    yse_dsp_band_limited_random_seed(r, 5u);
    yse_dsp_band_limited_random_process(r, 500.f, a.data(), LENGTH);
    yse_dsp_band_limited_random_seed(r, 5u);
    yse_dsp_band_limited_random_process(r, 500.f, b.data(), LENGTH);
    CheckEqual(a, b);
    CHECK(a[0] == 0.f); // a reseed starts from 0, as a new generator does

    for (YseDspProcessor* h : {p, q, r})
      yse_dsp_processor_destroy(h);
  }

  TEST_CASE("c api oscillators: the handle-free value calls match the static formulas") {
    // Equal, or both NaN: the formulas decide what a NaN phase gives.
    auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    for (float x : {-0.4f, 0.f, 0.1f, 0.3f, 0.5f, 0.77f, 0.99f, 1.f, 4.3f, NAN}) {
      INFO("phase " << x);
      for (float s : {0.f, 0.5f, 1.f, 2.5f, -1.f})
        CHECK(same(yse_dsp_kink_value(x, s), DSP::kink::process(x, s)));
      for (float pk : {0.f, 0.3f, 0.5f, 1.f, 2.f})
        CHECK(same(yse_dsp_triangle_shape_value(x, pk, -1.f, 1.f),
                   DSP::triangleShape::process(x, pk, -1.f, 1.f)));
      CHECK(same(yse_dsp_triangle_shape_value(x, 0.25f, 3.f, -2.f),
                 DSP::triangleShape::process(x, 0.25f, 3.f, -2.f)));
      for (int wrap : {0, 1})
        CHECK(same(yse_dsp_trapezoid_shape_value(x, 0.2f, 0.6f, -1.f, 2.f, wrap),
                   DSP::trapezoidShape::process(x, 0.2f, 0.6f, -1.f, 2.f, wrap != 0)));
    }
    CHECK(yse_dsp_triangle_shape_value(0.25f, 0.5f, -1.f, 1.f) == doctest::Approx(0.f));
    CHECK(yse_dsp_trapezoid_shape_value(0.5f, YSE_DSP_TRAPEZOID_DEFAULT_UP,
                                        YSE_DSP_TRAPEZOID_DEFAULT_DOWN, 0.f, 1.f, 1) == 1.f);
  }

  TEST_CASE("c api oscillators: process, value and setter calls allocate nothing") {
    const std::vector<float> freq = Signal(LENGTH, 300.f, 500.f);
    const std::vector<float> unit = Signal(LENGTH, 0.4f, 0.5f);
    std::vector<float> o1(LENGTH), o2(LENGTH);
    const float pairs[4] = {220.f, 0.5f, 330.f, 0.25f};
    const float wave[4] = {1.f, 0.5f, -0.5f, -1.f};
    std::vector<float> table(64, 0.5f);

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* ph = all[0];
    YseDspProcessor* cy = all[1];
    YseDspProcessor* sw = all[2];
    YseDspProcessor* rc = all[3];
    YseDspProcessor* tr = all[4];
    YseDspProcessor* pn = all[5];
    YseDspProcessor* br = all[6];
    YseDspProcessor* cl = all[7];
    YseDspProcessor* pt = all[8];
    YseDspProcessor* kk = all[9];
    YseDspProcessor* ob = all[10];
    YseDspProcessor* ts = all[11];
    YseDspProcessor* zs = all[12];
    YseDspProcessor* sc = all[13];
    // set_table allocates the first time a bank sees a table this size, so
    // that call happens before the probe; a later one at the same size must
    // not allocate.
    yse_dsp_oscillator_bank_set_table(ob, table.data(), 64);

    {
      TestHelpers::ProbeScope probe;
      yse_dsp_phasor_set_sync_update(ph, 1);
      yse_dsp_phasor_set_phase(ph, 0.5f);
      yse_dsp_phasor_update(ph);
      yse_dsp_phasor_process(ph, 440.f, o1.data(), LENGTH);
      yse_dsp_phasor_process_signal(ph, freq.data(), o1.data(), LENGTH);
      yse_dsp_cycle_set_table(cy, table.data(), 64);
      yse_dsp_cycle_process_signal(cy, freq.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_cycle_cosine(cy);
      yse_dsp_cycle_process_frequency(cy, freq.data(), 0.f, o1.data(), LENGTH);
      yse_dsp_cycle_process_phase(cy, 440.f, unit.data(), o1.data(), LENGTH);
      yse_dsp_bl_saw_set_sync_threshold(sw, 0.3f);
      yse_dsp_bl_saw_process_sync(sw, freq.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_bl_rect_set_width(rc, 0.3f);
      yse_dsp_bl_rect_process_sync(rc, freq.data(), unit.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_bl_tri_set_duty(tr, 0.3f);
      yse_dsp_bl_tri_process_signal(tr, freq.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_pink_noise_seed(pn, 3u);
      yse_dsp_pink_noise_process(pn, o1.data(), LENGTH);
      yse_dsp_band_limited_random_seed(br, 3u);
      yse_dsp_band_limited_random_process_signal(br, freq.data(), o1.data(), LENGTH);
      yse_dsp_click_set(cl, wave, 4);
      yse_dsp_click_trigger(cl, 1.f);
      yse_dsp_click_process(cl, o1.data(), LENGTH);
      yse_dsp_click_process_signal(cl, unit.data(), o1.data(), LENGTH);
      yse_dsp_pulse_train_set_interval(pt, 2.f);
      yse_dsp_pulse_train_trigger(pt);
      yse_dsp_pulse_train_process(pt, o1.data(), o2.data(), LENGTH);
      yse_dsp_pulse_train_process_signal(pt, unit.data(), unit.data(), unit.data(), o1.data(),
                                         o2.data(), LENGTH);
      yse_dsp_kink_process_signal(kk, unit.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_oscillator_bank_set_table(ob, table.data(), 64);
      yse_dsp_oscillator_bank_set_pairs(ob, pairs, 2);
      yse_dsp_oscillator_bank_set_phase(ob, 1, 0.5f);
      yse_dsp_oscillator_bank_set_table_size(ob, 256);
      yse_dsp_oscillator_bank_process(ob, o1.data(), LENGTH);
      yse_dsp_oscillator_bank_process_signal(ob, freq.data(), unit.data(), unit.data(), unit.data(),
                                             o1.data(), LENGTH);
      yse_dsp_triangle_shape_set_range(ts, 0.f, 1.f);
      yse_dsp_triangle_shape_process_signal(ts, unit.data(), unit.data(), o1.data(), LENGTH);
      yse_dsp_trapezoid_shape_set_wrap(zs, 0);
      yse_dsp_trapezoid_shape_process(zs, unit.data(), 0.2f, 0.8f, o1.data(), LENGTH);
      yse_dsp_sample_counter_start(sc);
      yse_dsp_sample_counter_process(sc, o1.data(), o2.data(), LENGTH);
      yse_dsp_sample_counter_process_signal(sc, unit.data(), o1.data(), o2.data(), LENGTH);
      volatile float v = yse_dsp_kink_value(0.3f, 2.f) +
                         yse_dsp_triangle_shape_value(0.3f, 0.5f, -1.f, 1.f) +
                         yse_dsp_trapezoid_shape_value(0.3f, 0.1f, 0.9f, 0.f, 1.f, 1);
      (void)v;
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_phasor_process(sc, 440.f, o1.data(), LENGTH);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
