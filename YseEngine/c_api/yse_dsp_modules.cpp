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
#include "../dsp/modules/delay/feedbackDelay.hpp"
#include "../dsp/modules/chorus.hpp"
#include "../dsp/modules/plateReverb.hpp"
#include "../dsp/modules/parametricEQ.hpp"
#include "../dsp/modules/compressor.hpp"

#include <exception>

namespace {
  inline YSE::DSP::dspObject* to_cpp(YseDspObject* o) {
    return reinterpret_cast<YSE::DSP::dspObject*>(o);
  }

  template <typename T> inline T* as(YseDspObject* o) {
    return o ? static_cast<T*>(to_cpp(o)) : nullptr;
  }

  template <typename T> inline YseDspObject* mk() {
    try {
      return reinterpret_cast<YseDspObject*>(new T());
    } catch (const std::exception& e) {
      yse_c::set_last_error(e.what());
      return nullptr;
    } catch (...) {
      yse_c::set_last_error("dsp_module create: unknown C++ exception");
      return nullptr;
    }
  }
} // namespace

extern "C" {

// ─── constructors ──────────────────────────────────────────────────────

YSE_C_API YseDspObject* yse_dsp_lowpass_create(void) {
  return mk<YSE::DSP::MODULES::lowPassFilter>();
}
YSE_C_API YseDspObject* yse_dsp_highpass_create(void) {
  return mk<YSE::DSP::MODULES::highPassFilter>();
}
YSE_C_API YseDspObject* yse_dsp_bandpass_create(void) {
  return mk<YSE::DSP::MODULES::bandPassFilter>();
}

YSE_C_API YseDspObject* yse_dsp_sweep_create(YseDspSweepShape shape) {
  try {
    return reinterpret_cast<YseDspObject*>(new YSE::DSP::MODULES::sweepFilter(
        static_cast<YSE::DSP::MODULES::sweepFilter::SHAPE>(shape)));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("sweep_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseDspObject* yse_dsp_basic_delay_create(void) {
  return mk<YSE::DSP::MODULES::basicDelay>();
}
YSE_C_API YseDspObject* yse_dsp_lowpass_delay_create(void) {
  return mk<YSE::DSP::MODULES::lowPassDelay>();
}
YSE_C_API YseDspObject* yse_dsp_highpass_delay_create(void) {
  return mk<YSE::DSP::MODULES::highPassDelay>();
}
YSE_C_API YseDspObject* yse_dsp_phaser_create(void) {
  return mk<YSE::DSP::MODULES::phaser>();
}
YSE_C_API YseDspObject* yse_dsp_ring_modulator_create(void) {
  return mk<YSE::DSP::ringModulator>();
}
YSE_C_API YseDspObject* yse_dsp_difference_create(void) {
  return mk<YSE::DSP::MODULES::difference>();
}

YSE_C_API YseDspObject* yse_dsp_granulator_create(unsigned int pool_size, unsigned int max_grains) {
  try {
    return reinterpret_cast<YseDspObject*>(
        new YSE::DSP::MODULES::granulator(pool_size, max_grains));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("granulator_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseDspObject* yse_dsp_feedback_delay_create(void) {
  return mk<YSE::DSP::MODULES::feedbackDelay>();
}
YSE_C_API YseDspObject* yse_dsp_chorus_create(void) {
  return mk<YSE::DSP::MODULES::chorus>();
}
YSE_C_API YseDspObject* yse_dsp_plate_reverb_create(void) {
  return mk<YSE::DSP::MODULES::plateReverb>();
}
YSE_C_API YseDspObject* yse_dsp_eq_create(void) {
  return mk<YSE::DSP::MODULES::parametricEQ>();
}
YSE_C_API YseDspObject* yse_dsp_compressor_create(void) {
  return mk<YSE::DSP::MODULES::compressor>();
}

YSE_C_API void yse_dsp_object_destroy(YseDspObject* obj) {
  if (!obj) return;
  delete to_cpp(obj);
}

// ─── inherited dspObject control surface ──────────────────────────────

YSE_C_API void yse_dsp_object_set_bypass(YseDspObject* o, int on) {
  if (o) to_cpp(o)->bypass(on != 0);
}
YSE_C_API int yse_dsp_object_get_bypass(YseDspObject* o) {
  return o && to_cpp(o)->bypass() ? 1 : 0;
}
YSE_C_API void yse_dsp_object_set_impact(YseDspObject* o, float v) {
  if (o) to_cpp(o)->impact(v);
}
YSE_C_API float yse_dsp_object_get_impact(YseDspObject* o) {
  return o ? to_cpp(o)->impact() : 0.0f;
}

YSE_C_API void yse_dsp_object_set_lfo_type(YseDspObject* o, YseLfoType type) {
  if (o) to_cpp(o)->lfoType(static_cast<YSE::DSP::LFO_TYPE>(type));
}
YSE_C_API YseLfoType yse_dsp_object_get_lfo_type(YseDspObject* o) {
  return o ? static_cast<YseLfoType>(to_cpp(o)->lfoType()) : YSE_LFO_NONE;
}
YSE_C_API void yse_dsp_object_set_lfo_frequency(YseDspObject* o, float v) {
  if (o) to_cpp(o)->lfoFrequency(v);
}
YSE_C_API float yse_dsp_object_get_lfo_frequency(YseDspObject* o) {
  return o ? to_cpp(o)->lfoFrequency() : 0.0f;
}

YSE_C_API void yse_dsp_object_link(YseDspObject* head, YseDspObject* next) {
  if (!head || !next) return;
  to_cpp(head)->link(*to_cpp(next));
}

// ─── filter modules ──────────────────────────────────────────────────

YSE_C_API void yse_dsp_lowpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::lowPassFilter>(o);
  if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_lowpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::lowPassFilter>(o);
  return f ? f->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_highpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::highPassFilter>(o);
  if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_highpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::highPassFilter>(o);
  return f ? f->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_bandpass_set_frequency(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o);
  if (f) f->frequency(hz);
}
YSE_C_API float yse_dsp_bandpass_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o);
  return f ? f->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_bandpass_set_q(YseDspObject* o, float q) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o);
  if (f) f->setQ(q);
}
YSE_C_API float yse_dsp_bandpass_get_q(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::bandPassFilter>(o);
  return f ? f->getQ() : 0.0f;
}

YSE_C_API void yse_dsp_sweep_set_speed(YseDspObject* o, float hz) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  if (f) f->speed(hz);
}
YSE_C_API float yse_dsp_sweep_get_speed(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  return f ? f->speed() : 0.0f;
}
YSE_C_API void yse_dsp_sweep_set_depth(YseDspObject* o, int v) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  if (f) f->depth(v);
}
YSE_C_API int yse_dsp_sweep_get_depth(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  return f ? f->depth() : 0;
}
YSE_C_API void yse_dsp_sweep_set_frequency(YseDspObject* o, int v) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  if (f) f->frequency(v);
}
YSE_C_API int yse_dsp_sweep_get_frequency(YseDspObject* o) {
  auto* f = as<YSE::DSP::MODULES::sweepFilter>(o);
  return f ? f->frequency() : 0;
}

// ─── delay modules ───────────────────────────────────────────────────

YSE_C_API void yse_dsp_basic_delay_set_tap(YseDspObject* o, YseDspDelayTap tap, float time_ms,
                                           float gain) {
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
  auto* d = as<YSE::DSP::MODULES::lowPassDelay>(o);
  if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_lowpass_delay_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::lowPassDelay>(o);
  return d ? d->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_highpass_delay_set_frequency(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::highPassDelay>(o);
  if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_highpass_delay_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::highPassDelay>(o);
  return d ? d->frequency() : 0.0f;
}

// ─── modulation modules ──────────────────────────────────────────────

YSE_C_API void yse_dsp_phaser_set_frequency(YseDspObject* o, float hz) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o);
  if (p) p->frequency(hz);
}
YSE_C_API float yse_dsp_phaser_get_frequency(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o);
  return p ? p->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_phaser_set_range(YseDspObject* o, float v) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o);
  if (p) p->range(v);
}
YSE_C_API float yse_dsp_phaser_get_range(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::phaser>(o);
  return p ? p->range() : 0.0f;
}

YSE_C_API void yse_dsp_ring_modulator_set_frequency(YseDspObject* o, float hz) {
  auto* r = as<YSE::DSP::ringModulator>(o);
  if (r) r->frequency(hz);
}
YSE_C_API float yse_dsp_ring_modulator_get_frequency(YseDspObject* o) {
  auto* r = as<YSE::DSP::ringModulator>(o);
  return r ? r->frequency() : 0.0f;
}

YSE_C_API void yse_dsp_difference_set_frequency(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::difference>(o);
  if (d) d->frequency(hz);
}
YSE_C_API float yse_dsp_difference_get_frequency(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::difference>(o);
  return d ? d->frequency() : 0.0f;
}
YSE_C_API void yse_dsp_difference_set_amplitude(YseDspObject* o, float v) {
  auto* d = as<YSE::DSP::MODULES::difference>(o);
  if (d) d->amplitude(v);
}
YSE_C_API float yse_dsp_difference_get_amplitude(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::difference>(o);
  return d ? d->amplitude() : 0.0f;
}

YSE_C_API void yse_dsp_granulator_set_grain_frequency(YseDspObject* o, unsigned int v) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  if (g) g->grainFrequency(v);
}
YSE_C_API unsigned int yse_dsp_granulator_get_grain_frequency(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  return g ? g->grainFrequency() : 0;
}
YSE_C_API void yse_dsp_granulator_set_grain_length(YseDspObject* o, unsigned int samples,
                                                   unsigned int random_samples) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  if (g) g->grainLength(samples, random_samples);
}
YSE_C_API unsigned int yse_dsp_granulator_get_grain_length(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  return g ? g->grainLength() : 0;
}
YSE_C_API void yse_dsp_granulator_set_grain_transpose(YseDspObject* o, float pitch, float random) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  if (g) g->grainTranspose(pitch, random);
}
YSE_C_API float yse_dsp_granulator_get_grain_transpose(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  return g ? g->grainTranspose() : 0.0f;
}
YSE_C_API void yse_dsp_granulator_set_gain(YseDspObject* o, float v) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  if (g) g->gain(v);
}
YSE_C_API float yse_dsp_granulator_get_gain(YseDspObject* o) {
  auto* g = as<YSE::DSP::MODULES::granulator>(o);
  return g ? g->gain() : 0.0f;
}

// ─── feedback delay (#160) ────────────────────────────────────────────

YSE_C_API void yse_dsp_feedback_delay_set_time(YseDspObject* o, float ms) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  if (d) d->time(ms);
}
YSE_C_API float yse_dsp_feedback_delay_get_time(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  return d ? d->time() : 0.0f;
}
YSE_C_API void yse_dsp_feedback_delay_set_feedback(YseDspObject* o, float amount) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  if (d) d->feedback(amount);
}
YSE_C_API float yse_dsp_feedback_delay_get_feedback(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  return d ? d->feedback() : 0.0f;
}
YSE_C_API void yse_dsp_feedback_delay_set_damping(YseDspObject* o, float hz) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  if (d) d->damping(hz);
}
YSE_C_API float yse_dsp_feedback_delay_get_damping(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  return d ? d->damping() : 0.0f;
}
YSE_C_API void yse_dsp_feedback_delay_set_crossfeed(YseDspObject* o, float amount) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  if (d) d->crossfeed(amount);
}
YSE_C_API float yse_dsp_feedback_delay_get_crossfeed(YseDspObject* o) {
  auto* d = as<YSE::DSP::MODULES::feedbackDelay>(o);
  return d ? d->crossfeed() : 0.0f;
}

// ─── chorus / flanger (#161) ──────────────────────────────────────────

YSE_C_API void yse_dsp_chorus_set_mode(YseDspObject* o, YseChorusMode mode) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  if (c) c->mode(static_cast<YSE::DSP::MODULES::chorusMode>(mode));
}
YSE_C_API YseChorusMode yse_dsp_chorus_get_mode(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  return c ? static_cast<YseChorusMode>(c->mode()) : YSE_CHORUS_MODE_CHORUS;
}
YSE_C_API void yse_dsp_chorus_set_rate(YseDspObject* o, float hz) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  if (c) c->rate(hz);
}
YSE_C_API float yse_dsp_chorus_get_rate(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  return c ? c->rate() : 0.0f;
}
YSE_C_API void yse_dsp_chorus_set_depth(YseDspObject* o, float value) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  if (c) c->depth(value);
}
YSE_C_API float yse_dsp_chorus_get_depth(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  return c ? c->depth() : 0.0f;
}
YSE_C_API void yse_dsp_chorus_set_feedback(YseDspObject* o, float value) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  if (c) c->feedback(value);
}
YSE_C_API float yse_dsp_chorus_get_feedback(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  return c ? c->feedback() : 0.0f;
}
YSE_C_API void yse_dsp_chorus_set_spread(YseDspObject* o, float value) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  if (c) c->spread(value);
}
YSE_C_API float yse_dsp_chorus_get_spread(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::chorus>(o);
  return c ? c->spread() : 0.0f;
}

// ─── plate reverb (#162) ──────────────────────────────────────────────

YSE_C_API void yse_dsp_plate_reverb_set_decay(YseDspObject* o, float value) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  if (p) p->decay(value);
}
YSE_C_API float yse_dsp_plate_reverb_get_decay(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  return p ? p->decay() : 0.0f;
}
YSE_C_API void yse_dsp_plate_reverb_set_damping(YseDspObject* o, float hz) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  if (p) p->damping(hz);
}
YSE_C_API float yse_dsp_plate_reverb_get_damping(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  return p ? p->damping() : 0.0f;
}
YSE_C_API void yse_dsp_plate_reverb_set_predelay(YseDspObject* o, float ms) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  if (p) p->preDelay(ms);
}
YSE_C_API float yse_dsp_plate_reverb_get_predelay(YseDspObject* o) {
  auto* p = as<YSE::DSP::MODULES::plateReverb>(o);
  return p ? p->preDelay() : 0.0f;
}

// ─── parametric EQ (#163) ─────────────────────────────────────────────

YSE_C_API void yse_dsp_eq_set_frequency(YseDspObject* o, YseEqBand band, float hz) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  if (e) e->frequency(static_cast<YSE::DSP::MODULES::eqBand>(band), hz);
}
YSE_C_API float yse_dsp_eq_get_frequency(YseDspObject* o, YseEqBand band) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  return e ? e->frequency(static_cast<YSE::DSP::MODULES::eqBand>(band)) : 0.0f;
}
YSE_C_API void yse_dsp_eq_set_gain(YseDspObject* o, YseEqBand band, float db) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  if (e) e->gain(static_cast<YSE::DSP::MODULES::eqBand>(band), db);
}
YSE_C_API float yse_dsp_eq_get_gain(YseDspObject* o, YseEqBand band) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  return e ? e->gain(static_cast<YSE::DSP::MODULES::eqBand>(band)) : 0.0f;
}
YSE_C_API void yse_dsp_eq_set_q(YseDspObject* o, YseEqBand band, float value) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  if (e) e->q(static_cast<YSE::DSP::MODULES::eqBand>(band), value);
}
YSE_C_API float yse_dsp_eq_get_q(YseDspObject* o, YseEqBand band) {
  auto* e = as<YSE::DSP::MODULES::parametricEQ>(o);
  return e ? e->q(static_cast<YSE::DSP::MODULES::eqBand>(band)) : 0.0f;
}

// ─── compressor (#163) ────────────────────────────────────────────────

YSE_C_API void yse_dsp_compressor_set_detector(YseDspObject* o, YseCompressorDetector mode) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->detector(static_cast<YSE::DSP::MODULES::compressorDetector>(mode));
}
YSE_C_API YseCompressorDetector yse_dsp_compressor_get_detector(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? static_cast<YseCompressorDetector>(c->detector()) : YSE_COMPRESSOR_DETECT_PEAK;
}
YSE_C_API void yse_dsp_compressor_set_threshold(YseDspObject* o, float db) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->threshold(db);
}
YSE_C_API float yse_dsp_compressor_get_threshold(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->threshold() : 0.0f;
}
YSE_C_API void yse_dsp_compressor_set_ratio(YseDspObject* o, float value) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->ratio(value);
}
YSE_C_API float yse_dsp_compressor_get_ratio(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->ratio() : 0.0f;
}
YSE_C_API void yse_dsp_compressor_set_attack(YseDspObject* o, float ms) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->attack(ms);
}
YSE_C_API float yse_dsp_compressor_get_attack(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->attack() : 0.0f;
}
YSE_C_API void yse_dsp_compressor_set_release(YseDspObject* o, float ms) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->release(ms);
}
YSE_C_API float yse_dsp_compressor_get_release(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->release() : 0.0f;
}
YSE_C_API void yse_dsp_compressor_set_makeup(YseDspObject* o, float db) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  if (c) c->makeup(db);
}
YSE_C_API float yse_dsp_compressor_get_makeup(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->makeup() : 0.0f;
}
YSE_C_API float yse_dsp_compressor_get_gain_reduction_db(YseDspObject* o) {
  auto* c = as<YSE::DSP::MODULES::compressor>(o);
  return c ? c->gainReductionDb() : 0.0f;
}

} // extern "C"
