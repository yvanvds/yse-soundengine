// C-API tests for the filter processors of the MSP port (issue #1173):
// yse_c/yse_dsp_processor.h (the shared YseDspProcessor handle) and
// yse_c/yse_dsp_filters.h (onePole, lores, reson, stateVariableFilter,
// allpass, phaseShift, biquadCascade, crossover, filterBank,
// filterCoefficients, slide, rampSmooth, slewLimiter).
//
// The DSP itself is covered by each class's own suite (test_lores.cpp, ...).
// What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated (the handle owns its object
//     through a polymorphic wrapper, as YseDspBuffer does since #662).
//   * The class check — a handle passed to another class's functions is
//     treated like NULL: setters no-op, getters and process functions return
//     0, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair.
//   * Parity — the C call produces exactly what the engine class produces
//     when driven directly in 128-sample blocks, for a length that is not a
//     multiple of 128 (so the bridge's block split is exercised), in place
//     as well as out of place, for the plain, the signal-rate and the
//     per-sample entry points.
//   * Real-time safety — process, sample and setter calls allocate nothing.
//
// No engine session is needed: processors are plain heap objects that read the
// global SAMPLERATE, which the C and C++ sides here share.

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include "support/alloc_probe.hpp"

#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_processor.h"
#include "yse_c/yse_enums.h"

#include "dsp/buffer.hpp"
#include "dsp/filters/allpass.hpp"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/crossover.hpp"
#include "dsp/filters/filterBank.hpp"
#include "dsp/filters/filterCoefficients.hpp"
#include "dsp/filters/lores.hpp"
#include "dsp/filters/onePole.hpp"
#include "dsp/filters/phaseShift.hpp"
#include "dsp/filters/rampSmooth.hpp"
#include "dsp/filters/reson.hpp"
#include "dsp/filters/slewLimiter.hpp"
#include "dsp/filters/slide.hpp"
#include "dsp/filters/stateVariableFilter.hpp"
#include "headers/constants.hpp"

namespace {
  namespace DSP = YSE::DSP;
  constexpr unsigned BLOCK = YSE::STANDARD_BUFFERSIZE;
  // Not a multiple of the block size: the last block is short.
  constexpr unsigned LENGTH = 3 * BLOCK + 37;

  // A deterministic, broadband test signal.
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

  // Drives an engine object directly the way the bridge must: consecutive
  // blocks of at most BLOCK samples. `block` stages the inputs for one block
  // (offset, count, scratch buffers) and returns the engine output buffer.
  using BlockFn = std::function<DSP::buffer&(std::vector<DSP::buffer>&, unsigned, unsigned)>;

  std::vector<float> Reference(unsigned inputs, unsigned length, const BlockFn& block) {
    std::vector<float> out(length, 0.f);
    std::vector<DSP::buffer> scratch(inputs);
    for (unsigned offset = 0; offset < length; offset += BLOCK) {
      const unsigned n = std::min(BLOCK, length - offset);
      DSP::buffer& r = block(scratch, offset, n);
      REQUIRE(r.getLength() == n);
      std::memcpy(out.data() + offset, r.getPtr(), sizeof(float) * n);
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

  using Ctor = std::function<YseDspProcessor*()>;

  struct Named {
    const char* name;
    Ctor make;
  };

  std::vector<Named> AllCtors() {
    return {
        {"one_pole", [] { return yse_dsp_one_pole_create(); }},
        {"lores", [] { return yse_dsp_lores_create(); }},
        {"reson", [] { return yse_dsp_reson_create(); }},
        {"svf", [] { return yse_dsp_svf_create(); }},
        {"allpass", [] { return yse_dsp_allpass_create(10.f, 0); }},
        {"phase_shift", [] { return yse_dsp_phase_shift_create(); }},
        {"biquad_cascade", [] { return yse_dsp_biquad_cascade_create(0); }},
        {"crossover", [] { return yse_dsp_crossover_create(); }},
        {"filter_bank", [] { return yse_dsp_filter_bank_create(0); }},
        {"filter_coefficients", [] { return yse_dsp_filter_coefficients_create(); }},
        {"slide", [] { return yse_dsp_slide_create(); }},
        {"ramp_smooth", [] { return yse_dsp_ramp_smooth_create(); }},
        {"slew_limiter", [] { return yse_dsp_slew_limiter_create(); }},
    };
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api filters: every constructor pairs with destroy and frees what it allocated") {
    yse_dsp_processor_destroy(nullptr); // no-op

    for (const Named& c : AllCtors()) {
      INFO("constructor: " << c.name);
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

  TEST_CASE("c api filters: a handle of another class is refused like NULL") {
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(lp != nullptr);
    yse_dsp_lores_set_cutoff(lp, 500.f);
    yse_dsp_lores_set_resonance(lp, 0.25f);

    // Every other class's functions refuse it ...
    yse_dsp_reson_set_q(lp, 99.f);
    yse_dsp_one_pole_set_frequency(lp, 1.f);
    yse_dsp_svf_clear(lp);
    yse_dsp_slide_reset(lp);
    yse_dsp_biquad_cascade_set_bypass(lp, 1);
    CHECK(yse_dsp_reson_get_q(lp) == 0.f);
    CHECK(yse_dsp_one_pole_get_frequency(lp) == 0.f);
    CHECK(yse_dsp_filter_bank_get_size(lp) == 0);
    CHECK(yse_dsp_biquad_cascade_get_bypass(lp) == 0);
    CHECK(yse_dsp_svf_get_mode(lp) == YSE_DSP_FREQUENCY_HZ);
    CHECK(yse_dsp_filter_coefficients_get_type(lp) == YSE_DSP_FILTER_OFF);

    std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f);
    CHECK(yse_dsp_reson_process(lp, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_svf_process(lp, in.data(), out.data(), nullptr, nullptr, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_filter_coefficients_process(lp, out.data(), nullptr, nullptr, nullptr, nullptr,
                                              LENGTH) == 0);
    CHECK(yse_dsp_one_pole_process_sample(lp, 1.f) == 0.f);
    float low = 7.f;
    yse_dsp_crossover_process_sample(lp, 1.f, &low, nullptr);
    CHECK(low == 0.f);
    for (float v : out)
      REQUIRE(v == 7.f);

    // ... and its own state is what it was.
    CHECK(yse_dsp_lores_get_cutoff(lp) == 500.f);
    CHECK(yse_dsp_lores_get_resonance(lp) == 0.25f);
    CHECK(yse_dsp_lores_process(lp, in.data(), out.data(), LENGTH) == LENGTH);
    yse_dsp_processor_destroy(lp);
  }

  TEST_CASE("c api filters: NULL handles and NULL arrays are no-ops that write nothing") {
    std::vector<float> in = Signal(LENGTH);
    std::vector<float> out(LENGTH, 7.f);
    float c5[5] = {7.f, 7.f, 7.f, 7.f, 7.f};

    // NULL handle: setters no-op, getters 0, process 0.
    yse_dsp_one_pole_set_frequency(nullptr, 1.f);
    yse_dsp_svf_set_mode(nullptr, YSE_DSP_FREQUENCY_LINEAR);
    yse_dsp_filter_bank_set_harmonic(nullptr, 100.f);
    yse_dsp_biquad_cascade_set_coefficients(nullptr, c5, 5);
    CHECK(yse_dsp_one_pole_get_frequency(nullptr) == 0.f);
    CHECK(yse_dsp_one_pole_get_mode(nullptr) == YSE_DSP_FREQUENCY_HZ);
    CHECK(yse_dsp_allpass_get_capacity(nullptr) == 0.f);
    CHECK(yse_dsp_biquad_cascade_get_capacity(nullptr) == 0);
    CHECK(yse_dsp_filter_coefficients_get_resampling(nullptr) == 0);
    CHECK(yse_dsp_slew_limiter_get_value(nullptr) == 0.f);
    CHECK(yse_dsp_biquad_cascade_get_coefficients(nullptr, 0, c5) == 0);
    CHECK(yse_dsp_filter_bank_get_coefficients(nullptr, 0, c5) == 0);
    CHECK(yse_dsp_filter_coefficients_get_coefficients(nullptr, c5) == 0);
    CHECK(c5[0] == 7.f);
    CHECK(yse_dsp_lores_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_lores_process_signal(nullptr, in.data(), in.data(), in.data(), out.data(),
                                       LENGTH) == 0);
    CHECK(yse_dsp_filter_bank_process(nullptr, in.data(), nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_lores_process_sample(nullptr, 1.f) == 0.f);
    float l = 7.f, h = 7.f, b = 7.f, n = 7.f;
    yse_dsp_svf_process_sample(nullptr, 1.f, &l, &h, &b, &n);
    CHECK(l == 0.f);
    CHECK(h == 0.f);
    CHECK(b == 0.f);
    CHECK(n == 0.f);
    yse_dsp_svf_process_sample(nullptr, 1.f, nullptr, nullptr, nullptr, nullptr);

    // Live handle, NULL required array: 0 and nothing written.
    YseDspProcessor* p = yse_dsp_reson_create();
    CHECK(yse_dsp_reson_process(p, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_reson_process(p, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_reson_process_signal(p, in.data(), in.data(), nullptr, in.data(), out.data(),
                                       LENGTH) == 0);
    CHECK(yse_dsp_reson_process(p, in.data(), out.data(), 0) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    CHECK(yse_dsp_filter_coefficients_compute(YSE_DSP_FILTER_LOWPASS, 1000.f, 1.f, 0.7f, 48000.f,
                                              nullptr) == 0);
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api filters: setters and getters round-trip") {
    YseDspProcessor* p = yse_dsp_one_pole_create();
    yse_dsp_one_pole_set_frequency(p, 0.25f);
    yse_dsp_one_pole_set_mode(p, YSE_DSP_FREQUENCY_LINEAR);
    CHECK(yse_dsp_one_pole_get_frequency(p) == 0.25f);
    CHECK(yse_dsp_one_pole_get_mode(p) == YSE_DSP_FREQUENCY_LINEAR);
    yse_dsp_one_pole_set_mode(p, static_cast<YseDspFrequencyMode>(17)); // out of range: kept
    CHECK(yse_dsp_one_pole_get_mode(p) == YSE_DSP_FREQUENCY_LINEAR);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_reson_create();
    yse_dsp_reson_set_gain(p, 2.f);
    yse_dsp_reson_set_frequency(p, 440.f);
    yse_dsp_reson_set_q(p, 12.f);
    CHECK(yse_dsp_reson_get_gain(p) == 2.f);
    CHECK(yse_dsp_reson_get_frequency(p) == 440.f);
    CHECK(yse_dsp_reson_get_q(p) == 12.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_svf_create();
    yse_dsp_svf_set_frequency(p, 0.3f);
    yse_dsp_svf_set_resonance(p, 0.6f);
    yse_dsp_svf_set_mode(p, YSE_DSP_FREQUENCY_RADIANS);
    CHECK(yse_dsp_svf_get_frequency(p) == 0.3f);
    CHECK(yse_dsp_svf_get_resonance(p) == 0.6f);
    CHECK(yse_dsp_svf_get_mode(p) == YSE_DSP_FREQUENCY_RADIANS);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_allpass_create(20.f, 96000);
    yse_dsp_allpass_set_delay(p, 5.f);
    yse_dsp_allpass_set_gain(p, 3.f); // clamped
    CHECK(yse_dsp_allpass_get_delay(p) == 5.f);
    CHECK(yse_dsp_allpass_get_gain(p) == 1.f);
    CHECK(yse_dsp_allpass_get_maximum_delay(p) == 20.f);
    DSP::allpass ref(20.f, 96000);
    ref.delay(5.f);
    CHECK(yse_dsp_allpass_get_capacity(p) == ref.capacity());
    CHECK(yse_dsp_allpass_get_delay_samples(p) == ref.delaySamples());
    yse_dsp_processor_destroy(p);

    p = yse_dsp_phase_shift_create();
    yse_dsp_phase_shift_set_frequency(p, 300.f);
    yse_dsp_phase_shift_set_q(p, 4.f);
    CHECK(yse_dsp_phase_shift_get_frequency(p) == 300.f);
    CHECK(yse_dsp_phase_shift_get_q(p) == 4.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_biquad_cascade_create(0);
    CHECK(yse_dsp_biquad_cascade_get_capacity(p) == DSP::biquadCascade::DEFAULT_CAPACITY);
    CHECK(yse_dsp_biquad_cascade_get_sections(p) == 0);
    const float coeffs[10] = {0.5f, 0.2f, 0.1f, -0.3f, 0.05f, 1.f, 0.f, 0.f, 0.f, 0.f};
    yse_dsp_biquad_cascade_set_coefficients(p, coeffs, 10);
    CHECK(yse_dsp_biquad_cascade_get_sections(p) == 2);
    float got[5] = {};
    CHECK(yse_dsp_biquad_cascade_get_coefficients(p, 0, got) == 1);
    for (int i = 0; i < 5; ++i)
      CHECK(got[i] == coeffs[i]);
    yse_dsp_biquad_cascade_set_smooth(p, 0);
    yse_dsp_biquad_cascade_set_bypass(p, 1);
    yse_dsp_biquad_cascade_set_zero(p, 1);
    CHECK(yse_dsp_biquad_cascade_get_smooth(p) == 0);
    CHECK(yse_dsp_biquad_cascade_get_bypass(p) == 1);
    CHECK(yse_dsp_biquad_cascade_get_zero(p) == 1);
    yse_dsp_processor_destroy(p);
    p = yse_dsp_biquad_cascade_create(1000); // clamped
    CHECK(yse_dsp_biquad_cascade_get_capacity(p) == DSP::biquadCascade::MAX_CAPACITY);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_crossover_create();
    yse_dsp_crossover_set_cutoff(p, 2500.f);
    CHECK(yse_dsp_crossover_get_cutoff(p) == 2500.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_filter_bank_create(6);
    CHECK(yse_dsp_filter_bank_get_size(p) == 6);
    yse_dsp_filter_bank_set_frequency(p, 2, 900.f);
    yse_dsp_filter_bank_set_q(p, 2, 7.f);
    yse_dsp_filter_bank_set_gain(p, 2, 0.5f);
    CHECK(yse_dsp_filter_bank_get_frequency(p, 2) == 900.f);
    CHECK(yse_dsp_filter_bank_get_q(p, 2) == 7.f);
    CHECK(yse_dsp_filter_bank_get_gain(p, 2) == 0.5f);
    yse_dsp_filter_bank_set_harmonic(p, 110.f);
    CHECK(yse_dsp_filter_bank_get_frequency(p, 3) == DSP::filterBank::HarmonicFrequency(110.f, 3));
    yse_dsp_filter_bank_set_frequency_ratio(p, 100.f, 1.5f);
    CHECK(yse_dsp_filter_bank_get_frequency(p, 4) ==
          DSP::filterBank::RatioFrequency(100.f, 1.5f, 4));
    yse_dsp_filter_bank_set_frequency_all(p, 250.f);
    yse_dsp_filter_bank_set_q_all(p, 3.f);
    yse_dsp_filter_bank_set_gain_all(p, 0.25f);
    CHECK(yse_dsp_filter_bank_get_frequency(p, 5) == 250.f);
    CHECK(yse_dsp_filter_bank_get_q(p, 0) == 3.f);
    CHECK(yse_dsp_filter_bank_get_gain(p, 1) == 0.25f);
    {
      DSP::filterBank ref(6);
      ref.frequencyAll(250.f).qAll(3.f).gainAll(0.25f);
      const auto set = ref.coefficients(1);
      CHECK(yse_dsp_filter_bank_get_coefficients(p, 1, got) == 1);
      CHECK(got[0] == set.a0);
      CHECK(got[3] == set.b1);
      CHECK(got[4] == set.b2);
    }
    yse_dsp_processor_destroy(p);
    p = yse_dsp_filter_bank_create(0);
    CHECK(yse_dsp_filter_bank_get_size(p) == DSP::filterBank::DEFAULT_FILTERS);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_filter_coefficients_create();
    CHECK(yse_dsp_filter_coefficients_get_type(p) == YSE_DSP_FILTER_LOWPASS);
    yse_dsp_filter_coefficients_set_type(p, YSE_DSP_FILTER_PEAKNOTCH);
    yse_dsp_filter_coefficients_set_type(p, YSE_DSP_FILTER_TYPE_COUNT); // out of range: kept
    yse_dsp_filter_coefficients_set_frequency(p, 2000.f);
    yse_dsp_filter_coefficients_set_gain(p, 2.f);
    yse_dsp_filter_coefficients_set_q(p, 3.f);
    yse_dsp_filter_coefficients_set_resampling(p, 8);
    CHECK(yse_dsp_filter_coefficients_get_type(p) == YSE_DSP_FILTER_PEAKNOTCH);
    CHECK(yse_dsp_filter_coefficients_get_frequency(p) == 2000.f);
    CHECK(yse_dsp_filter_coefficients_get_gain(p) == 2.f);
    CHECK(yse_dsp_filter_coefficients_get_q(p) == 3.f);
    CHECK(yse_dsp_filter_coefficients_get_resampling(p) == 8);
    {
      const auto set =
          DSP::filterCoefficients::Compute(DSP::filterCoefficients::PEAKNOTCH, 2000.f, 2.f, 3.f,
                                           static_cast<float>(YSE::SAMPLERATE));
      CHECK(yse_dsp_filter_coefficients_get_coefficients(p, got) == 1);
      CHECK(got[0] == set.a0);
      CHECK(got[1] == set.a1);
      CHECK(got[2] == set.a2);
      CHECK(got[3] == set.b1);
      CHECK(got[4] == set.b2);
    }
    yse_dsp_processor_destroy(p);

    p = yse_dsp_slide_create();
    yse_dsp_slide_set_up(p, 10.f);
    yse_dsp_slide_set_down(p, 20.f);
    CHECK(yse_dsp_slide_get_up(p) == 10.f);
    CHECK(yse_dsp_slide_get_down(p) == 20.f);
    yse_dsp_slide_process_sample(p, 1.f);
    CHECK(yse_dsp_slide_get_value(p) > 0.f);
    yse_dsp_slide_reset(p);
    CHECK(yse_dsp_slide_get_value(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_ramp_smooth_create();
    yse_dsp_ramp_smooth_set_up(p, 10.f);
    yse_dsp_ramp_smooth_set_down(p, 20.f);
    CHECK(yse_dsp_ramp_smooth_get_up(p) == 10.f);
    CHECK(yse_dsp_ramp_smooth_get_down(p) == 20.f);
    yse_dsp_ramp_smooth_set_ramp(p, 4.f);
    CHECK(yse_dsp_ramp_smooth_get_up(p) == 4.f);
    CHECK(yse_dsp_ramp_smooth_get_down(p) == 4.f);
    yse_dsp_ramp_smooth_process_sample(p, 1.f);
    CHECK(yse_dsp_ramp_smooth_get_value(p) == doctest::Approx(0.25f));
    yse_dsp_ramp_smooth_reset(p);
    CHECK(yse_dsp_ramp_smooth_get_value(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_slew_limiter_create();
    yse_dsp_slew_limiter_set_minimum(p, -0.1f);
    yse_dsp_slew_limiter_set_maximum(p, 0.2f);
    CHECK(yse_dsp_slew_limiter_get_minimum(p) == -0.1f);
    CHECK(yse_dsp_slew_limiter_get_maximum(p) == 0.2f);
    yse_dsp_slew_limiter_set_value(p, 0.5f);
    CHECK(yse_dsp_slew_limiter_get_value(p) == 0.5f);
    CHECK(yse_dsp_slew_limiter_process_sample(p, 1.f) == doctest::Approx(0.7f));
    yse_dsp_slew_limiter_reset(p);
    CHECK(yse_dsp_slew_limiter_get_value(p) == 0.f);
    yse_dsp_processor_destroy(p);

    p = yse_dsp_lores_create();
    yse_dsp_lores_set_cutoff(p, 1200.f);
    yse_dsp_lores_set_resonance(p, 0.4f);
    CHECK(yse_dsp_lores_get_cutoff(p) == 1200.f);
    CHECK(yse_dsp_lores_get_resonance(p) == 0.4f);
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE(
      "c api filters: one-in one-out processors match the engine class, in and out of place") {
    const std::vector<float> in = Signal(LENGTH);

    // Each entry: the C process call on a configured handle, and the engine
    // class configured the same way.
    struct Case {
      const char* name;
      std::function<YseDspProcessor*()> make;
      std::function<unsigned(YseDspProcessor*, const float*, float*, unsigned)> process;
      std::function<std::vector<float>()> reference;
    };

    const std::vector<Case> cases = {
        {"one_pole",
         [] {
           auto* p = yse_dsp_one_pole_create();
           yse_dsp_one_pole_set_frequency(p, 800.f);
           return p;
         },
         yse_dsp_one_pole_process,
         [&] {
           DSP::onePole f;
           f.frequency(800.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"lores",
         [] {
           auto* p = yse_dsp_lores_create();
           yse_dsp_lores_set_cutoff(p, 1500.f);
           yse_dsp_lores_set_resonance(p, 0.7f);
           return p;
         },
         yse_dsp_lores_process,
         [&] {
           DSP::lores f;
           f.cutoff(1500.f).resonance(0.7f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"reson",
         [] {
           auto* p = yse_dsp_reson_create();
           yse_dsp_reson_set_gain(p, 1.5f);
           yse_dsp_reson_set_frequency(p, 600.f);
           yse_dsp_reson_set_q(p, 8.f);
           return p;
         },
         yse_dsp_reson_process,
         [&] {
           DSP::reson f;
           f.gain(1.5f).frequency(600.f).q(8.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"allpass",
         [] {
           auto* p = yse_dsp_allpass_create(10.f, 0);
           yse_dsp_allpass_set_delay(p, 2.3f);
           yse_dsp_allpass_set_gain(p, 0.6f);
           return p;
         },
         yse_dsp_allpass_process,
         [&] {
           DSP::allpass f(10.f);
           f.delay(2.3f).gain(0.6f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"phase_shift",
         [] {
           auto* p = yse_dsp_phase_shift_create();
           yse_dsp_phase_shift_set_frequency(p, 700.f);
           yse_dsp_phase_shift_set_q(p, 2.f);
           return p;
         },
         yse_dsp_phase_shift_process,
         [&] {
           DSP::phaseShift f;
           f.frequency(700.f).q(2.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"biquad_cascade",
         [] {
           auto* p = yse_dsp_biquad_cascade_create(4);
           const float c[10] = {0.2f, 0.4f, 0.2f, -0.5f, 0.3f, 0.9f, -0.1f, 0.f, 0.2f, 0.f};
           yse_dsp_biquad_cascade_set_coefficients(p, c, 10);
           return p;
         },
         yse_dsp_biquad_cascade_process,
         [&] {
           DSP::biquadCascade f(4);
           const float c[10] = {0.2f, 0.4f, 0.2f, -0.5f, 0.3f, 0.9f, -0.1f, 0.f, 0.2f, 0.f};
           f.coefficients(c, 10);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"slide",
         [] {
           auto* p = yse_dsp_slide_create();
           yse_dsp_slide_set_up(p, 30.f);
           yse_dsp_slide_set_down(p, 5.f);
           return p;
         },
         yse_dsp_slide_process,
         [&] {
           DSP::slide f;
           f.up(30.f).down(5.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"ramp_smooth",
         [] {
           auto* p = yse_dsp_ramp_smooth_create();
           yse_dsp_ramp_smooth_set_up(p, 40.f);
           yse_dsp_ramp_smooth_set_down(p, 9.f);
           return p;
         },
         yse_dsp_ramp_smooth_process,
         [&] {
           DSP::rampSmooth f;
           f.up(40.f).down(9.f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
        {"slew_limiter",
         [] {
           auto* p = yse_dsp_slew_limiter_create();
           yse_dsp_slew_limiter_set_minimum(p, -0.01f);
           yse_dsp_slew_limiter_set_maximum(p, 0.02f);
           return p;
         },
         yse_dsp_slew_limiter_process,
         [&] {
           DSP::slewLimiter f;
           f.minimum(-0.01f).maximum(0.02f);
           return Reference(1, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
             return f(Stage(s[0], in, o, n));
           });
         }},
    };

    for (const Case& c : cases) {
      INFO("processor: " << c.name);
      const std::vector<float> expected = c.reference();

      YseDspProcessor* p = c.make();
      std::vector<float> out(LENGTH, 0.f);
      CHECK(c.process(p, in.data(), out.data(), LENGTH) == LENGTH);
      CheckEqual(out, expected);
      yse_dsp_processor_destroy(p);

      // In place: the output array is the input array.
      p = c.make();
      std::vector<float> io = in;
      CHECK(c.process(p, io.data(), io.data(), LENGTH) == LENGTH);
      CheckEqual(io, expected);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api filters: signal-rate processing matches the engine class") {
    const std::vector<float> in = Signal(LENGTH);
    const std::vector<float> freq = Signal(LENGTH, 400.f, 1000.f);
    const std::vector<float> amount = Signal(LENGTH, 0.3f, 0.5f);
    const std::vector<float> wide = Signal(LENGTH, 4.f, 6.f);
    std::vector<float> out(LENGTH);

    SUBCASE("one_pole") {
      auto* p = yse_dsp_one_pole_create();
      CHECK(yse_dsp_one_pole_process_signal(p, in.data(), freq.data(), out.data(), LENGTH) ==
            LENGTH);
      CHECK(yse_dsp_one_pole_get_frequency(p) == freq[3 * BLOCK]);
      DSP::onePole f;
      CheckEqual(out, Reference(2, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], freq, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("lores") {
      auto* p = yse_dsp_lores_create();
      CHECK(yse_dsp_lores_process_signal(p, in.data(), freq.data(), amount.data(), out.data(),
                                         LENGTH) == LENGTH);
      DSP::lores f;
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], freq, o, n),
                            Stage(s[2], amount, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("reson") {
      auto* p = yse_dsp_reson_create();
      CHECK(yse_dsp_reson_process_signal(p, in.data(), amount.data(), freq.data(), wide.data(),
                                         out.data(), LENGTH) == LENGTH);
      DSP::reson f;
      CheckEqual(out, Reference(4, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], amount, o, n),
                            Stage(s[2], freq, o, n), Stage(s[3], wide, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("allpass") {
      auto* p = yse_dsp_allpass_create(10.f, 0);
      CHECK(yse_dsp_allpass_process_signal(p, in.data(), wide.data(), amount.data(), out.data(),
                                           LENGTH) == LENGTH);
      DSP::allpass f(10.f);
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], wide, o, n),
                            Stage(s[2], amount, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("phase_shift") {
      auto* p = yse_dsp_phase_shift_create();
      CHECK(yse_dsp_phase_shift_process_signal(p, in.data(), freq.data(), wide.data(), out.data(),
                                               LENGTH) == LENGTH);
      DSP::phaseShift f;
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], freq, o, n),
                            Stage(s[2], wide, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("slide") {
      auto* p = yse_dsp_slide_create();
      CHECK(yse_dsp_slide_process_signal(p, in.data(), wide.data(), wide.data(), out.data(),
                                         LENGTH) == LENGTH);
      DSP::slide f;
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], wide, o, n),
                            Stage(s[2], wide, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("ramp_smooth") {
      auto* p = yse_dsp_ramp_smooth_create();
      CHECK(yse_dsp_ramp_smooth_process_signal(p, in.data(), wide.data(), wide.data(), out.data(),
                                               LENGTH) == LENGTH);
      DSP::rampSmooth f;
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], wide, o, n),
                            Stage(s[2], wide, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("slew_limiter") {
      const std::vector<float> lo = Signal(LENGTH, 0.01f, -0.02f);
      const std::vector<float> hi = Signal(LENGTH, 0.01f, 0.02f);
      auto* p = yse_dsp_slew_limiter_create();
      CHECK(yse_dsp_slew_limiter_process_signal(p, in.data(), lo.data(), hi.data(), out.data(),
                                                LENGTH) == LENGTH);
      DSP::slewLimiter f;
      CheckEqual(out, Reference(3, LENGTH, [&](auto& s, unsigned o, unsigned n) -> DSP::buffer& {
                   return f(Stage(s[0], in, o, n), Stage(s[1], lo, o, n), Stage(s[2], hi, o, n));
                 }));
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api filters: multi-output processors fill every requested output") {
    const std::vector<float> in = Signal(LENGTH);
    const std::vector<float> freq = Signal(LENGTH, 400.f, 1000.f);
    const std::vector<float> amount = Signal(LENGTH, 0.3f, 0.5f);

    SUBCASE("svf, plain and signal-rate") {
      for (int signal = 0; signal < 2; ++signal) {
        INFO("signal-rate: " << signal);
        auto* p = yse_dsp_svf_create();
        yse_dsp_svf_set_frequency(p, 900.f);
        yse_dsp_svf_set_resonance(p, 0.3f);
        std::vector<float> low(LENGTH), high(LENGTH), band(LENGTH), notch(LENGTH);
        if (signal != 0) {
          CHECK(yse_dsp_svf_process_signal(p, in.data(), freq.data(), amount.data(), low.data(),
                                           high.data(), band.data(), notch.data(),
                                           LENGTH) == LENGTH);
        } else {
          CHECK(yse_dsp_svf_process(p, in.data(), low.data(), high.data(), band.data(),
                                    notch.data(), LENGTH) == LENGTH);
        }
        yse_dsp_processor_destroy(p);

        DSP::stateVariableFilter f;
        f.frequency(900.f).resonance(0.3f);
        std::vector<float> rl(LENGTH), rh(LENGTH), rb(LENGTH), rn(LENGTH);
        std::vector<DSP::buffer> s(3);
        for (unsigned o = 0; o < LENGTH; o += BLOCK) {
          const unsigned n = std::min(BLOCK, LENGTH - o);
          if (signal != 0) {
            f(Stage(s[0], in, o, n), Stage(s[1], freq, o, n), Stage(s[2], amount, o, n));
          } else {
            f(Stage(s[0], in, o, n));
          }
          std::memcpy(rl.data() + o, f.lowpass().getPtr(), sizeof(float) * n);
          std::memcpy(rh.data() + o, f.highpass().getPtr(), sizeof(float) * n);
          std::memcpy(rb.data() + o, f.bandpass().getPtr(), sizeof(float) * n);
          std::memcpy(rn.data() + o, f.notch().getPtr(), sizeof(float) * n);
        }
        CheckEqual(low, rl);
        CheckEqual(high, rh);
        CheckEqual(band, rb);
        CheckEqual(notch, rn);
      }

      // Optional outputs: only the bandpass requested, run in place.
      auto* p = yse_dsp_svf_create();
      yse_dsp_svf_set_frequency(p, 900.f);
      std::vector<float> io = in;
      CHECK(yse_dsp_svf_process(p, io.data(), nullptr, nullptr, io.data(), nullptr, LENGTH) ==
            LENGTH);
      CHECK(yse_dsp_svf_process(p, in.data(), nullptr, nullptr, nullptr, nullptr, LENGTH) ==
            LENGTH);
      yse_dsp_processor_destroy(p);
      DSP::stateVariableFilter f;
      f.frequency(900.f);
      std::vector<DSP::buffer> s(1);
      std::vector<float> rb(LENGTH);
      for (unsigned o = 0; o < LENGTH; o += BLOCK) {
        const unsigned n = std::min(BLOCK, LENGTH - o);
        f(Stage(s[0], in, o, n));
        std::memcpy(rb.data() + o, f.bandpass().getPtr(), sizeof(float) * n);
      }
      CheckEqual(io, rb);
    }

    SUBCASE("crossover, plain and signal-rate") {
      for (int signal = 0; signal < 2; ++signal) {
        INFO("signal-rate: " << signal);
        auto* p = yse_dsp_crossover_create();
        yse_dsp_crossover_set_cutoff(p, 1200.f);
        std::vector<float> low(LENGTH), high(LENGTH);
        if (signal != 0) {
          CHECK(yse_dsp_crossover_process_signal(p, in.data(), freq.data(), low.data(), high.data(),
                                                 LENGTH) == LENGTH);
        } else {
          CHECK(yse_dsp_crossover_process(p, in.data(), low.data(), high.data(), LENGTH) == LENGTH);
        }
        yse_dsp_processor_destroy(p);

        DSP::crossover f;
        f.cutoff(1200.f);
        std::vector<float> rl(LENGTH), rh(LENGTH);
        std::vector<DSP::buffer> s(2);
        for (unsigned o = 0; o < LENGTH; o += BLOCK) {
          const unsigned n = std::min(BLOCK, LENGTH - o);
          if (signal != 0) {
            f(Stage(s[0], in, o, n), Stage(s[1], freq, o, n));
          } else {
            f(Stage(s[0], in, o, n));
          }
          std::memcpy(rl.data() + o, f.lowpass().getPtr(), sizeof(float) * n);
          std::memcpy(rh.data() + o, f.highpass().getPtr(), sizeof(float) * n);
        }
        CheckEqual(low, rl);
        CheckEqual(high, rh);
      }
    }

    SUBCASE("filter bank: outputs beyond the bank and NULL entries are skipped") {
      auto* p = yse_dsp_filter_bank_create(3);
      yse_dsp_filter_bank_set_harmonic(p, 220.f);
      std::vector<float> o0(LENGTH), o2(LENGTH), extra(LENGTH, 7.f);
      float* outs[4] = {o0.data(), nullptr, o2.data(), extra.data()};
      CHECK(yse_dsp_filter_bank_process(p, in.data(), outs, 4, LENGTH) == LENGTH);
      CHECK(yse_dsp_filter_bank_process(p, in.data(), nullptr, 4, LENGTH) == LENGTH);
      yse_dsp_processor_destroy(p);
      for (float v : extra)
        REQUIRE(v == 7.f);

      DSP::filterBank f(3);
      f.harmonic(220.f);
      std::vector<float> r0(LENGTH), r2(LENGTH);
      std::vector<DSP::buffer> s(1);
      for (unsigned o = 0; o < LENGTH; o += BLOCK) {
        const unsigned n = std::min(BLOCK, LENGTH - o);
        f(Stage(s[0], in, o, n));
        std::memcpy(r0.data() + o, f.output(0).getPtr(), sizeof(float) * n);
        std::memcpy(r2.data() + o, f.output(2).getPtr(), sizeof(float) * n);
      }
      CheckEqual(o0, r0);
      CheckEqual(o2, r2);
    }

    SUBCASE("filter coefficients, plain and signal-rate") {
      const std::vector<float> gain = Signal(LENGTH, 0.2f, 1.f);
      const std::vector<float> q = Signal(LENGTH, 2.f, 4.f);
      for (int signal = 0; signal < 2; ++signal) {
        INFO("signal-rate: " << signal);
        auto* p = yse_dsp_filter_coefficients_create();
        yse_dsp_filter_coefficients_set_type(p, YSE_DSP_FILTER_BANDPASS);
        std::vector<std::vector<float>> c(5, std::vector<float>(LENGTH));
        if (signal != 0) {
          CHECK(yse_dsp_filter_coefficients_process_signal(
                    p, freq.data(), gain.data(), q.data(), c[0].data(), c[1].data(), c[2].data(),
                    c[3].data(), c[4].data(), LENGTH) == LENGTH);
        } else {
          CHECK(yse_dsp_filter_coefficients_process(p, c[0].data(), c[1].data(), c[2].data(),
                                                    c[3].data(), c[4].data(), LENGTH) == LENGTH);
        }
        yse_dsp_processor_destroy(p);

        DSP::filterCoefficients f;
        f.type(DSP::filterCoefficients::BANDPASS);
        std::vector<std::vector<float>> r(5, std::vector<float>(LENGTH));
        std::vector<DSP::buffer> s(3);
        for (unsigned o = 0; o < LENGTH; o += BLOCK) {
          const unsigned n = std::min(BLOCK, LENGTH - o);
          if (signal != 0) {
            f(Stage(s[0], freq, o, n), Stage(s[1], gain, o, n), Stage(s[2], q, o, n));
          } else {
            f(n);
          }
          for (unsigned k = 0; k < 5; ++k)
            std::memcpy(r[k].data() + o, f.output(k).getPtr(), sizeof(float) * n);
        }
        for (unsigned k = 0; k < 5; ++k) {
          INFO("coefficient " << k);
          CheckEqual(c[k], r[k]);
        }
      }

      float got[5] = {};
      for (int t = YSE_DSP_FILTER_OFF; t < YSE_DSP_FILTER_TYPE_COUNT; ++t) {
        INFO("type " << t);
        const auto type = static_cast<DSP::filterCoefficients::TYPE>(t);
        const auto a = DSP::filterCoefficients::Compute(type, 1500.f, 2.f, 1.5f, 44100.f);
        CHECK(yse_dsp_filter_coefficients_compute(static_cast<YseDspFilterType>(t), 1500.f, 2.f,
                                                  1.5f, 44100.f, got) == 1);
        CHECK(got[0] == a.a0);
        CHECK(got[1] == a.a1);
        CHECK(got[2] == a.a2);
        CHECK(got[3] == a.b1);
        CHECK(got[4] == a.b2);
        const auto m = DSP::filterCoefficients::ComputeMatched(type, 1500.f, 2.f, 1.5f, 44100.f);
        CHECK(yse_dsp_filter_coefficients_compute_matched(static_cast<YseDspFilterType>(t), 1500.f,
                                                          2.f, 1.5f, 44100.f, got) == 1);
        CHECK(got[0] == m.a0);
        CHECK(got[4] == m.b2);
      }
    }
  }

  TEST_CASE("c api filters: the per-sample entry points match the engine class") {
    const std::vector<float> in = Signal(64);

    auto* lp = yse_dsp_lores_create();
    yse_dsp_lores_set_cutoff(lp, 900.f);
    DSP::lores rl;
    rl.cutoff(900.f);
    auto* sv = yse_dsp_svf_create();
    yse_dsp_svf_set_frequency(sv, 900.f);
    DSP::stateVariableFilter rs;
    rs.frequency(900.f);
    auto* xo = yse_dsp_crossover_create();
    DSP::crossover rx;
    auto* op = yse_dsp_one_pole_create();
    yse_dsp_one_pole_set_frequency(op, 300.f);
    DSP::onePole ro;
    ro.frequency(300.f);

    for (float x : in) {
      CHECK(yse_dsp_lores_process_sample(lp, x) == rl.process(x));
      CHECK(yse_dsp_one_pole_process_sample(op, x) == ro.process(x));
      float l, h, b, n;
      yse_dsp_svf_process_sample(sv, x, &l, &h, &b, &n);
      const auto e = rs.process(x);
      CHECK(l == e.lowpass);
      CHECK(h == e.highpass);
      CHECK(b == e.bandpass);
      CHECK(n == e.notch);
      float xl, xh;
      yse_dsp_crossover_process_sample(xo, x, &xl, &xh);
      const auto ex = rx.process(x);
      CHECK(xl == ex.lowpass);
      CHECK(xh == ex.highpass);
    }
    yse_dsp_processor_destroy(lp);
    yse_dsp_processor_destroy(sv);
    yse_dsp_processor_destroy(xo);
    yse_dsp_processor_destroy(op);
  }

  TEST_CASE("c api filters: clear resets the state") {
    const std::vector<float> in = Signal(LENGTH);
    std::vector<float> a(LENGTH), b(LENGTH);
    auto* p = yse_dsp_reson_create();
    yse_dsp_reson_set_gain(p, 1.f);
    yse_dsp_reson_set_frequency(p, 500.f);
    yse_dsp_reson_set_q(p, 20.f);
    yse_dsp_reson_process(p, in.data(), a.data(), LENGTH);
    yse_dsp_reson_clear(p);
    yse_dsp_reson_process(p, in.data(), b.data(), LENGTH);
    CheckEqual(a, b);
    yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api filters: process, sample and setter calls allocate nothing") {
    const std::vector<float> in = Signal(LENGTH);
    const std::vector<float> freq = Signal(LENGTH, 400.f, 1000.f);
    std::vector<float> o1(LENGTH), o2(LENGTH), o3(LENGTH), o4(LENGTH), o5(LENGTH);
    float* bank[3] = {o1.data(), o2.data(), o3.data()};
    const float coeffs[5] = {0.5f, 0.1f, 0.f, -0.2f, 0.f};

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* op = all[0];
    YseDspProcessor* lp = all[1];
    YseDspProcessor* rs = all[2];
    YseDspProcessor* sv = all[3];
    YseDspProcessor* ap = all[4];
    YseDspProcessor* ps = all[5];
    YseDspProcessor* bc = all[6];
    YseDspProcessor* xo = all[7];
    YseDspProcessor* fb = all[8];
    YseDspProcessor* fc = all[9];
    YseDspProcessor* sl = all[10];
    YseDspProcessor* rm = all[11];
    YseDspProcessor* sw = all[12];

    {
      TestHelpers::ProbeScope probe;
      yse_dsp_one_pole_set_frequency(op, 500.f);
      yse_dsp_one_pole_process(op, in.data(), o1.data(), LENGTH);
      yse_dsp_one_pole_process_signal(op, in.data(), freq.data(), o1.data(), LENGTH);
      yse_dsp_lores_set_cutoff(lp, 500.f);
      yse_dsp_lores_process_signal(lp, in.data(), freq.data(), freq.data(), o1.data(), LENGTH);
      yse_dsp_reson_set_q(rs, 5.f);
      yse_dsp_reson_process_signal(rs, in.data(), in.data(), freq.data(), freq.data(), o1.data(),
                                   LENGTH);
      yse_dsp_svf_set_mode(sv, YSE_DSP_FREQUENCY_LINEAR);
      yse_dsp_svf_process(sv, in.data(), o1.data(), o2.data(), o3.data(), o4.data(), LENGTH);
      yse_dsp_allpass_set_delay(ap, 3.f);
      yse_dsp_allpass_process(ap, in.data(), o1.data(), LENGTH);
      yse_dsp_phase_shift_process_signal(ps, in.data(), freq.data(), freq.data(), o1.data(),
                                         LENGTH);
      yse_dsp_biquad_cascade_set_coefficients(bc, coeffs, 5);
      yse_dsp_biquad_cascade_process(bc, in.data(), o1.data(), LENGTH);
      yse_dsp_crossover_process_signal(xo, in.data(), freq.data(), o1.data(), o2.data(), LENGTH);
      yse_dsp_filter_bank_set_harmonic(fb, 200.f);
      yse_dsp_filter_bank_process(fb, in.data(), bank, 3, LENGTH);
      yse_dsp_filter_coefficients_set_type(fc, YSE_DSP_FILTER_HIGHSHELF);
      yse_dsp_filter_coefficients_process(fc, o1.data(), o2.data(), o3.data(), o4.data(), o5.data(),
                                          LENGTH);
      yse_dsp_slide_process(sl, in.data(), o1.data(), LENGTH);
      yse_dsp_ramp_smooth_set_ramp(rm, 8.f);
      yse_dsp_ramp_smooth_process(rm, in.data(), o1.data(), LENGTH);
      yse_dsp_slew_limiter_set_maximum(sw, 0.1f);
      yse_dsp_slew_limiter_process(sw, in.data(), o1.data(), LENGTH);
      float l, h;
      yse_dsp_crossover_process_sample(xo, 0.5f, &l, &h);
      yse_dsp_lores_process_sample(lp, 0.5f);
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_reson_process(lp, in.data(), o1.data(), LENGTH);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
