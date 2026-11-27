#include "yse_c/yse_dsp_filter_design.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/filters/filterBands.hpp"
#include "../dsp/filters/filterDesign.hpp"
#include "../dsp/filters/filterResponse.hpp"
#include "../dsp/filters/zPlane.hpp"

#include <algorithm>
#include <complex>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::copy_set;
  using yse_c::processor::handle;
  using yse_c::processor::make;
  using yse_c::processor::object;
  using yse_c::processor::query;
  using yse_c::processor::with;

  // Design and editor state: no samples are staged, so no scratch.
  using FilterDesign = handle<DSP::filterDesign, 0>;
  using FilterResponse = handle<DSP::filterResponse, 0>;
  using FilterBands = handle<DSP::filterBands, 0>;
  using ZPlane = handle<DSP::zPlane, 0>;

  // The limits the header spells as macros.
  static_assert(YSE_DSP_FILTER_DESIGN_MAX_ORDER == DSP::filterDesign::MAX_ORDER,
                "yse_dsp_filter_design.h: YSE_DSP_FILTER_DESIGN_MAX_ORDER drifted");
  static_assert(YSE_DSP_FILTER_DESIGN_MAX_SECTIONS == DSP::filterDesign::MAX_SECTIONS,
                "yse_dsp_filter_design.h: YSE_DSP_FILTER_DESIGN_MAX_SECTIONS drifted");
  static_assert(YSE_DSP_FILTER_BANDS_MAX == DSP::filterBands::MAX_BANDS,
                "yse_dsp_filter_design.h: YSE_DSP_FILTER_BANDS_MAX drifted");
  static_assert(YSE_DSP_ZPLANE_MAX_SECTIONS == DSP::zPlane::MAX_SECTIONS,
                "yse_dsp_filter_design.h: YSE_DSP_ZPLANE_MAX_SECTIONS drifted");

  // The count-and-copy-out shape every list getter shares: `total` values in
  // entries of `unit`, of which `write(out, capacity)` copies as many whole
  // entries as `capacity` holds (the engine writers already stop at a whole
  // entry). Returns `total`, whatever was written.
  template <typename T, typename F>
  unsigned int list(unsigned int total, T* out, unsigned int capacity, F&& write) {
    if (out != nullptr && capacity > 0) write(out, capacity);
    return total;
  }

  // Roots as real / imaginary pairs: `count` roots, `root(i)` each.
  template <typename F>
  void write_roots(double* out, unsigned int capacity, unsigned int count, F&& root) {
    const unsigned int n = std::min(count, capacity / 2);
    for (unsigned int i = 0; i < n; ++i) {
      const std::complex<double> r = root(i);
      out[2 * i] = r.real();
      out[2 * i + 1] = r.imag();
    }
  }

  DSP::biQuad::coefficientSet to_set(const float* v) {
    return DSP::biQuad::coefficientSet{v[0], v[1], v[2], v[3], v[4]};
  }

  void put_pair(const std::complex<double>& a, const std::complex<double>& b, double* out) {
    out[0] = a.real();
    out[1] = a.imag();
    out[2] = b.real();
    out[3] = b.imag();
  }

  // A curve getter of filterResponse: `points()` values from `curve`.
  template <typename F>
  unsigned int curve(YseDspProcessor* p, float* out, unsigned int capacity, F&& data) {
    auto* r = object<FilterResponse>(p);
    if (r == nullptr) return 0;
    return list(r->points(), out, capacity, [&](float* o, unsigned int cap) {
      const float* src = data(*r);
      std::copy(src, src + std::min(cap, r->points()), o);
    });
  }
} // namespace

extern "C" {

// ─── filterDesign ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_filter_design_create(void) {
  return make<FilterDesign>("yse_dsp_filter_design_create");
}
YSE_C_API void yse_dsp_filter_design_set_response(YseDspProcessor* p, YseDspDesignResponse value) {
  if (value < YSE_DSP_DESIGN_LOWPASS || value > YSE_DSP_DESIGN_BANDSTOP) return;
  with<FilterDesign>(p, [&](DSP::filterDesign& d) {
    d.response(static_cast<DSP::filterDesign::RESPONSE>(value));
  });
}
YSE_C_API YseDspDesignResponse yse_dsp_filter_design_get_response(YseDspProcessor* p) {
  return query<FilterDesign>(p, YSE_DSP_DESIGN_LOWPASS, [](DSP::filterDesign& d) {
    return static_cast<YseDspDesignResponse>(d.response());
  });
}
YSE_C_API void yse_dsp_filter_design_set_topology(YseDspProcessor* p, YseDspDesignTopology value) {
  if (value < YSE_DSP_DESIGN_BUTTERWORTH || value > YSE_DSP_DESIGN_CHEBYSHEV_2) return;
  with<FilterDesign>(p, [&](DSP::filterDesign& d) {
    d.topology(static_cast<DSP::filterDesign::TOPOLOGY>(value));
  });
}
YSE_C_API YseDspDesignTopology yse_dsp_filter_design_get_topology(YseDspProcessor* p) {
  return query<FilterDesign>(p, YSE_DSP_DESIGN_BUTTERWORTH, [](DSP::filterDesign& d) {
    return static_cast<YseDspDesignTopology>(d.topology());
  });
}
YSE_C_API void yse_dsp_filter_design_set_order(YseDspProcessor* p, unsigned int order) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.order(order); });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_order(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0u, [](DSP::filterDesign& d) { return d.order(); });
}
YSE_C_API void yse_dsp_filter_design_set_frequency(YseDspProcessor* p, float value) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.frequency(value); });
}
YSE_C_API void yse_dsp_filter_design_set_frequencies(YseDspProcessor* p, float first,
                                                     float second) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.frequency(first, second); });
}
YSE_C_API float yse_dsp_filter_design_get_frequency(YseDspProcessor* p, unsigned int index) {
  return query<FilterDesign>(p, 0.f, [&](DSP::filterDesign& d) { return d.frequencyAt(index); });
}
YSE_C_API void yse_dsp_filter_design_set_units(YseDspProcessor* p, YseDspDesignUnits value) {
  if (value < YSE_DSP_DESIGN_HERTZ || value > YSE_DSP_DESIGN_RADIANS) return;
  with<FilterDesign>(
      p, [&](DSP::filterDesign& d) { d.units(static_cast<DSP::filterDesign::UNITS>(value)); });
}
YSE_C_API YseDspDesignUnits yse_dsp_filter_design_get_units(YseDspProcessor* p) {
  return query<FilterDesign>(p, YSE_DSP_DESIGN_HERTZ, [](DSP::filterDesign& d) {
    return static_cast<YseDspDesignUnits>(d.units());
  });
}
YSE_C_API void yse_dsp_filter_design_set_sample_rate(YseDspProcessor* p, float hz) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.sampleRate(hz); });
}
YSE_C_API float yse_dsp_filter_design_get_sample_rate(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0.f, [](DSP::filterDesign& d) { return d.sampleRate(); });
}
YSE_C_API void yse_dsp_filter_design_set_passband_ripple(YseDspProcessor* p, float decibels) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.passbandRipple(decibels); });
}
YSE_C_API float yse_dsp_filter_design_get_passband_ripple(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0.f, [](DSP::filterDesign& d) { return d.passbandRipple(); });
}
YSE_C_API void yse_dsp_filter_design_set_stopband_attenuation(YseDspProcessor* p, float decibels) {
  with<FilterDesign>(p, [&](DSP::filterDesign& d) { d.stopbandAttenuation(decibels); });
}
YSE_C_API float yse_dsp_filter_design_get_stopband_attenuation(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0.f, [](DSP::filterDesign& d) { return d.stopbandAttenuation(); });
}
YSE_C_API int yse_dsp_filter_design_design(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0, [](DSP::filterDesign& d) { return d.design() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_sections(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0u, [](DSP::filterDesign& d) { return d.sections(); });
}
YSE_C_API int yse_dsp_filter_design_get_section(YseDspProcessor* p, unsigned int index,
                                                float* out) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr || out == nullptr) return 0;
  return copy_set(d->section(index), out);
}
YSE_C_API unsigned int yse_dsp_filter_design_get_coefficients(YseDspProcessor* p, float* out,
                                                              unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(5 * d->sections(), out, capacity,
              [&](float* o, unsigned int cap) { d->coefficients(o, cap); });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_coefficients_double(YseDspProcessor* p,
                                                                     double* out,
                                                                     unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(5 * d->sections(), out, capacity,
              [&](double* o, unsigned int cap) { d->coefficients(o, cap); });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_pole_count(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0u, [](DSP::filterDesign& d) { return d.poles(); });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_zeroes(YseDspProcessor* p, double* out,
                                                        unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(2 * d->poles(), out, capacity, [&](double* o, unsigned int cap) {
    write_roots(o, cap, d->poles(), [&](unsigned int i) { return d->zero(i); });
  });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_poles(YseDspProcessor* p, double* out,
                                                       unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(2 * d->poles(), out, capacity, [&](double* o, unsigned int cap) {
    write_roots(o, cap, d->poles(), [&](unsigned int i) { return d->pole(i); });
  });
}
YSE_C_API double yse_dsp_filter_design_get_gain(YseDspProcessor* p) {
  return query<FilterDesign>(p, 0.0, [](DSP::filterDesign& d) { return d.gain(); });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_numerator(YseDspProcessor* p, double* out,
                                                           unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(d->poles() + 1, out, capacity, [&](double* o, unsigned int cap) {
    const unsigned int n = std::min(cap, d->poles() + 1);
    for (unsigned int i = 0; i < n; ++i)
      o[i] = d->numerator(i);
  });
}
YSE_C_API unsigned int yse_dsp_filter_design_get_denominator(YseDspProcessor* p, double* out,
                                                             unsigned int capacity) {
  auto* d = object<FilterDesign>(p);
  if (d == nullptr) return 0;
  return list(d->poles() + 1, out, capacity, [&](double* o, unsigned int cap) {
    const unsigned int n = std::min(cap, d->poles() + 1);
    for (unsigned int i = 0; i < n; ++i)
      o[i] = d->denominator(i);
  });
}
YSE_C_API int yse_dsp_filter_design_section_roots(const float* coefficients, double* zeroes,
                                                  double* poles) {
  if (coefficients == nullptr || zeroes == nullptr || poles == nullptr) return 0;
  std::complex<double> z0, z1, p0, p1;
  const bool ok = DSP::filterDesign::sectionRoots(to_set(coefficients), z0, z1, p0, p1);
  put_pair(z0, z1, zeroes);
  put_pair(p0, p1, poles);
  return ok ? 1 : 0;
}
YSE_C_API int yse_dsp_filter_design_section_from_roots(double gain, const double* zeroes,
                                                       const double* poles, float* out) {
  if (zeroes == nullptr || poles == nullptr || out == nullptr) return 0;
  return copy_set(DSP::filterDesign::sectionFromRoots(gain, {zeroes[0], zeroes[1]},
                                                      {zeroes[2], zeroes[3]}, {poles[0], poles[1]},
                                                      {poles[2], poles[3]}),
                  out);
}

// ─── filterResponse ─────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_filter_response_create(unsigned int capacity) {
  return make<FilterResponse>("yse_dsp_filter_response_create",
                              capacity != 0 ? capacity : DSP::filterResponse::DEFAULT_POINTS);
}
YSE_C_API void yse_dsp_filter_response_set_points(YseDspProcessor* p, unsigned int count) {
  with<FilterResponse>(p, [&](DSP::filterResponse& r) { r.points(count); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_points(YseDspProcessor* p) {
  return query<FilterResponse>(p, 0u, [](DSP::filterResponse& r) { return r.points(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_capacity(YseDspProcessor* p) {
  return query<FilterResponse>(p, 0u, [](DSP::filterResponse& r) { return r.capacity(); });
}
YSE_C_API int yse_dsp_filter_response_analyse(YseDspProcessor* p, const float* values,
                                              unsigned int count) {
  return query<FilterResponse>(
      p, 0, [&](DSP::filterResponse& r) { return r.analyse(values, count) ? 1 : 0; });
}
YSE_C_API int yse_dsp_filter_response_analyse_double(YseDspProcessor* p, const double* values,
                                                     unsigned int count) {
  return query<FilterResponse>(
      p, 0, [&](DSP::filterResponse& r) { return r.analyse(values, count) ? 1 : 0; });
}
YSE_C_API int yse_dsp_filter_response_analyse_design(YseDspProcessor* p, YseDspProcessor* design) {
  auto* r = object<FilterResponse>(p);
  auto* d = object<FilterDesign>(design);
  if (r == nullptr || d == nullptr) return 0;
  return r->analyse(*d) ? 1 : 0;
}
YSE_C_API unsigned int yse_dsp_filter_response_get_sections(YseDspProcessor* p) {
  return query<FilterResponse>(p, 0u, [](DSP::filterResponse& r) { return r.sections(); });
}
YSE_C_API double yse_dsp_filter_response_get_frequency(YseDspProcessor* p, unsigned int index) {
  return query<FilterResponse>(p, 0.0, [&](DSP::filterResponse& r) { return r.frequency(index); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_magnitude(YseDspProcessor* p, float* out,
                                                             unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.magnitude(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_phase(YseDspProcessor* p, float* out,
                                                         unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.phase(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_phase_delay(YseDspProcessor* p, float* out,
                                                               unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.phaseDelay(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_group_delay(YseDspProcessor* p, float* out,
                                                               unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.groupDelay(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_impulse(YseDspProcessor* p, float* out,
                                                           unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.impulse(); });
}
YSE_C_API unsigned int yse_dsp_filter_response_get_step(YseDspProcessor* p, float* out,
                                                        unsigned int capacity) {
  return curve(p, out, capacity, [](DSP::filterResponse& r) { return r.step(); });
}

// ─── filterBands ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_filter_bands_create(void) {
  return make<FilterBands>("yse_dsp_filter_bands_create");
}
YSE_C_API void yse_dsp_filter_bands_set_bands(YseDspProcessor* p, unsigned int count) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.bands(count); });
}
YSE_C_API unsigned int yse_dsp_filter_bands_get_bands(YseDspProcessor* p) {
  return query<FilterBands>(p, 0u, [](DSP::filterBands& b) { return b.bands(); });
}
YSE_C_API void yse_dsp_filter_bands_set_mode(YseDspProcessor* p, unsigned int band,
                                             YseDspBandMode mode) {
  // The engine ignores a mode out of range itself; checking here keeps an
  // out-of-range value from ever being cast to the engine enum.
  if (mode < YSE_DSP_BAND_DISPLAY || mode >= YSE_DSP_BAND_MODE_COUNT) return;
  with<FilterBands>(
      p, [&](DSP::filterBands& b) { b.mode(band, static_cast<DSP::filterBands::MODE>(mode)); });
}
YSE_C_API YseDspBandMode yse_dsp_filter_bands_get_mode(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, YSE_DSP_BAND_DISPLAY, [&](DSP::filterBands& b) {
    return static_cast<YseDspBandMode>(b.mode(band));
  });
}
YSE_C_API void yse_dsp_filter_bands_set_parameters(YseDspProcessor* p, unsigned int band,
                                                   float frequency, float gain, float q) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.parameters(band, frequency, gain, q); });
}
YSE_C_API void yse_dsp_filter_bands_set_frequency(YseDspProcessor* p, unsigned int band, float hz) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.frequency(band, hz); });
}
YSE_C_API float yse_dsp_filter_bands_get_frequency(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0.f, [&](DSP::filterBands& b) { return b.frequency(band); });
}
YSE_C_API void yse_dsp_filter_bands_set_gain(YseDspProcessor* p, unsigned int band, float value) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.gain(band, value); });
}
YSE_C_API float yse_dsp_filter_bands_get_gain(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0.f, [&](DSP::filterBands& b) { return b.gain(band); });
}
YSE_C_API void yse_dsp_filter_bands_set_q(YseDspProcessor* p, unsigned int band, float value) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.q(band, value); });
}
YSE_C_API float yse_dsp_filter_bands_get_q(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0.f, [&](DSP::filterBands& b) { return b.q(band); });
}
YSE_C_API void yse_dsp_filter_bands_set_gain_enabled(YseDspProcessor* p, unsigned int band,
                                                     int on) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.gainEnabled(band, on != 0); });
}
YSE_C_API int yse_dsp_filter_bands_get_gain_enabled(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0, [&](DSP::filterBands& b) { return b.gainEnabled(band) ? 1 : 0; });
}
YSE_C_API void yse_dsp_filter_bands_set_analog(YseDspProcessor* p, unsigned int band, int on) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.analog(band, on != 0); });
}
YSE_C_API int yse_dsp_filter_bands_get_analog(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0, [&](DSP::filterBands& b) { return b.analog(band) ? 1 : 0; });
}
YSE_C_API void yse_dsp_filter_bands_set_handle(YseDspProcessor* p, unsigned int band, int on) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.handle(band, on != 0); });
}
YSE_C_API int yse_dsp_filter_bands_get_handle(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0, [&](DSP::filterBands& b) { return b.handle(band) ? 1 : 0; });
}
YSE_C_API void yse_dsp_filter_bands_set_constraints(YseDspProcessor* p, unsigned int band,
                                                    float min_frequency, float max_frequency,
                                                    float min_gain, float max_gain, float min_q,
                                                    float max_q) {
  with<FilterBands>(p, [&](DSP::filterBands& b) {
    b.constraints(band, min_frequency, max_frequency, min_gain, max_gain, min_q, max_q);
  });
}
YSE_C_API float yse_dsp_filter_bands_get_constraint(YseDspProcessor* p, unsigned int band,
                                                    unsigned int which) {
  return query<FilterBands>(p, 0.f, [&](DSP::filterBands& b) { return b.constraint(band, which); });
}
YSE_C_API void yse_dsp_filter_bands_set_coefficients(YseDspProcessor* p, unsigned int band,
                                                     const float* values) {
  if (values == nullptr) return;
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.coefficients(band, to_set(values)); });
}
YSE_C_API void yse_dsp_filter_bands_set_coefficient(YseDspProcessor* p, unsigned int band,
                                                    unsigned int index, float value) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.coefficient(band, index, value); });
}
YSE_C_API unsigned int yse_dsp_filter_bands_set_cascade(YseDspProcessor* p, const float* values,
                                                        unsigned int count) {
  return query<FilterBands>(p, 0u, [&](DSP::filterBands& b) { return b.cascade(values, count); });
}
YSE_C_API void yse_dsp_filter_bands_flat(YseDspProcessor* p, unsigned int band) {
  with<FilterBands>(p, [&](DSP::filterBands& b) { b.flat(band); });
}
YSE_C_API float yse_dsp_filter_bands_get_bandwidth(YseDspProcessor* p, unsigned int band) {
  return query<FilterBands>(p, 0.f, [&](DSP::filterBands& b) { return b.bandwidth(band); });
}
YSE_C_API int yse_dsp_filter_bands_get_coefficients(YseDspProcessor* p, unsigned int band,
                                                    float sample_rate, float* out) {
  auto* b = object<FilterBands>(p);
  if (b == nullptr || out == nullptr) return 0;
  return copy_set(b->coefficients(band, sample_rate), out);
}
YSE_C_API unsigned int yse_dsp_filter_bands_get_sections(YseDspProcessor* p, float sample_rate,
                                                         float* out, unsigned int capacity) {
  auto* b = object<FilterBands>(p);
  if (b == nullptr) return 0;
  return list(5 * b->bands(), out, capacity,
              [&](float* o, unsigned int cap) { b->sections(o, cap, sample_rate); });
}
YSE_C_API int yse_dsp_filter_bands_response_at(YseDspProcessor* p, float hz, float sample_rate,
                                               float* amplitude, float* phase) {
  auto* b = object<FilterBands>(p);
  if (b == nullptr || amplitude == nullptr || phase == nullptr) return 0;
  return b->responseAt(hz, *amplitude, *phase, sample_rate) ? 1 : 0;
}
YSE_C_API int yse_dsp_filter_bands_response(YseDspProcessor* p, YseDspProcessor* curves,
                                            float sample_rate) {
  auto* b = object<FilterBands>(p);
  auto* r = object<FilterResponse>(curves);
  if (b == nullptr || r == nullptr) return 0;
  return b->response(*r, sample_rate) ? 1 : 0;
}

// ─── zPlane ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_zplane_create(void) {
  return make<ZPlane>("yse_dsp_zplane_create");
}
YSE_C_API void yse_dsp_zplane_set_sections(YseDspProcessor* p, unsigned int count) {
  with<ZPlane>(p, [&](DSP::zPlane& z) { z.sections(count); });
}
YSE_C_API unsigned int yse_dsp_zplane_get_sections(YseDspProcessor* p) {
  return query<ZPlane>(p, 0u, [](DSP::zPlane& z) { return z.sections(); });
}
YSE_C_API void yse_dsp_zplane_set_order(YseDspProcessor* p, unsigned int poles) {
  with<ZPlane>(p, [&](DSP::zPlane& z) { z.order(poles); });
}
YSE_C_API unsigned int yse_dsp_zplane_get_order(YseDspProcessor* p) {
  return query<ZPlane>(p, 0u, [](DSP::zPlane& z) { return z.order(); });
}
YSE_C_API void yse_dsp_zplane_set_coefficients(YseDspProcessor* p, unsigned int section,
                                               const float* values) {
  if (values == nullptr) return;
  with<ZPlane>(p, [&](DSP::zPlane& z) { z.coefficients(section, to_set(values)); });
}
YSE_C_API void yse_dsp_zplane_set_coefficient(YseDspProcessor* p, unsigned int section,
                                              unsigned int index, float value) {
  with<ZPlane>(p, [&](DSP::zPlane& z) { z.coefficient(section, index, value); });
}
YSE_C_API unsigned int yse_dsp_zplane_set_cascade(YseDspProcessor* p, const float* values,
                                                  unsigned int count) {
  return query<ZPlane>(p, 0u, [&](DSP::zPlane& z) { return z.cascade(values, count); });
}
YSE_C_API int yse_dsp_zplane_get_section(YseDspProcessor* p, unsigned int section, float* out) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr || out == nullptr) return 0;
  return copy_set(z->coefficients(section), out);
}
YSE_C_API unsigned int yse_dsp_zplane_get_coefficients(YseDspProcessor* p, float* out,
                                                       unsigned int capacity) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr) return 0;
  return list(5 * z->sections(), out, capacity,
              [&](float* o, unsigned int cap) { z->coefficients(o, cap); });
}
YSE_C_API int yse_dsp_zplane_get_finite(YseDspProcessor* p, unsigned int section) {
  return query<ZPlane>(p, 0, [&](DSP::zPlane& z) { return z.finite(section) ? 1 : 0; });
}
YSE_C_API int yse_dsp_zplane_get_zero(YseDspProcessor* p, unsigned int index, double* out) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr || out == nullptr) return 0;
  const std::complex<double> r = z->zero(index);
  out[0] = r.real();
  out[1] = r.imag();
  return 1;
}
YSE_C_API int yse_dsp_zplane_get_pole(YseDspProcessor* p, unsigned int index, double* out) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr || out == nullptr) return 0;
  const std::complex<double> r = z->pole(index);
  out[0] = r.real();
  out[1] = r.imag();
  return 1;
}
YSE_C_API float yse_dsp_zplane_get_gain(YseDspProcessor* p, unsigned int section) {
  return query<ZPlane>(p, 0.f, [&](DSP::zPlane& z) { return z.gain(section); });
}
YSE_C_API unsigned int yse_dsp_zplane_get_zeroes(YseDspProcessor* p, float* out,
                                                 unsigned int capacity) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr) return 0;
  return list(4 * z->sections(), out, capacity,
              [&](float* o, unsigned int cap) { z->zeroes(o, cap); });
}
YSE_C_API unsigned int yse_dsp_zplane_get_poles(YseDspProcessor* p, float* out,
                                                unsigned int capacity) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr) return 0;
  return list(4 * z->sections(), out, capacity,
              [&](float* o, unsigned int cap) { z->poles(o, cap); });
}
YSE_C_API unsigned int yse_dsp_zplane_get_gains(YseDspProcessor* p, float* out,
                                                unsigned int capacity) {
  auto* z = object<ZPlane>(p);
  if (z == nullptr) return 0;
  return list(z->sections(), out, capacity, [&](float* o, unsigned int cap) { z->gains(o, cap); });
}
YSE_C_API int yse_dsp_zplane_move_zero(YseDspProcessor* p, unsigned int index, double real,
                                       double imaginary) {
  return query<ZPlane>(p, 0,
                       [&](DSP::zPlane& z) { return z.moveZero(index, real, imaginary) ? 1 : 0; });
}
YSE_C_API int yse_dsp_zplane_move_pole(YseDspProcessor* p, unsigned int index, double real,
                                       double imaginary) {
  return query<ZPlane>(p, 0,
                       [&](DSP::zPlane& z) { return z.movePole(index, real, imaginary) ? 1 : 0; });
}
YSE_C_API void yse_dsp_zplane_set_constrain_poles(YseDspProcessor* p, int on) {
  with<ZPlane>(p, [&](DSP::zPlane& z) { z.constrainPoles(on != 0); });
}
YSE_C_API int yse_dsp_zplane_get_constrain_poles(YseDspProcessor* p) {
  return query<ZPlane>(p, 0, [](DSP::zPlane& z) { return z.constrainPoles() ? 1 : 0; });
}

} // extern "C"
