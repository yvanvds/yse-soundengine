// C-API tests for the filter-design toolkit of the MSP port (issue #1296):
// yse_c/yse_dsp_filter_design.h (filterDesign, filterResponse, filterBands,
// zPlane), on the shared YseDspProcessor handle of #1173.
//
// The design math itself is covered by each class's own suite
// (test_filter_design.cpp, ...). What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another toolkit class, or
//     a filter processor) is treated like NULL, also where one call takes two
//     handles (analyse_design, bands_response).
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair, enum range checks.
//   * Parity — every getter, list and computed curve matches the engine class
//     configured the same way, bit for bit.
//   * The list convention — a list getter returns the whole list's size and
//     writes only whole entries that fit, and NULL / 0 sizes the list.
//   * Real-time safety — every call but the constructors allocates nothing.
//
// No engine session is needed: the classes are plain objects that read the
// global SAMPLERATE, which the C and C++ sides here share.

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <algorithm>
#include <functional>
#include <vector>

#include "support/alloc_probe.hpp"

#include "yse_c/yse_dsp_filter_design.h"
#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_processor.h"
#include "yse_c/yse_enums.h"

#include "dsp/filters/filterBands.hpp"
#include "dsp/filters/filterDesign.hpp"
#include "dsp/filters/filterResponse.hpp"
#include "dsp/filters/zPlane.hpp"

namespace {
  namespace DSP = YSE::DSP;
  using cplx = std::complex<double>;

  void CheckSet(const float* c, const DSP::biQuad::coefficientSet& s) {
    CHECK(c[0] == s.a0);
    CHECK(c[1] == s.a1);
    CHECK(c[2] == s.a2);
    CHECK(c[3] == s.b1);
    CHECK(c[4] == s.b2);
  }

  template <typename T> void CheckEqual(const std::vector<T>& a, const T* b, std::size_t n) {
    REQUIRE(a.size() >= n);
    for (std::size_t i = 0; i < n; ++i) {
      if (a[i] != b[i]) {
        INFO("index " << i);
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
        {"filter_design", [] { return yse_dsp_filter_design_create(); }},
        {"filter_response", [] { return yse_dsp_filter_response_create(0); }},
        {"filter_response_64", [] { return yse_dsp_filter_response_create(64); }},
        {"filter_bands", [] { return yse_dsp_filter_bands_create(); }},
        {"zplane", [] { return yse_dsp_zplane_create(); }},
    };
  }

  // A sixth-order Chebyshev type 1 bandpass, and the same in the engine.
  void ConfigureDesign(YseDspProcessor* d) {
    yse_dsp_filter_design_set_response(d, YSE_DSP_DESIGN_BANDPASS);
    yse_dsp_filter_design_set_topology(d, YSE_DSP_DESIGN_CHEBYSHEV_1);
    yse_dsp_filter_design_set_order(d, 5); // odd: a bandpass gets 6 poles
    yse_dsp_filter_design_set_frequencies(d, 400.f, 2500.f);
    yse_dsp_filter_design_set_sample_rate(d, 44100.f);
    yse_dsp_filter_design_set_passband_ripple(d, 0.5f);
  }
  void ConfigureDesign(DSP::filterDesign& d) {
    d.response(DSP::filterDesign::RESPONSE::BANDPASS)
        .topology(DSP::filterDesign::TOPOLOGY::CHEBYSHEV_1)
        .order(5)
        .frequency(400.f, 2500.f)
        .sampleRate(44100.f)
        .passbandRipple(0.5f);
  }

  // A few bands of different modes, and the same in the engine.
  void ConfigureBands(YseDspProcessor* b) {
    yse_dsp_filter_bands_set_bands(b, 4);
    yse_dsp_filter_bands_set_mode(b, 0, YSE_DSP_BAND_LOWSHELF);
    yse_dsp_filter_bands_set_parameters(b, 0, 200.f, 1.5f, 0.8f);
    yse_dsp_filter_bands_set_mode(b, 1, YSE_DSP_BAND_PEAKNOTCH);
    yse_dsp_filter_bands_set_parameters(b, 1, 1200.f, 0.5f, 3.f);
    yse_dsp_filter_bands_set_analog(b, 1, 1);
    yse_dsp_filter_bands_set_mode(b, 2, YSE_DSP_BAND_BANDPASS);
    yse_dsp_filter_bands_set_parameters(b, 2, 3000.f, 2.f, 1.5f);
    yse_dsp_filter_bands_set_gain_enabled(b, 2, 1);
    const float raw[5] = {0.9f, -0.1f, 0.05f, -0.2f, 0.1f};
    yse_dsp_filter_bands_set_mode(b, 3, YSE_DSP_BAND_DISPLAY);
    yse_dsp_filter_bands_set_coefficients(b, 3, raw);
  }
  void ConfigureBands(DSP::filterBands& b) {
    b.bands(4);
    b.mode(0, DSP::filterBands::LOWSHELF).parameters(0, 200.f, 1.5f, 0.8f);
    b.mode(1, DSP::filterBands::PEAKNOTCH).parameters(1, 1200.f, 0.5f, 3.f).analog(1, true);
    b.mode(2, DSP::filterBands::BANDPASS).parameters(2, 3000.f, 2.f, 1.5f).gainEnabled(2, true);
    b.mode(3, DSP::filterBands::DISPLAY)
        .coefficients(3, DSP::biQuad::coefficientSet{0.9f, -0.1f, 0.05f, -0.2f, 0.1f});
  }

  // Three sections of a z plane.
  const float kCascade[15] = {0.5f, 0.2f,  0.1f, -0.4f, 0.3f, 1.f,   -1.2f, 0.8f,
                              0.6f, 0.25f, 0.7f, 0.f,   0.f,  -0.3f, 0.f};
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE(
      "c api filter design: every constructor pairs with destroy and frees what it allocated") {
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

  TEST_CASE("c api filter design: a handle of another class is refused like NULL") {
    YseDspProcessor* d = yse_dsp_filter_design_create();
    YseDspProcessor* r = yse_dsp_filter_response_create(32);
    YseDspProcessor* b = yse_dsp_filter_bands_create();
    YseDspProcessor* z = yse_dsp_zplane_create();
    YseDspProcessor* lp = yse_dsp_lores_create();
    yse_dsp_filter_design_set_order(d, 4);
    REQUIRE(yse_dsp_filter_design_design(d) == 1);

    float c5[5] = {7.f, 7.f, 7.f, 7.f, 7.f};
    double d4[4] = {7.0, 7.0, 7.0, 7.0};
    std::vector<float> out(64, 7.f);

    // The toolkit refuses a filter processor and each other's handles ...
    for (YseDspProcessor* wrong : {lp, r, b, z}) {
      yse_dsp_filter_design_set_order(wrong, 9);
      CHECK(yse_dsp_filter_design_get_order(wrong) == 0);
      CHECK(yse_dsp_filter_design_design(wrong) == 0);
      CHECK(yse_dsp_filter_design_get_section(wrong, 0, c5) == 0);
      CHECK(yse_dsp_filter_design_get_coefficients(wrong, out.data(), 64) == 0);
      CHECK(yse_dsp_filter_design_get_zeroes(wrong, d4, 4) == 0);
      CHECK(yse_dsp_filter_design_get_response(wrong) == YSE_DSP_DESIGN_LOWPASS);
    }
    for (YseDspProcessor* wrong : {lp, d, b, z}) {
      yse_dsp_filter_response_set_points(wrong, 3);
      CHECK(yse_dsp_filter_response_get_points(wrong) == 0);
      CHECK(yse_dsp_filter_response_analyse(wrong, kCascade, 15) == 0);
      CHECK(yse_dsp_filter_response_get_magnitude(wrong, out.data(), 64) == 0);
    }
    for (YseDspProcessor* wrong : {lp, d, r, z}) {
      yse_dsp_filter_bands_set_bands(wrong, 5);
      CHECK(yse_dsp_filter_bands_get_bands(wrong) == 0);
      CHECK(yse_dsp_filter_bands_get_mode(wrong, 0) == YSE_DSP_BAND_DISPLAY);
      CHECK(yse_dsp_filter_bands_get_sections(wrong, 0.f, out.data(), 64) == 0);
      CHECK(yse_dsp_filter_bands_set_cascade(wrong, kCascade, 15) == 0);
    }
    for (YseDspProcessor* wrong : {lp, d, r, b}) {
      yse_dsp_zplane_set_sections(wrong, 5);
      CHECK(yse_dsp_zplane_get_sections(wrong) == 0);
      CHECK(yse_dsp_zplane_set_cascade(wrong, kCascade, 15) == 0);
      CHECK(yse_dsp_zplane_move_pole(wrong, 0, 0.5, 0.0) == 0);
      CHECK(yse_dsp_zplane_get_zero(wrong, 0, d4) == 0);
      CHECK(yse_dsp_zplane_get_poles(wrong, out.data(), 64) == 0);
    }
    // ... and so does the filter side, a toolkit handle.
    CHECK(yse_dsp_lores_process(d, out.data(), out.data(), 64) == 0);

    // A call that takes two handles checks both.
    CHECK(yse_dsp_filter_response_analyse_design(r, z) == 0);
    CHECK(yse_dsp_filter_response_analyse_design(d, d) == 0);
    CHECK(yse_dsp_filter_bands_response(b, d, 0.f) == 0);
    CHECK(yse_dsp_filter_bands_response(r, r, 0.f) == 0);

    for (float v : out)
      REQUIRE(v == 7.f);
    CHECK(c5[0] == 7.f);
    CHECK(d4[0] == 7.0);

    // Nothing changed behind the refusals.
    CHECK(yse_dsp_filter_design_get_order(d) == 4);
    CHECK(yse_dsp_filter_design_get_sections(d) == 2);
    CHECK(yse_dsp_filter_response_get_points(r) == 32);
    CHECK(yse_dsp_filter_response_get_sections(r) == 0);
    CHECK(yse_dsp_filter_bands_get_bands(b) == 1);
    CHECK(yse_dsp_zplane_get_sections(z) == 1);
    CHECK(yse_dsp_filter_response_analyse_design(r, d) == 1);
    CHECK(yse_dsp_filter_bands_response(b, r, 0.f) == 1);

    for (YseDspProcessor* p : {d, r, b, z, lp})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api filter design: NULL handles and NULL arrays are no-ops that write nothing") {
    float c5[5] = {7.f, 7.f, 7.f, 7.f, 7.f};
    double d4[4] = {7.0, 7.0, 7.0, 7.0};
    float a = 7.f, ph = 7.f;
    std::vector<float> out(64, 7.f);

    // NULL handle.
    yse_dsp_filter_design_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_filter_design_design(nullptr) == 0);
    CHECK(yse_dsp_filter_design_get_topology(nullptr) == YSE_DSP_DESIGN_BUTTERWORTH);
    CHECK(yse_dsp_filter_design_get_units(nullptr) == YSE_DSP_DESIGN_HERTZ);
    CHECK(yse_dsp_filter_design_get_gain(nullptr) == 0.0);
    CHECK(yse_dsp_filter_design_get_numerator(nullptr, nullptr, 0) == 0);
    CHECK(yse_dsp_filter_design_get_coefficients_double(nullptr, nullptr, 0) == 0);
    CHECK(yse_dsp_filter_response_get_capacity(nullptr) == 0);
    CHECK(yse_dsp_filter_response_get_frequency(nullptr, 1) == 0.0);
    CHECK(yse_dsp_filter_response_analyse_design(nullptr, nullptr) == 0);
    CHECK(yse_dsp_filter_response_get_step(nullptr, out.data(), 64) == 0);
    yse_dsp_filter_bands_flat(nullptr, 0);
    CHECK(yse_dsp_filter_bands_get_bandwidth(nullptr, 0) == 0.f);
    CHECK(yse_dsp_filter_bands_get_coefficients(nullptr, 0, 0.f, c5) == 0);
    CHECK(yse_dsp_filter_bands_response_at(nullptr, 100.f, 0.f, &a, &ph) == 0);
    CHECK(yse_dsp_filter_bands_response(nullptr, nullptr, 0.f) == 0);
    yse_dsp_zplane_set_constrain_poles(nullptr, 1);
    CHECK(yse_dsp_zplane_get_constrain_poles(nullptr) == 0);
    CHECK(yse_dsp_zplane_get_section(nullptr, 0, c5) == 0);
    CHECK(yse_dsp_zplane_get_pole(nullptr, 0, d4) == 0);
    CHECK(yse_dsp_zplane_get_gains(nullptr, out.data(), 64) == 0);
    CHECK(yse_dsp_zplane_move_zero(nullptr, 0, 0.0, 0.0) == 0);
    CHECK(a == 7.f);
    CHECK(ph == 7.f);

    // Live handles, NULL arrays.
    YseDspProcessor* d = yse_dsp_filter_design_create();
    YseDspProcessor* r = yse_dsp_filter_response_create(16);
    YseDspProcessor* b = yse_dsp_filter_bands_create();
    YseDspProcessor* z = yse_dsp_zplane_create();
    CHECK(yse_dsp_filter_design_get_section(d, 0, nullptr) == 0);
    CHECK(yse_dsp_filter_response_analyse(r, nullptr, 5) == 0);
    CHECK(yse_dsp_filter_response_analyse_double(r, nullptr, 5) == 0);
    CHECK(yse_dsp_filter_response_get_sections(r) == 0);
    yse_dsp_filter_bands_set_coefficients(b, 0, nullptr);
    CHECK(yse_dsp_filter_bands_set_cascade(b, nullptr, 10) == 0);
    CHECK(yse_dsp_filter_bands_get_coefficients(b, 0, 0.f, nullptr) == 0);
    CHECK(yse_dsp_filter_bands_response_at(b, 100.f, 0.f, nullptr, &ph) == 0);
    CHECK(yse_dsp_filter_bands_response_at(b, 100.f, 0.f, &a, nullptr) == 0);
    CHECK(a == 7.f);
    CHECK(ph == 7.f);
    yse_dsp_zplane_set_coefficients(z, 0, nullptr);
    CHECK(yse_dsp_zplane_set_cascade(z, nullptr, 10) == 0);
    CHECK(yse_dsp_zplane_get_section(z, 0, nullptr) == 0);
    CHECK(yse_dsp_zplane_get_zero(z, 0, nullptr) == 0);
    CHECK(yse_dsp_zplane_get_gain(z, 0) == 1.f);

    // The handle-free root helpers need every array.
    const float one[5] = {1.f, 0.f, 0.f, 0.f, 0.f};
    CHECK(yse_dsp_filter_design_section_roots(nullptr, d4, d4) == 0);
    CHECK(yse_dsp_filter_design_section_roots(one, nullptr, d4) == 0);
    CHECK(yse_dsp_filter_design_section_roots(one, d4, nullptr) == 0);
    CHECK(d4[0] == 7.0);
    CHECK(yse_dsp_filter_design_section_from_roots(1.0, nullptr, d4, c5) == 0);
    CHECK(yse_dsp_filter_design_section_from_roots(1.0, d4, nullptr, c5) == 0);
    CHECK(yse_dsp_filter_design_section_from_roots(1.0, d4, d4, nullptr) == 0);
    CHECK(c5[0] == 7.f);

    for (YseDspProcessor* p : {d, r, b, z})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api filter design: setters and getters round-trip") {
    YseDspProcessor* d = yse_dsp_filter_design_create();
    CHECK(yse_dsp_filter_design_get_order(d) == 2);
    CHECK(yse_dsp_filter_design_get_frequency(d, 0) == 1000.f);
    CHECK(yse_dsp_filter_design_get_frequency(d, 1) == 2000.f);
    CHECK(yse_dsp_filter_design_get_sections(d) == 0);
    yse_dsp_filter_design_set_response(d, YSE_DSP_DESIGN_BANDSTOP);
    yse_dsp_filter_design_set_topology(d, YSE_DSP_DESIGN_CHEBYSHEV_2);
    yse_dsp_filter_design_set_units(d, YSE_DSP_DESIGN_NORMALIZED);
    CHECK(yse_dsp_filter_design_get_response(d) == YSE_DSP_DESIGN_BANDSTOP);
    CHECK(yse_dsp_filter_design_get_topology(d) == YSE_DSP_DESIGN_CHEBYSHEV_2);
    CHECK(yse_dsp_filter_design_get_units(d) == YSE_DSP_DESIGN_NORMALIZED);
    // Out of range: kept.
    yse_dsp_filter_design_set_response(d, static_cast<YseDspDesignResponse>(4));
    yse_dsp_filter_design_set_topology(d, static_cast<YseDspDesignTopology>(7));
    yse_dsp_filter_design_set_units(d, static_cast<YseDspDesignUnits>(3));
    CHECK(yse_dsp_filter_design_get_response(d) == YSE_DSP_DESIGN_BANDSTOP);
    CHECK(yse_dsp_filter_design_get_topology(d) == YSE_DSP_DESIGN_CHEBYSHEV_2);
    CHECK(yse_dsp_filter_design_get_units(d) == YSE_DSP_DESIGN_NORMALIZED);
    yse_dsp_filter_design_set_order(d, 100); // clamped
    CHECK(yse_dsp_filter_design_get_order(d) == YSE_DSP_FILTER_DESIGN_MAX_ORDER);
    yse_dsp_filter_design_set_order(d, 0);
    CHECK(yse_dsp_filter_design_get_order(d) == 1);
    yse_dsp_filter_design_set_frequency(d, 0.2f);
    CHECK(yse_dsp_filter_design_get_frequency(d, 0) == 0.2f);
    yse_dsp_filter_design_set_frequencies(d, NAN, 0.6f); // first kept
    CHECK(yse_dsp_filter_design_get_frequency(d, 0) == 0.2f);
    CHECK(yse_dsp_filter_design_get_frequency(d, 1) == 0.6f);
    CHECK(yse_dsp_filter_design_get_frequency(d, 7) == 0.6f);
    yse_dsp_filter_design_set_sample_rate(d, 96000.f);
    yse_dsp_filter_design_set_sample_rate(d, -1.f); // ignored
    CHECK(yse_dsp_filter_design_get_sample_rate(d) == 96000.f);
    yse_dsp_filter_design_set_passband_ripple(d, 2.f);
    yse_dsp_filter_design_set_passband_ripple(d, 0.f); // ignored
    CHECK(yse_dsp_filter_design_get_passband_ripple(d) == 2.f);
    yse_dsp_filter_design_set_stopband_attenuation(d, 60.f);
    yse_dsp_filter_design_set_stopband_attenuation(d, -3.f); // ignored
    CHECK(yse_dsp_filter_design_get_stopband_attenuation(d) == 60.f);
    yse_dsp_processor_destroy(d);

    YseDspProcessor* r = yse_dsp_filter_response_create(0);
    CHECK(yse_dsp_filter_response_get_capacity(r) == 512);
    CHECK(yse_dsp_filter_response_get_points(r) == 512);
    yse_dsp_filter_response_set_points(r, 100);
    CHECK(yse_dsp_filter_response_get_points(r) == 100);
    yse_dsp_filter_response_set_points(r, 100000); // clamped to the capacity
    CHECK(yse_dsp_filter_response_get_points(r) == 512);
    yse_dsp_filter_response_set_points(r, 0);
    CHECK(yse_dsp_filter_response_get_points(r) == 1);
    yse_dsp_processor_destroy(r);
    r = yse_dsp_filter_response_create(1u << 20); // clamped
    CHECK(yse_dsp_filter_response_get_capacity(r) == DSP::filterResponse::MAX_POINTS);
    yse_dsp_processor_destroy(r);

    YseDspProcessor* b = yse_dsp_filter_bands_create();
    CHECK(yse_dsp_filter_bands_get_bands(b) == 1);
    CHECK(yse_dsp_filter_bands_get_mode(b, 0) == YSE_DSP_BAND_LOWPASS);
    yse_dsp_filter_bands_set_bands(b, 99);
    CHECK(yse_dsp_filter_bands_get_bands(b) == YSE_DSP_FILTER_BANDS_MAX);
    yse_dsp_filter_bands_set_mode(b, 2, YSE_DSP_BAND_HIGHSHELF);
    yse_dsp_filter_bands_set_mode(b, 2, YSE_DSP_BAND_MODE_COUNT); // out of range: kept
    yse_dsp_filter_bands_set_mode(b, 2, static_cast<YseDspBandMode>(17));
    CHECK(yse_dsp_filter_bands_get_mode(b, 2) == YSE_DSP_BAND_HIGHSHELF);
    yse_dsp_filter_bands_set_frequency(b, 2, 440.f);
    yse_dsp_filter_bands_set_gain(b, 2, 0.5f);
    yse_dsp_filter_bands_set_q(b, 2, 2.f);
    CHECK(yse_dsp_filter_bands_get_frequency(b, 2) == 440.f);
    CHECK(yse_dsp_filter_bands_get_gain(b, 2) == 0.5f);
    CHECK(yse_dsp_filter_bands_get_q(b, 2) == 2.f);
    yse_dsp_filter_bands_set_parameters(b, 3, 880.f, 1.25f, 4.f);
    CHECK(yse_dsp_filter_bands_get_frequency(b, 3) == 880.f);
    CHECK(yse_dsp_filter_bands_get_gain(b, 3) == 1.25f);
    CHECK(yse_dsp_filter_bands_get_q(b, 3) == 4.f);
    yse_dsp_filter_bands_set_gain_enabled(b, 2, 1);
    yse_dsp_filter_bands_set_analog(b, 2, 1);
    yse_dsp_filter_bands_set_handle(b, 2, 1);
    CHECK(yse_dsp_filter_bands_get_gain_enabled(b, 2) == 1);
    CHECK(yse_dsp_filter_bands_get_analog(b, 2) == 1);
    CHECK(yse_dsp_filter_bands_get_handle(b, 2) == 1);
    CHECK(yse_dsp_filter_bands_get_handle(b, 1) == 0);
    yse_dsp_filter_bands_set_constraints(b, 2, 100.f, 400.f, 0.f, 0.f, 0.5f, 1.5f);
    CHECK(yse_dsp_filter_bands_get_constraint(b, 2, 0) == 100.f);
    CHECK(yse_dsp_filter_bands_get_constraint(b, 2, 1) == 400.f);
    CHECK(yse_dsp_filter_bands_get_constraint(b, 2, 5) == 1.5f);
    CHECK(yse_dsp_filter_bands_get_constraint(b, 2, 6) == 0.f);
    CHECK(yse_dsp_filter_bands_get_frequency(b, 2) == 400.f); // clamped into the bounds
    CHECK(yse_dsp_filter_bands_get_q(b, 2) == 1.5f);
    yse_dsp_filter_bands_set_frequency(b, 2, 50.f);
    CHECK(yse_dsp_filter_bands_get_frequency(b, 2) == 100.f);
    // An index past the bands: setters ignore it, getters read a default band.
    yse_dsp_filter_bands_set_frequency(b, YSE_DSP_FILTER_BANDS_MAX, 5.f);
    CHECK(yse_dsp_filter_bands_get_frequency(b, YSE_DSP_FILTER_BANDS_MAX) == 1000.f);
    // DISPLAY coefficients, whole and one at a time.
    const float raw[5] = {0.5f, 0.25f, 0.125f, -0.5f, 0.25f};
    float c5[5];
    yse_dsp_filter_bands_set_mode(b, 4, YSE_DSP_BAND_DISPLAY);
    yse_dsp_filter_bands_set_coefficients(b, 4, raw);
    yse_dsp_filter_bands_set_coefficient(b, 4, 2, 0.75f);
    REQUIRE(yse_dsp_filter_bands_get_coefficients(b, 4, 0.f, c5) == 1);
    CHECK(c5[0] == 0.5f);
    CHECK(c5[2] == 0.75f);
    CHECK(c5[4] == 0.25f);
    yse_dsp_processor_destroy(b);

    YseDspProcessor* z = yse_dsp_zplane_create();
    CHECK(yse_dsp_zplane_get_sections(z) == 1);
    CHECK(yse_dsp_zplane_get_order(z) == 2);
    yse_dsp_zplane_set_order(z, 7); // rounded up to 4 sections
    CHECK(yse_dsp_zplane_get_sections(z) == 4);
    CHECK(yse_dsp_zplane_get_order(z) == 8);
    yse_dsp_zplane_set_sections(z, 99);
    CHECK(yse_dsp_zplane_get_sections(z) == YSE_DSP_ZPLANE_MAX_SECTIONS);
    yse_dsp_zplane_set_constrain_poles(z, 1);
    CHECK(yse_dsp_zplane_get_constrain_poles(z) == 1);
    yse_dsp_zplane_set_coefficients(z, 1, raw);
    yse_dsp_zplane_set_coefficient(z, 1, 0, 2.f);
    yse_dsp_zplane_set_coefficient(z, 1, 3, NAN); // ignored
    REQUIRE(yse_dsp_zplane_get_section(z, 1, c5) == 1);
    CHECK(c5[0] == 2.f);
    CHECK(c5[3] == -0.5f);
    CHECK(yse_dsp_zplane_get_gain(z, 1) == 2.f);
    CHECK(yse_dsp_zplane_get_finite(z, 1) == 1);
    yse_dsp_zplane_set_coefficient(z, 1, 0, 0.f);
    CHECK(yse_dsp_zplane_get_finite(z, 1) == 0);
    yse_dsp_processor_destroy(z);
  }

  TEST_CASE("c api filter design: filterDesign matches the engine class") {
    YseDspProcessor* d = yse_dsp_filter_design_create();
    ConfigureDesign(d);
    DSP::filterDesign ref;
    ConfigureDesign(ref);
    REQUIRE(yse_dsp_filter_design_design(d) == 1);
    REQUIRE(ref.design());

    const unsigned sections = ref.sections();
    const unsigned poles = ref.poles();
    REQUIRE(sections == 3);
    REQUIRE(poles == 6);
    CHECK(yse_dsp_filter_design_get_sections(d) == sections);
    CHECK(yse_dsp_filter_design_get_pole_count(d) == poles);
    CHECK(yse_dsp_filter_design_get_gain(d) == ref.gain());

    for (unsigned s = 0; s <= sections; ++s) {
      float c5[5];
      REQUIRE(yse_dsp_filter_design_get_section(d, s, c5) == 1);
      CheckSet(c5, ref.section(s)); // past the last: all 0
    }

    // Coefficient lists, float and double.
    CHECK(yse_dsp_filter_design_get_coefficients(d, nullptr, 0) == 5 * sections);
    std::vector<float> cf(5 * sections, 9.f);
    std::vector<float> rf(5 * sections);
    CHECK(yse_dsp_filter_design_get_coefficients(d, cf.data(), 5 * sections) == 5 * sections);
    ref.coefficients(rf.data(), 5 * sections);
    CheckEqual(rf, cf.data(), cf.size());
    std::vector<double> cd(5 * sections);
    std::vector<double> rd(5 * sections);
    CHECK(yse_dsp_filter_design_get_coefficients_double(d, cd.data(), 5 * sections) ==
          5 * sections);
    ref.coefficients(rd.data(), 5 * sections);
    CheckEqual(rd, cd.data(), cd.size());

    // Roots, gain and transfer function.
    std::vector<double> zs(2 * poles), ps(2 * poles), rz, rp;
    CHECK(yse_dsp_filter_design_get_zeroes(d, zs.data(), 2 * poles) == 2 * poles);
    CHECK(yse_dsp_filter_design_get_poles(d, ps.data(), 2 * poles) == 2 * poles);
    for (unsigned i = 0; i < poles; ++i) {
      rz.push_back(ref.zero(i).real());
      rz.push_back(ref.zero(i).imag());
      rp.push_back(ref.pole(i).real());
      rp.push_back(ref.pole(i).imag());
    }
    CheckEqual(rz, zs.data(), zs.size());
    CheckEqual(rp, ps.data(), ps.size());
    std::vector<double> num(poles + 1), den(poles + 1), rn, rdn;
    CHECK(yse_dsp_filter_design_get_numerator(d, num.data(), poles + 1) == poles + 1);
    CHECK(yse_dsp_filter_design_get_denominator(d, den.data(), poles + 1) == poles + 1);
    for (unsigned i = 0; i <= poles; ++i) {
      rn.push_back(ref.numerator(i));
      rdn.push_back(ref.denominator(i));
    }
    CheckEqual(rn, num.data(), num.size());
    CheckEqual(rdn, den.data(), den.size());
    CHECK(den[0] == 1.0);

    // A failed design leaves no sections, no poles and one zero coefficient.
    yse_dsp_filter_design_set_frequencies(d, 30000.f, 2500.f); // above Nyquist
    CHECK(yse_dsp_filter_design_design(d) == 0);
    CHECK(yse_dsp_filter_design_get_sections(d) == 0);
    CHECK(yse_dsp_filter_design_get_pole_count(d) == 0);
    CHECK(yse_dsp_filter_design_get_coefficients(d, cf.data(), 5 * sections) == 0);
    double one = 9.0;
    CHECK(yse_dsp_filter_design_get_denominator(d, &one, 1) == 1);
    CHECK(one == 0.0);
    yse_dsp_processor_destroy(d);
  }

  TEST_CASE("c api filter design: list getters return the full size and write whole entries") {
    YseDspProcessor* d = yse_dsp_filter_design_create();
    ConfigureDesign(d);
    REQUIRE(yse_dsp_filter_design_design(d) == 1);

    // 14 values hold two whole sections of three: 10 written, the rest untouched.
    std::vector<float> cf(15, 9.f);
    CHECK(yse_dsp_filter_design_get_coefficients(d, cf.data(), 14) == 15);
    CHECK(cf[9] != 9.f);
    CHECK(cf[10] == 9.f);
    CHECK(cf[14] == 9.f);
    // Too small for one section: nothing written.
    std::fill(cf.begin(), cf.end(), 9.f);
    CHECK(yse_dsp_filter_design_get_coefficients(d, cf.data(), 4) == 15);
    CHECK(cf[0] == 9.f);
    // NULL with a capacity: sizes the list, writes nothing.
    CHECK(yse_dsp_filter_design_get_coefficients(d, nullptr, 15) == 15);

    // Roots as whole pairs: 5 values hold two zeroes.
    std::vector<double> zs(12, 9.0);
    CHECK(yse_dsp_filter_design_get_zeroes(d, zs.data(), 5) == 12);
    CHECK(zs[3] != 9.0);
    CHECK(zs[4] == 9.0);
    // A transfer function prefix.
    std::vector<double> num(7, 9.0);
    CHECK(yse_dsp_filter_design_get_numerator(d, num.data(), 3) == 7);
    CHECK(num[2] != 9.0);
    CHECK(num[3] == 9.0);
    yse_dsp_processor_destroy(d);

    // zPlane: sections of 5, root lists of 4 per section, gains of 1.
    YseDspProcessor* z = yse_dsp_zplane_create();
    REQUIRE(yse_dsp_zplane_set_cascade(z, kCascade, 15) == 3);
    std::vector<float> out(16, 9.f);
    CHECK(yse_dsp_zplane_get_coefficients(z, out.data(), 7) == 15);
    CHECK(out[4] == kCascade[4]);
    CHECK(out[5] == 9.f);
    std::fill(out.begin(), out.end(), 9.f);
    CHECK(yse_dsp_zplane_get_poles(z, out.data(), 7) == 12);
    CHECK(out[3] != 9.f);
    CHECK(out[4] == 9.f);
    std::fill(out.begin(), out.end(), 9.f);
    CHECK(yse_dsp_zplane_get_gains(z, out.data(), 2) == 3);
    CHECK(out[1] == kCascade[5]);
    CHECK(out[2] == 9.f);
    yse_dsp_processor_destroy(z);

    // filterBands: sections of 5. filterResponse: curves of `points`.
    YseDspProcessor* b = yse_dsp_filter_bands_create();
    ConfigureBands(b);
    CHECK(yse_dsp_filter_bands_get_sections(b, 0.f, nullptr, 0) == 20);
    std::fill(out.begin(), out.end(), 9.f);
    CHECK(yse_dsp_filter_bands_get_sections(b, 0.f, out.data(), 12) == 20);
    CHECK(out[9] != 9.f);
    CHECK(out[10] == 9.f);
    YseDspProcessor* r = yse_dsp_filter_response_create(32);
    REQUIRE(yse_dsp_filter_bands_response(b, r, 0.f) == 1);
    std::fill(out.begin(), out.end(), 9.f);
    CHECK(yse_dsp_filter_response_get_magnitude(r, out.data(), 3) == 32);
    CHECK(out[2] != 9.f);
    CHECK(out[3] == 9.f);
    yse_dsp_processor_destroy(b);
    yse_dsp_processor_destroy(r);
  }

  TEST_CASE("c api filter design: the section root helpers match the engine") {
    const float sets[3][5] = {
        {0.5f, 0.2f, 0.1f, -0.4f, 0.3f}, // complex poles, complex zeroes
        {1.f, -1.2f, 0.32f, 0.5f, 0.06f}, // real pairs
        {0.f, 1.f, 0.f, 0.2f, 0.f}, // a0 = 0: no finite zeroes
    };
    for (const auto& s : sets) {
      const DSP::biQuad::coefficientSet set{s[0], s[1], s[2], s[3], s[4]};
      cplx z0, z1, p0, p1;
      const bool ok = DSP::filterDesign::sectionRoots(set, z0, z1, p0, p1);
      double zeroes[4], poles[4];
      CHECK(yse_dsp_filter_design_section_roots(s, zeroes, poles) == (ok ? 1 : 0));
      CHECK(zeroes[0] == z0.real());
      CHECK(zeroes[1] == z0.imag());
      CHECK(zeroes[2] == z1.real());
      CHECK(zeroes[3] == z1.imag());
      CHECK(poles[0] == p0.real());
      CHECK(poles[1] == p0.imag());
      CHECK(poles[2] == p1.real());
      CHECK(poles[3] == p1.imag());

      float back[5];
      REQUIRE(yse_dsp_filter_design_section_from_roots(0.75, zeroes, poles, back) == 1);
      CheckSet(back, DSP::filterDesign::sectionFromRoots(0.75, z0, z1, p0, p1));
    }
    CHECK(yse_dsp_filter_design_section_roots(sets[2], std::vector<double>(4).data(),
                                              std::vector<double>(4).data()) == 0);
  }

  TEST_CASE("c api filter design: filterResponse matches the engine class") {
    using Getter = unsigned int (*)(YseDspProcessor*, float*, unsigned int);
    const Getter getters[6] = {
        yse_dsp_filter_response_get_magnitude,   yse_dsp_filter_response_get_phase,
        yse_dsp_filter_response_get_phase_delay, yse_dsp_filter_response_get_group_delay,
        yse_dsp_filter_response_get_impulse,     yse_dsp_filter_response_get_step,
    };
    const auto curves = [](const DSP::filterResponse& r, int i) -> const float* {
      switch (i) {
      case 0:
        return r.magnitude();
      case 1:
        return r.phase();
      case 2:
        return r.phaseDelay();
      case 3:
        return r.groupDelay();
      case 4:
        return r.impulse();
      default:
        return r.step();
      }
    };
    const auto compare = [&](YseDspProcessor* p, const DSP::filterResponse& ref) {
      const unsigned n = ref.points();
      CHECK(yse_dsp_filter_response_get_points(p) == n);
      CHECK(yse_dsp_filter_response_get_sections(p) == ref.sections());
      for (int i = 0; i < 6; ++i) {
        INFO("curve " << i);
        std::vector<float> out(n, 9.f);
        CHECK(getters[i](p, out.data(), n) == n);
        std::vector<float> want(curves(ref, i), curves(ref, i) + n);
        CheckEqual(want, out.data(), n);
      }
      for (unsigned i : {0u, 1u, n / 2, n - 1})
        CHECK(yse_dsp_filter_response_get_frequency(p, i) == ref.frequency(i));
    };

    // Before an analysis every curve is 0.
    YseDspProcessor* r = yse_dsp_filter_response_create(128);
    DSP::filterResponse ref(128);
    compare(r, ref);

    // From floats, with fewer points than the capacity.
    yse_dsp_filter_response_set_points(r, 100);
    ref.points(100);
    float padded[17];
    std::copy(kCascade, kCascade + 15, padded);
    padded[15] = padded[16] = 0.5f; // an incomplete trailing set: ignored
    REQUIRE(yse_dsp_filter_response_analyse(r, padded, 17) == 1);
    REQUIRE(ref.analyse(padded, 17));
    compare(r, ref);

    // A non-finite value changes nothing.
    float bad[5] = {1.f, NAN, 0.f, 0.f, 0.f};
    CHECK(yse_dsp_filter_response_analyse(r, bad, 5) == 0);
    CHECK(yse_dsp_filter_response_analyse(r, kCascade, 4) == 0);
    compare(r, ref);

    // From doubles.
    const double dbl[10] = {0.3, 0.1, -0.05, -0.6, 0.2, 1.0, 0.0, -1.0, 0.0, 0.81};
    REQUIRE(yse_dsp_filter_response_analyse_double(r, dbl, 10) == 1);
    REQUIRE(ref.analyse(dbl, 10));
    compare(r, ref);

    // From a design handle.
    YseDspProcessor* d = yse_dsp_filter_design_create();
    DSP::filterDesign design;
    CHECK(yse_dsp_filter_response_analyse_design(r, d) == 0); // nothing designed yet
    compare(r, ref);
    ConfigureDesign(d);
    ConfigureDesign(design);
    REQUIRE(yse_dsp_filter_design_design(d) == 1);
    REQUIRE(design.design());
    REQUIRE(yse_dsp_filter_response_analyse_design(r, d) == 1);
    REQUIRE(ref.analyse(design));
    compare(r, ref);

    yse_dsp_processor_destroy(d);
    yse_dsp_processor_destroy(r);
  }

  TEST_CASE("c api filter design: filterBands matches the engine class") {
    YseDspProcessor* b = yse_dsp_filter_bands_create();
    ConfigureBands(b);
    DSP::filterBands ref;
    ConfigureBands(ref);

    for (float rate : {0.f, 44100.f, 96000.f}) {
      INFO("sample rate " << rate);
      for (unsigned band = 0; band < 5; ++band) {
        float c5[5];
        REQUIRE(yse_dsp_filter_bands_get_coefficients(b, band, rate, c5) == 1);
        CheckSet(c5, ref.coefficients(band, rate));
      }
      std::vector<float> sec(20), want(20);
      CHECK(yse_dsp_filter_bands_get_sections(b, rate, sec.data(), 20) == 20);
      ref.sections(want.data(), 20, rate);
      CheckEqual(want, sec.data(), 20);

      for (float hz : {0.f, 100.f, 1200.f, 5000.f, 15000.f}) {
        float a = 0.f, ph = 0.f, ra = 0.f, rph = 0.f;
        REQUIRE(yse_dsp_filter_bands_response_at(b, hz, rate, &a, &ph) == 1);
        REQUIRE(ref.responseAt(hz, ra, rph, rate));
        CHECK(a == ra);
        CHECK(ph == rph);
      }

      YseDspProcessor* r = yse_dsp_filter_response_create(64);
      DSP::filterResponse curves(64);
      REQUIRE(yse_dsp_filter_bands_response(b, r, rate) == 1);
      REQUIRE(ref.response(curves, rate));
      std::vector<float> mag(64), ph(64);
      yse_dsp_filter_response_get_magnitude(r, mag.data(), 64);
      yse_dsp_filter_response_get_phase(r, ph.data(), 64);
      CheckEqual(std::vector<float>(curves.magnitude(), curves.magnitude() + 64), mag.data(), 64);
      CheckEqual(std::vector<float>(curves.phase(), curves.phase() + 64), ph.data(), 64);
      yse_dsp_processor_destroy(r);
    }
    float a = 7.f, ph = 7.f;
    CHECK(yse_dsp_filter_bands_response_at(b, NAN, 0.f, &a, &ph) == 0);
    CHECK(a == 7.f);

    for (unsigned band = 0; band < 4; ++band)
      CHECK(yse_dsp_filter_bands_get_bandwidth(b, band) == ref.bandwidth(band));

    // flat, then a cascade given as coefficients.
    for (unsigned band = 0; band < 4; ++band) {
      yse_dsp_filter_bands_flat(b, band);
      ref.flat(band);
      CHECK(yse_dsp_filter_bands_get_gain(b, band) == ref.gain(band));
      CHECK(yse_dsp_filter_bands_get_q(b, band) == ref.q(band));
    }
    CHECK(yse_dsp_filter_bands_set_cascade(b, kCascade, 15) == 3);
    CHECK(ref.cascade(kCascade, 15) == 3);
    CHECK(yse_dsp_filter_bands_get_bands(b) == 3);
    for (unsigned band = 0; band < 3; ++band) {
      CHECK(yse_dsp_filter_bands_get_mode(b, band) == YSE_DSP_BAND_DISPLAY);
      float c5[5];
      yse_dsp_filter_bands_get_coefficients(b, band, 0.f, c5);
      CheckSet(c5, ref.coefficients(band));
    }
    CHECK(yse_dsp_filter_bands_set_cascade(b, kCascade, 4) == 0); // no complete set
    CHECK(yse_dsp_filter_bands_get_bands(b) == 3);
    yse_dsp_processor_destroy(b);
  }

  TEST_CASE("c api filter design: zPlane matches the engine class") {
    YseDspProcessor* z = yse_dsp_zplane_create();
    DSP::zPlane ref;
    REQUIRE(yse_dsp_zplane_set_cascade(z, kCascade, 15) == 3);
    REQUIRE(ref.cascade(kCascade, 15) == 3);

    const auto compare = [&] {
      const unsigned sections = ref.sections();
      CHECK(yse_dsp_zplane_get_sections(z) == sections);
      CHECK(yse_dsp_zplane_get_order(z) == ref.order());
      for (unsigned s = 0; s <= sections; ++s) {
        float c5[5];
        REQUIRE(yse_dsp_zplane_get_section(z, s, c5) == 1);
        CheckSet(c5, ref.coefficients(s));
        CHECK(yse_dsp_zplane_get_finite(z, s) == (ref.finite(s) ? 1 : 0));
        CHECK(yse_dsp_zplane_get_gain(z, s) == ref.gain(s));
      }
      for (unsigned i = 0; i <= ref.order(); ++i) { // one past: 0
        double zr[2], pr[2];
        REQUIRE(yse_dsp_zplane_get_zero(z, i, zr) == 1);
        REQUIRE(yse_dsp_zplane_get_pole(z, i, pr) == 1);
        CHECK(zr[0] == ref.zero(i).real());
        CHECK(zr[1] == ref.zero(i).imag());
        CHECK(pr[0] == ref.pole(i).real());
        CHECK(pr[1] == ref.pole(i).imag());
      }
      std::vector<float> out(5 * sections), want(5 * sections);
      CHECK(yse_dsp_zplane_get_coefficients(z, out.data(), 5 * sections) == 5 * sections);
      ref.coefficients(want.data(), 5 * sections);
      CheckEqual(want, out.data(), want.size());
      out.assign(4 * sections, 0.f);
      want.assign(4 * sections, 0.f);
      CHECK(yse_dsp_zplane_get_zeroes(z, out.data(), 4 * sections) == 4 * sections);
      ref.zeroes(want.data(), 4 * sections);
      CheckEqual(want, out.data(), want.size());
      CHECK(yse_dsp_zplane_get_poles(z, out.data(), 4 * sections) == 4 * sections);
      ref.poles(want.data(), 4 * sections);
      CheckEqual(want, out.data(), want.size());
      out.assign(sections, 0.f);
      want.assign(sections, 0.f);
      CHECK(yse_dsp_zplane_get_gains(z, out.data(), sections) == sections);
      ref.gains(want.data(), sections);
      CheckEqual(want, out.data(), want.size());
    };
    compare();

    // Moving roots: onto and off the real axis, a zero of an a0 = 0 section,
    // a pole beyond the unit circle with and without the constraint.
    CHECK(yse_dsp_zplane_move_zero(z, 0, 0.3, 0.4) == (ref.moveZero(0, 0.3, 0.4) ? 1 : 0));
    compare();
    CHECK(yse_dsp_zplane_move_pole(z, 3, 0.2, 0.0) == (ref.movePole(3, 0.2, 0.0) ? 1 : 0));
    compare();
    yse_dsp_zplane_set_coefficient(z, 2, 0, 0.f); // a0 = 0: no finite zeroes
    ref.coefficient(2, 0, 0.f);
    compare();
    CHECK(yse_dsp_zplane_move_zero(z, 4, -0.5, 0.0) == (ref.moveZero(4, -0.5, 0.0) ? 1 : 0));
    CHECK(yse_dsp_zplane_get_gain(z, 2) == 1.f);
    compare();
    CHECK(yse_dsp_zplane_move_pole(z, 1, 1.5, 0.5) == (ref.movePole(1, 1.5, 0.5) ? 1 : 0));
    compare();
    yse_dsp_zplane_set_constrain_poles(z, 1);
    ref.constrainPoles(true);
    CHECK(yse_dsp_zplane_move_pole(z, 2, 0.0, 2.0) == (ref.movePole(2, 0.0, 2.0) ? 1 : 0));
    compare();
    // Refused moves.
    CHECK(yse_dsp_zplane_move_zero(z, 6, 0.1, 0.1) == 0);
    CHECK(yse_dsp_zplane_move_pole(z, 0, NAN, 0.1) == 0);
    compare();

    // Coefficients set directly, a section count, an order.
    const float set[5] = {0.4f, 0.1f, 0.2f, 0.3f, -0.2f};
    yse_dsp_zplane_set_coefficients(z, 1, set);
    ref.coefficients(1, DSP::biQuad::coefficientSet{0.4f, 0.1f, 0.2f, 0.3f, -0.2f});
    yse_dsp_zplane_set_coefficient(z, 2, 4, 0.5f);
    ref.coefficient(2, 4, 0.5f);
    compare();
    yse_dsp_zplane_set_order(z, 3);
    ref.order(3);
    compare();
    yse_dsp_processor_destroy(z);
  }

  TEST_CASE("c api filter design: every call but the constructors allocates nothing") {
    YseDspProcessor* d = yse_dsp_filter_design_create();
    YseDspProcessor* r = yse_dsp_filter_response_create(256);
    YseDspProcessor* b = yse_dsp_filter_bands_create();
    YseDspProcessor* z = yse_dsp_zplane_create();
    std::vector<float> out(256);
    std::vector<double> dout(64);
    float c5[5];
    double d4[4], d4b[4];
    float a = 0.f, ph = 0.f;

    {
      TestHelpers::ProbeScope probe;
      ConfigureDesign(d);
      yse_dsp_filter_design_set_units(d, YSE_DSP_DESIGN_HERTZ);
      yse_dsp_filter_design_set_stopband_attenuation(d, 50.f);
      yse_dsp_filter_design_design(d);
      yse_dsp_filter_design_get_section(d, 0, c5);
      yse_dsp_filter_design_get_coefficients(d, out.data(), 256);
      yse_dsp_filter_design_get_coefficients_double(d, dout.data(), 64);
      yse_dsp_filter_design_get_zeroes(d, dout.data(), 64);
      yse_dsp_filter_design_get_poles(d, dout.data(), 64);
      yse_dsp_filter_design_get_numerator(d, dout.data(), 64);
      yse_dsp_filter_design_get_denominator(d, dout.data(), 64);
      yse_dsp_filter_design_section_roots(kCascade, d4, d4b);
      yse_dsp_filter_design_section_from_roots(1.0, d4, d4b, c5);

      yse_dsp_filter_response_set_points(r, 200);
      yse_dsp_filter_response_analyse(r, kCascade, 15);
      yse_dsp_filter_response_analyse_design(r, d);
      yse_dsp_filter_response_get_magnitude(r, out.data(), 256);
      yse_dsp_filter_response_get_group_delay(r, out.data(), 256);
      yse_dsp_filter_response_get_step(r, out.data(), 256);

      ConfigureBands(b);
      yse_dsp_filter_bands_set_constraints(b, 0, 100.f, 500.f, 0.f, 0.f, 0.f, 0.f);
      yse_dsp_filter_bands_flat(b, 1);
      yse_dsp_filter_bands_get_coefficients(b, 1, 0.f, c5);
      yse_dsp_filter_bands_get_sections(b, 0.f, out.data(), 256);
      yse_dsp_filter_bands_response_at(b, 1000.f, 0.f, &a, &ph);
      yse_dsp_filter_bands_response(b, r, 0.f);
      yse_dsp_filter_bands_set_cascade(b, kCascade, 15);

      yse_dsp_zplane_set_cascade(z, kCascade, 15);
      yse_dsp_zplane_set_constrain_poles(z, 1);
      yse_dsp_zplane_move_zero(z, 0, 0.1, 0.2);
      yse_dsp_zplane_move_pole(z, 1, 0.9, 0.9);
      yse_dsp_zplane_get_zero(z, 0, d4);
      yse_dsp_zplane_get_zeroes(z, out.data(), 256);
      yse_dsp_zplane_get_poles(z, out.data(), 256);
      yse_dsp_zplane_get_gains(z, out.data(), 256);
      yse_dsp_zplane_get_coefficients(z, out.data(), 256);
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_zplane_get_poles(d, out.data(), 256);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : {d, r, b, z})
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
