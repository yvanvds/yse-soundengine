#include "yse_c/yse_dsp_oscillators.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/generators/bandLimitedOscillators.hpp"
#include "../dsp/generators/bandLimitedRandom.hpp"
#include "../dsp/generators/click.hpp"
#include "../dsp/generators/cycle.hpp"
#include "../dsp/generators/kink.hpp"
#include "../dsp/generators/oscillatorBank.hpp"
#include "../dsp/generators/phaseShapes.hpp"
#include "../dsp/generators/phasor.hpp"
#include "../dsp/generators/pinkNoise.hpp"
#include "../dsp/generators/pulseTrain.hpp"
#include "../dsp/generators/sampleCounter.hpp"

#include <cstdint>
#include <cstring>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::drive;
  using yse_c::processor::emit;
  using yse_c::processor::handle;
  using yse_c::processor::make;
  using yse_c::processor::query;
  using yse_c::processor::stage;
  using yse_c::processor::with;

  // One wrapper type per class; the count is how many host arrays a process
  // call stages (the signal-rate settings, or the phase a shaper reads).
  using Phasor = handle<DSP::phasor, 1>;
  using Cycle = handle<DSP::cycle, 2>;
  using BlSaw = handle<DSP::blSaw, 2>;
  using BlRect = handle<DSP::blRect, 3>;
  using BlTri = handle<DSP::blTri, 3>;
  using PinkNoise = handle<DSP::pinkNoise, 0>;
  using BandLimitedRandom = handle<DSP::bandLimitedRandom, 1>;
  using Click = handle<DSP::click, 1>;
  using PulseTrain = handle<DSP::pulseTrain, 3>;
  using Kink = handle<DSP::kink, 2>;
  using OscillatorBank = handle<DSP::oscillatorBank, 4>;
  using TriangleShape = handle<DSP::triangleShape, 2>;
  using TrapezoidShape = handle<DSP::trapezoidShape, 3>;
  using SampleCounter = handle<DSP::sampleCounter, 1>;

  // The constants the header spells as macros.
  static_assert(YSE_DSP_CYCLE_TABLE_SIZE == DSP::cycle::TABLE_SIZE,
                "yse_dsp_oscillators.h: YSE_DSP_CYCLE_TABLE_SIZE drifted");
  static_assert(YSE_DSP_NOISE_DEFAULT_SEED == DSP::pinkNoise::DEFAULT_SEED,
                "yse_dsp_oscillators.h: YSE_DSP_NOISE_DEFAULT_SEED drifted (pinkNoise)");
  static_assert(YSE_DSP_NOISE_DEFAULT_SEED == DSP::bandLimitedRandom::DEFAULT_SEED,
                "yse_dsp_oscillators.h: YSE_DSP_NOISE_DEFAULT_SEED drifted (bandLimitedRandom)");
  static_assert(YSE_DSP_CLICK_MAX_LENGTH == DSP::click::MAX_LENGTH,
                "yse_dsp_oscillators.h: YSE_DSP_CLICK_MAX_LENGTH drifted");
  static_assert(YSE_DSP_OSCILLATOR_BANK_DEFAULT_OSCILLATORS ==
                    DSP::oscillatorBank::DEFAULT_OSCILLATORS,
                "yse_dsp_oscillators.h: YSE_DSP_OSCILLATOR_BANK_DEFAULT_OSCILLATORS drifted");
  static_assert(YSE_DSP_OSCILLATOR_BANK_DEFAULT_TABLE_SIZE ==
                    DSP::oscillatorBank::DEFAULT_TABLE_SIZE,
                "yse_dsp_oscillators.h: YSE_DSP_OSCILLATOR_BANK_DEFAULT_TABLE_SIZE drifted");
  static_assert(YSE_DSP_OSCILLATOR_BANK_MIN_TABLE_SIZE == DSP::oscillatorBank::MIN_TABLE_SIZE,
                "yse_dsp_oscillators.h: YSE_DSP_OSCILLATOR_BANK_MIN_TABLE_SIZE drifted");
  static_assert(YSE_DSP_OSCILLATOR_BANK_MAX_TABLE_SIZE == DSP::oscillatorBank::MAX_TABLE_SIZE,
                "yse_dsp_oscillators.h: YSE_DSP_OSCILLATOR_BANK_MAX_TABLE_SIZE drifted");
  static_assert(YSE_DSP_TRAPEZOID_DEFAULT_UP == DSP::trapezoidShape::DEFAULT_UP,
                "yse_dsp_oscillators.h: YSE_DSP_TRAPEZOID_DEFAULT_UP drifted");
  static_assert(YSE_DSP_TRAPEZOID_DEFAULT_DOWN == DSP::trapezoidShape::DEFAULT_DOWN,
                "yse_dsp_oscillators.h: YSE_DSP_TRAPEZOID_DEFAULT_DOWN drifted");
  // A seed crosses as unsigned int and is narrowed to the engine's uint32_t.
  static_assert(sizeof(unsigned int) >= sizeof(std::uint32_t),
                "yse_dsp_oscillators.h: a seed must fit an unsigned int");

  // A generator block at constant settings: `make(h, count)` runs the engine
  // overload that takes a length.
  template <typename H, typename F>
  unsigned int generate(YseDspProcessor* p, float* out, unsigned int length, F&& run) {
    if (out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) { emit(run(h, n), out + o, n); });
  }

  // A block that stages one host array: `run(h, staged)` runs the engine
  // overload that takes a buffer.
  template <typename H, typename F>
  unsigned int one_signal(YseDspProcessor* p, const float* a, float* out, unsigned int length,
                          F&& run) {
    if (a == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(run(h, stage(h.scratch[0], a + o, n)), out + o, n);
    });
  }

  // The same with two staged host arrays.
  template <typename H, typename F>
  unsigned int two_signals(YseDspProcessor* p, const float* a, const float* b, float* out,
                           unsigned int length, F&& run) {
    if (a == nullptr || b == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(run(h, stage(h.scratch[0], a + o, n), stage(h.scratch[1], b + o, n)), out + o, n);
    });
  }

  // The same with three staged host arrays.
  template <typename H, typename F>
  unsigned int three_signals(YseDspProcessor* p, const float* a, const float* b, const float* c,
                             float* out, unsigned int length, F&& run) {
    if (a == nullptr || b == nullptr || c == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(run(h, stage(h.scratch[0], a + o, n), stage(h.scratch[1], b + o, n),
               stage(h.scratch[2], c + o, n)),
           out + o, n);
    });
  }

  // pulseTrain's onsets of the block just made, as 1 at each onset sample
  // and 0 elsewhere, into the host's optional onset array.
  void emit_onsets(const DSP::pulseTrain& t, float* onsets, unsigned o, unsigned n) {
    if (onsets == nullptr) return;
    std::memset(onsets + o, 0, sizeof(float) * n);
    for (unsigned i = 0; i < t.onsets(); ++i) {
      const unsigned at = t.onset(i);
      if (at < n) onsets[o + at] = 1.f;
    }
  }
} // namespace

extern "C" {

// ─── phasor ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_create(void) {
  return make<Phasor>("yse_dsp_phasor_create");
}
YSE_C_API void yse_dsp_phasor_set_sync_update(YseDspProcessor* p, int on) {
  with<Phasor>(p, [&](DSP::phasor& f) { f.syncUpdate(on != 0); });
}
YSE_C_API int yse_dsp_phasor_get_sync_update(YseDspProcessor* p) {
  return query<Phasor>(p, 0, [](DSP::phasor& f) { return f.syncUpdate() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_update(YseDspProcessor* p) {
  with<Phasor>(p, [](DSP::phasor& f) { f.update(); });
}
YSE_C_API void yse_dsp_phasor_set_phase(YseDspProcessor* p, float value) {
  with<Phasor>(p, [&](DSP::phasor& f) { f.phase(value); });
}
YSE_C_API float yse_dsp_phasor_get_phase(YseDspProcessor* p) {
  return query<Phasor>(p, 0.f, [](DSP::phasor& f) { return f.phase(); });
}
YSE_C_API unsigned int yse_dsp_phasor_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length) {
  return generate<Phasor>(p, out, length, [&](Phasor& h, unsigned n) -> DSP::buffer& {
    return h.object(frequency, n);
  });
}
YSE_C_API unsigned int yse_dsp_phasor_process_signal(YseDspProcessor* p, const float* frequency,
                                                     float* out, unsigned int length) {
  return one_signal<Phasor>(p, frequency, out, length,
                            [](Phasor& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f); });
}

// ─── cycle ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_cycle_create(void) {
  return make<Cycle>("yse_dsp_cycle_create");
}
YSE_C_API void yse_dsp_cycle_set_table(YseDspProcessor* p, const float* samples,
                                       unsigned int size) {
  with<Cycle>(p, [&](DSP::cycle& f) { f.table(samples, size); });
}
YSE_C_API void yse_dsp_cycle_cosine(YseDspProcessor* p) {
  with<Cycle>(p, [](DSP::cycle& f) { f.cosine(); });
}
YSE_C_API unsigned int yse_dsp_cycle_get_table_size(YseDspProcessor* p) {
  return query<Cycle>(p, 0u, [](DSP::cycle& f) { return f.tableSize(); });
}
YSE_C_API void yse_dsp_cycle_reset(YseDspProcessor* p, float phase) {
  with<Cycle>(p, [&](DSP::cycle& f) { f.reset(phase); });
}
YSE_C_API float yse_dsp_cycle_get_phase(YseDspProcessor* p) {
  return query<Cycle>(p, 0.f, [](DSP::cycle& f) { return f.phase(); });
}
YSE_C_API unsigned int yse_dsp_cycle_process(YseDspProcessor* p, float frequency, float phase,
                                             float* out, unsigned int length) {
  return generate<Cycle>(p, out, length, [&](Cycle& h, unsigned n) -> DSP::buffer& {
    return h.object(frequency, phase, n);
  });
}
YSE_C_API unsigned int yse_dsp_cycle_process_frequency(YseDspProcessor* p, const float* frequency,
                                                       float phase, float* out,
                                                       unsigned int length) {
  return one_signal<Cycle>(
      p, frequency, out, length,
      [&](Cycle& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f, phase); });
}
YSE_C_API unsigned int yse_dsp_cycle_process_phase(YseDspProcessor* p, float frequency,
                                                   const float* phase, float* out,
                                                   unsigned int length) {
  return one_signal<Cycle>(p, phase, out, length, [&](Cycle& h, DSP::buffer& ph) -> DSP::buffer& {
    return h.object(frequency, ph);
  });
}
YSE_C_API unsigned int yse_dsp_cycle_process_signal(YseDspProcessor* p, const float* frequency,
                                                    const float* phase, float* out,
                                                    unsigned int length) {
  return two_signals<Cycle>(
      p, frequency, phase, out, length,
      [](Cycle& h, DSP::buffer& f, DSP::buffer& ph) -> DSP::buffer& { return h.object(f, ph); });
}

// ─── blSaw ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bl_saw_create(void) {
  return make<BlSaw>("yse_dsp_bl_saw_create");
}
YSE_C_API void yse_dsp_bl_saw_set_phase(YseDspProcessor* p, float value) {
  with<BlSaw>(p, [&](DSP::blSaw& f) { f.phase(value); });
}
YSE_C_API float yse_dsp_bl_saw_get_phase(YseDspProcessor* p) {
  return query<BlSaw>(p, 0.f, [](DSP::blSaw& f) { return f.phase(); });
}
YSE_C_API void yse_dsp_bl_saw_set_sync_threshold(YseDspProcessor* p, float value) {
  with<BlSaw>(p, [&](DSP::blSaw& f) { f.syncThreshold(value); });
}
YSE_C_API float yse_dsp_bl_saw_get_sync_threshold(YseDspProcessor* p) {
  return query<BlSaw>(p, 0.f, [](DSP::blSaw& f) { return f.syncThreshold(); });
}
YSE_C_API unsigned int yse_dsp_bl_saw_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length) {
  return generate<BlSaw>(
      p, out, length, [&](BlSaw& h, unsigned n) -> DSP::buffer& { return h.object(frequency, n); });
}
YSE_C_API unsigned int yse_dsp_bl_saw_process_signal(YseDspProcessor* p, const float* frequency,
                                                     float* out, unsigned int length) {
  return one_signal<BlSaw>(p, frequency, out, length,
                           [](BlSaw& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f); });
}
YSE_C_API unsigned int yse_dsp_bl_saw_process_sync(YseDspProcessor* p, const float* frequency,
                                                   const float* sync, float* out,
                                                   unsigned int length) {
  return two_signals<BlSaw>(
      p, frequency, sync, out, length,
      [](BlSaw& h, DSP::buffer& f, DSP::buffer& s) -> DSP::buffer& { return h.object(f, s); });
}

// ─── blRect ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bl_rect_create(void) {
  return make<BlRect>("yse_dsp_bl_rect_create");
}
YSE_C_API void yse_dsp_bl_rect_set_width(YseDspProcessor* p, float value) {
  with<BlRect>(p, [&](DSP::blRect& f) { f.width(value); });
}
YSE_C_API float yse_dsp_bl_rect_get_width(YseDspProcessor* p) {
  return query<BlRect>(p, 0.f, [](DSP::blRect& f) { return f.width(); });
}
YSE_C_API void yse_dsp_bl_rect_set_phase(YseDspProcessor* p, float value) {
  with<BlRect>(p, [&](DSP::blRect& f) { f.phase(value); });
}
YSE_C_API float yse_dsp_bl_rect_get_phase(YseDspProcessor* p) {
  return query<BlRect>(p, 0.f, [](DSP::blRect& f) { return f.phase(); });
}
YSE_C_API void yse_dsp_bl_rect_set_sync_threshold(YseDspProcessor* p, float value) {
  with<BlRect>(p, [&](DSP::blRect& f) { f.syncThreshold(value); });
}
YSE_C_API float yse_dsp_bl_rect_get_sync_threshold(YseDspProcessor* p) {
  return query<BlRect>(p, 0.f, [](DSP::blRect& f) { return f.syncThreshold(); });
}
YSE_C_API unsigned int yse_dsp_bl_rect_process(YseDspProcessor* p, float frequency, float* out,
                                               unsigned int length) {
  return generate<BlRect>(p, out, length, [&](BlRect& h, unsigned n) -> DSP::buffer& {
    return h.object(frequency, n);
  });
}
YSE_C_API unsigned int yse_dsp_bl_rect_process_frequency(YseDspProcessor* p, const float* frequency,
                                                         float* out, unsigned int length) {
  return one_signal<BlRect>(p, frequency, out, length,
                            [](BlRect& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f); });
}
YSE_C_API unsigned int yse_dsp_bl_rect_process_signal(YseDspProcessor* p, const float* frequency,
                                                      const float* width, float* out,
                                                      unsigned int length) {
  return two_signals<BlRect>(
      p, frequency, width, out, length,
      [](BlRect& h, DSP::buffer& f, DSP::buffer& w) -> DSP::buffer& { return h.object(f, w); });
}
YSE_C_API unsigned int yse_dsp_bl_rect_process_sync(YseDspProcessor* p, const float* frequency,
                                                    const float* width, const float* sync,
                                                    float* out, unsigned int length) {
  return three_signals<BlRect>(p, frequency, width, sync, out, length,
                               [](BlRect& h, DSP::buffer& f, DSP::buffer& w,
                                  DSP::buffer& s) -> DSP::buffer& { return h.object(f, w, s); });
}

// ─── blTri ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_bl_tri_create(void) {
  return make<BlTri>("yse_dsp_bl_tri_create");
}
YSE_C_API void yse_dsp_bl_tri_set_duty(YseDspProcessor* p, float value) {
  with<BlTri>(p, [&](DSP::blTri& f) { f.duty(value); });
}
YSE_C_API float yse_dsp_bl_tri_get_duty(YseDspProcessor* p) {
  return query<BlTri>(p, 0.f, [](DSP::blTri& f) { return f.duty(); });
}
YSE_C_API void yse_dsp_bl_tri_set_phase(YseDspProcessor* p, float value) {
  with<BlTri>(p, [&](DSP::blTri& f) { f.phase(value); });
}
YSE_C_API float yse_dsp_bl_tri_get_phase(YseDspProcessor* p) {
  return query<BlTri>(p, 0.f, [](DSP::blTri& f) { return f.phase(); });
}
YSE_C_API void yse_dsp_bl_tri_set_sync_threshold(YseDspProcessor* p, float value) {
  with<BlTri>(p, [&](DSP::blTri& f) { f.syncThreshold(value); });
}
YSE_C_API float yse_dsp_bl_tri_get_sync_threshold(YseDspProcessor* p) {
  return query<BlTri>(p, 0.f, [](DSP::blTri& f) { return f.syncThreshold(); });
}
YSE_C_API unsigned int yse_dsp_bl_tri_process(YseDspProcessor* p, float frequency, float* out,
                                              unsigned int length) {
  return generate<BlTri>(
      p, out, length, [&](BlTri& h, unsigned n) -> DSP::buffer& { return h.object(frequency, n); });
}
YSE_C_API unsigned int yse_dsp_bl_tri_process_frequency(YseDspProcessor* p, const float* frequency,
                                                        float* out, unsigned int length) {
  return one_signal<BlTri>(p, frequency, out, length,
                           [](BlTri& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f); });
}
YSE_C_API unsigned int yse_dsp_bl_tri_process_signal(YseDspProcessor* p, const float* frequency,
                                                     const float* duty, float* out,
                                                     unsigned int length) {
  return two_signals<BlTri>(
      p, frequency, duty, out, length,
      [](BlTri& h, DSP::buffer& f, DSP::buffer& d) -> DSP::buffer& { return h.object(f, d); });
}
YSE_C_API unsigned int yse_dsp_bl_tri_process_sync(YseDspProcessor* p, const float* frequency,
                                                   const float* duty, const float* sync, float* out,
                                                   unsigned int length) {
  return three_signals<BlTri>(p, frequency, duty, sync, out, length,
                              [](BlTri& h, DSP::buffer& f, DSP::buffer& d,
                                 DSP::buffer& s) -> DSP::buffer& { return h.object(f, d, s); });
}

// ─── pinkNoise ──────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_pink_noise_create(void) {
  return make<PinkNoise>("yse_dsp_pink_noise_create");
}
YSE_C_API void yse_dsp_pink_noise_seed(YseDspProcessor* p, unsigned int seed) {
  with<PinkNoise>(p, [&](DSP::pinkNoise& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API unsigned int yse_dsp_pink_noise_process(YseDspProcessor* p, float* out,
                                                  unsigned int length) {
  return generate<PinkNoise>(p, out, length,
                             [](PinkNoise& h, unsigned n) -> DSP::buffer& { return h.object(n); });
}

// ─── bandLimitedRandom ──────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_band_limited_random_create(void) {
  return make<BandLimitedRandom>("yse_dsp_band_limited_random_create");
}
YSE_C_API void yse_dsp_band_limited_random_seed(YseDspProcessor* p, unsigned int seed) {
  with<BandLimitedRandom>(
      p, [&](DSP::bandLimitedRandom& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API unsigned int yse_dsp_band_limited_random_process(YseDspProcessor* p, float frequency,
                                                           float* out, unsigned int length) {
  return generate<BandLimitedRandom>(
      p, out, length,
      [&](BandLimitedRandom& h, unsigned n) -> DSP::buffer& { return h.object(frequency, n); });
}
YSE_C_API unsigned int yse_dsp_band_limited_random_process_signal(YseDspProcessor* p,
                                                                  const float* frequency,
                                                                  float* out, unsigned int length) {
  return one_signal<BandLimitedRandom>(
      p, frequency, out, length,
      [](BandLimitedRandom& h, DSP::buffer& f) -> DSP::buffer& { return h.object(f); });
}

// ─── click ──────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_click_create(void) {
  return make<Click>("yse_dsp_click_create");
}
YSE_C_API void yse_dsp_click_set(YseDspProcessor* p, const float* values, unsigned int count) {
  with<Click>(p, [&](DSP::click& f) { f.set(values, count); });
}
YSE_C_API unsigned int yse_dsp_click_get_length(YseDspProcessor* p) {
  return query<Click>(p, 0u, [](DSP::click& f) { return f.length(); });
}
YSE_C_API float yse_dsp_click_get_value(YseDspProcessor* p, unsigned int index) {
  return query<Click>(p, 0.f, [&](DSP::click& f) { return f.value(index); });
}
YSE_C_API void yse_dsp_click_trigger(YseDspProcessor* p, float scale) {
  with<Click>(p, [&](DSP::click& f) { f.trigger(scale); });
}
YSE_C_API void yse_dsp_click_reset(YseDspProcessor* p) {
  with<Click>(p, [](DSP::click& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_click_process(YseDspProcessor* p, float* out, unsigned int length) {
  return generate<Click>(p, out, length,
                         [](Click& h, unsigned n) -> DSP::buffer& { return h.object(n); });
}
YSE_C_API unsigned int yse_dsp_click_process_signal(YseDspProcessor* p, const float* triggers,
                                                    float* out, unsigned int length) {
  return one_signal<Click>(p, triggers, out, length,
                           [](Click& h, DSP::buffer& t) -> DSP::buffer& { return h.object(t); });
}

// ─── pulseTrain ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_pulse_train_create(void) {
  return make<PulseTrain>("yse_dsp_pulse_train_create");
}
YSE_C_API void yse_dsp_pulse_train_set_interval(YseDspProcessor* p, float ms) {
  with<PulseTrain>(p, [&](DSP::pulseTrain& f) { f.interval(ms); });
}
YSE_C_API float yse_dsp_pulse_train_get_interval(YseDspProcessor* p) {
  return query<PulseTrain>(p, 0.f, [](DSP::pulseTrain& f) { return f.interval(); });
}
YSE_C_API void yse_dsp_pulse_train_set_width(YseDspProcessor* p, float value) {
  with<PulseTrain>(p, [&](DSP::pulseTrain& f) { f.width(value); });
}
YSE_C_API float yse_dsp_pulse_train_get_width(YseDspProcessor* p) {
  return query<PulseTrain>(p, 0.f, [](DSP::pulseTrain& f) { return f.width(); });
}
YSE_C_API void yse_dsp_pulse_train_set_phase(YseDspProcessor* p, float value) {
  with<PulseTrain>(p, [&](DSP::pulseTrain& f) { f.phase(value); });
}
YSE_C_API float yse_dsp_pulse_train_get_phase(YseDspProcessor* p) {
  return query<PulseTrain>(p, 0.f, [](DSP::pulseTrain& f) { return f.phase(); });
}
YSE_C_API void yse_dsp_pulse_train_set_defer_interval(YseDspProcessor* p, int on) {
  with<PulseTrain>(p, [&](DSP::pulseTrain& f) { f.deferInterval(on != 0); });
}
YSE_C_API int yse_dsp_pulse_train_get_defer_interval(YseDspProcessor* p) {
  return query<PulseTrain>(p, 0, [](DSP::pulseTrain& f) { return f.deferInterval() ? 1 : 0; });
}
YSE_C_API void yse_dsp_pulse_train_trigger(YseDspProcessor* p) {
  with<PulseTrain>(p, [](DSP::pulseTrain& f) { f.trigger(); });
}
YSE_C_API void yse_dsp_pulse_train_reset(YseDspProcessor* p) {
  with<PulseTrain>(p, [](DSP::pulseTrain& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_pulse_train_process(YseDspProcessor* p, float* out, float* onsets,
                                                   unsigned int length) {
  if (out == nullptr) return 0;
  return drive<PulseTrain>(p, length, [&](PulseTrain& h, unsigned o, unsigned n) {
    emit(h.object(n), out + o, n);
    emit_onsets(h.object, onsets, o, n);
  });
}
YSE_C_API unsigned int yse_dsp_pulse_train_process_interval(YseDspProcessor* p,
                                                            const float* interval, float* out,
                                                            float* onsets, unsigned int length) {
  if (interval == nullptr || out == nullptr) return 0;
  return drive<PulseTrain>(p, length, [&](PulseTrain& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], interval + o, n)), out + o, n);
    emit_onsets(h.object, onsets, o, n);
  });
}
YSE_C_API unsigned int yse_dsp_pulse_train_process_signal(YseDspProcessor* p, const float* interval,
                                                          const float* width, const float* phase,
                                                          float* out, float* onsets,
                                                          unsigned int length) {
  if (interval == nullptr || width == nullptr || phase == nullptr || out == nullptr) return 0;
  return drive<PulseTrain>(p, length, [&](PulseTrain& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], interval + o, n), stage(h.scratch[1], width + o, n),
                  stage(h.scratch[2], phase + o, n)),
         out + o, n);
    emit_onsets(h.object, onsets, o, n);
  });
}

// ─── kink ───────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_kink_create(void) {
  return make<Kink>("yse_dsp_kink_create");
}
YSE_C_API float yse_dsp_kink_value(float phase, float slope) {
  return DSP::kink::process(phase, slope);
}
YSE_C_API unsigned int yse_dsp_kink_process(YseDspProcessor* p, const float* phase, float slope,
                                            float* out, unsigned int length) {
  return one_signal<Kink>(p, phase, out, length, [&](Kink& h, DSP::buffer& ph) -> DSP::buffer& {
    return h.object(ph, slope);
  });
}
YSE_C_API unsigned int yse_dsp_kink_process_signal(YseDspProcessor* p, const float* phase,
                                                   const float* slope, float* out,
                                                   unsigned int length) {
  return two_signals<Kink>(
      p, phase, slope, out, length,
      [](Kink& h, DSP::buffer& ph, DSP::buffer& s) -> DSP::buffer& { return h.object(ph, s); });
}

// ─── oscillatorBank ─────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_oscillator_bank_create(unsigned int capacity, int interpolating,
                                                          unsigned int table_size) {
  return make<OscillatorBank>(
      "yse_dsp_oscillator_bank_create",
      capacity != 0 ? capacity : DSP::oscillatorBank::DEFAULT_OSCILLATORS, interpolating != 0,
      table_size != 0 ? table_size : DSP::oscillatorBank::DEFAULT_TABLE_SIZE);
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_capacity(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0u, [](DSP::oscillatorBank& f) { return f.capacity(); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_size(YseDspProcessor* p, unsigned int count) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.size(count); });
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_size(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0u, [](DSP::oscillatorBank& f) { return f.size(); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_frequency_smoothing(YseDspProcessor* p,
                                                               unsigned int samples) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.frequencySmoothing(samples); });
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_frequency_smoothing(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0u,
                               [](DSP::oscillatorBank& f) { return f.frequencySmoothing(); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_magnitude_smoothing(YseDspProcessor* p,
                                                               unsigned int samples) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.magnitudeSmoothing(samples); });
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_magnitude_smoothing(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0u,
                               [](DSP::oscillatorBank& f) { return f.magnitudeSmoothing(); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_frame_sync(YseDspProcessor* p, int on) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.frameSync(on != 0); });
}
YSE_C_API int yse_dsp_oscillator_bank_get_frame_sync(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0, [](DSP::oscillatorBank& f) { return f.frameSync() ? 1 : 0; });
}
YSE_C_API int yse_dsp_oscillator_bank_get_interpolating(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0,
                               [](DSP::oscillatorBank& f) { return f.interpolating() ? 1 : 0; });
}
YSE_C_API void yse_dsp_oscillator_bank_set_table_size(YseDspProcessor* p, unsigned int points) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.tableSize(points); });
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_get_table_size(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0u, [](DSP::oscillatorBank& f) { return f.tableSize(); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_table(YseDspProcessor* p, const float* samples,
                                                 unsigned int count) {
  // The one call in this header that can allocate (a table larger than any
  // the bank had): the engine's vector grows, so guard the bad_alloc. It is a
  // setter, so a failure leaves the table as it was and reports nothing.
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) {
    try {
      f.setTable(samples, count);
    } catch (...) { // NOLINT(bugprone-empty-catch): a void setter has no error channel
    }
  });
}
YSE_C_API int yse_dsp_oscillator_bank_get_custom_table(YseDspProcessor* p) {
  return query<OscillatorBank>(p, 0,
                               [](DSP::oscillatorBank& f) { return f.customTable() ? 1 : 0; });
}
YSE_C_API void yse_dsp_oscillator_bank_set(YseDspProcessor* p, unsigned int index, float frequency,
                                           float magnitude) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.set(index, frequency, magnitude); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_frequency(YseDspProcessor* p, unsigned int index,
                                                     float frequency) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.setFrequency(index, frequency); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_magnitude(YseDspProcessor* p, unsigned int index,
                                                     float magnitude) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.setMagnitude(index, magnitude); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_phase(YseDspProcessor* p, unsigned int index,
                                                 float phase) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.setPhase(index, phase); });
}
YSE_C_API void yse_dsp_oscillator_bank_set_pairs(YseDspProcessor* p, const float* pairs,
                                                 unsigned int count) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.setPairs(pairs, count); });
}
YSE_C_API void yse_dsp_oscillator_bank_fade_from(YseDspProcessor* p, unsigned int first) {
  with<OscillatorBank>(p, [&](DSP::oscillatorBank& f) { f.fadeFrom(first); });
}
YSE_C_API void yse_dsp_oscillator_bank_clear(YseDspProcessor* p) {
  with<OscillatorBank>(p, [](DSP::oscillatorBank& f) { f.clear(); });
}
YSE_C_API void yse_dsp_oscillator_bank_silence(YseDspProcessor* p) {
  with<OscillatorBank>(p, [](DSP::oscillatorBank& f) { f.silence(); });
}
YSE_C_API void yse_dsp_oscillator_bank_reset(YseDspProcessor* p) {
  with<OscillatorBank>(p, [](DSP::oscillatorBank& f) { f.reset(); });
}
YSE_C_API float yse_dsp_oscillator_bank_get_frequency(YseDspProcessor* p, unsigned int index) {
  return query<OscillatorBank>(p, 0.f, [&](DSP::oscillatorBank& f) { return f.frequency(index); });
}
YSE_C_API float yse_dsp_oscillator_bank_get_magnitude(YseDspProcessor* p, unsigned int index) {
  return query<OscillatorBank>(p, 0.f, [&](DSP::oscillatorBank& f) { return f.magnitude(index); });
}
YSE_C_API float yse_dsp_oscillator_bank_get_phase(YseDspProcessor* p, unsigned int index) {
  return query<OscillatorBank>(p, 0.f, [&](DSP::oscillatorBank& f) { return f.phase(index); });
}
YSE_C_API unsigned int yse_dsp_oscillator_bank_process(YseDspProcessor* p, float* out,
                                                       unsigned int length) {
  return generate<OscillatorBank>(
      p, out, length, [](OscillatorBank& h, unsigned n) -> DSP::buffer& { return h.object(n); });
}
YSE_C_API unsigned int
yse_dsp_oscillator_bank_process_signal(YseDspProcessor* p, const float* frequency,
                                       const float* magnitude, const float* phase,
                                       const float* index, float* out, unsigned int length) {
  if (frequency == nullptr || magnitude == nullptr || index == nullptr || out == nullptr) return 0;
  return drive<OscillatorBank>(p, length, [&](OscillatorBank& h, unsigned o, unsigned n) {
    DSP::buffer& f = stage(h.scratch[0], frequency + o, n);
    DSP::buffer& m = stage(h.scratch[1], magnitude + o, n);
    DSP::buffer& i = stage(h.scratch[3], index + o, n);
    if (phase != nullptr) {
      emit(h.object(f, m, stage(h.scratch[2], phase + o, n), i), out + o, n);
    } else {
      emit(h.object(f, m, i), out + o, n);
    }
  });
}

// ─── triangleShape ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_triangle_shape_create(void) {
  return make<TriangleShape>("yse_dsp_triangle_shape_create");
}
YSE_C_API float yse_dsp_triangle_shape_value(float phase, float peak, float lo, float hi) {
  return DSP::triangleShape::process(phase, peak, lo, hi);
}
YSE_C_API void yse_dsp_triangle_shape_set_range(YseDspProcessor* p, float lo, float hi) {
  with<TriangleShape>(p, [&](DSP::triangleShape& f) { f.range(lo, hi); });
}
YSE_C_API float yse_dsp_triangle_shape_get_lo(YseDspProcessor* p) {
  return query<TriangleShape>(p, 0.f, [](DSP::triangleShape& f) { return f.lo(); });
}
YSE_C_API float yse_dsp_triangle_shape_get_hi(YseDspProcessor* p) {
  return query<TriangleShape>(p, 0.f, [](DSP::triangleShape& f) { return f.hi(); });
}
YSE_C_API unsigned int yse_dsp_triangle_shape_process(YseDspProcessor* p, const float* phase,
                                                      float peak, float* out, unsigned int length) {
  return one_signal<TriangleShape>(
      p, phase, out, length,
      [&](TriangleShape& h, DSP::buffer& ph) -> DSP::buffer& { return h.object(ph, peak); });
}
YSE_C_API unsigned int yse_dsp_triangle_shape_process_signal(YseDspProcessor* p, const float* phase,
                                                             const float* peak, float* out,
                                                             unsigned int length) {
  return two_signals<TriangleShape>(
      p, phase, peak, out, length,
      [](TriangleShape& h, DSP::buffer& ph, DSP::buffer& pk) -> DSP::buffer& {
        return h.object(ph, pk);
      });
}

// ─── trapezoidShape ─────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_trapezoid_shape_create(void) {
  return make<TrapezoidShape>("yse_dsp_trapezoid_shape_create");
}
YSE_C_API float yse_dsp_trapezoid_shape_value(float phase, float up, float down, float lo, float hi,
                                              int wrap) {
  return DSP::trapezoidShape::process(phase, up, down, lo, hi, wrap != 0);
}
YSE_C_API void yse_dsp_trapezoid_shape_set_range(YseDspProcessor* p, float lo, float hi) {
  with<TrapezoidShape>(p, [&](DSP::trapezoidShape& f) { f.range(lo, hi); });
}
YSE_C_API float yse_dsp_trapezoid_shape_get_lo(YseDspProcessor* p) {
  return query<TrapezoidShape>(p, 0.f, [](DSP::trapezoidShape& f) { return f.lo(); });
}
YSE_C_API float yse_dsp_trapezoid_shape_get_hi(YseDspProcessor* p) {
  return query<TrapezoidShape>(p, 0.f, [](DSP::trapezoidShape& f) { return f.hi(); });
}
YSE_C_API void yse_dsp_trapezoid_shape_set_wrap(YseDspProcessor* p, int on) {
  with<TrapezoidShape>(p, [&](DSP::trapezoidShape& f) { f.wrap(on != 0); });
}
YSE_C_API int yse_dsp_trapezoid_shape_get_wrap(YseDspProcessor* p) {
  return query<TrapezoidShape>(p, 0, [](DSP::trapezoidShape& f) { return f.wrap() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_trapezoid_shape_process(YseDspProcessor* p, const float* phase,
                                                       float up, float down, float* out,
                                                       unsigned int length) {
  return one_signal<TrapezoidShape>(
      p, phase, out, length,
      [&](TrapezoidShape& h, DSP::buffer& ph) -> DSP::buffer& { return h.object(ph, up, down); });
}
YSE_C_API unsigned int yse_dsp_trapezoid_shape_process_signal(YseDspProcessor* p,
                                                              const float* phase, const float* up,
                                                              const float* down, float* out,
                                                              unsigned int length) {
  return three_signals<TrapezoidShape>(
      p, phase, up, down, out, length,
      [](TrapezoidShape& h, DSP::buffer& ph, DSP::buffer& u, DSP::buffer& d) -> DSP::buffer& {
        return h.object(ph, u, d);
      });
}

// ─── sampleCounter ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_sample_counter_create(void) {
  return make<SampleCounter>("yse_dsp_sample_counter_create");
}
YSE_C_API void yse_dsp_sample_counter_set_minimum(YseDspProcessor* p, int value) {
  with<SampleCounter>(p, [&](DSP::sampleCounter& f) { f.minimum(value); });
}
YSE_C_API int yse_dsp_sample_counter_get_minimum(YseDspProcessor* p) {
  return query<SampleCounter>(p, 0, [](DSP::sampleCounter& f) { return f.minimum(); });
}
YSE_C_API void yse_dsp_sample_counter_set_limit(YseDspProcessor* p, int value) {
  with<SampleCounter>(p, [&](DSP::sampleCounter& f) { f.limit(value); });
}
YSE_C_API int yse_dsp_sample_counter_get_limit(YseDspProcessor* p) {
  return query<SampleCounter>(p, 0, [](DSP::sampleCounter& f) { return f.limit(); });
}
YSE_C_API void yse_dsp_sample_counter_start(YseDspProcessor* p) {
  with<SampleCounter>(p, [](DSP::sampleCounter& f) { f.start(); });
}
YSE_C_API void yse_dsp_sample_counter_start_at(YseDspProcessor* p, int value) {
  with<SampleCounter>(p, [&](DSP::sampleCounter& f) { f.start(value); });
}
YSE_C_API void yse_dsp_sample_counter_stop(YseDspProcessor* p) {
  with<SampleCounter>(p, [](DSP::sampleCounter& f) { f.stop(); });
}
YSE_C_API int yse_dsp_sample_counter_is_counting(YseDspProcessor* p) {
  return query<SampleCounter>(p, 0, [](DSP::sampleCounter& f) { return f.counting() ? 1 : 0; });
}
YSE_C_API long long yse_dsp_sample_counter_get_count(YseDspProcessor* p) {
  return query<SampleCounter>(p, 0LL, [](DSP::sampleCounter& f) { return f.count(); });
}
YSE_C_API long long yse_dsp_sample_counter_get_carries(YseDspProcessor* p) {
  return query<SampleCounter>(p, 0LL, [](DSP::sampleCounter& f) { return f.carries(); });
}
YSE_C_API unsigned int yse_dsp_sample_counter_process(YseDspProcessor* p, float* out, float* carry,
                                                      unsigned int length) {
  if (out == nullptr) return 0;
  return drive<SampleCounter>(p, length, [&](SampleCounter& h, unsigned o, unsigned n) {
    emit(h.object(n), out + o, n);
    emit(h.object.carry(), carry != nullptr ? carry + o : nullptr, n);
  });
}
YSE_C_API unsigned int yse_dsp_sample_counter_process_signal(YseDspProcessor* p,
                                                             const float* enable, float* out,
                                                             float* carry, unsigned int length) {
  if (enable == nullptr || out == nullptr) return 0;
  return drive<SampleCounter>(p, length, [&](SampleCounter& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], enable + o, n)), out + o, n);
    emit(h.object.carry(), carry != nullptr ? carry + o : nullptr, n);
  });
}

} // extern "C"
