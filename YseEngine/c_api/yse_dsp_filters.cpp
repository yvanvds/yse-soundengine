#include "yse_c/yse_dsp_filters.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/filters/allpass.hpp"
#include "../dsp/filters/biquadCascade.hpp"
#include "../dsp/filters/crossover.hpp"
#include "../dsp/filters/filterBank.hpp"
#include "../dsp/filters/filterCoefficients.hpp"
#include "../dsp/filters/lores.hpp"
#include "../dsp/filters/onePole.hpp"
#include "../dsp/filters/phaseShift.hpp"
#include "../dsp/filters/rampSmooth.hpp"
#include "../dsp/filters/reson.hpp"
#include "../dsp/filters/slewLimiter.hpp"
#include "../dsp/filters/slide.hpp"
#include "../dsp/filters/stateVariableFilter.hpp"

#include <algorithm>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::blocks;
  using yse_c::processor::copy_set;
  using yse_c::processor::emit;
  using yse_c::processor::get;
  using yse_c::processor::handle;
  using yse_c::processor::make;
  using yse_c::processor::object;
  using yse_c::processor::query;
  using yse_c::processor::stage;
  using yse_c::processor::with;

  // One wrapper type per class; the count is how many host arrays a process
  // call stages (the audio input plus the signal-rate settings).
  using OnePole = handle<DSP::onePole, 2>;
  using Lores = handle<DSP::lores, 3>;
  using Reson = handle<DSP::reson, 4>;
  using Svf = handle<DSP::stateVariableFilter, 3>;
  using Allpass = handle<DSP::allpass, 3>;
  using PhaseShift = handle<DSP::phaseShift, 3>;
  using BiquadCascade = handle<DSP::biquadCascade, 1>;
  using Crossover = handle<DSP::crossover, 2>;
  using FilterBank = handle<DSP::filterBank, 1>;
  using FilterCoefficients = handle<DSP::filterCoefficients, 3>;
  using Slide = handle<DSP::slide, 3>;
  using RampSmooth = handle<DSP::rampSmooth, 3>;
  using SlewLimiter = handle<DSP::slewLimiter, 3>;

  // Run `body(handle, offset, count)` over `length` samples in engine blocks.
  // Returns the samples processed: `length`, or 0 if `p` is not an H.
  template <typename H, typename F>
  unsigned int drive(YseDspProcessor* p, unsigned int length, F&& body) {
    H* h = get<H>(p);
    if (h == nullptr) return 0;
    blocks(length, [&](unsigned offset, unsigned count) { body(*h, offset, count); });
    return length;
  }

  // The common one-in, one-out block call at the current settings.
  template <typename H>
  unsigned int process_plain(YseDspProcessor* p, const float* in, float* out, unsigned int length) {
    if (in == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(h.object(stage(h.scratch[0], in + o, n)), out + o, n);
    });
  }

  // The one-in, one-out block call with two signal-rate settings.
  template <typename H>
  unsigned int process_two(YseDspProcessor* p, const float* in, const float* s1, const float* s2,
                           float* out, unsigned int length) {
    if (in == nullptr || s1 == nullptr || s2 == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], s1 + o, n),
                    stage(h.scratch[2], s2 + o, n)),
           out + o, n);
    });
  }

  // One sample through `process`, 0 if `p` is not an H.
  template <typename H> float process_one(YseDspProcessor* p, float in) {
    auto* o = object<H>(p);
    return o != nullptr ? o->process(in) : 0.f;
  }

  // Clamp a C enum to an engine FREQUENCY_MODE; out of range keeps the mode.
  template <typename M> bool to_mode(YseDspFrequencyMode mode, M& out) {
    if (mode < YSE_DSP_FREQUENCY_HZ || mode > YSE_DSP_FREQUENCY_RADIANS) return false;
    out = static_cast<M>(mode);
    return true;
  }
} // namespace

extern "C" {

// ─── onePole ────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_one_pole_create(void) {
  return make<OnePole>("yse_dsp_one_pole_create");
}
YSE_C_API void yse_dsp_one_pole_set_frequency(YseDspProcessor* p, float value) {
  with<OnePole>(p, [&](DSP::onePole& f) { f.frequency(value); });
}
YSE_C_API float yse_dsp_one_pole_get_frequency(YseDspProcessor* p) {
  return query<OnePole>(p, 0.f, [](DSP::onePole& f) { return f.frequency(); });
}
YSE_C_API void yse_dsp_one_pole_set_mode(YseDspProcessor* p, YseDspFrequencyMode mode) {
  with<OnePole>(p, [&](DSP::onePole& f) {
    DSP::onePole::FREQUENCY_MODE m;
    if (to_mode(mode, m)) f.mode(m);
  });
}
YSE_C_API YseDspFrequencyMode yse_dsp_one_pole_get_mode(YseDspProcessor* p) {
  return query<OnePole>(p, YSE_DSP_FREQUENCY_HZ,
                        [](DSP::onePole& f) { return static_cast<YseDspFrequencyMode>(f.mode()); });
}
YSE_C_API void yse_dsp_one_pole_clear(YseDspProcessor* p) {
  with<OnePole>(p, [](DSP::onePole& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_one_pole_process(YseDspProcessor* p, const float* in, float* out,
                                                unsigned int length) {
  return process_plain<OnePole>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_one_pole_process_signal(YseDspProcessor* p, const float* in,
                                                       const float* frequency, float* out,
                                                       unsigned int length) {
  if (in == nullptr || frequency == nullptr || out == nullptr) return 0;
  return drive<OnePole>(p, length, [&](OnePole& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], frequency + o, n)), out + o,
         n);
  });
}
YSE_C_API float yse_dsp_one_pole_process_sample(YseDspProcessor* p, float in) {
  return process_one<OnePole>(p, in);
}

// ─── lores ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_lores_create(void) {
  return make<Lores>("yse_dsp_lores_create");
}
YSE_C_API void yse_dsp_lores_set_cutoff(YseDspProcessor* p, float hz) {
  with<Lores>(p, [&](DSP::lores& f) { f.cutoff(hz); });
}
YSE_C_API float yse_dsp_lores_get_cutoff(YseDspProcessor* p) {
  return query<Lores>(p, 0.f, [](DSP::lores& f) { return f.cutoff(); });
}
YSE_C_API void yse_dsp_lores_set_resonance(YseDspProcessor* p, float value) {
  with<Lores>(p, [&](DSP::lores& f) { f.resonance(value); });
}
YSE_C_API float yse_dsp_lores_get_resonance(YseDspProcessor* p) {
  return query<Lores>(p, 0.f, [](DSP::lores& f) { return f.resonance(); });
}
YSE_C_API void yse_dsp_lores_clear(YseDspProcessor* p) {
  with<Lores>(p, [](DSP::lores& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_lores_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return process_plain<Lores>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_lores_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* cutoff, const float* resonance,
                                                    float* out, unsigned int length) {
  return process_two<Lores>(p, in, cutoff, resonance, out, length);
}
YSE_C_API float yse_dsp_lores_process_sample(YseDspProcessor* p, float in) {
  return process_one<Lores>(p, in);
}

// ─── reson ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_reson_create(void) {
  return make<Reson>("yse_dsp_reson_create");
}
YSE_C_API void yse_dsp_reson_set_gain(YseDspProcessor* p, float value) {
  with<Reson>(p, [&](DSP::reson& f) { f.gain(value); });
}
YSE_C_API float yse_dsp_reson_get_gain(YseDspProcessor* p) {
  return query<Reson>(p, 0.f, [](DSP::reson& f) { return f.gain(); });
}
YSE_C_API void yse_dsp_reson_set_frequency(YseDspProcessor* p, float hz) {
  with<Reson>(p, [&](DSP::reson& f) { f.frequency(hz); });
}
YSE_C_API float yse_dsp_reson_get_frequency(YseDspProcessor* p) {
  return query<Reson>(p, 0.f, [](DSP::reson& f) { return f.frequency(); });
}
YSE_C_API void yse_dsp_reson_set_q(YseDspProcessor* p, float value) {
  with<Reson>(p, [&](DSP::reson& f) { f.q(value); });
}
YSE_C_API float yse_dsp_reson_get_q(YseDspProcessor* p) {
  return query<Reson>(p, 0.f, [](DSP::reson& f) { return f.q(); });
}
YSE_C_API void yse_dsp_reson_clear(YseDspProcessor* p) {
  with<Reson>(p, [](DSP::reson& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_reson_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return process_plain<Reson>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_reson_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* gain, const float* frequency,
                                                    const float* q, float* out,
                                                    unsigned int length) {
  if (in == nullptr || gain == nullptr || frequency == nullptr || q == nullptr || out == nullptr)
    return 0;
  return drive<Reson>(p, length, [&](Reson& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], gain + o, n),
                  stage(h.scratch[2], frequency + o, n), stage(h.scratch[3], q + o, n)),
         out + o, n);
  });
}
YSE_C_API float yse_dsp_reson_process_sample(YseDspProcessor* p, float in) {
  return process_one<Reson>(p, in);
}

// ─── stateVariableFilter ────────────────────────────────────────────────

namespace {
  void emit_svf(DSP::stateVariableFilter& f, float* lowpass, float* highpass, float* bandpass,
                float* notch, unsigned o, unsigned n) {
    emit(f.lowpass(), lowpass != nullptr ? lowpass + o : nullptr, n);
    emit(f.highpass(), highpass != nullptr ? highpass + o : nullptr, n);
    emit(f.bandpass(), bandpass != nullptr ? bandpass + o : nullptr, n);
    emit(f.notch(), notch != nullptr ? notch + o : nullptr, n);
  }
} // namespace

YSE_C_API YseDspProcessor* yse_dsp_svf_create(void) {
  return make<Svf>("yse_dsp_svf_create");
}
YSE_C_API void yse_dsp_svf_set_frequency(YseDspProcessor* p, float value) {
  with<Svf>(p, [&](DSP::stateVariableFilter& f) { f.frequency(value); });
}
YSE_C_API float yse_dsp_svf_get_frequency(YseDspProcessor* p) {
  return query<Svf>(p, 0.f, [](DSP::stateVariableFilter& f) { return f.frequency(); });
}
YSE_C_API void yse_dsp_svf_set_resonance(YseDspProcessor* p, float value) {
  with<Svf>(p, [&](DSP::stateVariableFilter& f) { f.resonance(value); });
}
YSE_C_API float yse_dsp_svf_get_resonance(YseDspProcessor* p) {
  return query<Svf>(p, 0.f, [](DSP::stateVariableFilter& f) { return f.resonance(); });
}
YSE_C_API void yse_dsp_svf_set_mode(YseDspProcessor* p, YseDspFrequencyMode mode) {
  with<Svf>(p, [&](DSP::stateVariableFilter& f) {
    DSP::stateVariableFilter::FREQUENCY_MODE m;
    if (to_mode(mode, m)) f.mode(m);
  });
}
YSE_C_API YseDspFrequencyMode yse_dsp_svf_get_mode(YseDspProcessor* p) {
  return query<Svf>(p, YSE_DSP_FREQUENCY_HZ, [](DSP::stateVariableFilter& f) {
    return static_cast<YseDspFrequencyMode>(f.mode());
  });
}
YSE_C_API void yse_dsp_svf_clear(YseDspProcessor* p) {
  with<Svf>(p, [](DSP::stateVariableFilter& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_svf_process(YseDspProcessor* p, const float* in, float* lowpass,
                                           float* highpass, float* bandpass, float* notch,
                                           unsigned int length) {
  if (in == nullptr) return 0;
  return drive<Svf>(p, length, [&](Svf& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
    emit_svf(h.object, lowpass, highpass, bandpass, notch, o, n);
  });
}
YSE_C_API unsigned int yse_dsp_svf_process_signal(YseDspProcessor* p, const float* in,
                                                  const float* frequency, const float* resonance,
                                                  float* lowpass, float* highpass, float* bandpass,
                                                  float* notch, unsigned int length) {
  if (in == nullptr || frequency == nullptr || resonance == nullptr) return 0;
  return drive<Svf>(p, length, [&](Svf& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], frequency + o, n),
             stage(h.scratch[2], resonance + o, n));
    emit_svf(h.object, lowpass, highpass, bandpass, notch, o, n);
  });
}
YSE_C_API void yse_dsp_svf_process_sample(YseDspProcessor* p, float in, float* lowpass,
                                          float* highpass, float* bandpass, float* notch) {
  DSP::stateVariableFilter::outputs r;
  if (auto* f = object<Svf>(p)) r = f->process(in);
  if (lowpass != nullptr) *lowpass = r.lowpass;
  if (highpass != nullptr) *highpass = r.highpass;
  if (bandpass != nullptr) *bandpass = r.bandpass;
  if (notch != nullptr) *notch = r.notch;
}

// ─── allpass ────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_allpass_create(float maximum_delay_ms,
                                                  unsigned int highest_sample_rate) {
  return make<Allpass>("yse_dsp_allpass_create", maximum_delay_ms,
                       highest_sample_rate != 0 ? highest_sample_rate
                                                : DSP::allpass::HIGHEST_SAMPLERATE);
}
YSE_C_API void yse_dsp_allpass_set_delay(YseDspProcessor* p, float ms) {
  with<Allpass>(p, [&](DSP::allpass& f) { f.delay(ms); });
}
YSE_C_API float yse_dsp_allpass_get_delay(YseDspProcessor* p) {
  return query<Allpass>(p, 0.f, [](DSP::allpass& f) { return f.delay(); });
}
YSE_C_API void yse_dsp_allpass_set_gain(YseDspProcessor* p, float value) {
  with<Allpass>(p, [&](DSP::allpass& f) { f.gain(value); });
}
YSE_C_API float yse_dsp_allpass_get_gain(YseDspProcessor* p) {
  return query<Allpass>(p, 0.f, [](DSP::allpass& f) { return f.gain(); });
}
YSE_C_API float yse_dsp_allpass_get_maximum_delay(YseDspProcessor* p) {
  return query<Allpass>(p, 0.f, [](DSP::allpass& f) { return f.maximumDelay(); });
}
YSE_C_API float yse_dsp_allpass_get_capacity(YseDspProcessor* p) {
  return query<Allpass>(p, 0.f, [](DSP::allpass& f) { return f.capacity(); });
}
YSE_C_API float yse_dsp_allpass_get_delay_samples(YseDspProcessor* p) {
  return query<Allpass>(p, 0.f, [](DSP::allpass& f) { return f.delaySamples(); });
}
YSE_C_API void yse_dsp_allpass_clear(YseDspProcessor* p) {
  with<Allpass>(p, [](DSP::allpass& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_allpass_process(YseDspProcessor* p, const float* in, float* out,
                                               unsigned int length) {
  return process_plain<Allpass>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_allpass_process_signal(YseDspProcessor* p, const float* in,
                                                      const float* delay, const float* gain,
                                                      float* out, unsigned int length) {
  return process_two<Allpass>(p, in, delay, gain, out, length);
}
YSE_C_API float yse_dsp_allpass_process_sample(YseDspProcessor* p, float in) {
  return process_one<Allpass>(p, in);
}

// ─── phaseShift ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phase_shift_create(void) {
  return make<PhaseShift>("yse_dsp_phase_shift_create");
}
YSE_C_API void yse_dsp_phase_shift_set_frequency(YseDspProcessor* p, float hz) {
  with<PhaseShift>(p, [&](DSP::phaseShift& f) { f.frequency(hz); });
}
YSE_C_API float yse_dsp_phase_shift_get_frequency(YseDspProcessor* p) {
  return query<PhaseShift>(p, 0.f, [](DSP::phaseShift& f) { return f.frequency(); });
}
YSE_C_API void yse_dsp_phase_shift_set_q(YseDspProcessor* p, float value) {
  with<PhaseShift>(p, [&](DSP::phaseShift& f) { f.q(value); });
}
YSE_C_API float yse_dsp_phase_shift_get_q(YseDspProcessor* p) {
  return query<PhaseShift>(p, 0.f, [](DSP::phaseShift& f) { return f.q(); });
}
YSE_C_API void yse_dsp_phase_shift_clear(YseDspProcessor* p) {
  with<PhaseShift>(p, [](DSP::phaseShift& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_phase_shift_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length) {
  return process_plain<PhaseShift>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_phase_shift_process_signal(YseDspProcessor* p, const float* in,
                                                          const float* frequency, const float* q,
                                                          float* out, unsigned int length) {
  return process_two<PhaseShift>(p, in, frequency, q, out, length);
}
YSE_C_API float yse_dsp_phase_shift_process_sample(YseDspProcessor* p, float in) {
  return process_one<PhaseShift>(p, in);
}

// ─── biquadCascade ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_biquad_cascade_create(unsigned int capacity) {
  return make<BiquadCascade>("yse_dsp_biquad_cascade_create",
                             capacity != 0 ? capacity : DSP::biquadCascade::DEFAULT_CAPACITY);
}
YSE_C_API void yse_dsp_biquad_cascade_set_coefficients(YseDspProcessor* p, const float* values,
                                                       unsigned int count) {
  if (values == nullptr) return;
  with<BiquadCascade>(p, [&](DSP::biquadCascade& f) { f.coefficients(values, count); });
}
YSE_C_API int yse_dsp_biquad_cascade_get_coefficients(YseDspProcessor* p, unsigned int section,
                                                      float* out) {
  auto* f = object<BiquadCascade>(p);
  if (f == nullptr || out == nullptr) return 0;
  return copy_set(f->coefficients(section), out);
}
YSE_C_API unsigned int yse_dsp_biquad_cascade_get_sections(YseDspProcessor* p) {
  return query<BiquadCascade>(p, 0u, [](DSP::biquadCascade& f) { return f.sections(); });
}
YSE_C_API unsigned int yse_dsp_biquad_cascade_get_capacity(YseDspProcessor* p) {
  return query<BiquadCascade>(p, 0u, [](DSP::biquadCascade& f) { return f.capacity(); });
}
YSE_C_API void yse_dsp_biquad_cascade_set_smooth(YseDspProcessor* p, int on) {
  with<BiquadCascade>(p, [&](DSP::biquadCascade& f) { f.smooth(on != 0); });
}
YSE_C_API int yse_dsp_biquad_cascade_get_smooth(YseDspProcessor* p) {
  return query<BiquadCascade>(p, 0, [](DSP::biquadCascade& f) { return f.smooth() ? 1 : 0; });
}
YSE_C_API void yse_dsp_biquad_cascade_set_bypass(YseDspProcessor* p, int on) {
  with<BiquadCascade>(p, [&](DSP::biquadCascade& f) { f.bypass(on != 0); });
}
YSE_C_API int yse_dsp_biquad_cascade_get_bypass(YseDspProcessor* p) {
  return query<BiquadCascade>(p, 0, [](DSP::biquadCascade& f) { return f.bypass() ? 1 : 0; });
}
YSE_C_API void yse_dsp_biquad_cascade_set_zero(YseDspProcessor* p, int on) {
  with<BiquadCascade>(p, [&](DSP::biquadCascade& f) { f.zero(on != 0); });
}
YSE_C_API int yse_dsp_biquad_cascade_get_zero(YseDspProcessor* p) {
  return query<BiquadCascade>(p, 0, [](DSP::biquadCascade& f) { return f.zero() ? 1 : 0; });
}
YSE_C_API void yse_dsp_biquad_cascade_clear(YseDspProcessor* p) {
  with<BiquadCascade>(p, [](DSP::biquadCascade& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_biquad_cascade_process(YseDspProcessor* p, const float* in,
                                                      float* out, unsigned int length) {
  return process_plain<BiquadCascade>(p, in, out, length);
}
YSE_C_API float yse_dsp_biquad_cascade_process_sample(YseDspProcessor* p, float in) {
  return process_one<BiquadCascade>(p, in);
}

// ─── crossover ──────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_crossover_create(void) {
  return make<Crossover>("yse_dsp_crossover_create");
}
YSE_C_API void yse_dsp_crossover_set_cutoff(YseDspProcessor* p, float hz) {
  with<Crossover>(p, [&](DSP::crossover& f) { f.cutoff(hz); });
}
YSE_C_API float yse_dsp_crossover_get_cutoff(YseDspProcessor* p) {
  return query<Crossover>(p, 0.f, [](DSP::crossover& f) { return f.cutoff(); });
}
YSE_C_API void yse_dsp_crossover_clear(YseDspProcessor* p) {
  with<Crossover>(p, [](DSP::crossover& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_crossover_process(YseDspProcessor* p, const float* in,
                                                 float* lowpass, float* highpass,
                                                 unsigned int length) {
  if (in == nullptr) return 0;
  return drive<Crossover>(p, length, [&](Crossover& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
    emit(h.object.lowpass(), lowpass != nullptr ? lowpass + o : nullptr, n);
    emit(h.object.highpass(), highpass != nullptr ? highpass + o : nullptr, n);
  });
}
YSE_C_API unsigned int yse_dsp_crossover_process_signal(YseDspProcessor* p, const float* in,
                                                        const float* cutoff, float* lowpass,
                                                        float* highpass, unsigned int length) {
  if (in == nullptr || cutoff == nullptr) return 0;
  return drive<Crossover>(p, length, [&](Crossover& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], cutoff + o, n));
    emit(h.object.lowpass(), lowpass != nullptr ? lowpass + o : nullptr, n);
    emit(h.object.highpass(), highpass != nullptr ? highpass + o : nullptr, n);
  });
}
YSE_C_API void yse_dsp_crossover_process_sample(YseDspProcessor* p, float in, float* lowpass,
                                                float* highpass) {
  DSP::crossover::outputs r;
  if (auto* f = object<Crossover>(p)) r = f->process(in);
  if (lowpass != nullptr) *lowpass = r.lowpass;
  if (highpass != nullptr) *highpass = r.highpass;
}

// ─── filterBank ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_filter_bank_create(unsigned int filters) {
  return make<FilterBank>("yse_dsp_filter_bank_create",
                          filters != 0 ? filters : DSP::filterBank::DEFAULT_FILTERS);
}
YSE_C_API unsigned int yse_dsp_filter_bank_get_size(YseDspProcessor* p) {
  return query<FilterBank>(p, 0u, [](DSP::filterBank& f) { return f.size(); });
}
YSE_C_API void yse_dsp_filter_bank_set_frequency(YseDspProcessor* p, unsigned int index, float hz) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.frequency(index, hz); });
}
YSE_C_API float yse_dsp_filter_bank_get_frequency(YseDspProcessor* p, unsigned int index) {
  return query<FilterBank>(p, 0.f, [&](DSP::filterBank& f) { return f.frequency(index); });
}
YSE_C_API void yse_dsp_filter_bank_set_q(YseDspProcessor* p, unsigned int index, float value) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.q(index, value); });
}
YSE_C_API float yse_dsp_filter_bank_get_q(YseDspProcessor* p, unsigned int index) {
  return query<FilterBank>(p, 0.f, [&](DSP::filterBank& f) { return f.q(index); });
}
YSE_C_API void yse_dsp_filter_bank_set_gain(YseDspProcessor* p, unsigned int index, float value) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.gain(index, value); });
}
YSE_C_API float yse_dsp_filter_bank_get_gain(YseDspProcessor* p, unsigned int index) {
  return query<FilterBank>(p, 0.f, [&](DSP::filterBank& f) { return f.gain(index); });
}
YSE_C_API void yse_dsp_filter_bank_set_frequency_all(YseDspProcessor* p, float hz) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.frequencyAll(hz); });
}
YSE_C_API void yse_dsp_filter_bank_set_q_all(YseDspProcessor* p, float value) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.qAll(value); });
}
YSE_C_API void yse_dsp_filter_bank_set_gain_all(YseDspProcessor* p, float value) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.gainAll(value); });
}
YSE_C_API void yse_dsp_filter_bank_set_frequency_ratio(YseDspProcessor* p, float first,
                                                       float ratio) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.frequencyRatio(first, ratio); });
}
YSE_C_API void yse_dsp_filter_bank_set_harmonic(YseDspProcessor* p, float first) {
  with<FilterBank>(p, [&](DSP::filterBank& f) { f.harmonic(first); });
}
YSE_C_API int yse_dsp_filter_bank_get_coefficients(YseDspProcessor* p, unsigned int index,
                                                   float* out) {
  auto* f = object<FilterBank>(p);
  if (f == nullptr || out == nullptr) return 0;
  return copy_set(f->coefficients(index), out);
}
YSE_C_API void yse_dsp_filter_bank_clear(YseDspProcessor* p) {
  with<FilterBank>(p, [](DSP::filterBank& f) { f.clear(); });
}
YSE_C_API unsigned int yse_dsp_filter_bank_process(YseDspProcessor* p, const float* in,
                                                   float* const* outputs, unsigned int output_count,
                                                   unsigned int length) {
  if (in == nullptr) return 0;
  return drive<FilterBank>(p, length, [&](FilterBank& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], in + o, n));
    if (outputs == nullptr) return;
    const unsigned count = std::min(output_count, h.object.size());
    for (unsigned i = 0; i < count; ++i) {
      if (outputs[i] != nullptr) emit(h.object.output(i), outputs[i] + o, n);
    }
  });
}

// ─── filterCoefficients ─────────────────────────────────────────────────

namespace {
  void emit_coefficients(DSP::filterCoefficients& f, float* a0, float* a1, float* a2, float* b1,
                         float* b2, unsigned o, unsigned n) {
    float* outs[5] = {a0, a1, a2, b1, b2};
    for (unsigned i = 0; i < 5; ++i)
      emit(f.output(i), outs[i] != nullptr ? outs[i] + o : nullptr, n);
  }

  bool valid_type(YseDspFilterType type) {
    return type >= YSE_DSP_FILTER_OFF && type < YSE_DSP_FILTER_TYPE_COUNT;
  }
} // namespace

YSE_C_API YseDspProcessor* yse_dsp_filter_coefficients_create(void) {
  return make<FilterCoefficients>("yse_dsp_filter_coefficients_create");
}
YSE_C_API void yse_dsp_filter_coefficients_set_type(YseDspProcessor* p, YseDspFilterType type) {
  if (!valid_type(type)) return;
  with<FilterCoefficients>(p, [&](DSP::filterCoefficients& f) {
    f.type(static_cast<DSP::filterCoefficients::TYPE>(type));
  });
}
YSE_C_API YseDspFilterType yse_dsp_filter_coefficients_get_type(YseDspProcessor* p) {
  return query<FilterCoefficients>(p, YSE_DSP_FILTER_OFF, [](DSP::filterCoefficients& f) {
    return static_cast<YseDspFilterType>(f.type());
  });
}
YSE_C_API void yse_dsp_filter_coefficients_set_frequency(YseDspProcessor* p, float hz) {
  with<FilterCoefficients>(p, [&](DSP::filterCoefficients& f) { f.frequency(hz); });
}
YSE_C_API float yse_dsp_filter_coefficients_get_frequency(YseDspProcessor* p) {
  return query<FilterCoefficients>(p, 0.f,
                                   [](DSP::filterCoefficients& f) { return f.frequency(); });
}
YSE_C_API void yse_dsp_filter_coefficients_set_gain(YseDspProcessor* p, float value) {
  with<FilterCoefficients>(p, [&](DSP::filterCoefficients& f) { f.gain(value); });
}
YSE_C_API float yse_dsp_filter_coefficients_get_gain(YseDspProcessor* p) {
  return query<FilterCoefficients>(p, 0.f, [](DSP::filterCoefficients& f) { return f.gain(); });
}
YSE_C_API void yse_dsp_filter_coefficients_set_q(YseDspProcessor* p, float value) {
  with<FilterCoefficients>(p, [&](DSP::filterCoefficients& f) { f.q(value); });
}
YSE_C_API float yse_dsp_filter_coefficients_get_q(YseDspProcessor* p) {
  return query<FilterCoefficients>(p, 0.f, [](DSP::filterCoefficients& f) { return f.q(); });
}
YSE_C_API void yse_dsp_filter_coefficients_set_resampling(YseDspProcessor* p, unsigned int factor) {
  with<FilterCoefficients>(p, [&](DSP::filterCoefficients& f) { f.resampling(factor); });
}
YSE_C_API unsigned int yse_dsp_filter_coefficients_get_resampling(YseDspProcessor* p) {
  return query<FilterCoefficients>(p, 0u,
                                   [](DSP::filterCoefficients& f) { return f.resampling(); });
}
YSE_C_API int yse_dsp_filter_coefficients_get_coefficients(YseDspProcessor* p, float* out) {
  auto* f = object<FilterCoefficients>(p);
  if (f == nullptr || out == nullptr) return 0;
  return copy_set(f->coefficients(), out);
}
YSE_C_API unsigned int yse_dsp_filter_coefficients_process(YseDspProcessor* p, float* a0, float* a1,
                                                           float* a2, float* b1, float* b2,
                                                           unsigned int length) {
  return drive<FilterCoefficients>(p, length, [&](FilterCoefficients& h, unsigned o, unsigned n) {
    h.object(n);
    emit_coefficients(h.object, a0, a1, a2, b1, b2, o, n);
  });
}
YSE_C_API unsigned int
yse_dsp_filter_coefficients_process_signal(YseDspProcessor* p, const float* frequency,
                                           const float* gain, const float* q, float* a0, float* a1,
                                           float* a2, float* b1, float* b2, unsigned int length) {
  if (frequency == nullptr || gain == nullptr || q == nullptr) return 0;
  return drive<FilterCoefficients>(p, length, [&](FilterCoefficients& h, unsigned o, unsigned n) {
    h.object(stage(h.scratch[0], frequency + o, n), stage(h.scratch[1], gain + o, n),
             stage(h.scratch[2], q + o, n));
    emit_coefficients(h.object, a0, a1, a2, b1, b2, o, n);
  });
}
YSE_C_API int yse_dsp_filter_coefficients_compute(YseDspFilterType type, float frequency,
                                                  float gain, float q, float sample_rate,
                                                  float* out) {
  if (out == nullptr) return 0;
  return copy_set(DSP::filterCoefficients::Compute(static_cast<DSP::filterCoefficients::TYPE>(type),
                                                   frequency, gain, q, sample_rate),
                  out);
}
YSE_C_API int yse_dsp_filter_coefficients_compute_matched(YseDspFilterType type, float frequency,
                                                          float gain, float q, float sample_rate,
                                                          float* out) {
  if (out == nullptr) return 0;
  return copy_set(
      DSP::filterCoefficients::ComputeMatched(static_cast<DSP::filterCoefficients::TYPE>(type),
                                              frequency, gain, q, sample_rate),
      out);
}

// ─── slide ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_slide_create(void) {
  return make<Slide>("yse_dsp_slide_create");
}
YSE_C_API void yse_dsp_slide_set_up(YseDspProcessor* p, float amount) {
  with<Slide>(p, [&](DSP::slide& f) { f.up(amount); });
}
YSE_C_API float yse_dsp_slide_get_up(YseDspProcessor* p) {
  return query<Slide>(p, 0.f, [](DSP::slide& f) { return f.up(); });
}
YSE_C_API void yse_dsp_slide_set_down(YseDspProcessor* p, float amount) {
  with<Slide>(p, [&](DSP::slide& f) { f.down(amount); });
}
YSE_C_API float yse_dsp_slide_get_down(YseDspProcessor* p) {
  return query<Slide>(p, 0.f, [](DSP::slide& f) { return f.down(); });
}
YSE_C_API float yse_dsp_slide_get_value(YseDspProcessor* p) {
  return query<Slide>(p, 0.f, [](DSP::slide& f) { return f.value(); });
}
YSE_C_API void yse_dsp_slide_reset(YseDspProcessor* p) {
  with<Slide>(p, [](DSP::slide& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_slide_process(YseDspProcessor* p, const float* in, float* out,
                                             unsigned int length) {
  return process_plain<Slide>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_slide_process_signal(YseDspProcessor* p, const float* in,
                                                    const float* up, const float* down, float* out,
                                                    unsigned int length) {
  return process_two<Slide>(p, in, up, down, out, length);
}
YSE_C_API float yse_dsp_slide_process_sample(YseDspProcessor* p, float in) {
  return process_one<Slide>(p, in);
}

// ─── rampSmooth ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_ramp_smooth_create(void) {
  return make<RampSmooth>("yse_dsp_ramp_smooth_create");
}
YSE_C_API void yse_dsp_ramp_smooth_set_up(YseDspProcessor* p, float samples) {
  with<RampSmooth>(p, [&](DSP::rampSmooth& f) { f.up(samples); });
}
YSE_C_API float yse_dsp_ramp_smooth_get_up(YseDspProcessor* p) {
  return query<RampSmooth>(p, 0.f, [](DSP::rampSmooth& f) { return f.up(); });
}
YSE_C_API void yse_dsp_ramp_smooth_set_down(YseDspProcessor* p, float samples) {
  with<RampSmooth>(p, [&](DSP::rampSmooth& f) { f.down(samples); });
}
YSE_C_API float yse_dsp_ramp_smooth_get_down(YseDspProcessor* p) {
  return query<RampSmooth>(p, 0.f, [](DSP::rampSmooth& f) { return f.down(); });
}
YSE_C_API void yse_dsp_ramp_smooth_set_ramp(YseDspProcessor* p, float samples) {
  with<RampSmooth>(p, [&](DSP::rampSmooth& f) { f.ramp(samples); });
}
YSE_C_API float yse_dsp_ramp_smooth_get_value(YseDspProcessor* p) {
  return query<RampSmooth>(p, 0.f, [](DSP::rampSmooth& f) { return f.value(); });
}
YSE_C_API void yse_dsp_ramp_smooth_reset(YseDspProcessor* p) {
  with<RampSmooth>(p, [](DSP::rampSmooth& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_ramp_smooth_process(YseDspProcessor* p, const float* in, float* out,
                                                   unsigned int length) {
  return process_plain<RampSmooth>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_ramp_smooth_process_signal(YseDspProcessor* p, const float* in,
                                                          const float* up, const float* down,
                                                          float* out, unsigned int length) {
  return process_two<RampSmooth>(p, in, up, down, out, length);
}
YSE_C_API float yse_dsp_ramp_smooth_process_sample(YseDspProcessor* p, float in) {
  return process_one<RampSmooth>(p, in);
}

// ─── slewLimiter ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_slew_limiter_create(void) {
  return make<SlewLimiter>("yse_dsp_slew_limiter_create");
}
YSE_C_API void yse_dsp_slew_limiter_set_minimum(YseDspProcessor* p, float slope) {
  with<SlewLimiter>(p, [&](DSP::slewLimiter& f) { f.minimum(slope); });
}
YSE_C_API float yse_dsp_slew_limiter_get_minimum(YseDspProcessor* p) {
  return query<SlewLimiter>(p, 0.f, [](DSP::slewLimiter& f) { return f.minimum(); });
}
YSE_C_API void yse_dsp_slew_limiter_set_maximum(YseDspProcessor* p, float slope) {
  with<SlewLimiter>(p, [&](DSP::slewLimiter& f) { f.maximum(slope); });
}
YSE_C_API float yse_dsp_slew_limiter_get_maximum(YseDspProcessor* p) {
  return query<SlewLimiter>(p, 0.f, [](DSP::slewLimiter& f) { return f.maximum(); });
}
YSE_C_API void yse_dsp_slew_limiter_set_value(YseDspProcessor* p, float value) {
  with<SlewLimiter>(p, [&](DSP::slewLimiter& f) { f.set(value); });
}
YSE_C_API float yse_dsp_slew_limiter_get_value(YseDspProcessor* p) {
  return query<SlewLimiter>(p, 0.f, [](DSP::slewLimiter& f) { return f.value(); });
}
YSE_C_API void yse_dsp_slew_limiter_reset(YseDspProcessor* p) {
  with<SlewLimiter>(p, [](DSP::slewLimiter& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_slew_limiter_process(YseDspProcessor* p, const float* in, float* out,
                                                    unsigned int length) {
  return process_plain<SlewLimiter>(p, in, out, length);
}
YSE_C_API unsigned int yse_dsp_slew_limiter_process_signal(YseDspProcessor* p, const float* in,
                                                           const float* minimum,
                                                           const float* maximum, float* out,
                                                           unsigned int length) {
  return process_two<SlewLimiter>(p, in, minimum, maximum, out, length);
}
YSE_C_API float yse_dsp_slew_limiter_process_sample(YseDspProcessor* p, float in) {
  return process_one<SlewLimiter>(p, in);
}

} // extern "C"
