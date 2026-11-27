#include "yse_c/yse_dsp_envelopes.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/generators/adsr.hpp"
#include "../dsp/generators/curveSegments.hpp"
#include "../dsp/generators/eventSequencer.hpp"
#include "../dsp/generators/lineSegments.hpp"
#include "../dsp/generators/shapeEnvelope.hpp"
#include "../dsp/generators/techno.hpp"
#include "../dsp/generators/triggeredRamp.hpp"
#include "../dsp/generators/updown.hpp"
#include "../dsp/generators/zigzag.hpp"

#include <cstring>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::drive;
  using yse_c::processor::emit;
  using yse_c::processor::handle;
  using yse_c::processor::make;
  using yse_c::processor::one_signal;
  using yse_c::processor::query;
  using yse_c::processor::stage;
  using yse_c::processor::with;

  // What the bridge keeps beside an object whose engine class reports an
  // event per block: the same event over the whole of the last process call,
  // which spans several blocks. Reset when a process call starts.
  struct envelopeEvents {
    bool started = false;
    bool finished = false;
  };
  struct zigzagEvents {
    unsigned int ends = 0;
  };
  struct sequencerEvents {
    unsigned int fired = 0;
    unsigned int missed = 0;
  };

  // One wrapper type per class; the count is how many host arrays a process
  // call stages.
  using Adsr = handle<DSP::adsr, 5, envelopeEvents>;
  using CurveSegments = handle<DSP::curveSegments, 0, envelopeEvents>;
  using LineSegments = handle<DSP::lineSegments, 0, envelopeEvents>;
  using Zigzag = handle<DSP::zigzag, 2, zigzagEvents>;
  using Techno = handle<DSP::techno, 1>;
  using EventSequencer = handle<DSP::eventSequencer, 1, sequencerEvents>;
  using Updown = handle<DSP::updown, 1>;
  using ShapeEnvelope = handle<DSP::shapeEnvelope, 1>;
  using TriggeredRamp = handle<DSP::triggeredRamp, 4>;

  // The constants the header spells as macros.
  static_assert(YSE_DSP_CURVE_SEGMENTS_DEFAULT_POINTS == DSP::curveSegments::DEFAULT_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_CURVE_SEGMENTS_DEFAULT_POINTS drifted");
  static_assert(YSE_DSP_CURVE_SEGMENTS_MAX_POINTS == DSP::curveSegments::MAX_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_CURVE_SEGMENTS_MAX_POINTS drifted");
  static_assert(YSE_DSP_LINE_SEGMENTS_DEFAULT_POINTS == DSP::lineSegments::DEFAULT_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_LINE_SEGMENTS_DEFAULT_POINTS drifted");
  static_assert(YSE_DSP_LINE_SEGMENTS_MAX_POINTS == DSP::lineSegments::MAX_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_LINE_SEGMENTS_MAX_POINTS drifted");
  static_assert(YSE_DSP_ZIGZAG_DEFAULT_POINTS == DSP::zigzag::DEFAULT_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_ZIGZAG_DEFAULT_POINTS drifted");
  static_assert(YSE_DSP_ZIGZAG_MAX_POINTS == DSP::zigzag::MAX_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_ZIGZAG_MAX_POINTS drifted");
  static_assert(YSE_DSP_TECHNO_DEFAULT_STEPS == DSP::techno::DEFAULT_STEPS,
                "yse_dsp_envelopes.h: YSE_DSP_TECHNO_DEFAULT_STEPS drifted");
  static_assert(YSE_DSP_TECHNO_MAX_STEPS == DSP::techno::MAX_STEPS,
                "yse_dsp_envelopes.h: YSE_DSP_TECHNO_MAX_STEPS drifted");
  static_assert(YSE_DSP_EVENT_SEQUENCER_DEFAULT_EVENTS == DSP::eventSequencer::DEFAULT_EVENTS,
                "yse_dsp_envelopes.h: YSE_DSP_EVENT_SEQUENCER_DEFAULT_EVENTS drifted");
  static_assert(YSE_DSP_EVENT_SEQUENCER_MAX_EVENTS == DSP::eventSequencer::MAX_EVENTS,
                "yse_dsp_envelopes.h: YSE_DSP_EVENT_SEQUENCER_MAX_EVENTS drifted");
  static_assert(YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY == DSP::eventSequencer::TEXT_CAPACITY,
                "yse_dsp_envelopes.h: YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY drifted");
  static_assert(YSE_DSP_EVENT_SEQUENCER_DEFAULT_FIRED == DSP::eventSequencer::DEFAULT_FIRED,
                "yse_dsp_envelopes.h: YSE_DSP_EVENT_SEQUENCER_DEFAULT_FIRED drifted");
  static_assert(YSE_DSP_SHAPE_ENVELOPE_DEFAULT_POINTS == DSP::shapeEnvelope::DEFAULT_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_SHAPE_ENVELOPE_DEFAULT_POINTS drifted");
  static_assert(YSE_DSP_SHAPE_ENVELOPE_MAX_POINTS == DSP::shapeEnvelope::MAX_POINTS,
                "yse_dsp_envelopes.h: YSE_DSP_SHAPE_ENVELOPE_MAX_POINTS drifted");

  // A constructor's capacity: 0 picks the class default.
  unsigned int or_default(unsigned int value, unsigned int fallback) noexcept {
    return value != 0 ? value : fallback;
  }

  // The engine's reset() clears its per-block events, so the bridge clears
  // the ones it gathered over the last call too.
  template <typename H> void reset(YseDspProcessor* p) {
    if (H* h = yse_c::processor::get<H>(p)) {
      h->object.reset();
      h->state = decltype(h->state){};
    }
  }

  // adsr's four engine overloads, with the gate output emitted beside the
  // envelope and started / finished gathered over the whole call.
  template <typename F>
  unsigned int adsr_blocks(YseDspProcessor* p, float* out, float* gate_out, unsigned int length,
                           F&& run) {
    if (out == nullptr) return 0;
    if (Adsr* h = yse_c::processor::get<Adsr>(p)) h->state = envelopeEvents{};
    return drive<Adsr>(p, length, [&](Adsr& h, unsigned o, unsigned n) {
      emit(run(h, o, n), out + o, n);
      emit(h.object.gate(), gate_out != nullptr ? gate_out + o : nullptr, n);
      h.state.started = h.state.started || h.object.started();
      h.state.finished = h.state.finished || h.object.finished();
    });
  }
} // namespace

extern "C" {

// ─── adsr ───────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_adsr_create(void) {
  return make<Adsr>("yse_dsp_adsr_create");
}
YSE_C_API void yse_dsp_adsr_trigger(YseDspProcessor* p, float amplitude) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.trigger(amplitude); });
}
YSE_C_API void yse_dsp_adsr_set_attack(YseDspProcessor* p, float ms) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.attack(ms); });
}
YSE_C_API float yse_dsp_adsr_get_attack(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.attack(); });
}
YSE_C_API void yse_dsp_adsr_set_decay(YseDspProcessor* p, float ms) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.decay(ms); });
}
YSE_C_API float yse_dsp_adsr_get_decay(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.decay(); });
}
YSE_C_API void yse_dsp_adsr_set_sustain(YseDspProcessor* p, float factor) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.sustain(factor); });
}
YSE_C_API float yse_dsp_adsr_get_sustain(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.sustain(); });
}
YSE_C_API void yse_dsp_adsr_set_release(YseDspProcessor* p, float ms) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.release(ms); });
}
YSE_C_API float yse_dsp_adsr_get_release(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.release(); });
}
YSE_C_API void yse_dsp_adsr_set_retrigger(YseDspProcessor* p, float ms) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.retrigger(ms); });
}
YSE_C_API float yse_dsp_adsr_get_retrigger(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.retrigger(); });
}
YSE_C_API void yse_dsp_adsr_set_max_sustain(YseDspProcessor* p, float ms) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.maxSustain(ms); });
}
YSE_C_API float yse_dsp_adsr_get_max_sustain(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.maxSustain(); });
}
YSE_C_API void yse_dsp_adsr_set_legato(YseDspProcessor* p, int on) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.legato(on != 0); });
}
YSE_C_API int yse_dsp_adsr_get_legato(YseDspProcessor* p) {
  return query<Adsr>(p, 0, [](DSP::adsr& f) { return f.legato() ? 1 : 0; });
}
YSE_C_API void yse_dsp_adsr_set_play_to_end(YseDspProcessor* p, int on) {
  with<Adsr>(p, [&](DSP::adsr& f) { f.playToEnd(on != 0); });
}
YSE_C_API int yse_dsp_adsr_get_play_to_end(YseDspProcessor* p) {
  return query<Adsr>(p, 0, [](DSP::adsr& f) { return f.playToEnd() ? 1 : 0; });
}
YSE_C_API void yse_dsp_adsr_reset(YseDspProcessor* p) {
  reset<Adsr>(p);
}
YSE_C_API int yse_dsp_adsr_get_started(YseDspProcessor* p) {
  Adsr* h = yse_c::processor::get<Adsr>(p);
  return h != nullptr && h->state.started ? 1 : 0;
}
YSE_C_API int yse_dsp_adsr_get_finished(YseDspProcessor* p) {
  Adsr* h = yse_c::processor::get<Adsr>(p);
  return h != nullptr && h->state.finished ? 1 : 0;
}
YSE_C_API int yse_dsp_adsr_is_active(YseDspProcessor* p) {
  return query<Adsr>(p, 0, [](DSP::adsr& f) { return f.active() ? 1 : 0; });
}
YSE_C_API YseDspAdsrStage yse_dsp_adsr_get_stage(YseDspProcessor* p) {
  return query<Adsr>(p, YSE_DSP_ADSR_IDLE,
                     [](DSP::adsr& f) { return static_cast<YseDspAdsrStage>(f.stage()); });
}
YSE_C_API float yse_dsp_adsr_get_level(YseDspProcessor* p) {
  return query<Adsr>(p, 0.f, [](DSP::adsr& f) { return f.level(); });
}
YSE_C_API unsigned int yse_dsp_adsr_process(YseDspProcessor* p, float* out, float* gate_out,
                                            unsigned int length) {
  return adsr_blocks(p, out, gate_out, length,
                     [](Adsr& h, unsigned, unsigned n) -> DSP::buffer& { return h.object(n); });
}
YSE_C_API unsigned int yse_dsp_adsr_process_gate(YseDspProcessor* p, const float* gate, float* out,
                                                 float* gate_out, unsigned int length) {
  if (gate == nullptr) return 0;
  return adsr_blocks(p, out, gate_out, length,
                     [&](Adsr& h, unsigned o, unsigned n) -> DSP::buffer& {
                       return h.object(stage(h.scratch[0], gate + o, n));
                     });
}
YSE_C_API unsigned int yse_dsp_adsr_process_signal(YseDspProcessor* p, const float* attack,
                                                   const float* decay, const float* sustain,
                                                   const float* release, float* out,
                                                   float* gate_out, unsigned int length) {
  if (attack == nullptr || decay == nullptr || sustain == nullptr || release == nullptr) return 0;
  return adsr_blocks(
      p, out, gate_out, length, [&](Adsr& h, unsigned o, unsigned n) -> DSP::buffer& {
        return h.object(stage(h.scratch[1], attack + o, n), stage(h.scratch[2], decay + o, n),
                        stage(h.scratch[3], sustain + o, n), stage(h.scratch[4], release + o, n));
      });
}
YSE_C_API unsigned int yse_dsp_adsr_process_gate_signal(YseDspProcessor* p, const float* gate,
                                                        const float* attack, const float* decay,
                                                        const float* sustain, const float* release,
                                                        float* out, float* gate_out,
                                                        unsigned int length) {
  if (gate == nullptr || attack == nullptr || decay == nullptr || sustain == nullptr ||
      release == nullptr)
    return 0;
  return adsr_blocks(
      p, out, gate_out, length, [&](Adsr& h, unsigned o, unsigned n) -> DSP::buffer& {
        return h.object(stage(h.scratch[0], gate + o, n), stage(h.scratch[1], attack + o, n),
                        stage(h.scratch[2], decay + o, n), stage(h.scratch[3], sustain + o, n),
                        stage(h.scratch[4], release + o, n));
      });
}

// ─── curveSegments ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_curve_segments_create(unsigned int capacity) {
  return make<CurveSegments>("yse_dsp_curve_segments_create",
                             or_default(capacity, DSP::curveSegments::DEFAULT_POINTS));
}
YSE_C_API unsigned int yse_dsp_curve_segments_get_capacity(YseDspProcessor* p) {
  return query<CurveSegments>(p, 0u, [](DSP::curveSegments& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_curve_segments_set(YseDspProcessor* p, const float* targets,
                                                  const float* times, const float* curves,
                                                  unsigned int count) {
  if (targets == nullptr || times == nullptr) return 0;
  return query<CurveSegments>(
      p, 0u, [&](DSP::curveSegments& f) { return f.segments(targets, times, curves, count); });
}
YSE_C_API void yse_dsp_curve_segments_jump(YseDspProcessor* p, float value) {
  with<CurveSegments>(p, [&](DSP::curveSegments& f) { f.jump(value); });
}
YSE_C_API void yse_dsp_curve_segments_stop(YseDspProcessor* p) {
  with<CurveSegments>(p, [](DSP::curveSegments& f) { f.stop(); });
}
YSE_C_API void yse_dsp_curve_segments_pause(YseDspProcessor* p) {
  with<CurveSegments>(p, [](DSP::curveSegments& f) { f.pause(); });
}
YSE_C_API void yse_dsp_curve_segments_resume(YseDspProcessor* p) {
  with<CurveSegments>(p, [](DSP::curveSegments& f) { f.resume(); });
}
YSE_C_API void yse_dsp_curve_segments_set_shape(YseDspProcessor* p, YseDspCurveShape shape) {
  if (shape != YSE_DSP_CURVE_POWER && shape != YSE_DSP_CURVE_EXPONENTIAL) return;
  with<CurveSegments>(
      p, [&](DSP::curveSegments& f) { f.shape(static_cast<DSP::curveSegments::SHAPE>(shape)); });
}
YSE_C_API YseDspCurveShape yse_dsp_curve_segments_get_shape(YseDspProcessor* p) {
  return query<CurveSegments>(p, YSE_DSP_CURVE_POWER, [](DSP::curveSegments& f) {
    return static_cast<YseDspCurveShape>(f.shape());
  });
}
YSE_C_API float yse_dsp_curve_segments_get_value(YseDspProcessor* p) {
  return query<CurveSegments>(p, 0.f, [](DSP::curveSegments& f) { return f.value(); });
}
YSE_C_API int yse_dsp_curve_segments_is_active(YseDspProcessor* p) {
  return query<CurveSegments>(p, 0, [](DSP::curveSegments& f) { return f.active() ? 1 : 0; });
}
YSE_C_API int yse_dsp_curve_segments_is_paused(YseDspProcessor* p) {
  return query<CurveSegments>(p, 0, [](DSP::curveSegments& f) { return f.paused() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_curve_segments_get_remaining(YseDspProcessor* p) {
  return query<CurveSegments>(p, 0u, [](DSP::curveSegments& f) { return f.remaining(); });
}
YSE_C_API int yse_dsp_curve_segments_get_finished(YseDspProcessor* p) {
  CurveSegments* h = yse_c::processor::get<CurveSegments>(p);
  return h != nullptr && h->state.finished ? 1 : 0;
}
YSE_C_API void yse_dsp_curve_segments_reset(YseDspProcessor* p) {
  reset<CurveSegments>(p);
}
YSE_C_API unsigned int yse_dsp_curve_segments_process(YseDspProcessor* p, float* out,
                                                      unsigned int length) {
  if (out == nullptr) return 0;
  if (CurveSegments* h = yse_c::processor::get<CurveSegments>(p)) h->state = envelopeEvents{};
  return drive<CurveSegments>(p, length, [&](CurveSegments& h, unsigned o, unsigned n) {
    emit(h.object(n), out + o, n);
    h.state.finished = h.state.finished || h.object.finished();
  });
}

// ─── lineSegments ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_line_segments_create(unsigned int capacity) {
  return make<LineSegments>("yse_dsp_line_segments_create",
                            or_default(capacity, DSP::lineSegments::DEFAULT_POINTS));
}
YSE_C_API unsigned int yse_dsp_line_segments_get_capacity(YseDspProcessor* p) {
  return query<LineSegments>(p, 0u, [](DSP::lineSegments& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_line_segments_set(YseDspProcessor* p, const float* targets,
                                                 const float* times, unsigned int count) {
  if (targets == nullptr || times == nullptr) return 0;
  return query<LineSegments>(
      p, 0u, [&](DSP::lineSegments& f) { return f.segments(targets, times, count); });
}
YSE_C_API void yse_dsp_line_segments_set_target(YseDspProcessor* p, float target, float ms) {
  with<LineSegments>(p, [&](DSP::lineSegments& f) { f.set(target, ms); });
}
YSE_C_API void yse_dsp_line_segments_jump(YseDspProcessor* p, float value) {
  with<LineSegments>(p, [&](DSP::lineSegments& f) { f.jump(value); });
}
YSE_C_API void yse_dsp_line_segments_stop(YseDspProcessor* p) {
  with<LineSegments>(p, [](DSP::lineSegments& f) { f.stop(); });
}
YSE_C_API void yse_dsp_line_segments_pause(YseDspProcessor* p) {
  with<LineSegments>(p, [](DSP::lineSegments& f) { f.pause(); });
}
YSE_C_API void yse_dsp_line_segments_resume(YseDspProcessor* p) {
  with<LineSegments>(p, [](DSP::lineSegments& f) { f.resume(); });
}
YSE_C_API float yse_dsp_line_segments_get_value(YseDspProcessor* p) {
  return query<LineSegments>(p, 0.f, [](DSP::lineSegments& f) { return f.value(); });
}
YSE_C_API int yse_dsp_line_segments_is_active(YseDspProcessor* p) {
  return query<LineSegments>(p, 0, [](DSP::lineSegments& f) { return f.active() ? 1 : 0; });
}
YSE_C_API int yse_dsp_line_segments_is_paused(YseDspProcessor* p) {
  return query<LineSegments>(p, 0, [](DSP::lineSegments& f) { return f.paused() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_line_segments_get_remaining(YseDspProcessor* p) {
  return query<LineSegments>(p, 0u, [](DSP::lineSegments& f) { return f.remaining(); });
}
YSE_C_API int yse_dsp_line_segments_get_finished(YseDspProcessor* p) {
  LineSegments* h = yse_c::processor::get<LineSegments>(p);
  return h != nullptr && h->state.finished ? 1 : 0;
}
YSE_C_API void yse_dsp_line_segments_reset(YseDspProcessor* p) {
  reset<LineSegments>(p);
}
YSE_C_API unsigned int yse_dsp_line_segments_process(YseDspProcessor* p, float* out,
                                                     float* activity, unsigned int length) {
  if (out == nullptr) return 0;
  if (LineSegments* h = yse_c::processor::get<LineSegments>(p)) h->state = envelopeEvents{};
  return drive<LineSegments>(p, length, [&](LineSegments& h, unsigned o, unsigned n) {
    emit(h.object(n), out + o, n);
    emit(h.object.activity(), activity != nullptr ? activity + o : nullptr, n);
    h.state.finished = h.state.finished || h.object.finished();
  });
}

// ─── zigzag ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_zigzag_create(unsigned int capacity) {
  return make<Zigzag>("yse_dsp_zigzag_create", or_default(capacity, DSP::zigzag::DEFAULT_POINTS));
}
YSE_C_API unsigned int yse_dsp_zigzag_get_capacity(YseDspProcessor* p) {
  return query<Zigzag>(p, 0u, [](DSP::zigzag& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_zigzag_get_size(YseDspProcessor* p) {
  return query<Zigzag>(p, 0u, [](DSP::zigzag& f) { return f.size(); });
}
YSE_C_API float yse_dsp_zigzag_get_point_value(YseDspProcessor* p, unsigned int index) {
  return query<Zigzag>(p, 0.f, [&](DSP::zigzag& f) { return f.pointValue(index); });
}
YSE_C_API float yse_dsp_zigzag_get_point_delta(YseDspProcessor* p, unsigned int index) {
  return query<Zigzag>(p, 0.f, [&](DSP::zigzag& f) { return f.pointDelta(index); });
}
YSE_C_API unsigned int yse_dsp_zigzag_set_line(YseDspProcessor* p, const float* values,
                                               const float* deltas, unsigned int count) {
  return query<Zigzag>(p, 0u, [&](DSP::zigzag& f) { return f.setLine(values, deltas, count); });
}
YSE_C_API unsigned int yse_dsp_zigzag_line(YseDspProcessor* p, const float* values,
                                           const float* deltas, unsigned int count) {
  return query<Zigzag>(p, 0u, [&](DSP::zigzag& f) { return f.line(values, deltas, count); });
}
YSE_C_API void yse_dsp_zigzag_clear(YseDspProcessor* p) {
  with<Zigzag>(p, [](DSP::zigzag& f) { f.clear(); });
}
YSE_C_API int yse_dsp_zigzag_append(YseDspProcessor* p, float value, float ms, int trigger) {
  return query<Zigzag>(p, 0,
                       [&](DSP::zigzag& f) { return f.append(value, ms, trigger != 0) ? 1 : 0; });
}
YSE_C_API int yse_dsp_zigzag_insert(YseDspProcessor* p, unsigned int index, float value, float ms) {
  return query<Zigzag>(p, 0, [&](DSP::zigzag& f) { return f.insert(index, value, ms) ? 1 : 0; });
}
YSE_C_API int yse_dsp_zigzag_remove(YseDspProcessor* p, unsigned int index) {
  return query<Zigzag>(p, 0, [&](DSP::zigzag& f) { return f.remove(index) ? 1 : 0; });
}
YSE_C_API int yse_dsp_zigzag_set_point(YseDspProcessor* p, unsigned int index, float value,
                                       float ms) {
  return query<Zigzag>(p, 0, [&](DSP::zigzag& f) { return f.set(index, value, ms) ? 1 : 0; });
}
YSE_C_API void yse_dsp_zigzag_play(YseDspProcessor* p) {
  with<Zigzag>(p, [](DSP::zigzag& f) { f.play(); });
}
YSE_C_API void yse_dsp_zigzag_bang(YseDspProcessor* p) {
  with<Zigzag>(p, [](DSP::zigzag& f) { f.bang(); });
}
YSE_C_API void yse_dsp_zigzag_jump(YseDspProcessor* p, int index, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.jump(index, ms); });
}
YSE_C_API void yse_dsp_zigzag_jump_start(YseDspProcessor* p, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.jumpStart(ms); });
}
YSE_C_API void yse_dsp_zigzag_jump_end(YseDspProcessor* p, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.jumpEnd(ms); });
}
YSE_C_API void yse_dsp_zigzag_skip(YseDspProcessor* p, int points, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.skip(points, ms); });
}
YSE_C_API void yse_dsp_zigzag_next(YseDspProcessor* p, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.next(ms); });
}
YSE_C_API void yse_dsp_zigzag_prev(YseDspProcessor* p, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.prev(ms); });
}
YSE_C_API void yse_dsp_zigzag_stop(YseDspProcessor* p) {
  with<Zigzag>(p, [](DSP::zigzag& f) { f.stop(); });
}
YSE_C_API void yse_dsp_zigzag_hold(YseDspProcessor* p, float value) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.hold(value); });
}
YSE_C_API void yse_dsp_zigzag_set_bang_delta(YseDspProcessor* p, float ms) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.bangDelta(ms); });
}
YSE_C_API float yse_dsp_zigzag_get_bang_delta(YseDspProcessor* p) {
  return query<Zigzag>(p, 0.f, [](DSP::zigzag& f) { return f.bangDelta(); });
}
YSE_C_API void yse_dsp_zigzag_set_start(YseDspProcessor* p, unsigned int index) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.start(index); });
}
YSE_C_API void yse_dsp_zigzag_set_end(YseDspProcessor* p, int index) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.end(index); });
}
YSE_C_API void yse_dsp_zigzag_bound(YseDspProcessor* p, unsigned int first, int last) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.bound(first, last); });
}
YSE_C_API void yse_dsp_zigzag_set_speed(YseDspProcessor* p, float rate) {
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.speed(rate); });
}
YSE_C_API float yse_dsp_zigzag_get_speed(YseDspProcessor* p) {
  return query<Zigzag>(p, 0.f, [](DSP::zigzag& f) { return f.speed(); });
}
YSE_C_API void yse_dsp_zigzag_set_loop_mode(YseDspProcessor* p, YseDspZigzagLoop mode) {
  if (mode < YSE_DSP_ZIGZAG_LOOP_OFF || mode > YSE_DSP_ZIGZAG_LOOP_PENDULUM) return;
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.loopMode(static_cast<DSP::zigzag::LOOP>(mode)); });
}
YSE_C_API YseDspZigzagLoop yse_dsp_zigzag_get_loop_mode(YseDspProcessor* p) {
  return query<Zigzag>(p, YSE_DSP_ZIGZAG_LOOP_OFF,
                       [](DSP::zigzag& f) { return static_cast<YseDspZigzagLoop>(f.loopMode()); });
}
YSE_C_API void yse_dsp_zigzag_set_mode(YseDspProcessor* p, YseDspZigzagMode mode) {
  if (mode < YSE_DSP_ZIGZAG_START || mode > YSE_DSP_ZIGZAG_STEP) return;
  with<Zigzag>(p, [&](DSP::zigzag& f) { f.mode(static_cast<DSP::zigzag::MODE>(mode)); });
}
YSE_C_API YseDspZigzagMode yse_dsp_zigzag_get_mode(YseDspProcessor* p) {
  return query<Zigzag>(p, YSE_DSP_ZIGZAG_START,
                       [](DSP::zigzag& f) { return static_cast<YseDspZigzagMode>(f.mode()); });
}
YSE_C_API float yse_dsp_zigzag_get_value(YseDspProcessor* p) {
  return query<Zigzag>(p, 0.f, [](DSP::zigzag& f) { return f.value(); });
}
YSE_C_API unsigned int yse_dsp_zigzag_get_index(YseDspProcessor* p) {
  return query<Zigzag>(p, 0u, [](DSP::zigzag& f) { return f.index(); });
}
YSE_C_API int yse_dsp_zigzag_is_active(YseDspProcessor* p) {
  return query<Zigzag>(p, 0, [](DSP::zigzag& f) { return f.active() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_zigzag_get_ends(YseDspProcessor* p) {
  Zigzag* h = yse_c::processor::get<Zigzag>(p);
  return h != nullptr ? h->state.ends : 0u;
}
YSE_C_API int yse_dsp_zigzag_get_finished(YseDspProcessor* p) {
  return yse_dsp_zigzag_get_ends(p) > 0 ? 1 : 0;
}
YSE_C_API void yse_dsp_zigzag_reset(YseDspProcessor* p) {
  reset<Zigzag>(p);
}
YSE_C_API unsigned int yse_dsp_zigzag_process_signal(YseDspProcessor* p, const float* input,
                                                     const float* speed, float* out,
                                                     float* index_out, unsigned int length) {
  if (out == nullptr) return 0;
  if (Zigzag* h = yse_c::processor::get<Zigzag>(p)) h->state = zigzagEvents{};
  return drive<Zigzag>(p, length, [&](Zigzag& h, unsigned o, unsigned n) {
    DSP::buffer* in = input != nullptr ? &stage(h.scratch[0], input + o, n) : nullptr;
    DSP::buffer* rate = speed != nullptr ? &stage(h.scratch[1], speed + o, n) : nullptr;
    emit(h.object(in, rate, n), out + o, n);
    emit(h.object.indices(), index_out != nullptr ? index_out + o : nullptr, n);
    h.state.ends += h.object.ends();
  });
}
YSE_C_API unsigned int yse_dsp_zigzag_process(YseDspProcessor* p, float* out, float* index_out,
                                              unsigned int length) {
  return yse_dsp_zigzag_process_signal(p, nullptr, nullptr, out, index_out, length);
}

// ─── techno ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_techno_create(unsigned int capacity) {
  return make<Techno>("yse_dsp_techno_create", or_default(capacity, DSP::techno::DEFAULT_STEPS));
}
YSE_C_API unsigned int yse_dsp_techno_get_capacity(YseDspProcessor* p) {
  return query<Techno>(p, 0u, [](DSP::techno& f) { return f.capacity(); });
}
YSE_C_API void yse_dsp_techno_set_length(YseDspProcessor* p, unsigned int steps) {
  with<Techno>(p, [&](DSP::techno& f) { f.length(steps); });
}
YSE_C_API unsigned int yse_dsp_techno_get_length(YseDspProcessor* p) {
  return query<Techno>(p, 0u, [](DSP::techno& f) { return f.length(); });
}
YSE_C_API void yse_dsp_techno_set_pitch(YseDspProcessor* p, unsigned int step, float hz) {
  with<Techno>(p, [&](DSP::techno& f) { f.pitch(step, hz); });
}
YSE_C_API float yse_dsp_techno_get_pitch(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.pitch(step); });
}
YSE_C_API void yse_dsp_techno_set_amplitude(YseDspProcessor* p, unsigned int step, float value) {
  with<Techno>(p, [&](DSP::techno& f) { f.amplitude(step, value); });
}
YSE_C_API float yse_dsp_techno_get_amplitude(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.amplitude(step); });
}
YSE_C_API void yse_dsp_techno_set_curve(YseDspProcessor* p, unsigned int step, float exponent) {
  with<Techno>(p, [&](DSP::techno& f) { f.curve(step, exponent); });
}
YSE_C_API float yse_dsp_techno_get_curve(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.curve(step); });
}
YSE_C_API void yse_dsp_techno_set_attack(YseDspProcessor* p, unsigned int step, float exponent) {
  with<Techno>(p, [&](DSP::techno& f) { f.attack(step, exponent); });
}
YSE_C_API float yse_dsp_techno_get_attack(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.attack(step); });
}
YSE_C_API void yse_dsp_techno_set_decay(YseDspProcessor* p, unsigned int step, float exponent) {
  with<Techno>(p, [&](DSP::techno& f) { f.decay(step, exponent); });
}
YSE_C_API float yse_dsp_techno_get_decay(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.decay(step); });
}
YSE_C_API void yse_dsp_techno_set_position(YseDspProcessor* p, unsigned int step, float start) {
  with<Techno>(p, [&](DSP::techno& f) { f.position(step, start); });
}
YSE_C_API float yse_dsp_techno_get_position(YseDspProcessor* p, unsigned int step) {
  return query<Techno>(p, 0.f, [&](DSP::techno& f) { return f.position(step); });
}
YSE_C_API int yse_dsp_techno_repeat_positions(YseDspProcessor* p, const float* starts,
                                              unsigned int count) {
  if (starts == nullptr && count > 0) return 0;
  return query<Techno>(p, 0,
                       [&](DSP::techno& f) { return f.repeatPositions(starts, count) ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_techno_process(YseDspProcessor* p, const float* phase, float* out,
                                              float* envelope, float* position,
                                              unsigned int length) {
  if (phase == nullptr || out == nullptr) return 0;
  return drive<Techno>(p, length, [&](Techno& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], phase + o, n)), out + o, n);
    emit(h.object.envelope(), envelope != nullptr ? envelope + o : nullptr, n);
    emit(h.object.stepPosition(), position != nullptr ? position + o : nullptr, n);
  });
}

// ─── eventSequencer ─────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_event_sequencer_create(unsigned int capacity,
                                                          unsigned int fired_capacity) {
  return make<EventSequencer>("yse_dsp_event_sequencer_create",
                              or_default(capacity, DSP::eventSequencer::DEFAULT_EVENTS),
                              or_default(fired_capacity, DSP::eventSequencer::DEFAULT_FIRED));
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_capacity(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0u, [](DSP::eventSequencer& f) { return f.capacity(); });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_fired_capacity(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0u, [](DSP::eventSequencer& f) { return f.firedCapacity(); });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_size(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0u, [](DSP::eventSequencer& f) { return f.size(); });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_count(YseDspProcessor* p, int sequence) {
  return query<EventSequencer>(p, 0u, [&](DSP::eventSequencer& f) { return f.count(sequence); });
}
YSE_C_API int yse_dsp_event_sequencer_add(YseDspProcessor* p, int sequence, float phase,
                                          const char* text, unsigned int length) {
  return query<EventSequencer>(
      p, 0, [&](DSP::eventSequencer& f) { return f.add(sequence, phase, text, length) ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_remove(YseDspProcessor* p, int sequence, float from,
                                                      float to, const char* text,
                                                      unsigned int length) {
  return query<EventSequencer>(p, 0u, [&](DSP::eventSequencer& f) {
    return f.remove(sequence, from, to, text, text != nullptr ? length : 0);
  });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_clear(YseDspProcessor* p, int sequence) {
  return query<EventSequencer>(p, 0u, [&](DSP::eventSequencer& f) { return f.clear(sequence); });
}
YSE_C_API void yse_dsp_event_sequencer_clear_all(YseDspProcessor* p) {
  with<EventSequencer>(p, [](DSP::eventSequencer& f) { f.clearAll(); });
}
YSE_C_API void yse_dsp_event_sequencer_select(YseDspProcessor* p, int sequence) {
  with<EventSequencer>(p, [&](DSP::eventSequencer& f) { f.select(sequence); });
}
YSE_C_API int yse_dsp_event_sequencer_get_selected(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0, [](DSP::eventSequencer& f) { return f.selected(); });
}
YSE_C_API void yse_dsp_event_sequencer_set_play(YseDspProcessor* p, int on) {
  with<EventSequencer>(p, [&](DSP::eventSequencer& f) { f.play(on != 0); });
}
YSE_C_API int yse_dsp_event_sequencer_is_playing(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0, [](DSP::eventSequencer& f) { return f.playing() ? 1 : 0; });
}
YSE_C_API void yse_dsp_event_sequencer_set_record(YseDspProcessor* p, int on) {
  with<EventSequencer>(p, [&](DSP::eventSequencer& f) { f.record(on != 0); });
}
YSE_C_API int yse_dsp_event_sequencer_is_recording(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0, [](DSP::eventSequencer& f) { return f.recording() ? 1 : 0; });
}
YSE_C_API void yse_dsp_event_sequencer_set_overdub(YseDspProcessor* p, int on) {
  with<EventSequencer>(p, [&](DSP::eventSequencer& f) { f.overdub(on != 0); });
}
YSE_C_API int yse_dsp_event_sequencer_is_overdubbing(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0,
                               [](DSP::eventSequencer& f) { return f.overdubbing() ? 1 : 0; });
}
YSE_C_API int yse_dsp_event_sequencer_record_event(YseDspProcessor* p, const char* text,
                                                   unsigned int length) {
  return query<EventSequencer>(
      p, 0, [&](DSP::eventSequencer& f) { return f.recordEvent(text, length) ? 1 : 0; });
}
YSE_C_API float yse_dsp_event_sequencer_get_phase(YseDspProcessor* p) {
  return query<EventSequencer>(p, 0.f, [](DSP::eventSequencer& f) { return f.phase(); });
}
YSE_C_API int yse_dsp_event_sequencer_get_event_sequence(YseDspProcessor* p, unsigned int index) {
  return query<EventSequencer>(p, 0,
                               [&](DSP::eventSequencer& f) { return f.eventSequence(index); });
}
YSE_C_API float yse_dsp_event_sequencer_get_event_phase(YseDspProcessor* p, unsigned int index) {
  return query<EventSequencer>(p, 0.f, [&](DSP::eventSequencer& f) { return f.eventPhase(index); });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_event_length(YseDspProcessor* p,
                                                                unsigned int index) {
  return query<EventSequencer>(p, 0u, [&](DSP::eventSequencer& f) { return f.eventLength(index); });
}
YSE_C_API size_t yse_dsp_event_sequencer_get_event_text(YseDspProcessor* p, unsigned int index,
                                                        char* buf, size_t cap) {
  // snprintf style without building a string: copy what fits, terminate,
  // return the full length.
  DSP::eventSequencer* f = yse_c::processor::object<EventSequencer>(p);
  const char* text = f != nullptr ? f->eventText(index) : "";
  const size_t length = f != nullptr ? f->eventLength(index) : 0;
  if (buf != nullptr && cap > 0) {
    const size_t n = length < cap - 1 ? length : cap - 1;
    std::memcpy(buf, text, n);
    buf[n] = '\0';
  }
  return length;
}
YSE_C_API unsigned int yse_dsp_event_sequencer_process(YseDspProcessor* p, const float* phase,
                                                       YseDspEventFiring* fired,
                                                       unsigned int capacity, unsigned int length) {
  if (phase == nullptr) return 0;
  if (fired == nullptr) capacity = 0;
  if (EventSequencer* h = yse_c::processor::get<EventSequencer>(p)) h->state = sequencerEvents{};
  return drive<EventSequencer>(p, length, [&](EventSequencer& h, unsigned o, unsigned n) {
    DSP::eventSequencer& f = h.object;
    f(stage(h.scratch[0], phase + o, n), n);
    for (unsigned i = 0; i < f.fired(); ++i) {
      const unsigned at = h.state.fired++;
      if (at >= capacity) continue;
      const DSP::eventSequencer::firing& e = f.firedEvent(i);
      YseDspEventFiring& to = fired[at];
      to.offset = o + e.offset;
      to.phase = e.phase;
      to.length = e.length;
      // The engine keeps every text shorter than TEXT_CAPACITY, terminated.
      std::memcpy(to.text, e.text, e.length);
      to.text[e.length] = '\0';
    }
    h.state.missed += f.missed();
  });
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_fired(YseDspProcessor* p) {
  EventSequencer* h = yse_c::processor::get<EventSequencer>(p);
  return h != nullptr ? h->state.fired : 0u;
}
YSE_C_API unsigned int yse_dsp_event_sequencer_get_missed(YseDspProcessor* p) {
  EventSequencer* h = yse_c::processor::get<EventSequencer>(p);
  return h != nullptr ? h->state.missed : 0u;
}
YSE_C_API void yse_dsp_event_sequencer_reset(YseDspProcessor* p) {
  reset<EventSequencer>(p);
}

// ─── updown ─────────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_updown_create(void) {
  return make<Updown>("yse_dsp_updown_create");
}
YSE_C_API void yse_dsp_updown_set_up(YseDspProcessor* p, float ms) {
  with<Updown>(p, [&](DSP::updown& f) { f.up(ms); });
}
YSE_C_API float yse_dsp_updown_get_up(YseDspProcessor* p) {
  return query<Updown>(p, 0.f, [](DSP::updown& f) { return f.up(); });
}
YSE_C_API void yse_dsp_updown_set_down(YseDspProcessor* p, float ms) {
  with<Updown>(p, [&](DSP::updown& f) { f.down(ms); });
}
YSE_C_API float yse_dsp_updown_get_down(YseDspProcessor* p) {
  return query<Updown>(p, 0.f, [](DSP::updown& f) { return f.down(); });
}
YSE_C_API void yse_dsp_updown_set_level(YseDspProcessor* p, float value) {
  with<Updown>(p, [&](DSP::updown& f) { f.level(value); });
}
YSE_C_API float yse_dsp_updown_get_level(YseDspProcessor* p) {
  return query<Updown>(p, 0.f, [](DSP::updown& f) { return f.level(); });
}
YSE_C_API void yse_dsp_updown_set_reset_threshold(YseDspProcessor* p, float value) {
  with<Updown>(p, [&](DSP::updown& f) { f.resetThreshold(value); });
}
YSE_C_API float yse_dsp_updown_get_reset_threshold(YseDspProcessor* p) {
  return query<Updown>(p, 0.f, [](DSP::updown& f) { return f.resetThreshold(); });
}
YSE_C_API void yse_dsp_updown_restart(YseDspProcessor* p) {
  with<Updown>(p, [](DSP::updown& f) { f.restart(); });
}
YSE_C_API float yse_dsp_updown_get_envelope(YseDspProcessor* p) {
  return query<Updown>(p, 0.f, [](DSP::updown& f) { return f.envelope(); });
}
YSE_C_API unsigned int yse_dsp_updown_process(YseDspProcessor* p, const float* input, float* out,
                                              unsigned int length) {
  return one_signal<Updown>(p, input, out, length, [](Updown& h, DSP::buffer& in) -> DSP::buffer& {
    return h.object(in);
  });
}

// ─── shapeEnvelope ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_shape_envelope_create(unsigned int capacity) {
  return make<ShapeEnvelope>("yse_dsp_shape_envelope_create",
                             or_default(capacity, DSP::shapeEnvelope::DEFAULT_POINTS));
}
YSE_C_API unsigned int yse_dsp_shape_envelope_get_capacity(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0u, [](DSP::shapeEnvelope& f) { return f.capacity(); });
}
YSE_C_API int yse_dsp_shape_envelope_set_breakpoints(YseDspProcessor* p, float initial,
                                                     const float* values, const float* times,
                                                     const float* curves, unsigned int count) {
  if ((values == nullptr || times == nullptr) && count > 0) return 0;
  return query<ShapeEnvelope>(p, 0, [&](DSP::shapeEnvelope& f) {
    return f.breakpoints(initial, values, times, curves, count) ? 1 : 0;
  });
}
YSE_C_API unsigned int yse_dsp_shape_envelope_get_count(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0u, [](DSP::shapeEnvelope& f) { return f.count(); });
}
YSE_C_API float yse_dsp_shape_envelope_get_initial(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0.f, [](DSP::shapeEnvelope& f) { return f.initial(); });
}
YSE_C_API float yse_dsp_shape_envelope_get_value(YseDspProcessor* p, unsigned int point) {
  return query<ShapeEnvelope>(p, 0.f, [&](DSP::shapeEnvelope& f) { return f.value(point); });
}
YSE_C_API float yse_dsp_shape_envelope_get_time(YseDspProcessor* p, unsigned int point) {
  return query<ShapeEnvelope>(p, 0.f, [&](DSP::shapeEnvelope& f) { return f.time(point); });
}
YSE_C_API float yse_dsp_shape_envelope_get_curve(YseDspProcessor* p, unsigned int point) {
  return query<ShapeEnvelope>(p, 0.f, [&](DSP::shapeEnvelope& f) { return f.curve(point); });
}
YSE_C_API int yse_dsp_shape_envelope_is_pending(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0, [](DSP::shapeEnvelope& f) { return f.pending() ? 1 : 0; });
}
YSE_C_API void yse_dsp_shape_envelope_set_constant(YseDspProcessor* p, unsigned int segments) {
  with<ShapeEnvelope>(p, [&](DSP::shapeEnvelope& f) { f.constant(segments); });
}
YSE_C_API unsigned int yse_dsp_shape_envelope_get_constant(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0u, [](DSP::shapeEnvelope& f) { return f.constant(); });
}
YSE_C_API void yse_dsp_shape_envelope_set_curved(YseDspProcessor* p, int on) {
  with<ShapeEnvelope>(p, [&](DSP::shapeEnvelope& f) { f.curved(on != 0); });
}
YSE_C_API int yse_dsp_shape_envelope_get_curved(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0, [](DSP::shapeEnvelope& f) { return f.curved() ? 1 : 0; });
}
YSE_C_API void yse_dsp_shape_envelope_set_sync_update(YseDspProcessor* p, int on) {
  with<ShapeEnvelope>(p, [&](DSP::shapeEnvelope& f) { f.syncUpdate(on != 0); });
}
YSE_C_API int yse_dsp_shape_envelope_get_sync_update(YseDspProcessor* p) {
  return query<ShapeEnvelope>(p, 0, [](DSP::shapeEnvelope& f) { return f.syncUpdate() ? 1 : 0; });
}
YSE_C_API unsigned int yse_dsp_shape_envelope_process(YseDspProcessor* p, const float* input,
                                                      float* out, unsigned int length) {
  return one_signal<ShapeEnvelope>(
      p, input, out, length,
      [](ShapeEnvelope& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}

// ─── triggeredRamp ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_triggered_ramp_create(void) {
  return make<TriggeredRamp>("yse_dsp_triggered_ramp_create");
}
YSE_C_API void yse_dsp_triggered_ramp_set_duration(YseDspProcessor* p, float ms) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.duration(ms); });
}
YSE_C_API float yse_dsp_triggered_ramp_get_duration(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0.f, [](DSP::triggeredRamp& f) { return f.duration(); });
}
YSE_C_API void yse_dsp_triggered_ramp_set_start(YseDspProcessor* p, float value) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.start(value); });
}
YSE_C_API float yse_dsp_triggered_ramp_get_start(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0.f, [](DSP::triggeredRamp& f) { return f.start(); });
}
YSE_C_API void yse_dsp_triggered_ramp_set_end(YseDspProcessor* p, float value) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.end(value); });
}
YSE_C_API float yse_dsp_triggered_ramp_get_end(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0.f, [](DSP::triggeredRamp& f) { return f.end(); });
}
YSE_C_API void yse_dsp_triggered_ramp_set_curve(YseDspProcessor* p, float value) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.curve(value); });
}
YSE_C_API float yse_dsp_triggered_ramp_get_curve(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0.f, [](DSP::triggeredRamp& f) { return f.curve(); });
}
YSE_C_API void yse_dsp_triggered_ramp_set_mode(YseDspProcessor* p, YseDspRampDurationMode mode) {
  if (mode != YSE_DSP_RAMP_DURATION_AT_START && mode != YSE_DSP_RAMP_DURATION_CONTINUOUS) return;
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) {
    f.mode(static_cast<DSP::triggeredRamp::durationMode>(mode));
  });
}
YSE_C_API YseDspRampDurationMode yse_dsp_triggered_ramp_get_mode(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, YSE_DSP_RAMP_DURATION_AT_START, [](DSP::triggeredRamp& f) {
    return static_cast<YseDspRampDurationMode>(f.mode());
  });
}
YSE_C_API void yse_dsp_triggered_ramp_set_reset_to_zero(YseDspProcessor* p, int on) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.resetToZero(on != 0); });
}
YSE_C_API int yse_dsp_triggered_ramp_get_reset_to_zero(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0, [](DSP::triggeredRamp& f) { return f.resetToZero() ? 1 : 0; });
}
YSE_C_API void yse_dsp_triggered_ramp_set_retrigger(YseDspProcessor* p, int on) {
  with<TriggeredRamp>(p, [&](DSP::triggeredRamp& f) { f.retrigger(on != 0); });
}
YSE_C_API int yse_dsp_triggered_ramp_get_retrigger(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0, [](DSP::triggeredRamp& f) { return f.retrigger() ? 1 : 0; });
}
YSE_C_API int yse_dsp_triggered_ramp_is_running(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0, [](DSP::triggeredRamp& f) { return f.running() ? 1 : 0; });
}
YSE_C_API float yse_dsp_triggered_ramp_get_phase(YseDspProcessor* p) {
  return query<TriggeredRamp>(p, 0.f, [](DSP::triggeredRamp& f) { return f.phase(); });
}
YSE_C_API unsigned int yse_dsp_triggered_ramp_process(YseDspProcessor* p, const float* trigger,
                                                      float* out, unsigned int length) {
  return one_signal<TriggeredRamp>(
      p, trigger, out, length,
      [](TriggeredRamp& h, DSP::buffer& t) -> DSP::buffer& { return h.object(t); });
}
YSE_C_API unsigned int yse_dsp_triggered_ramp_process_signal(YseDspProcessor* p,
                                                             const float* trigger,
                                                             const float* duration,
                                                             const float* start, const float* end,
                                                             float* out, unsigned int length) {
  if (trigger == nullptr || duration == nullptr || start == nullptr || end == nullptr ||
      out == nullptr)
    return 0;
  return drive<TriggeredRamp>(p, length, [&](TriggeredRamp& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], trigger + o, n), stage(h.scratch[1], duration + o, n),
                  stage(h.scratch[2], start + o, n), stage(h.scratch[3], end + o, n)),
         out + o, n);
  });
}

} // extern "C"
