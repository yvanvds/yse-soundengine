#include "yse_c/yse_dsp_modules.h"
#include "yse_c_internal.hpp"

#include "../dsp/dspObject.hpp"
#include "../dsp/lfo.hpp"
#include "../dsp/modules/filters/lowpass.hpp"
#include "../dsp/modules/filters/highpass.hpp"
#include "../dsp/modules/filters/bandpass.hpp"
#include "../dsp/modules/filters/sweep.hpp"
#include "../dsp/modules/delay/basicDelay.hpp"
#include "../dsp/modules/delay/lowpassDelay.hpp"
#include "../dsp/modules/delay/highpassDelay.hpp"
#include "../dsp/modules/phaser.hpp"
#include "../dsp/modules/ringModulator.hpp"
#include "../dsp/modules/granulator.hpp"
#include "../dsp/modules/fm/difference.hpp"

#include <exception>

namespace {
  inline YSE::DSP::dspObject* to_cpp(YseDspObject* o) {
    return reinterpret_cast<YSE::DSP::dspObject*>(o);
  }

  template <typename T>
  inline T* as(YseDspObject* o) {
    return o ? static_cast<T*>(to_cpp(o)) : nullptr;
  }

  template <typename T>
  inline YseDspObject* mk() {
    try { return reinterpret_cast<YseDspObject*>(new T()); }
    catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
    catch (...) { yse_c::set_last_error("dsp_module create: unknown C++ exception"); return nullptr; }
  }
}

extern "C" {

// ─── constructors ──────────────────────────────────────────────────────

YSE_C_API YseDspObject* yse_dsp_lowpass_create(void)         { return mk<YSE::DSP::MODULES::lowPassFilter>(); }
YSE_C_API YseDspObject* yse_dsp_highpass_create(void)        { return mk<YSE::DSP::MODULES::highPassFilter>(); }
YSE_C_API YseDspObject* yse_dsp_bandpass_create(void)        { return mk<YSE::DSP::MODULES::bandPassFilter>(); }

YSE_C_API YseDspObject* yse_dsp_sweep_create(YseDspSweepShape shape) {
  try {
    return reinterpret_cast<YseDspObject*>(
        new YSE::DSP::MODULES::sweepFilter(
            static_cast<YSE::DSP::MODULES::sweepFilter::SHAPE>(shape)));
  } catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
    catch (...) { yse_c::set_last_error("sweep_create: unknown C++ exception"); return nullptr; }
}

YSE_C_API YseDspObject* yse_dsp_basic_delay_create(void)     { return mk<YSE::DSP::MODULES::basicDelay>(); }
YSE_C_API YseDspObject* yse_dsp_lowpass_delay_create(void)   { return mk<YSE::DSP::MODULES::lowPassDelay>(); }
YSE_C_API YseDspObject* yse_dsp_highpass_delay_create(void)  { return mk<YSE::DSP::MODULES::highPassDelay>(); }
YSE_C_API YseDspObject* yse_dsp_phaser_create(void)          { return mk<YSE::DSP::MODULES::phaser>(); }
YSE_C_API YseDspObject* yse_dsp_ring_modulator_create(void)  { return mk<YSE::DSP::ringModulator>(); }
YSE_C_API YseDspObject* yse_dsp_difference_create(void)      { return mk<YSE::DSP::MODULES::difference>(); }

YSE_C_API YseDspObject* yse_dsp_granulator_create(unsigned int pool_size, unsigned int max_grains) {
  try {
    return reinterpret_cast<YseDspObject*>(
        new YSE::DSP::MODULES::granulator(pool_size, max_grains));
  } catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
    catch (...) { yse_c::set_last_error("granulator_create: unknown C++ exception"); return nullptr; }
}

YSE_C_API void yse_dsp_object_destroy(YseDspObject* obj) {
  if (!obj) return;
  delete to_cpp(obj);
}

// ─── inherited dspObject control surface ──────────────────────────────

YSE_C_API void  yse_dsp_object_set_bypass(YseDspObject* o, int on) { if (o) to_cpp(o)->bypass(on != 0); }
YSE_C_API int   yse_dsp_object_get_bypass(YseDspObject* o)         { return o && to_cpp(o)->bypass() ? 1 : 0; }
YSE_C_API void  yse_dsp_object_set_impact(YseDspObject* o, float v){ if (o) to_cpp(o)->impact(v); }
YSE_C_API float yse_dsp_object_get_impact(YseDspObject* o)         { return o ? to_cpp(o)->impact() : 0.0f; }

YSE_C_API void yse_dsp_object_set_lfo_type(YseDspObject* o, YseLfoType type) {
  if (o) to_cpp(o)->lfoType(static_cast<YSE::DSP::LFO_TYPE>(type));
}
YSE_C_API YseLfoType yse_dsp_object_get_lfo_type(YseDspObject* o) {
  return o ? static_cast<YseLfoType>(to_cpp(o)->lfoType()) : YSE_LFO_NONE;
}
YSE_C_API void  yse_dsp_object_set_lfo_frequency(YseDspObject* o, float v) { if (o) to_cpp(o)->lfoFrequency(v); }
YSE_C_API float yse_dsp_object_get_lfo_frequency(YseDspObject* o)          { return o ? to_cpp(o)->lfoFrequency() : 0.0f; }

YSE_C_API void yse_dsp_object_link(YseDspObject* head, YseDspObject* next) {
  if (!head || !next) return;
  to_cpp(head)->link(*to_cpp(next));
}

// ─── filter modules ──────────────────────────────────────────────────

YSE_C_API void  yse_dsp_lowpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::lowPassFilter>(o); if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_lowpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::lowPassFilter>(o); return f ? f->frequency() : 0.0f;
}
YSE_C_API void  yse_dsp_highpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::highPassFilter>(o); if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_highpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::highPassFilter>(o); return f ? f->frequency() : 0.0f;
}
YSE_C_API void  yse_dsp_bandpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o); if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_bandpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o); return f ? f->frequency() : 0.0f;
}
YSE_C_API void  yse_dsp_bandpass_set_q(YseDspObject* o, float q) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o); if (f) f->setQ(q);
}
YSE_C_API float yse_dsp_bandpass_get_q(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o); return f ? f->getQ() : 0.0f;
}

YSE_C_API void  yse_dsp_sweep_set_speed(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); if (f) f->speed(hz);
}
YSE_C_API float yse_dsp_sweep_get_speed(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); return f ? f->speed() : 0.0f;
}
YSE_C_API void  yse_dsp_sweep_set_depth(YseDspObject* o, int v) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); if (f) f->depth(v);
}
YSE_C_API int   yse_dsp_sweep_get_depth(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); return f ? f->depth() : 0;
}
YSE_C_API void  yse_dsp_sweep_set_frequency(YseDspObject* o, int v) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); if (f) f->frequency(v);
}
YSE_C_API int   yse_dsp_sweep_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o); return f ? f->frequency() : 0;
}

// ─── delay modules ───────────────────────────────────────────────────

YSE_C_API void yse_dsp_basic_delay_set_tap(YseDspObject* o, YseDspDelayTap tap, float time_ms, float gain) {
  auto* d = as<YSE::DSP::MODULES::basicDelay>(o);
  if (d) d->set(static_cast<YSE::DSP::MODULES::basicDelay::DELAY_NR>(tap), time_ms, gain);
}
YSE_C_API float yse_dsp_basic_delay_get_time(YseDspObject* o, YseDspDelayTap tap) {
  auto* d = as<YSE::DSP::MODULES::basicDelay>(o);
  return d ? d->time(static_cast<YSE::DSP::MODULES::basicDelay::DELAY_NR>(tap)) : 0.0f;
}
YSE_C_API float yse_dsp_basic_delay_get_gain(YseDspObject* o, YseDspDelayTap tap) {
  auto* d = as<YSE::DSP::MODULES::basicDelay>(o);
  return d ? d->gain(static_cast<YSE::DSP::MODULES::basicDelay::DELAY_NR>(tap)) : 0.0f;
}

YSE_C_API void yse_dsp_lowpass_delay_set_frequency(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::lowPassDelay>(o); if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_lowpass_delay_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::lowPassDelay>(o); return d ? d->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_highpass_delay_set_frequency(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::highPassDelay>(o); if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_highpass_delay_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::highPassDelay>(o); return d ? d->frequency() : 0.0f;
}

// ─── modulation modules ──────────────────────────────────────────────

YSE_C_API void  yse_dsp_phaser_set_frequency(YseDspObject* o, float hz) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o); if (p) p->frequency(hz);
}
YSE_C_API float yse_dsp_phaser_get_frequency(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o); return p ? p->frequency() : 0.0f;
}
YSE_C_API void  yse_dsp_phaser_set_range(YseDspObject* o, float v) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o); if (p) p->range(v);
}
YSE_C_API float yse_dsp_phaser_get_range(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o); return p ? p->range() : 0.0f;
}

YSE_C_API void  yse_dsp_ring_modulator_set_frequency(YseDspObject* o, float hz) {
  auto* r = as<YSE::DSP::ringModulator>(o); if (r) r->frequency(hz);
}
YSE_C_API float yse_dsp_ring_modulator_get_frequency(YseDspObject* o) {
  auto* r = as<YSE::DSP::ringModulator>(o); return r ? r->frequency() : 0.0f;
}

YSE_C_API void  yse_dsp_difference_set_frequency(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::difference>(o); if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_difference_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::difference>(o); return d ? d->frequency() : 0.0f;
}
YSE_C_API void  yse_dsp_difference_set_amplitude(YseDspObject* o, float v) {
  auto* d = as<YSE::DSP::MODULES::difference>(o); if (d) d->amplitude(v);
}
YSE_C_API float yse_dsp_difference_get_amplitude(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::difference>(o); return d ? d->amplitude() : 0.0f;
}

YSE_C_API void yse_dsp_granulator_set_grain_frequency(YseDspObject* o, unsigned int v) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); if (g) g->grainFrequency(v);
}
YSE_C_API unsigned int yse_dsp_granulator_get_grain_frequency(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); return g ? g->grainFrequency() : 0;
}
YSE_C_API void yse_dsp_granulator_set_grain_length(YseDspObject* o, unsigned int samples, unsigned int random_samples) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); if (g) g->grainLength(samples, random_samples);
}
YSE_C_API unsigned int yse_dsp_granulator_get_grain_length(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); return g ? g->grainLength() : 0;
}
YSE_C_API void yse_dsp_granulator_set_grain_transpose(YseDspObject* o, float pitch, float random) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); if (g) g->grainTranspose(pitch, random);
}
YSE_C_API float yse_dsp_granulator_get_grain_transpose(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); return g ? g->grainTranspose() : 0.0f;
}
YSE_C_API void yse_dsp_granulator_set_gain(YseDspObject* o, float v) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); if (g) g->gain(v);
}
YSE_C_API float yse_dsp_granulator_get_gain(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o); return g ? g->gain() : 0.0f;
}

} // extern "C"
