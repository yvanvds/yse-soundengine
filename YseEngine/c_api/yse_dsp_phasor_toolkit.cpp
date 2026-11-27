#include "yse_c/yse_dsp_phasor_toolkit.h"
#include "yse_dsp_processor_internal.hpp"

#include "../dsp/generators/beatSpan.hpp"
#include "../dsp/generators/clockFollower.hpp"
#include "../dsp/generators/phasorRate.hpp"
#include "../dsp/generators/phasorStepDiv.hpp"
#include "../dsp/generators/phasorSubdiv.hpp"
#include "../dsp/generators/phasorSwing.hpp"
#include "../dsp/generators/phasorTwist.hpp"
#include "../dsp/generators/phasorWhat.hpp"
#include "../dsp/generators/phasorWhere.hpp"
#include "../dsp/generators/sampleHoldMemory.hpp"
#include "../dsp/generators/signalStash.hpp"
#include "../dsp/generators/snowfall.hpp"
#include "../dsp/generators/stepCounter.hpp"
#include "../dsp/generators/stepFunction.hpp"

#include <cstdint>
#include <cstring>
#include <utility>

namespace {
  namespace DSP = YSE::DSP;
  using yse_c::processor::beat_span;
  using yse_c::processor::drive;
  using yse_c::processor::emit;
  using yse_c::processor::follow;
  using yse_c::processor::handle;
  using yse_c::processor::in_enum;
  using yse_c::processor::make;
  using yse_c::processor::one_signal;
  using yse_c::processor::query;
  using yse_c::processor::set_list;
  using yse_c::processor::stage;
  using yse_c::processor::three_signals;
  using yse_c::processor::two_signals;
  using yse_c::processor::with;

  // What the bridge keeps beside a clockFollower: its per-block report over
  // the whole of the last process call, which spans several blocks. Reset
  // when a process call starts.
  struct clockReports {
    unsigned int taps = 0;
    unsigned int clicks = 0;
    unsigned int ticks = 0;
    unsigned int bytes = 0;
    bool tempoChanged = false;
  };

  // One wrapper type per class; the count is how many host arrays a process
  // call stages.
  using PhasorRate = handle<DSP::phasorRate, 2>;
  using PhasorSubdiv = handle<DSP::phasorSubdiv, 1>;
  using PhasorStepDiv = handle<DSP::phasorStepDiv, 1>;
  using PhasorSwing = handle<DSP::phasorSwing, 1>;
  using PhasorTwist = handle<DSP::phasorTwist, 1>;
  using PhasorWhere = handle<DSP::phasorWhere, 1>;
  using PhasorWhat = handle<DSP::phasorWhat, 1>;
  using StepCounter = handle<DSP::stepCounter, 1>;
  using StepFunction = handle<DSP::stepFunction, 2>;
  using SampleHoldMemory = handle<DSP::sampleHoldMemory, 3>;
  using SignalStash = handle<DSP::signalStash, 4>;
  using Snowfall = handle<DSP::snowfall, 1>;
  using BeatSpan = beat_span;
  using ClockFollower = handle<DSP::clockFollower, 1, clockReports>;

  // The constants the header spells as macros.
  static_assert(YSE_DSP_PHASOR_SUBDIV_MAX_DIV == DSP::phasorSubdiv::MAX_DIV,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_SUBDIV_MAX_DIV drifted");
  static_assert(YSE_DSP_PHASOR_SUBDIV_MAX_PATTERN == DSP::phasorSubdiv::MAX_PATTERN,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_SUBDIV_MAX_PATTERN drifted");
  static_assert(YSE_DSP_PHASOR_STEP_DIV_MAX_STEPS == DSP::phasorStepDiv::MAX_STEPS,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_STEP_DIV_MAX_STEPS drifted");
  static_assert(YSE_DSP_PHASOR_SWING_MIN == DSP::phasorSwing::MIN_SWING,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_SWING_MIN drifted");
  static_assert(YSE_DSP_PHASOR_SWING_MAX == DSP::phasorSwing::MAX_SWING,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_SWING_MAX drifted");
  static_assert(YSE_DSP_PHASOR_WHAT_MAX_MATCHES == DSP::phasorWhat::MAX_MATCHES,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_PHASOR_WHAT_MAX_MATCHES drifted");
  static_assert(YSE_DSP_STEP_COUNTER_MAX_STEPS == DSP::stepCounter::MAX_STEPS,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_STEP_COUNTER_MAX_STEPS drifted");
  static_assert(YSE_DSP_STEP_FUNCTION_MAX_STEPS == DSP::stepFunction::MAX_STEPS,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_STEP_FUNCTION_MAX_STEPS drifted");
  static_assert(YSE_DSP_MEMORY_DEFAULT_MAX_SIZE == DSP::sampleHoldMemory::DEFAULT_MAX_SIZE,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MEMORY_DEFAULT_MAX_SIZE drifted "
                "(sampleHoldMemory)");
  static_assert(YSE_DSP_MEMORY_DEFAULT_MAX_SIZE == DSP::signalStash::DEFAULT_MAX_SIZE,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MEMORY_DEFAULT_MAX_SIZE drifted (signalStash)");
  static_assert(YSE_DSP_SNOWFALL_MAX_DIMENSIONS == DSP::snowfall::MAX_DIMENSIONS,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_SNOWFALL_MAX_DIMENSIONS drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO == DSP::clockFollower::DEFAULT_TEMPO,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_DEFAULT_TEMPO drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_MIN_TEMPO == DSP::clockFollower::MIN_TEMPO,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_MIN_TEMPO drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_MAX_TEMPO == DSP::clockFollower::MAX_TEMPO,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_MAX_TEMPO drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_DEFAULT_PPQ == DSP::clockFollower::DEFAULT_PPQ,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_DEFAULT_PPQ drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_MAX_PPQ == DSP::clockFollower::MAX_PPQ,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_MAX_PPQ drifted");
  static_assert(YSE_DSP_CLOCK_FOLLOWER_MAX_CLOCK_BYTES == DSP::clockFollower::MAX_CLOCK_BYTES,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_CLOCK_FOLLOWER_MAX_CLOCK_BYTES drifted");
  static_assert(YSE_DSP_MIDI_CLOCK_TICK == DSP::clockFollower::MIDI_TICK,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MIDI_CLOCK_TICK drifted");
  static_assert(YSE_DSP_MIDI_CLOCK_START == DSP::clockFollower::MIDI_START,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MIDI_CLOCK_START drifted");
  static_assert(YSE_DSP_MIDI_CLOCK_CONTINUE == DSP::clockFollower::MIDI_CONTINUE,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MIDI_CLOCK_CONTINUE drifted");
  static_assert(YSE_DSP_MIDI_CLOCK_STOP == DSP::clockFollower::MIDI_STOP,
                "yse_dsp_phasor_toolkit.h: YSE_DSP_MIDI_CLOCK_STOP drifted");
  // A seed crosses as unsigned int and is narrowed to the engine's uint32_t.
  static_assert(sizeof(unsigned int) >= sizeof(std::uint32_t),
                "yse_dsp_phasor_toolkit.h: a seed must fit an unsigned int");

  // A constructor's capacity: 0 picks the class default.
  unsigned int or_default(unsigned int value, unsigned int fallback) noexcept {
    return value != 0 ? value : fallback;
  }

  // sampleHoldMemory / signalStash maxSize(), the one setter here that
  // allocates. The engine sets two vectors one after the other, so a
  // bad_alloc between them would leave them disagreeing. Instead the new
  // capacity is built on a copy, and the copy is moved in only once every
  // allocation succeeded: the move hands the vectors over and copies the
  // output buffer into one of the same size, neither of which allocates or
  // throws. A failure leaves the object as it was and reports nothing, as a
  // setter has no error channel.
  template <typename H> void set_max_size(YseDspProcessor* p, unsigned int capacity) {
    with<H>(p, [&](typename H::object_type& f) {
      try {
        typename H::object_type next(f);
        next.maxSize(capacity);
        f = std::move(next);
      } catch (...) { // NOLINT(bugprone-empty-catch): a void setter has no error channel
      }
    });
  }

  // clockFollower's process calls: `run(h, o, n)` makes one engine block,
  // whose report is gathered into the bridge state and whose clock bytes go
  // to the host's array at the block's offset.
  void gather(ClockFollower& h, unsigned o, YseDspClockByte* clock, unsigned capacity) {
    const DSP::clockFollower& f = h.object;
    h.state.taps += f.taps();
    h.state.clicks += f.clicks();
    h.state.ticks += f.ticks();
    h.state.tempoChanged = h.state.tempoChanged || f.tempoChanged();
    for (unsigned i = 0; i < f.clockBytes(); ++i) {
      const unsigned at = h.state.bytes++;
      if (at >= capacity) continue;
      clock[at].offset = o;
      clock[at].status = f.clockByte(i);
    }
  }

  template <typename F>
  unsigned int follower_blocks(YseDspProcessor* p, float* out, YseDspClockByte* clock,
                               unsigned int capacity, unsigned int length, F&& run) {
    if (out == nullptr) return 0;
    if (clock == nullptr) capacity = 0;
    if (ClockFollower* h = yse_c::processor::get<ClockFollower>(p)) h->state = clockReports{};
    return drive<ClockFollower>(p, length, [&](ClockFollower& h, unsigned o, unsigned n) {
      emit(run(h, o, n), out + o, n);
      gather(h, o, clock, capacity);
    });
  }

  // A block with a second engine output (an index, a remaining time) that
  // the host takes as an optional array.
  template <typename H, typename F, typename G>
  unsigned int with_side(YseDspProcessor* p, const float* in, float* out, float* side,
                         unsigned int length, F&& run, G&& second) {
    if (in == nullptr || out == nullptr) return 0;
    return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
      emit(run(h, stage(h.scratch[0], in + o, n)), out + o, n);
      emit(second(h), side != nullptr ? side + o : nullptr, n);
    });
  }
} // namespace

extern "C" {

// ─── phasorRate ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_rate_create(void) {
  return make<PhasorRate>("yse_dsp_phasor_rate_create");
}
YSE_C_API void yse_dsp_phasor_rate_set_multiplier(YseDspProcessor* p, float value) {
  with<PhasorRate>(p, [&](DSP::phasorRate& f) { f.multiplier(value); });
}
YSE_C_API float yse_dsp_phasor_rate_get_multiplier(YseDspProcessor* p) {
  return query<PhasorRate>(p, 0.f, [](DSP::phasorRate& f) { return f.multiplier(); });
}
YSE_C_API void yse_dsp_phasor_rate_set_sync(YseDspProcessor* p, YseDspRateSync mode) {
  if (!in_enum(mode, YSE_DSP_RATE_SYNC_OFF)) return;
  with<PhasorRate>(
      p, [&](DSP::phasorRate& f) { f.sync(static_cast<DSP::phasorRate::syncMode>(mode)); });
}
YSE_C_API YseDspRateSync yse_dsp_phasor_rate_get_sync(YseDspProcessor* p) {
  return query<PhasorRate>(p, YSE_DSP_RATE_SYNC_CYCLE, [](DSP::phasorRate& f) {
    return static_cast<YseDspRateSync>(f.sync());
  });
}
YSE_C_API void yse_dsp_phasor_rate_set_oneshot(YseDspProcessor* p, int on) {
  with<PhasorRate>(p, [&](DSP::phasorRate& f) { f.oneshot(on != 0); });
}
YSE_C_API int yse_dsp_phasor_rate_get_oneshot(YseDspProcessor* p) {
  return query<PhasorRate>(p, 0, [](DSP::phasorRate& f) { return f.oneshot() ? 1 : 0; });
}
YSE_C_API int yse_dsp_phasor_rate_is_stopped(YseDspProcessor* p) {
  return query<PhasorRate>(p, 0, [](DSP::phasorRate& f) { return f.stopped() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_rate_jump(YseDspProcessor* p, float value) {
  with<PhasorRate>(p, [&](DSP::phasorRate& f) { f.jump(value); });
}
YSE_C_API void yse_dsp_phasor_rate_jump_at(YseDspProcessor* p, float value, float at) {
  with<PhasorRate>(p, [&](DSP::phasorRate& f) { f.jump(value, at); });
}
YSE_C_API void yse_dsp_phasor_rate_reset(YseDspProcessor* p) {
  with<PhasorRate>(p, [](DSP::phasorRate& f) { f.reset(); });
}
YSE_C_API float yse_dsp_phasor_rate_process_sample(YseDspProcessor* p, float phase) {
  return query<PhasorRate>(p, 0.f, [&](DSP::phasorRate& f) { return f.process(phase); });
}
YSE_C_API float yse_dsp_phasor_rate_process_sample_signal(YseDspProcessor* p, float phase,
                                                          float multiplier) {
  return query<PhasorRate>(p, 0.f,
                           [&](DSP::phasorRate& f) { return f.process(phase, multiplier); });
}
YSE_C_API unsigned int yse_dsp_phasor_rate_process(YseDspProcessor* p, const float* phase,
                                                   float* out, unsigned int length) {
  return one_signal<PhasorRate>(
      p, phase, out, length,
      [](PhasorRate& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}
YSE_C_API unsigned int yse_dsp_phasor_rate_process_signal(YseDspProcessor* p, const float* phase,
                                                          const float* multiplier, float* out,
                                                          unsigned int length) {
  return two_signals<PhasorRate>(p, phase, multiplier, out, length,
                                 [](PhasorRate& h, DSP::buffer& in,
                                    DSP::buffer& m) -> DSP::buffer& { return h.object(in, m); });
}

// ─── phasorSubdiv ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_subdiv_create(void) {
  return make<PhasorSubdiv>("yse_dsp_phasor_subdiv_create");
}
YSE_C_API void yse_dsp_phasor_subdiv_set_div(YseDspProcessor* p, int value) {
  with<PhasorSubdiv>(p, [&](DSP::phasorSubdiv& f) { f.div(value); });
}
YSE_C_API int yse_dsp_phasor_subdiv_get_div(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, 0, [](DSP::phasorSubdiv& f) { return f.div(); });
}
YSE_C_API int yse_dsp_phasor_subdiv_set_pattern(YseDspProcessor* p, const int* steps,
                                                unsigned int count) {
  return set_list<PhasorSubdiv>(p, steps, count,
                                [&](DSP::phasorSubdiv& f) { f.pattern(steps, count); });
}
YSE_C_API unsigned int yse_dsp_phasor_subdiv_get_pattern_length(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, 0u, [](DSP::phasorSubdiv& f) { return f.patternLength(); });
}
YSE_C_API int yse_dsp_phasor_subdiv_set_prob(YseDspProcessor* p, const float* chances,
                                             unsigned int count) {
  return set_list<PhasorSubdiv>(p, chances, count,
                                [&](DSP::phasorSubdiv& f) { f.prob(chances, count); });
}
YSE_C_API unsigned int yse_dsp_phasor_subdiv_get_prob_length(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, 0u, [](DSP::phasorSubdiv& f) { return f.probLength(); });
}
YSE_C_API void yse_dsp_phasor_subdiv_set_lock_prob(YseDspProcessor* p, int on) {
  with<PhasorSubdiv>(p, [&](DSP::phasorSubdiv& f) { f.lockProb(on != 0); });
}
YSE_C_API int yse_dsp_phasor_subdiv_get_lock_prob(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, 0, [](DSP::phasorSubdiv& f) { return f.lockProb() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_subdiv_set_silent_mode(YseDspProcessor* p, YseDspSubdivSilent mode) {
  if (!in_enum(mode, YSE_DSP_SUBDIV_SILENT_MINUS_ONE)) return;
  with<PhasorSubdiv>(
      p, [&](DSP::phasorSubdiv& f) { f.silentMode(static_cast<DSP::phasorSubdiv::silent>(mode)); });
}
YSE_C_API YseDspSubdivSilent yse_dsp_phasor_subdiv_get_silent_mode(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, YSE_DSP_SUBDIV_SILENT_HOLD, [](DSP::phasorSubdiv& f) {
    return static_cast<YseDspSubdivSilent>(f.silentMode());
  });
}
YSE_C_API void yse_dsp_phasor_subdiv_set_sync_update(YseDspProcessor* p, YseDspSubdivUpdate mode) {
  if (!in_enum(mode, YSE_DSP_SUBDIV_UPDATE_INPUT)) return;
  with<PhasorSubdiv>(
      p, [&](DSP::phasorSubdiv& f) { f.syncUpdate(static_cast<DSP::phasorSubdiv::update>(mode)); });
}
YSE_C_API YseDspSubdivUpdate yse_dsp_phasor_subdiv_get_sync_update(YseDspProcessor* p) {
  return query<PhasorSubdiv>(p, YSE_DSP_SUBDIV_UPDATE_NOW, [](DSP::phasorSubdiv& f) {
    return static_cast<YseDspSubdivUpdate>(f.syncUpdate());
  });
}
YSE_C_API void yse_dsp_phasor_subdiv_seed(YseDspProcessor* p, unsigned int seed) {
  with<PhasorSubdiv>(p, [&](DSP::phasorSubdiv& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API float yse_dsp_phasor_subdiv_process_sample(YseDspProcessor* p, float phase,
                                                     float* index) {
  DSP::phasorSubdiv* f = yse_c::processor::object<PhasorSubdiv>(p);
  if (f == nullptr) return 0.f;
  const float out = f->process(phase);
  if (index != nullptr) *index = f->lastIndex();
  return out;
}
YSE_C_API unsigned int yse_dsp_phasor_subdiv_process(YseDspProcessor* p, const float* phase,
                                                     float* out, float* index_out,
                                                     unsigned int length) {
  return with_side<PhasorSubdiv>(
      p, phase, out, index_out, length,
      [](PhasorSubdiv& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); },
      [](PhasorSubdiv& h) -> DSP::buffer& { return h.object.index(); });
}

// ─── phasorStepDiv ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_step_div_create(void) {
  return make<PhasorStepDiv>("yse_dsp_phasor_step_div_create");
}
YSE_C_API int yse_dsp_phasor_step_div_set_pattern(YseDspProcessor* p, const float* times,
                                                  unsigned int count) {
  return set_list<PhasorStepDiv>(p, times, count,
                                 [&](DSP::phasorStepDiv& f) { f.pattern(times, count); });
}
YSE_C_API int yse_dsp_phasor_step_div_set_breakpoints(YseDspProcessor* p, const float* list,
                                                      unsigned int count, int curves) {
  return set_list<PhasorStepDiv>(
      p, list, count, [&](DSP::phasorStepDiv& f) { f.breakpoints(list, count, curves != 0); });
}
YSE_C_API int yse_dsp_phasor_step_div_set_onsets(YseDspProcessor* p, const float* offsets,
                                                 unsigned int count) {
  return set_list<PhasorStepDiv>(p, offsets, count,
                                 [&](DSP::phasorStepDiv& f) { f.onsets(offsets, count); });
}
YSE_C_API unsigned int yse_dsp_phasor_step_div_get_pattern_length(YseDspProcessor* p) {
  return query<PhasorStepDiv>(p, 0u, [](DSP::phasorStepDiv& f) { return f.patternLength(); });
}
YSE_C_API float yse_dsp_phasor_step_div_get_time(YseDspProcessor* p, unsigned int index) {
  return query<PhasorStepDiv>(p, 0.f, [&](DSP::phasorStepDiv& f) { return f.time(index); });
}
YSE_C_API void yse_dsp_phasor_step_div_set_loop(YseDspProcessor* p, int on) {
  with<PhasorStepDiv>(p, [&](DSP::phasorStepDiv& f) { f.loop(on != 0); });
}
YSE_C_API int yse_dsp_phasor_step_div_get_loop(YseDspProcessor* p) {
  return query<PhasorStepDiv>(p, 0, [](DSP::phasorStepDiv& f) { return f.loop() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_step_div_set_sync_update(YseDspProcessor* p, int on) {
  with<PhasorStepDiv>(p, [&](DSP::phasorStepDiv& f) { f.syncUpdate(on != 0); });
}
YSE_C_API int yse_dsp_phasor_step_div_get_sync_update(YseDspProcessor* p) {
  return query<PhasorStepDiv>(p, 0, [](DSP::phasorStepDiv& f) { return f.syncUpdate() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_step_div_start(YseDspProcessor* p) {
  with<PhasorStepDiv>(p, [](DSP::phasorStepDiv& f) { f.start(); });
}
YSE_C_API void yse_dsp_phasor_step_div_step(YseDspProcessor* p, int n) {
  with<PhasorStepDiv>(p, [&](DSP::phasorStepDiv& f) { f.step(n); });
}
YSE_C_API int yse_dsp_phasor_step_div_is_running(YseDspProcessor* p) {
  return query<PhasorStepDiv>(p, 0, [](DSP::phasorStepDiv& f) { return f.running() ? 1 : 0; });
}
YSE_C_API float yse_dsp_phasor_step_div_process_sample(YseDspProcessor* p, float phase,
                                                       float* index) {
  DSP::phasorStepDiv* f = yse_c::processor::object<PhasorStepDiv>(p);
  if (f == nullptr) return 0.f;
  const float out = f->process(phase);
  if (index != nullptr) *index = f->lastIndex();
  return out;
}
YSE_C_API float yse_dsp_phasor_step_div_process_free_sample(YseDspProcessor* p, float* index) {
  DSP::phasorStepDiv* f = yse_c::processor::object<PhasorStepDiv>(p);
  if (f == nullptr) return 0.f;
  const float out = f->process();
  if (index != nullptr) *index = f->lastIndex();
  return out;
}
YSE_C_API unsigned int yse_dsp_phasor_step_div_process(YseDspProcessor* p, const float* phase,
                                                       float* out, float* index_out,
                                                       unsigned int length) {
  return with_side<PhasorStepDiv>(
      p, phase, out, index_out, length,
      [](PhasorStepDiv& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); },
      [](PhasorStepDiv& h) -> DSP::buffer& { return h.object.index(); });
}
YSE_C_API unsigned int yse_dsp_phasor_step_div_process_free(YseDspProcessor* p, float* out,
                                                            float* index_out, unsigned int length) {
  if (out == nullptr) return 0;
  return drive<PhasorStepDiv>(p, length, [&](PhasorStepDiv& h, unsigned o, unsigned n) {
    DSP::phasorStepDiv& f = h.object;
    if (n == yse_c::processor::BLOCK) {
      // The engine's free-running block is always a full one.
      emit(f(), out + o, n);
      emit(f.index(), index_out != nullptr ? index_out + o : nullptr, n);
      return;
    }
    // A short last block, one sample at a time: process() reads the sample
    // rate and steps exactly as the block does.
    for (unsigned i = 0; i < n; ++i) {
      out[o + i] = f.process();
      if (index_out != nullptr) index_out[o + i] = f.lastIndex();
    }
  });
}

// ─── phasorSwing ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_swing_create(void) {
  return make<PhasorSwing>("yse_dsp_phasor_swing_create");
}
YSE_C_API void yse_dsp_phasor_swing_set_swing(YseDspProcessor* p, float value) {
  with<PhasorSwing>(p, [&](DSP::phasorSwing& f) { f.swing(value); });
}
YSE_C_API float yse_dsp_phasor_swing_get_swing(YseDspProcessor* p) {
  return query<PhasorSwing>(p, 0.f, [](DSP::phasorSwing& f) { return f.swing(); });
}
YSE_C_API void yse_dsp_phasor_swing_set_sync_update(YseDspProcessor* p, int on) {
  with<PhasorSwing>(p, [&](DSP::phasorSwing& f) { f.syncUpdate(on != 0); });
}
YSE_C_API int yse_dsp_phasor_swing_get_sync_update(YseDspProcessor* p) {
  return query<PhasorSwing>(p, 0, [](DSP::phasorSwing& f) { return f.syncUpdate() ? 1 : 0; });
}
YSE_C_API float yse_dsp_phasor_swing_process_sample(YseDspProcessor* p, float phase) {
  return query<PhasorSwing>(p, 0.f, [&](DSP::phasorSwing& f) { return f.process(phase); });
}
YSE_C_API unsigned int yse_dsp_phasor_swing_process(YseDspProcessor* p, const float* phase,
                                                    float* out, unsigned int length) {
  return one_signal<PhasorSwing>(
      p, phase, out, length,
      [](PhasorSwing& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}

// ─── phasorTwist ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_twist_create(void) {
  return make<PhasorTwist>("yse_dsp_phasor_twist_create");
}
YSE_C_API void yse_dsp_phasor_twist_set_curve(YseDspProcessor* p, float value) {
  with<PhasorTwist>(p, [&](DSP::phasorTwist& f) { f.curve(value); });
}
YSE_C_API float yse_dsp_phasor_twist_get_curve(YseDspProcessor* p) {
  return query<PhasorTwist>(p, 0.f, [](DSP::phasorTwist& f) { return f.curve(); });
}
YSE_C_API void yse_dsp_phasor_twist_set_shape_mode(YseDspProcessor* p, int on) {
  with<PhasorTwist>(p, [&](DSP::phasorTwist& f) { f.shapeMode(on != 0); });
}
YSE_C_API int yse_dsp_phasor_twist_get_shape_mode(YseDspProcessor* p) {
  return query<PhasorTwist>(p, 0, [](DSP::phasorTwist& f) { return f.shapeMode() ? 1 : 0; });
}
YSE_C_API void yse_dsp_phasor_twist_set_sync_update(YseDspProcessor* p, int on) {
  with<PhasorTwist>(p, [&](DSP::phasorTwist& f) { f.syncUpdate(on != 0); });
}
YSE_C_API int yse_dsp_phasor_twist_get_sync_update(YseDspProcessor* p) {
  return query<PhasorTwist>(p, 0, [](DSP::phasorTwist& f) { return f.syncUpdate() ? 1 : 0; });
}
YSE_C_API float yse_dsp_phasor_twist_process_sample(YseDspProcessor* p, float phase) {
  return query<PhasorTwist>(p, 0.f, [&](DSP::phasorTwist& f) { return f.process(phase); });
}
YSE_C_API unsigned int yse_dsp_phasor_twist_process(YseDspProcessor* p, const float* phase,
                                                    float* out, unsigned int length) {
  return one_signal<PhasorTwist>(
      p, phase, out, length,
      [](PhasorTwist& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}

// ─── phasorWhere ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_where_create(void) {
  return make<PhasorWhere>("yse_dsp_phasor_where_create");
}
YSE_C_API float yse_dsp_phasor_where_process_sample(YseDspProcessor* p, float phase,
                                                    float* remaining) {
  DSP::phasorWhere* f = yse_c::processor::object<PhasorWhere>(p);
  if (f == nullptr) return 0.f;
  const float out = f->process(phase);
  if (remaining != nullptr) *remaining = f->lastRemaining();
  return out;
}
YSE_C_API unsigned int yse_dsp_phasor_where_process(YseDspProcessor* p, const float* phase,
                                                    float* out, float* remaining_out,
                                                    unsigned int length) {
  return with_side<PhasorWhere>(
      p, phase, out, remaining_out, length,
      [](PhasorWhere& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); },
      [](PhasorWhere& h) -> DSP::buffer& { return h.object.remaining(); });
}

// ─── phasorWhat ─────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_phasor_what_create(void) {
  return make<PhasorWhat>("yse_dsp_phasor_what_create");
}
YSE_C_API int yse_dsp_phasor_what_set_matches(YseDspProcessor* p, const float* values,
                                              unsigned int count) {
  return set_list<PhasorWhat>(p, values, count,
                              [&](DSP::phasorWhat& f) { f.matches(values, count); });
}
YSE_C_API void yse_dsp_phasor_what_clear(YseDspProcessor* p) {
  with<PhasorWhat>(p, [](DSP::phasorWhat& f) { f.clear(); });
}
YSE_C_API void yse_dsp_phasor_what_set_trigger_mode(YseDspProcessor* p, YseDspPhasorWhatMode mode) {
  if (!in_enum(mode, YSE_DSP_PHASOR_WHAT_EQUALS)) return;
  with<PhasorWhat>(p, [&](DSP::phasorWhat& f) {
    f.triggerMode(static_cast<DSP::phasorWhat::TRIGGER_MODE>(mode));
  });
}
YSE_C_API void yse_dsp_phasor_what_set_sync_update(YseDspProcessor* p, int on) {
  with<PhasorWhat>(p, [&](DSP::phasorWhat& f) { f.syncUpdate(on != 0); });
}
YSE_C_API unsigned int yse_dsp_phasor_what_get_count(YseDspProcessor* p) {
  return query<PhasorWhat>(p, 0u, [](DSP::phasorWhat& f) { return f.count(); });
}
YSE_C_API float yse_dsp_phasor_what_process_sample(YseDspProcessor* p, float phase) {
  return query<PhasorWhat>(p, 0.f, [&](DSP::phasorWhat& f) { return f.process(phase); });
}
YSE_C_API unsigned int yse_dsp_phasor_what_process(YseDspProcessor* p, const float* phase,
                                                   float* out, unsigned int length) {
  return one_signal<PhasorWhat>(
      p, phase, out, length,
      [](PhasorWhat& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}

// ─── stepCounter ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_step_counter_create(void) {
  return make<StepCounter>("yse_dsp_step_counter_create");
}
YSE_C_API int yse_dsp_step_counter_set_sequence(YseDspProcessor* p, const float* counts,
                                                unsigned int count) {
  return set_list<StepCounter>(p, counts, count,
                               [&](DSP::stepCounter& f) { f.sequence(counts, count); });
}
YSE_C_API void yse_dsp_step_counter_set_direction(YseDspProcessor* p,
                                                  YseDspStepCounterDirection direction) {
  if (!in_enum(direction, YSE_DSP_STEP_COUNTER_DOWN)) return;
  with<StepCounter>(p, [&](DSP::stepCounter& f) {
    f.direction(static_cast<DSP::stepCounter::DIRECTION>(direction));
  });
}
YSE_C_API void yse_dsp_step_counter_set_start_mode(YseDspProcessor* p, int on) {
  with<StepCounter>(p, [&](DSP::stepCounter& f) { f.startMode(on != 0); });
}
YSE_C_API void yse_dsp_step_counter_set_sync_update(YseDspProcessor* p,
                                                    YseDspStepCounterSync mode) {
  if (!in_enum(mode, YSE_DSP_STEP_COUNTER_AFTER_SEQUENCE)) return;
  with<StepCounter>(
      p, [&](DSP::stepCounter& f) { f.syncUpdate(static_cast<DSP::stepCounter::SYNC>(mode)); });
}
YSE_C_API void yse_dsp_step_counter_set_threshold(YseDspProcessor* p, float value) {
  with<StepCounter>(p, [&](DSP::stepCounter& f) { f.threshold(value); });
}
YSE_C_API void yse_dsp_step_counter_reset(YseDspProcessor* p) {
  with<StepCounter>(p, [](DSP::stepCounter& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_step_counter_get_length(YseDspProcessor* p) {
  return query<StepCounter>(p, 0u, [](DSP::stepCounter& f) { return f.length(); });
}
YSE_C_API float yse_dsp_step_counter_process_sample(YseDspProcessor* p, float in, float* count,
                                                    float* step) {
  DSP::stepCounter* f = yse_c::processor::object<StepCounter>(p);
  if (f == nullptr) return 0.f;
  const float out = f->process(in);
  if (count != nullptr) *count = f->lastCount();
  if (step != nullptr) *step = f->lastStep();
  return out;
}
YSE_C_API unsigned int yse_dsp_step_counter_process(YseDspProcessor* p, const float* in, float* out,
                                                    float* counts_out, float* steps_out,
                                                    unsigned int length) {
  if (in == nullptr || out == nullptr) return 0;
  return drive<StepCounter>(p, length, [&](StepCounter& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], in + o, n)), out + o, n);
    emit(h.object.counts(), counts_out != nullptr ? counts_out + o : nullptr, n);
    emit(h.object.steps(), steps_out != nullptr ? steps_out + o : nullptr, n);
  });
}

// ─── stepFunction ───────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_step_function_create(void) {
  return make<StepFunction>("yse_dsp_step_function_create");
}
YSE_C_API int yse_dsp_step_function_set_pattern(YseDspProcessor* p, const float* values,
                                                unsigned int count) {
  return set_list<StepFunction>(p, values, count,
                                [&](DSP::stepFunction& f) { f.pattern(values, count); });
}
YSE_C_API int yse_dsp_step_function_set_curve_pattern(YseDspProcessor* p, const float* curves,
                                                      unsigned int count) {
  return set_list<StepFunction>(p, curves, count,
                                [&](DSP::stepFunction& f) { f.curvePattern(curves, count); });
}
YSE_C_API int yse_dsp_step_function_set_breakpoints(YseDspProcessor* p, const float* list,
                                                    unsigned int count, int curves,
                                                    const float* initial) {
  return set_list<StepFunction>(p, list, count, [&](DSP::stepFunction& f) {
    f.breakpoints(list, count, curves != 0, initial);
  });
}
YSE_C_API void yse_dsp_step_function_set_sync_update(YseDspProcessor* p,
                                                     YseDspStepFunctionSync mode) {
  if (!in_enum(mode, YSE_DSP_STEP_FUNCTION_SEQUENCE_END)) return;
  with<StepFunction>(
      p, [&](DSP::stepFunction& f) { f.syncUpdate(static_cast<DSP::stepFunction::SYNC>(mode)); });
}
YSE_C_API void yse_dsp_step_function_step(YseDspProcessor* p, int n) {
  with<StepFunction>(p, [&](DSP::stepFunction& f) { f.step(n); });
}
YSE_C_API void yse_dsp_step_function_reset(YseDspProcessor* p) {
  with<StepFunction>(p, [](DSP::stepFunction& f) { f.reset(); });
}
YSE_C_API unsigned int yse_dsp_step_function_get_length(YseDspProcessor* p) {
  return query<StepFunction>(p, 0u, [](DSP::stepFunction& f) { return f.length(); });
}
YSE_C_API unsigned int yse_dsp_step_function_get_last_step(YseDspProcessor* p) {
  return query<StepFunction>(p, 0u, [](DSP::stepFunction& f) { return f.lastStep(); });
}
YSE_C_API float yse_dsp_step_function_process_sample(YseDspProcessor* p, float phase) {
  return query<StepFunction>(p, 0.f, [&](DSP::stepFunction& f) { return f.process(phase); });
}
YSE_C_API float yse_dsp_step_function_process_sample_signal(YseDspProcessor* p, float phase,
                                                            float step) {
  return query<StepFunction>(p, 0.f, [&](DSP::stepFunction& f) { return f.process(phase, step); });
}
YSE_C_API unsigned int yse_dsp_step_function_process(YseDspProcessor* p, const float* phase,
                                                     float* out, unsigned int length) {
  return one_signal<StepFunction>(
      p, phase, out, length,
      [](StepFunction& h, DSP::buffer& in) -> DSP::buffer& { return h.object(in); });
}
YSE_C_API unsigned int yse_dsp_step_function_process_signal(YseDspProcessor* p, const float* phase,
                                                            const float* step, float* out,
                                                            unsigned int length) {
  return two_signals<StepFunction>(p, phase, step, out, length,
                                   [](StepFunction& h, DSP::buffer& in,
                                      DSP::buffer& s) -> DSP::buffer& { return h.object(in, s); });
}

// ─── sampleHoldMemory ───────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_sample_hold_memory_create(unsigned int max_size) {
  return make<SampleHoldMemory>("yse_dsp_sample_hold_memory_create",
                                or_default(max_size, DSP::sampleHoldMemory::DEFAULT_MAX_SIZE));
}
YSE_C_API void yse_dsp_sample_hold_memory_set_max_size(YseDspProcessor* p, unsigned int capacity) {
  set_max_size<SampleHoldMemory>(p, capacity);
}
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_max_size(YseDspProcessor* p) {
  return query<SampleHoldMemory>(p, 0u, [](DSP::sampleHoldMemory& f) { return f.maxSize(); });
}
YSE_C_API void yse_dsp_sample_hold_memory_set_size(YseDspProcessor* p, unsigned int places) {
  with<SampleHoldMemory>(p, [&](DSP::sampleHoldMemory& f) { f.size(places); });
}
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_size(YseDspProcessor* p) {
  return query<SampleHoldMemory>(p, 0u, [](DSP::sampleHoldMemory& f) { return f.size(); });
}
YSE_C_API void yse_dsp_sample_hold_memory_set_sample_level(YseDspProcessor* p, float level) {
  with<SampleHoldMemory>(p, [&](DSP::sampleHoldMemory& f) { f.sampleLevel(level); });
}
YSE_C_API void yse_dsp_sample_hold_memory_set_advance_level(YseDspProcessor* p, float level) {
  with<SampleHoldMemory>(p, [&](DSP::sampleHoldMemory& f) { f.advanceLevel(level); });
}
YSE_C_API void yse_dsp_sample_hold_memory_set_direction(YseDspProcessor* p,
                                                        YseDspMemoryDirection direction) {
  if (!in_enum(direction, YSE_DSP_MEMORY_URN)) return;
  with<SampleHoldMemory>(p, [&](DSP::sampleHoldMemory& f) {
    f.direction(static_cast<DSP::sampleHoldMemory::DIRECTION>(direction));
  });
}
YSE_C_API void yse_dsp_sample_hold_memory_set_mode(YseDspProcessor* p, YseDspSashMode mode) {
  if (!in_enum(mode, YSE_DSP_SASH_SUBDIVISION)) return;
  with<SampleHoldMemory>(
      p, [&](DSP::sampleHoldMemory& f) { f.mode(static_cast<DSP::sampleHoldMemory::MODE>(mode)); });
}
YSE_C_API void yse_dsp_sample_hold_memory_seed(YseDspProcessor* p, unsigned int seed) {
  with<SampleHoldMemory>(
      p, [&](DSP::sampleHoldMemory& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API int yse_dsp_sample_hold_memory_load(YseDspProcessor* p, const float* values,
                                              unsigned int count) {
  return set_list<SampleHoldMemory>(p, values, count,
                                    [&](DSP::sampleHoldMemory& f) { f.load(values, count); });
}
YSE_C_API float yse_dsp_sample_hold_memory_get_value(YseDspProcessor* p, unsigned int place) {
  return query<SampleHoldMemory>(p, 0.f, [&](DSP::sampleHoldMemory& f) { return f.value(place); });
}
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_write_position(YseDspProcessor* p) {
  return query<SampleHoldMemory>(p, 0u, [](DSP::sampleHoldMemory& f) { return f.writePosition(); });
}
YSE_C_API unsigned int yse_dsp_sample_hold_memory_get_read_position(YseDspProcessor* p) {
  return query<SampleHoldMemory>(p, 0u, [](DSP::sampleHoldMemory& f) { return f.readPosition(); });
}
YSE_C_API float yse_dsp_sample_hold_memory_process_sample(YseDspProcessor* p, float in,
                                                          float sample_trigger,
                                                          float advance_trigger) {
  return query<SampleHoldMemory>(p, 0.f, [&](DSP::sampleHoldMemory& f) {
    return f.process(in, sample_trigger, advance_trigger);
  });
}
YSE_C_API unsigned int yse_dsp_sample_hold_memory_process(YseDspProcessor* p, const float* in,
                                                          const float* sample_trigger,
                                                          const float* advance_trigger, float* out,
                                                          unsigned int length) {
  return three_signals<SampleHoldMemory>(
      p, in, sample_trigger, advance_trigger, out, length,
      [](SampleHoldMemory& h, DSP::buffer& a, DSP::buffer& s, DSP::buffer& t) -> DSP::buffer& {
        return h.object(a, s, t);
      });
}

// ─── signalStash ────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_signal_stash_create(unsigned int max_size) {
  return make<SignalStash>("yse_dsp_signal_stash_create",
                           or_default(max_size, DSP::signalStash::DEFAULT_MAX_SIZE));
}
YSE_C_API void yse_dsp_signal_stash_set_max_size(YseDspProcessor* p, unsigned int capacity) {
  set_max_size<SignalStash>(p, capacity);
}
YSE_C_API unsigned int yse_dsp_signal_stash_get_max_size(YseDspProcessor* p) {
  return query<SignalStash>(p, 0u, [](DSP::signalStash& f) { return f.maxSize(); });
}
YSE_C_API void yse_dsp_signal_stash_set_size(YseDspProcessor* p, unsigned int places) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.size(places); });
}
YSE_C_API unsigned int yse_dsp_signal_stash_get_size(YseDspProcessor* p) {
  return query<SignalStash>(p, 0u, [](DSP::signalStash& f) { return f.size(); });
}
YSE_C_API void yse_dsp_signal_stash_set_sample_level(YseDspProcessor* p, float level) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.sampleLevel(level); });
}
YSE_C_API void yse_dsp_signal_stash_set_advance_level(YseDspProcessor* p, float level) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.advanceLevel(level); });
}
YSE_C_API void yse_dsp_signal_stash_set_sample_trigger_mode(YseDspProcessor* p,
                                                            YseDspStashTrigger mode) {
  if (!in_enum(mode, YSE_DSP_STASH_TRIGGER_PHASE)) return;
  with<SignalStash>(p, [&](DSP::signalStash& f) {
    f.sampleTriggerMode(static_cast<DSP::signalStash::TRIGGER_MODE>(mode));
  });
}
YSE_C_API void yse_dsp_signal_stash_set_advance_trigger_mode(YseDspProcessor* p,
                                                             YseDspStashTrigger mode) {
  if (!in_enum(mode, YSE_DSP_STASH_TRIGGER_PHASE)) return;
  with<SignalStash>(p, [&](DSP::signalStash& f) {
    f.advanceTriggerMode(static_cast<DSP::signalStash::TRIGGER_MODE>(mode));
  });
}
YSE_C_API void yse_dsp_signal_stash_set_direction(YseDspProcessor* p,
                                                  YseDspMemoryDirection direction) {
  if (!in_enum(direction, YSE_DSP_MEMORY_URN)) return;
  with<SignalStash>(p, [&](DSP::signalStash& f) {
    f.direction(static_cast<DSP::signalStash::DIRECTION>(direction));
  });
}
YSE_C_API void yse_dsp_signal_stash_set_mode(YseDspProcessor* p, YseDspStashMode mode) {
  if (!in_enum(mode, YSE_DSP_STASH_NORMALIZED)) return;
  with<SignalStash>(
      p, [&](DSP::signalStash& f) { f.mode(static_cast<DSP::signalStash::MODE>(mode)); });
}
YSE_C_API void yse_dsp_signal_stash_set_write_mode(YseDspProcessor* p, YseDspStashMode mode) {
  if (!in_enum(mode, YSE_DSP_STASH_NORMALIZED)) return;
  with<SignalStash>(
      p, [&](DSP::signalStash& f) { f.writeMode(static_cast<DSP::signalStash::MODE>(mode)); });
}
YSE_C_API void yse_dsp_signal_stash_set_range(YseDspProcessor* p, YseDspStashRange range) {
  if (!in_enum(range, YSE_DSP_STASH_RANGE_KEEP)) return;
  with<SignalStash>(
      p, [&](DSP::signalStash& f) { f.range(static_cast<DSP::signalStash::RANGE>(range)); });
}
YSE_C_API void yse_dsp_signal_stash_set_interpolation(YseDspProcessor* p,
                                                      YseDspStashInterpolation interpolation) {
  if (!in_enum(interpolation, YSE_DSP_STASH_ROUND)) return;
  with<SignalStash>(p, [&](DSP::signalStash& f) {
    f.interpolation(static_cast<DSP::signalStash::INTERPOLATION>(interpolation));
  });
}
YSE_C_API void yse_dsp_signal_stash_set_duration(YseDspProcessor* p, unsigned int samples) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.duration(samples); });
}
YSE_C_API void yse_dsp_signal_stash_seed(YseDspProcessor* p, unsigned int seed) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API int yse_dsp_signal_stash_load(YseDspProcessor* p, const float* values,
                                        unsigned int count) {
  return set_list<SignalStash>(p, values, count,
                               [&](DSP::signalStash& f) { f.load(values, count); });
}
YSE_C_API void yse_dsp_signal_stash_write(YseDspProcessor* p, float value) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.write(value); });
}
YSE_C_API void yse_dsp_signal_stash_set_write_position(YseDspProcessor* p, float index) {
  with<SignalStash>(p, [&](DSP::signalStash& f) { f.writePosition(index); });
}
YSE_C_API void yse_dsp_signal_stash_advance_write(YseDspProcessor* p) {
  with<SignalStash>(p, [](DSP::signalStash& f) { f.advanceWrite(); });
}
YSE_C_API void yse_dsp_signal_stash_advance(YseDspProcessor* p) {
  with<SignalStash>(p, [](DSP::signalStash& f) { f.advance(); });
}
YSE_C_API void yse_dsp_signal_stash_capture(YseDspProcessor* p) {
  with<SignalStash>(p, [](DSP::signalStash& f) { f.capture(); });
}
YSE_C_API float yse_dsp_signal_stash_get_value(YseDspProcessor* p, unsigned int place) {
  return query<SignalStash>(p, 0.f, [&](DSP::signalStash& f) { return f.value(place); });
}
YSE_C_API unsigned int yse_dsp_signal_stash_get_write_position(YseDspProcessor* p) {
  return query<SignalStash>(p, 0u, [](DSP::signalStash& f) { return f.writePosition(); });
}
YSE_C_API unsigned int yse_dsp_signal_stash_get_read_position(YseDspProcessor* p) {
  return query<SignalStash>(p, 0u, [](DSP::signalStash& f) { return f.readPosition(); });
}
YSE_C_API float yse_dsp_signal_stash_process_sample(YseDspProcessor* p, float in,
                                                    float sample_trigger, float advance_trigger,
                                                    float write_index) {
  return query<SignalStash>(p, 0.f, [&](DSP::signalStash& f) {
    return f.process(in, sample_trigger, advance_trigger, write_index);
  });
}
YSE_C_API unsigned int yse_dsp_signal_stash_process(YseDspProcessor* p, const float* in,
                                                    const float* sample_trigger,
                                                    const float* advance_trigger,
                                                    const float* write_index, float* out,
                                                    unsigned int length) {
  // The one class with four inputs: three_signals plus one more stage.
  if (in == nullptr || sample_trigger == nullptr || advance_trigger == nullptr ||
      write_index == nullptr || out == nullptr)
    return 0;
  return drive<SignalStash>(p, length, [&](SignalStash& h, unsigned o, unsigned n) {
    emit(h.object(stage(h.scratch[0], in + o, n), stage(h.scratch[1], sample_trigger + o, n),
                  stage(h.scratch[2], advance_trigger + o, n),
                  stage(h.scratch[3], write_index + o, n)),
         out + o, n);
  });
}

// ─── snowfall ───────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_snowfall_create(unsigned int dimensions) {
  return make<Snowfall>("yse_dsp_snowfall_create", dimensions);
}
YSE_C_API void yse_dsp_snowfall_set_dimensions(YseDspProcessor* p, unsigned int dimensions) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.dimensions(dimensions); });
}
YSE_C_API unsigned int yse_dsp_snowfall_get_dimensions(YseDspProcessor* p) {
  return query<Snowfall>(p, 0u, [](DSP::snowfall& f) { return f.dimensions(); });
}
YSE_C_API void yse_dsp_snowfall_set_direction(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.direction(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_direction_deviation(YseDspProcessor* p, unsigned int d,
                                                        float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.directionDeviation(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_energy_ramp(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.energyRamp(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_initial(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.initial(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_initial_deviation(YseDspProcessor* p, unsigned int d,
                                                      float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.initialDeviation(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_minimum(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.minimum(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_maximum(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.maximum(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_scale_minimum(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.scaleMinimum(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_scale_maximum(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.scaleMaximum(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_squish(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.squish(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_wander_probability(YseDspProcessor* p, unsigned int d,
                                                       float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.wanderProbability(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_end_value(YseDspProcessor* p, unsigned int d, float value) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.endValue(d, value); });
}
YSE_C_API void yse_dsp_snowfall_set_interval(YseDspProcessor* p, float ms) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.interval(ms); });
}
YSE_C_API void yse_dsp_snowfall_set_interval_deviation(YseDspProcessor* p, float ms) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.intervalDeviation(ms); });
}
YSE_C_API void yse_dsp_snowfall_set_boundary_mode(YseDspProcessor* p, YseDspSnowfallBoundary mode) {
  if (!in_enum(mode, YSE_DSP_SNOWFALL_CLIP)) return;
  with<Snowfall>(p, [&](DSP::snowfall& f) {
    f.boundaryMode(static_cast<DSP::snowfall::BOUNDARY_MODE>(mode));
  });
}
YSE_C_API void yse_dsp_snowfall_set_end_mode(YseDspProcessor* p, YseDspSnowfallEnd mode) {
  if (!in_enum(mode, YSE_DSP_SNOWFALL_CURRENT_VALUE)) return;
  with<Snowfall>(p,
                 [&](DSP::snowfall& f) { f.endMode(static_cast<DSP::snowfall::END_MODE>(mode)); });
}
YSE_C_API void yse_dsp_snowfall_set_start_mode(YseDspProcessor* p, YseDspSnowfallStart mode) {
  if (!in_enum(mode, YSE_DSP_SNOWFALL_START_CURRENT)) return;
  with<Snowfall>(
      p, [&](DSP::snowfall& f) { f.startMode(static_cast<DSP::snowfall::START_MODE>(mode)); });
}
YSE_C_API void yse_dsp_snowfall_seed(YseDspProcessor* p, unsigned int seed) {
  with<Snowfall>(p, [&](DSP::snowfall& f) { f.seed(static_cast<std::uint32_t>(seed)); });
}
YSE_C_API int yse_dsp_snowfall_is_alive(YseDspProcessor* p) {
  return query<Snowfall>(p, 0, [](DSP::snowfall& f) { return f.alive() ? 1 : 0; });
}
YSE_C_API float yse_dsp_snowfall_get_value(YseDspProcessor* p, unsigned int d) {
  return query<Snowfall>(p, 0.f, [&](DSP::snowfall& f) { return f.value(d); });
}
YSE_C_API float yse_dsp_snowfall_get_velocity(YseDspProcessor* p, unsigned int d) {
  return query<Snowfall>(p, 0.f, [&](DSP::snowfall& f) { return f.velocity(d); });
}
YSE_C_API unsigned int yse_dsp_snowfall_process_sample(YseDspProcessor* p, float ramp, float* out,
                                                       unsigned int capacity) {
  DSP::snowfall* f = yse_c::processor::object<Snowfall>(p);
  if (f == nullptr || out == nullptr) return 0;
  // The engine writes dimensions() values; stage them so a host array
  // shorter than that is not overrun.
  float values[DSP::snowfall::MAX_DIMENSIONS] = {};
  f->process(ramp, values);
  const unsigned dims = f->dimensions();
  const unsigned n = capacity < dims ? capacity : dims;
  std::memcpy(out, values, sizeof(float) * n);
  return dims;
}
YSE_C_API unsigned int yse_dsp_snowfall_process(YseDspProcessor* p, const float* ramp,
                                                float* const* outputs, unsigned int count,
                                                unsigned int length) {
  if (ramp == nullptr || outputs == nullptr) return 0;
  return drive<Snowfall>(p, length, [&](Snowfall& h, unsigned o, unsigned n) {
    DSP::snowfall& f = h.object;
    f(stage(h.scratch[0], ramp + o, n));
    const unsigned dims = f.dimensions();
    for (unsigned d = 0; d < count; ++d) {
      float* dst = outputs[d];
      if (dst == nullptr) continue;
      if (d < dims) {
        emit(f.output(d), dst + o, n);
      } else {
        std::memset(dst + o, 0, sizeof(float) * n);
      }
    }
  });
}

// ─── beatSpan ───────────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_beat_span_create(void) {
  return make<BeatSpan>("yse_dsp_beat_span_create");
}
YSE_C_API void yse_dsp_beat_span_read(YseDspProcessor* p, double beat, float bpm,
                                      unsigned int length) {
  with<BeatSpan>(p, [&](DSP::beatSpan& f) { f.read(beat, bpm, length); });
}
YSE_C_API void yse_dsp_beat_span_lose(YseDspProcessor* p) {
  with<BeatSpan>(p, [](DSP::beatSpan& f) { f.lose(); });
}
YSE_C_API int yse_dsp_beat_span_is_valid(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0, [](DSP::beatSpan& f) { return f.valid() ? 1 : 0; });
}
YSE_C_API double yse_dsp_beat_span_get_start(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0.0, [](DSP::beatSpan& f) { return f.start(); });
}
YSE_C_API double yse_dsp_beat_span_get_end(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0.0, [](DSP::beatSpan& f) { return f.end(); });
}
YSE_C_API float yse_dsp_beat_span_get_tempo(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0.f, [](DSP::beatSpan& f) { return f.tempo(); });
}
YSE_C_API unsigned int yse_dsp_beat_span_get_length(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0u, [](DSP::beatSpan& f) { return f.length(); });
}
YSE_C_API double yse_dsp_beat_span_get_step(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0.0, [](DSP::beatSpan& f) { return f.step(); });
}
YSE_C_API double yse_dsp_beat_span_get_at(YseDspProcessor* p, unsigned int index) {
  return query<BeatSpan>(p, 0.0, [&](DSP::beatSpan& f) { return f.at(index); });
}
YSE_C_API int yse_dsp_beat_span_is_moving(YseDspProcessor* p) {
  return query<BeatSpan>(p, 0, [](DSP::beatSpan& f) { return f.moving() ? 1 : 0; });
}

// ─── clockFollower ──────────────────────────────────────────────────────

YSE_C_API YseDspProcessor* yse_dsp_clock_follower_create(void) {
  return make<ClockFollower>("yse_dsp_clock_follower_create");
}
YSE_C_API void yse_dsp_clock_follower_reset(YseDspProcessor* p, float bpm) {
  if (ClockFollower* h = yse_c::processor::get<ClockFollower>(p)) {
    h->object.reset(bpm);
    h->state = clockReports{};
  }
}
YSE_C_API void yse_dsp_clock_follower_set_tempo(YseDspProcessor* p, float bpm) {
  with<ClockFollower>(p, [&](DSP::clockFollower& f) { f.tempo(bpm); });
}
YSE_C_API float yse_dsp_clock_follower_get_tempo(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0.f, [](DSP::clockFollower& f) { return f.tempo(); });
}
YSE_C_API void yse_dsp_clock_follower_set_offset(YseDspProcessor* p, float phase) {
  with<ClockFollower>(p, [&](DSP::clockFollower& f) { f.offset(phase); });
}
YSE_C_API float yse_dsp_clock_follower_get_offset(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0.f, [](DSP::clockFollower& f) { return f.offset(); });
}
YSE_C_API void yse_dsp_clock_follower_set_clock_offset(YseDspProcessor* p, float phase) {
  with<ClockFollower>(p, [&](DSP::clockFollower& f) { f.clockOffset(phase); });
}
YSE_C_API float yse_dsp_clock_follower_get_clock_offset(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0.f, [](DSP::clockFollower& f) { return f.clockOffset(); });
}
YSE_C_API void yse_dsp_clock_follower_set_ppq(YseDspProcessor* p, unsigned int ticks) {
  with<ClockFollower>(p, [&](DSP::clockFollower& f) { f.ppq(ticks); });
}
YSE_C_API unsigned int yse_dsp_clock_follower_get_ppq(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0u, [](DSP::clockFollower& f) { return f.ppq(); });
}
YSE_C_API void yse_dsp_clock_follower_start(YseDspProcessor* p) {
  with<ClockFollower>(p, [](DSP::clockFollower& f) { f.start(); });
}
YSE_C_API void yse_dsp_clock_follower_stop(YseDspProcessor* p) {
  with<ClockFollower>(p, [](DSP::clockFollower& f) { f.stop(); });
}
YSE_C_API int yse_dsp_clock_follower_is_running(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0, [](DSP::clockFollower& f) { return f.running() ? 1 : 0; });
}
YSE_C_API void yse_dsp_clock_follower_tap(YseDspProcessor* p) {
  with<ClockFollower>(p, [](DSP::clockFollower& f) { f.tap(); });
}
YSE_C_API void yse_dsp_clock_follower_midi(YseDspProcessor* p, int status) {
  with<ClockFollower>(p, [&](DSP::clockFollower& f) { f.midi(status); });
}
YSE_C_API int yse_dsp_clock_follower_is_following(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0, [](DSP::clockFollower& f) { return f.following() ? 1 : 0; });
}
YSE_C_API float yse_dsp_clock_follower_get_phase(YseDspProcessor* p) {
  return query<ClockFollower>(p, 0.f, [](DSP::clockFollower& f) { return f.phase(); });
}
YSE_C_API unsigned int yse_dsp_clock_follower_get_taps(YseDspProcessor* p) {
  ClockFollower* h = yse_c::processor::get<ClockFollower>(p);
  return h != nullptr ? h->state.taps : 0u;
}
YSE_C_API unsigned int yse_dsp_clock_follower_get_clicks(YseDspProcessor* p) {
  ClockFollower* h = yse_c::processor::get<ClockFollower>(p);
  return h != nullptr ? h->state.clicks : 0u;
}
YSE_C_API unsigned int yse_dsp_clock_follower_get_ticks(YseDspProcessor* p) {
  ClockFollower* h = yse_c::processor::get<ClockFollower>(p);
  return h != nullptr ? h->state.ticks : 0u;
}
YSE_C_API int yse_dsp_clock_follower_get_tempo_changed(YseDspProcessor* p) {
  ClockFollower* h = yse_c::processor::get<ClockFollower>(p);
  return h != nullptr && h->state.tempoChanged ? 1 : 0;
}
YSE_C_API unsigned int yse_dsp_clock_follower_get_clock_bytes(YseDspProcessor* p) {
  ClockFollower* h = yse_c::processor::get<ClockFollower>(p);
  return h != nullptr ? h->state.bytes : 0u;
}
YSE_C_API unsigned int yse_dsp_clock_follower_process(YseDspProcessor* p, float* out,
                                                      YseDspClockByte* clock,
                                                      unsigned int clock_capacity,
                                                      unsigned int length) {
  return follower_blocks(
      p, out, clock, clock_capacity, length,
      [](ClockFollower& h, unsigned, unsigned n) -> DSP::buffer& { return h.object(n); });
}
YSE_C_API unsigned int yse_dsp_clock_follower_process_click(YseDspProcessor* p, const float* click,
                                                            float* out, YseDspClockByte* clock,
                                                            unsigned int clock_capacity,
                                                            unsigned int length) {
  if (click == nullptr) return 0;
  return follower_blocks(p, out, clock, clock_capacity, length,
                         [&](ClockFollower& h, unsigned o, unsigned n) -> DSP::buffer& {
                           return h.object(stage(h.scratch[0], click + o, n));
                         });
}
YSE_C_API unsigned int yse_dsp_clock_follower_process_span(YseDspProcessor* p,
                                                           YseDspProcessor* span, float* out,
                                                           unsigned int capacity,
                                                           YseDspClockByte* clock,
                                                           unsigned int clock_capacity) {
  if (clock == nullptr) clock_capacity = 0;
  return follow<ClockFollower>(p, span, out, capacity,
                               [&](ClockFollower& h, const DSP::beatSpan& s) -> DSP::buffer& {
                                 h.state = clockReports{};
                                 DSP::buffer& result = h.object(s);
                                 gather(h, 0, clock, clock_capacity);
                                 return result;
                               });
}

} // extern "C"
