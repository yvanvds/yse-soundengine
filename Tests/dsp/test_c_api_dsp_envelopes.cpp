// C-API tests for the envelopes, ramps and sequencers of the MSP port (issue
// #1298): yse_c/yse_dsp_envelopes.h (adsr, curveSegments, lineSegments,
// zigzag, techno, eventSequencer, updown, shapeEnvelope, triggeredRamp), on
// the shared YseDspProcessor handle of #1173.
//
// The DSP itself is covered by each class's own suite (test_adsr.cpp, ...).
// What these cases pin is the bridge:
//
//   * Ownership — every constructor pairs with yse_dsp_processor_destroy, and
//     destroy frees the size that was allocated.
//   * The class check — a handle of another class (another envelope, or a
//     filter) is treated like NULL, and the real class's state is untouched.
//   * NULL handles and NULL arrays — 0 / no-op, and nothing is written.
//   * Round-trips — every setter / getter pair, the list setters and the
//     stored-entry getters, and enum setters ignoring a value out of range.
//   * Parity — every process entry point produces exactly what the engine
//     class produces when driven directly in 128-sample blocks, for a length
//     that is not a multiple of 128, side outputs (gate, activity, index,
//     envelope, step position) included.
//   * Polled events — started / finished / ends and the sequencer's fired
//     list cover the whole process call, not only its last engine block, and
//     a refused call leaves them as they were.
//   * Real-time safety — process, setter and list calls allocate nothing.
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

#include "yse_c/yse_dsp_envelopes.h"
#include "yse_c/yse_dsp_filters.h"
#include "yse_c/yse_dsp_processor.h"

#include "dsp/buffer.hpp"
#include "dsp/generators/adsr.hpp"
#include "dsp/generators/curveSegments.hpp"
#include "dsp/generators/eventSequencer.hpp"
#include "dsp/generators/lineSegments.hpp"
#include "dsp/generators/shapeEnvelope.hpp"
#include "dsp/generators/techno.hpp"
#include "dsp/generators/triggeredRamp.hpp"
#include "dsp/generators/updown.hpp"
#include "dsp/generators/zigzag.hpp"
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

  // Drives an engine object the way the bridge must: consecutive blocks of
  // at most BLOCK samples, `body(offset, count)` per block.
  template <typename F> void Blocks(unsigned length, F&& body) {
    for (unsigned offset = 0; offset < length; offset += BLOCK)
      body(offset, std::min(BLOCK, length - offset));
  }

  DSP::buffer& Stage(DSP::buffer& b, const std::vector<float>& src, unsigned offset, unsigned n) {
    b.resize(n);
    std::memcpy(b.getPtr(), src.data() + offset, sizeof(float) * n);
    return b;
  }

  // Copy an engine block into the reference run at `offset`.
  void Put(DSP::buffer& b, std::vector<float>& dst, unsigned offset, unsigned n) {
    REQUIRE(b.getLength() == n);
    std::memcpy(dst.data() + offset, b.getPtr(), sizeof(float) * n);
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

  // A signal that is not constant: a parity check against a flat line proves
  // little.
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
        {"adsr", [] { return yse_dsp_adsr_create(); }},
        {"curve_segments", [] { return yse_dsp_curve_segments_create(0); }},
        {"line_segments", [] { return yse_dsp_line_segments_create(0); }},
        {"zigzag", [] { return yse_dsp_zigzag_create(0); }},
        {"techno", [] { return yse_dsp_techno_create(0); }},
        {"event_sequencer", [] { return yse_dsp_event_sequencer_create(0, 0); }},
        {"updown", [] { return yse_dsp_updown_create(); }},
        {"shape_envelope", [] { return yse_dsp_shape_envelope_create(0); }},
        {"triggered_ramp", [] { return yse_dsp_triggered_ramp_create(); }},
    };
  }

  // The settings the adsr cases share: a short envelope at 48 kHz.
  void SetAdsr(YseDspProcessor* p) {
    yse_dsp_adsr_set_attack(p, 1.f);
    yse_dsp_adsr_set_decay(p, 0.5f);
    yse_dsp_adsr_set_sustain(p, 0.5f);
    yse_dsp_adsr_set_release(p, 1.f);
  }
  void SetAdsr(DSP::adsr& a) {
    a.attack(1.f).decay(0.5f).sustain(0.5f).release(1.f);
  }

  // A sequencer with four events in sequence 0 and one in sequence 1.
  const char* const TEXTS[] = {"one", "two 2", "three", "four 4 4"};
  const float PHASES[] = {0.1f, 0.25f, 0.5f, 0.9f};
  void Fill(YseDspProcessor* p) {
    for (unsigned i = 0; i < 4; ++i)
      REQUIRE(yse_dsp_event_sequencer_add(p, 0, PHASES[i], TEXTS[i],
                                          static_cast<unsigned>(std::strlen(TEXTS[i]))) == 1);
    REQUIRE(yse_dsp_event_sequencer_add(p, 1, 0.3f, "other", 5) == 1);
  }
  void Fill(DSP::eventSequencer& s) {
    for (unsigned i = 0; i < 4; ++i)
      REQUIRE(s.add(0, PHASES[i], TEXTS[i], static_cast<unsigned>(std::strlen(TEXTS[i]))));
    REQUIRE(s.add(1, 0.3f, "other", 5));
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("c api envelopes: every constructor pairs with destroy and frees what it allocated") {
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

  TEST_CASE("c api envelopes: a handle of another class is refused like NULL") {
    YseDspProcessor* ad = yse_dsp_adsr_create();
    REQUIRE(ad != nullptr);
    SetAdsr(ad);
    yse_dsp_adsr_set_legato(ad, 1);
    YseDspProcessor* lp = yse_dsp_lores_create();
    REQUIRE(lp != nullptr);

    const std::vector<float> in = Ramp(LENGTH, 61.f);
    const float list[3] = {1.f, 0.f, 0.5f};
    std::vector<float> out(LENGTH, 7.f);
    std::vector<float> extra(LENGTH, 7.f);
    YseDspEventFiring fired[2];
    std::memset(fired, 0x5A, sizeof fired);
    char text[8] = "keep";

    for (YseDspProcessor* wrong : {ad, lp}) {
      // Setters and list edits of every other class do nothing ...
      yse_dsp_curve_segments_jump(wrong, 0.5f);
      yse_dsp_line_segments_set_target(wrong, 1.f, 10.f);
      yse_dsp_zigzag_set_speed(wrong, 2.f);
      yse_dsp_techno_set_pitch(wrong, 0, 100.f);
      yse_dsp_event_sequencer_set_play(wrong, 1);
      yse_dsp_updown_set_up(wrong, 3.f);
      yse_dsp_shape_envelope_set_curved(wrong, 1);
      yse_dsp_triggered_ramp_set_duration(wrong, 3.f);
      CHECK(yse_dsp_curve_segments_set(wrong, list, list, nullptr, 3) == 0);
      CHECK(yse_dsp_line_segments_set(wrong, list, list, 3) == 0);
      CHECK(yse_dsp_zigzag_set_line(wrong, list, list, 3) == 0);
      CHECK(yse_dsp_zigzag_append(wrong, 1.f, 1.f, 1) == 0);
      CHECK(yse_dsp_techno_repeat_positions(wrong, nullptr, 0) == 0);
      CHECK(yse_dsp_event_sequencer_add(wrong, 0, 0.5f, "x", 1) == 0);
      CHECK(yse_dsp_shape_envelope_set_breakpoints(wrong, 0.f, list, list, nullptr, 3) == 0);
      // ... getters return 0 ...
      CHECK(yse_dsp_curve_segments_get_capacity(wrong) == 0);
      CHECK(yse_dsp_line_segments_get_finished(wrong) == 0);
      CHECK(yse_dsp_zigzag_get_ends(wrong) == 0);
      CHECK(yse_dsp_zigzag_get_mode(wrong) == YSE_DSP_ZIGZAG_START);
      CHECK(yse_dsp_techno_get_length(wrong) == 0);
      CHECK(yse_dsp_event_sequencer_get_capacity(wrong) == 0);
      CHECK(yse_dsp_event_sequencer_get_fired(wrong) == 0);
      CHECK(yse_dsp_event_sequencer_get_event_text(wrong, 0, text, sizeof text) == 0);
      CHECK(text[0] == '\0');
      std::strcpy(text, "keep");
      CHECK(yse_dsp_updown_get_level(wrong) == 0.f);
      CHECK(yse_dsp_shape_envelope_get_capacity(wrong) == 0);
      CHECK(yse_dsp_triggered_ramp_get_end(wrong) == 0.f);
      CHECK(yse_dsp_triggered_ramp_get_mode(wrong) == YSE_DSP_RAMP_DURATION_AT_START);
      // ... and process calls return 0 and write nothing.
      CHECK(yse_dsp_curve_segments_process(wrong, out.data(), LENGTH) == 0);
      CHECK(yse_dsp_line_segments_process(wrong, out.data(), extra.data(), LENGTH) == 0);
      CHECK(yse_dsp_zigzag_process_signal(wrong, in.data(), in.data(), out.data(), extra.data(),
                                          LENGTH) == 0);
      CHECK(yse_dsp_techno_process(wrong, in.data(), out.data(), extra.data(), extra.data(),
                                   LENGTH) == 0);
      CHECK(yse_dsp_event_sequencer_process(wrong, in.data(), fired, 2, LENGTH) == 0);
      CHECK(yse_dsp_updown_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_shape_envelope_process(wrong, in.data(), out.data(), LENGTH) == 0);
      CHECK(yse_dsp_triggered_ramp_process_signal(wrong, in.data(), in.data(), in.data(), in.data(),
                                                  out.data(), LENGTH) == 0);
    }
    // The adsr's own functions refuse the filter too.
    yse_dsp_adsr_set_attack(lp, 9.f);
    yse_dsp_adsr_trigger(lp, 1.f);
    CHECK(yse_dsp_adsr_get_attack(lp) == 0.f);
    CHECK(yse_dsp_adsr_get_stage(lp) == YSE_DSP_ADSR_IDLE);
    CHECK(yse_dsp_adsr_process(lp, out.data(), extra.data(), LENGTH) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);
    for (float v : extra)
      REQUIRE(v == 7.f);
    for (const YseDspEventFiring& f : fired)
      REQUIRE(f.offset == 0x5A5A5A5Au);

    // The adsr's own state is what it was.
    CHECK(yse_dsp_adsr_get_attack(ad) == 1.f);
    CHECK(yse_dsp_adsr_get_sustain(ad) == 0.5f);
    CHECK(yse_dsp_adsr_get_legato(ad) == 1);
    CHECK(yse_dsp_adsr_is_active(ad) == 0);
    CHECK(yse_dsp_adsr_process(ad, out.data(), nullptr, LENGTH) == LENGTH);
    yse_dsp_processor_destroy(ad);
    yse_dsp_processor_destroy(lp);
  }

  TEST_CASE("c api envelopes: NULL handles and NULL arrays are no-ops that write nothing") {
    const std::vector<float> in = Ramp(LENGTH, 61.f);
    const float list[2] = {1.f, 0.f};
    std::vector<float> out(LENGTH, 7.f);
    char text[8] = "keep";

    // NULL handle.
    yse_dsp_adsr_trigger(nullptr, 1.f);
    yse_dsp_zigzag_play(nullptr);
    yse_dsp_event_sequencer_clear_all(nullptr);
    yse_dsp_updown_restart(nullptr);
    CHECK(yse_dsp_adsr_get_started(nullptr) == 0);
    CHECK(yse_dsp_adsr_get_level(nullptr) == 0.f);
    CHECK(yse_dsp_curve_segments_get_shape(nullptr) == YSE_DSP_CURVE_POWER);
    CHECK(yse_dsp_curve_segments_get_finished(nullptr) == 0);
    CHECK(yse_dsp_line_segments_set(nullptr, list, list, 2) == 0);
    CHECK(yse_dsp_zigzag_get_point_value(nullptr, 0) == 0.f);
    CHECK(yse_dsp_zigzag_get_finished(nullptr) == 0);
    CHECK(yse_dsp_techno_get_pitch(nullptr, 0) == 0.f);
    CHECK(yse_dsp_event_sequencer_get_missed(nullptr) == 0);
    CHECK(yse_dsp_event_sequencer_get_event_length(nullptr, 0) == 0);
    CHECK(yse_dsp_event_sequencer_get_event_text(nullptr, 0, text, sizeof text) == 0);
    CHECK(text[0] == '\0');
    CHECK(yse_dsp_event_sequencer_get_event_text(nullptr, 0, nullptr, 0) == 0);
    CHECK(yse_dsp_shape_envelope_get_value(nullptr, 0) == 0.f);
    CHECK(yse_dsp_triggered_ramp_is_running(nullptr) == 0);
    CHECK(yse_dsp_adsr_process_gate(nullptr, in.data(), out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_zigzag_process(nullptr, out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_event_sequencer_process(nullptr, in.data(), nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_triggered_ramp_process(nullptr, in.data(), out.data(), LENGTH) == 0);
    yse_dsp_processor_destroy(nullptr);

    // A live handle with a NULL required array.
    auto* ad = yse_dsp_adsr_create();
    auto* cs = yse_dsp_curve_segments_create(0);
    auto* ls = yse_dsp_line_segments_create(0);
    auto* zz = yse_dsp_zigzag_create(0);
    auto* te = yse_dsp_techno_create(0);
    auto* es = yse_dsp_event_sequencer_create(0, 0);
    auto* ud = yse_dsp_updown_create();
    auto* se = yse_dsp_shape_envelope_create(0);
    auto* tr = yse_dsp_triggered_ramp_create();
    CHECK(yse_dsp_adsr_process(ad, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_adsr_process_gate(ad, nullptr, out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_adsr_process_signal(ad, in.data(), in.data(), nullptr, in.data(), out.data(),
                                      nullptr, LENGTH) == 0);
    CHECK(yse_dsp_adsr_process_gate_signal(ad, in.data(), in.data(), in.data(), in.data(), nullptr,
                                           out.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_curve_segments_process(cs, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_line_segments_process(ls, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_zigzag_process_signal(zz, in.data(), in.data(), nullptr, out.data(), LENGTH) ==
          0);
    CHECK(yse_dsp_techno_process(te, nullptr, out.data(), nullptr, nullptr, LENGTH) == 0);
    CHECK(yse_dsp_techno_process(te, in.data(), nullptr, out.data(), out.data(), LENGTH) == 0);
    CHECK(yse_dsp_event_sequencer_process(es, nullptr, nullptr, 0, LENGTH) == 0);
    CHECK(yse_dsp_updown_process(ud, nullptr, out.data(), LENGTH) == 0);
    CHECK(yse_dsp_shape_envelope_process(se, in.data(), nullptr, LENGTH) == 0);
    CHECK(yse_dsp_triggered_ramp_process_signal(tr, in.data(), in.data(), nullptr, in.data(),
                                                out.data(), LENGTH) == 0);
    for (float v : out)
      REQUIRE(v == 7.f);

    // NULL list arrays change nothing.
    CHECK(yse_dsp_curve_segments_set(cs, nullptr, list, nullptr, 2) == 0);
    CHECK(yse_dsp_curve_segments_set(cs, list, nullptr, nullptr, 2) == 0);
    CHECK(yse_dsp_curve_segments_get_remaining(cs) == 0);
    CHECK(yse_dsp_line_segments_set(ls, list, nullptr, 2) == 0);
    CHECK(yse_dsp_zigzag_set_line(zz, nullptr, list, 2) == 0);
    CHECK(yse_dsp_zigzag_get_size(zz) == 0);
    CHECK(yse_dsp_techno_repeat_positions(te, nullptr, 1) == 0);
    CHECK(yse_dsp_shape_envelope_set_breakpoints(se, 0.5f, nullptr, list, nullptr, 2) == 0);
    CHECK(yse_dsp_shape_envelope_get_initial(se) == 0.f);
    CHECK(yse_dsp_event_sequencer_add(es, 0, 0.5f, nullptr, 3) == 0);
    CHECK(yse_dsp_event_sequencer_record_event(es, nullptr, 3) == 0);
    CHECK(yse_dsp_event_sequencer_get_size(es) == 0);

    // The optional arrays may be NULL: the call still runs.
    CHECK(yse_dsp_adsr_process(ad, out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_line_segments_process(ls, out.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_zigzag_process_signal(zz, nullptr, nullptr, out.data(), nullptr, LENGTH) ==
          LENGTH);
    CHECK(yse_dsp_techno_process(te, in.data(), out.data(), nullptr, nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_event_sequencer_process(es, in.data(), nullptr, 0, LENGTH) == LENGTH);
    // A NULL fired array with a capacity is read as a capacity of 0.
    CHECK(yse_dsp_event_sequencer_process(es, in.data(), nullptr, 8, LENGTH) == LENGTH);
    CHECK(yse_dsp_curve_segments_set(cs, list, list, nullptr, 2) == 2);
    CHECK(yse_dsp_shape_envelope_set_breakpoints(se, 0.5f, nullptr, nullptr, nullptr, 0) == 1);
    CHECK(yse_dsp_shape_envelope_get_initial(se) == 0.5f);
    CHECK(yse_dsp_techno_repeat_positions(te, nullptr, 0) == 1);
    // A zero length is a valid, empty call.
    CHECK(yse_dsp_updown_process(ud, in.data(), out.data(), 0) == 0);

    for (YseDspProcessor* p : {ad, cs, ls, zz, te, es, ud, se, tr})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api envelopes: setters and getters round-trip") {
    SUBCASE("adsr") {
      auto* p = yse_dsp_adsr_create();
      CHECK(yse_dsp_adsr_get_attack(p) == 0.f);
      CHECK(yse_dsp_adsr_get_sustain(p) == 1.f);
      CHECK(yse_dsp_adsr_get_retrigger(p) == 5.f);
      CHECK(yse_dsp_adsr_get_max_sustain(p) == -1.f);
      SetAdsr(p);
      yse_dsp_adsr_set_retrigger(p, 2.f);
      yse_dsp_adsr_set_max_sustain(p, 30.f);
      yse_dsp_adsr_set_legato(p, 1);
      yse_dsp_adsr_set_play_to_end(p, 1);
      CHECK(yse_dsp_adsr_get_attack(p) == 1.f);
      CHECK(yse_dsp_adsr_get_decay(p) == 0.5f);
      CHECK(yse_dsp_adsr_get_sustain(p) == 0.5f);
      CHECK(yse_dsp_adsr_get_release(p) == 1.f);
      CHECK(yse_dsp_adsr_get_retrigger(p) == 2.f);
      CHECK(yse_dsp_adsr_get_max_sustain(p) == 30.f);
      CHECK(yse_dsp_adsr_get_legato(p) == 1);
      CHECK(yse_dsp_adsr_get_play_to_end(p) == 1);
      CHECK(yse_dsp_adsr_get_stage(p) == YSE_DSP_ADSR_IDLE);
      std::vector<float> out(16);
      yse_dsp_adsr_trigger(p, 1.f);
      yse_dsp_adsr_process(p, out.data(), nullptr, 16);
      CHECK(yse_dsp_adsr_get_stage(p) == YSE_DSP_ADSR_ATTACK);
      CHECK(yse_dsp_adsr_is_active(p) == 1);
      CHECK(yse_dsp_adsr_get_level(p) == out[15]);
      yse_dsp_adsr_reset(p);
      CHECK(yse_dsp_adsr_is_active(p) == 0);
      CHECK(yse_dsp_adsr_get_attack(p) == 1.f);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("curveSegments") {
      auto* p = yse_dsp_curve_segments_create(4);
      CHECK(yse_dsp_curve_segments_get_capacity(p) == 4);
      CHECK(yse_dsp_curve_segments_get_shape(p) == YSE_DSP_CURVE_POWER);
      yse_dsp_curve_segments_set_shape(p, YSE_DSP_CURVE_EXPONENTIAL);
      CHECK(yse_dsp_curve_segments_get_shape(p) == YSE_DSP_CURVE_EXPONENTIAL);
      yse_dsp_curve_segments_set_shape(p, static_cast<YseDspCurveShape>(7));
      CHECK(yse_dsp_curve_segments_get_shape(p) == YSE_DSP_CURVE_EXPONENTIAL);
      const float targets[6] = {1.f, 0.f, 1.f, 0.f, 1.f, 0.f};
      const float times[6] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
      // A list past the capacity is cut to it.
      CHECK(yse_dsp_curve_segments_set(p, targets, times, nullptr, 6) == 4);
      CHECK(yse_dsp_curve_segments_get_remaining(p) == 4);
      CHECK(yse_dsp_curve_segments_is_active(p) == 1);
      yse_dsp_curve_segments_pause(p);
      CHECK(yse_dsp_curve_segments_is_paused(p) == 1);
      CHECK(yse_dsp_curve_segments_is_active(p) == 0);
      yse_dsp_curve_segments_resume(p);
      CHECK(yse_dsp_curve_segments_is_active(p) == 1);
      yse_dsp_curve_segments_stop(p);
      CHECK(yse_dsp_curve_segments_get_remaining(p) == 0);
      yse_dsp_curve_segments_jump(p, 0.75f);
      CHECK(yse_dsp_curve_segments_get_value(p) == 0.75f);
      yse_dsp_curve_segments_reset(p);
      CHECK(yse_dsp_curve_segments_get_value(p) == 0.f);
      CHECK(yse_dsp_curve_segments_get_shape(p) == YSE_DSP_CURVE_EXPONENTIAL);
      yse_dsp_processor_destroy(p);
      // A capacity of 0 is the default.
      auto* d = yse_dsp_curve_segments_create(0);
      CHECK(yse_dsp_curve_segments_get_capacity(d) == YSE_DSP_CURVE_SEGMENTS_DEFAULT_POINTS);
      yse_dsp_processor_destroy(d);
    }
    SUBCASE("lineSegments") {
      auto* p = yse_dsp_line_segments_create(0);
      CHECK(yse_dsp_line_segments_get_capacity(p) == YSE_DSP_LINE_SEGMENTS_DEFAULT_POINTS);
      const float targets[2] = {1.f, 0.f};
      const float times[2] = {1.f, 1.f};
      CHECK(yse_dsp_line_segments_set(p, targets, times, 2) == 2);
      CHECK(yse_dsp_line_segments_get_remaining(p) == 2);
      yse_dsp_line_segments_pause(p);
      CHECK(yse_dsp_line_segments_is_paused(p) == 1);
      yse_dsp_line_segments_resume(p);
      CHECK(yse_dsp_line_segments_is_active(p) == 1);
      yse_dsp_line_segments_stop(p);
      CHECK(yse_dsp_line_segments_is_active(p) == 0);
      yse_dsp_line_segments_jump(p, 0.25f);
      CHECK(yse_dsp_line_segments_get_value(p) == 0.25f);
      yse_dsp_line_segments_set_target(p, 0.5f, 0.f);
      CHECK(yse_dsp_line_segments_get_value(p) == 0.5f);
      yse_dsp_line_segments_reset(p);
      CHECK(yse_dsp_line_segments_get_value(p) == 0.f);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("zigzag") {
      auto* p = yse_dsp_zigzag_create(4);
      CHECK(yse_dsp_zigzag_get_capacity(p) == 4);
      const float values[3] = {0.f, 1.f, 0.5f};
      const float deltas[3] = {0.f, 2.f, 3.f};
      CHECK(yse_dsp_zigzag_set_line(p, values, deltas, 3) == 3);
      CHECK(yse_dsp_zigzag_get_size(p) == 3);
      CHECK(yse_dsp_zigzag_get_point_value(p, 1) == 1.f);
      CHECK(yse_dsp_zigzag_get_point_delta(p, 2) == 3.f);
      CHECK(yse_dsp_zigzag_get_point_value(p, 9) == 0.f);
      CHECK(yse_dsp_zigzag_append(p, -1.f, 1.f, 0) == 1);
      CHECK(yse_dsp_zigzag_append(p, -1.f, 1.f, 0) == 0); // full
      CHECK(yse_dsp_zigzag_remove(p, 0) == 1);
      CHECK(yse_dsp_zigzag_remove(p, 9) == 0);
      CHECK(yse_dsp_zigzag_insert(p, 0, 0.25f, 4.f) == 1);
      CHECK(yse_dsp_zigzag_get_point_value(p, 0) == 0.25f);
      CHECK(yse_dsp_zigzag_set_point(p, 3, 0.125f, 6.f) == 1);
      CHECK(yse_dsp_zigzag_get_point_delta(p, 3) == 6.f);
      CHECK(yse_dsp_zigzag_set_point(p, 4, 0.f, 0.f) == 0);
      yse_dsp_zigzag_set_bang_delta(p, 7.f);
      CHECK(yse_dsp_zigzag_get_bang_delta(p) == 7.f);
      yse_dsp_zigzag_set_speed(p, -0.5f);
      CHECK(yse_dsp_zigzag_get_speed(p) == -0.5f);
      yse_dsp_zigzag_set_loop_mode(p, YSE_DSP_ZIGZAG_LOOP_PENDULUM);
      CHECK(yse_dsp_zigzag_get_loop_mode(p) == YSE_DSP_ZIGZAG_LOOP_PENDULUM);
      yse_dsp_zigzag_set_loop_mode(p, static_cast<YseDspZigzagLoop>(-1));
      CHECK(yse_dsp_zigzag_get_loop_mode(p) == YSE_DSP_ZIGZAG_LOOP_PENDULUM);
      yse_dsp_zigzag_set_mode(p, YSE_DSP_ZIGZAG_STEP);
      CHECK(yse_dsp_zigzag_get_mode(p) == YSE_DSP_ZIGZAG_STEP);
      yse_dsp_zigzag_set_mode(p, static_cast<YseDspZigzagMode>(3));
      CHECK(yse_dsp_zigzag_get_mode(p) == YSE_DSP_ZIGZAG_STEP);
      yse_dsp_zigzag_jump(p, 2, 0.f);
      CHECK(yse_dsp_zigzag_get_index(p) == 2);
      CHECK(yse_dsp_zigzag_is_active(p) == 1);
      yse_dsp_zigzag_stop(p);
      CHECK(yse_dsp_zigzag_is_active(p) == 0);
      yse_dsp_zigzag_hold(p, 0.3f);
      CHECK(yse_dsp_zigzag_get_value(p) == 0.3f);
      yse_dsp_zigzag_clear(p);
      CHECK(yse_dsp_zigzag_get_size(p) == 0);
      CHECK(yse_dsp_zigzag_line(p, values, deltas, 3) == 3);
      CHECK(yse_dsp_zigzag_is_active(p) == 1);
      yse_dsp_zigzag_reset(p);
      CHECK(yse_dsp_zigzag_get_size(p) == 0);
      CHECK(yse_dsp_zigzag_get_speed(p) == 1.f);
      CHECK(yse_dsp_zigzag_get_capacity(p) == 4);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("techno") {
      auto* p = yse_dsp_techno_create(8);
      CHECK(yse_dsp_techno_get_capacity(p) == 8);
      CHECK(yse_dsp_techno_get_length(p) == 1);
      yse_dsp_techno_set_length(p, 4);
      CHECK(yse_dsp_techno_get_length(p) == 4);
      CHECK(yse_dsp_techno_get_position(p, 2) == 0.5f);
      CHECK(yse_dsp_techno_get_pitch(p, 3) == 440.f);
      yse_dsp_techno_set_pitch(p, 3, 220.f);
      yse_dsp_techno_set_amplitude(p, 3, 0.5f);
      yse_dsp_techno_set_curve(p, 3, 2.f);
      yse_dsp_techno_set_attack(p, 3, 3.f);
      yse_dsp_techno_set_decay(p, 3, 4.f);
      yse_dsp_techno_set_position(p, 1, 0.2f);
      CHECK(yse_dsp_techno_get_pitch(p, 3) == 220.f);
      CHECK(yse_dsp_techno_get_amplitude(p, 3) == 0.5f);
      CHECK(yse_dsp_techno_get_curve(p, 3) == 2.f);
      CHECK(yse_dsp_techno_get_attack(p, 3) == 3.f);
      CHECK(yse_dsp_techno_get_decay(p, 3) == 4.f);
      CHECK(yse_dsp_techno_get_position(p, 1) == 0.2f);
      // Past the capacity: ignored, read as 0.
      yse_dsp_techno_set_pitch(p, 8, 1.f);
      CHECK(yse_dsp_techno_get_pitch(p, 8) == 0.f);
      const float swing[1] = {0.66f};
      CHECK(yse_dsp_techno_repeat_positions(p, swing, 1) == 1);
      CHECK(yse_dsp_techno_get_position(p, 1) == doctest::Approx(0.33f));
      const float bad[1] = {1.5f};
      CHECK(yse_dsp_techno_repeat_positions(p, bad, 1) == 0);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("eventSequencer") {
      auto* p = yse_dsp_event_sequencer_create(8, 4);
      CHECK(yse_dsp_event_sequencer_get_capacity(p) == 8);
      CHECK(yse_dsp_event_sequencer_get_fired_capacity(p) == 4);
      Fill(p);
      CHECK(yse_dsp_event_sequencer_get_size(p) == 5);
      CHECK(yse_dsp_event_sequencer_get_count(p, 0) == 4);
      CHECK(yse_dsp_event_sequencer_get_count(p, 1) == 1);
      // Sorted by sequence, then phase.
      CHECK(yse_dsp_event_sequencer_get_event_sequence(p, 4) == 1);
      CHECK(yse_dsp_event_sequencer_get_event_phase(p, 2) == 0.5f);
      CHECK(yse_dsp_event_sequencer_get_event_length(p, 3) == 8);
      char text[32];
      CHECK(yse_dsp_event_sequencer_get_event_text(p, 3, text, sizeof text) == 8);
      CHECK(std::string(text) == "four 4 4");
      // snprintf style: NULL and 0 give the length, a short buffer is cut and
      // terminated, past the end is "".
      CHECK(yse_dsp_event_sequencer_get_event_text(p, 3, nullptr, 0) == 8);
      CHECK(yse_dsp_event_sequencer_get_event_text(p, 3, text, 5) == 8);
      CHECK(std::string(text) == "four");
      CHECK(yse_dsp_event_sequencer_get_event_text(p, 9, text, sizeof text) == 0);
      CHECK(text[0] == '\0');
      // add's refusals.
      CHECK(yse_dsp_event_sequencer_add(p, 0, 0.5f, "a\nb", 3) == 0);
      CHECK(yse_dsp_event_sequencer_add(p, 0, NAN, "x", 1) == 0);
      const std::string longText(YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY, 'x');
      CHECK(yse_dsp_event_sequencer_add(p, 0, 0.5f, longText.c_str(),
                                        YSE_DSP_EVENT_SEQUENCER_TEXT_CAPACITY) == 0);
      // remove by range and by text.
      CHECK(yse_dsp_event_sequencer_remove(p, 0, 0.2f, 0.6f, "three", 5) == 1);
      CHECK(yse_dsp_event_sequencer_remove(p, 0, 0.6f, 0.0f, nullptr, 99) == 2);
      CHECK(yse_dsp_event_sequencer_get_count(p, 0) == 1);
      CHECK(yse_dsp_event_sequencer_clear(p, 1) == 1);
      // Transport.
      yse_dsp_event_sequencer_select(p, 3);
      CHECK(yse_dsp_event_sequencer_get_selected(p) == 3);
      yse_dsp_event_sequencer_set_play(p, 1);
      CHECK(yse_dsp_event_sequencer_is_playing(p) == 1);
      CHECK(yse_dsp_event_sequencer_record_event(p, "r", 1) == 0); // not recording
      yse_dsp_event_sequencer_set_overdub(p, 1);
      CHECK(yse_dsp_event_sequencer_is_overdubbing(p) == 1);
      CHECK(yse_dsp_event_sequencer_record_event(p, "r", 1) == 1);
      CHECK(yse_dsp_event_sequencer_get_count(p, 3) == 1);
      yse_dsp_event_sequencer_set_record(p, 1);
      CHECK(yse_dsp_event_sequencer_is_recording(p) == 1);
      CHECK(yse_dsp_event_sequencer_get_count(p, 3) == 0); // record empties it
      const std::vector<float> phase(10, 0.4f);
      yse_dsp_event_sequencer_process(p, phase.data(), nullptr, 0, 10);
      CHECK(yse_dsp_event_sequencer_get_phase(p) == 0.4f);
      yse_dsp_event_sequencer_clear_all(p);
      CHECK(yse_dsp_event_sequencer_get_size(p) == 0);
      yse_dsp_event_sequencer_reset(p);
      CHECK(yse_dsp_event_sequencer_is_playing(p) == 0);
      CHECK(yse_dsp_event_sequencer_get_phase(p) == 0.f);
      CHECK(yse_dsp_event_sequencer_get_capacity(p) == 8);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("updown") {
      auto* p = yse_dsp_updown_create();
      CHECK(yse_dsp_updown_get_up(p) == 10.f);
      CHECK(yse_dsp_updown_get_down(p) == 10.f);
      CHECK(yse_dsp_updown_get_level(p) == 1.f);
      CHECK(yse_dsp_updown_get_reset_threshold(p) == 0.5f);
      yse_dsp_updown_set_up(p, 2.f);
      yse_dsp_updown_set_down(p, 3.f);
      yse_dsp_updown_set_level(p, -0.5f);
      yse_dsp_updown_set_reset_threshold(p, 0.25f);
      CHECK(yse_dsp_updown_get_up(p) == 2.f);
      CHECK(yse_dsp_updown_get_down(p) == 3.f);
      CHECK(yse_dsp_updown_get_level(p) == -0.5f);
      CHECK(yse_dsp_updown_get_reset_threshold(p) == 0.25f);
      const std::vector<float> gate(64, 1.f);
      std::vector<float> out(64);
      yse_dsp_updown_process(p, gate.data(), out.data(), 64);
      CHECK(yse_dsp_updown_get_envelope(p) > 0.f);
      yse_dsp_updown_restart(p);
      CHECK(yse_dsp_updown_get_envelope(p) == 0.f);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("shapeEnvelope") {
      auto* p = yse_dsp_shape_envelope_create(4);
      CHECK(yse_dsp_shape_envelope_get_capacity(p) == 4);
      const float values[3] = {1.f, 0.3f, 0.f};
      const float times[3] = {10.f, 20.f, 30.f};
      const float curves[3] = {0.5f, 2.f, -0.5f};
      CHECK(yse_dsp_shape_envelope_set_breakpoints(p, 0.1f, values, times, curves, 3) == 1);
      CHECK(yse_dsp_shape_envelope_get_count(p) == 3);
      CHECK(yse_dsp_shape_envelope_get_initial(p) == 0.1f);
      CHECK(yse_dsp_shape_envelope_get_value(p, 1) == 0.3f);
      CHECK(yse_dsp_shape_envelope_get_time(p, 2) == 30.f);
      CHECK(yse_dsp_shape_envelope_get_curve(p, 1) == 1.f); // clamped
      CHECK(yse_dsp_shape_envelope_get_curve(p, 3) == 0.f);
      // Past the capacity: refused.
      const float five[5] = {};
      CHECK(yse_dsp_shape_envelope_set_breakpoints(p, 0.f, five, five, nullptr, 5) == 0);
      CHECK(yse_dsp_shape_envelope_get_count(p) == 3);
      yse_dsp_shape_envelope_set_constant(p, 1);
      yse_dsp_shape_envelope_set_curved(p, 1);
      yse_dsp_shape_envelope_set_sync_update(p, 1);
      CHECK(yse_dsp_shape_envelope_get_constant(p) == 1);
      CHECK(yse_dsp_shape_envelope_get_curved(p) == 1);
      CHECK(yse_dsp_shape_envelope_get_sync_update(p) == 1);
      CHECK(yse_dsp_shape_envelope_is_pending(p) == 0);
      CHECK(yse_dsp_shape_envelope_set_breakpoints(p, 0.f, values, times, nullptr, 2) == 1);
      CHECK(yse_dsp_shape_envelope_is_pending(p) == 1);
      CHECK(yse_dsp_shape_envelope_get_count(p) == 3); // still the playing one
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("triggeredRamp") {
      auto* p = yse_dsp_triggered_ramp_create();
      CHECK(yse_dsp_triggered_ramp_get_duration(p) == 100.f);
      CHECK(yse_dsp_triggered_ramp_get_start(p) == 0.f);
      CHECK(yse_dsp_triggered_ramp_get_end(p) == 1.f);
      yse_dsp_triggered_ramp_set_duration(p, 5.f);
      yse_dsp_triggered_ramp_set_start(p, -1.f);
      yse_dsp_triggered_ramp_set_end(p, 2.f);
      yse_dsp_triggered_ramp_set_curve(p, 3.f);
      yse_dsp_triggered_ramp_set_reset_to_zero(p, 1);
      yse_dsp_triggered_ramp_set_retrigger(p, 1);
      CHECK(yse_dsp_triggered_ramp_get_duration(p) == 5.f);
      CHECK(yse_dsp_triggered_ramp_get_start(p) == -1.f);
      CHECK(yse_dsp_triggered_ramp_get_end(p) == 2.f);
      CHECK(yse_dsp_triggered_ramp_get_curve(p) == 1.f); // clamped
      CHECK(yse_dsp_triggered_ramp_get_reset_to_zero(p) == 1);
      CHECK(yse_dsp_triggered_ramp_get_retrigger(p) == 1);
      CHECK(yse_dsp_triggered_ramp_get_mode(p) == YSE_DSP_RAMP_DURATION_AT_START);
      yse_dsp_triggered_ramp_set_mode(p, YSE_DSP_RAMP_DURATION_CONTINUOUS);
      CHECK(yse_dsp_triggered_ramp_get_mode(p) == YSE_DSP_RAMP_DURATION_CONTINUOUS);
      yse_dsp_triggered_ramp_set_mode(p, static_cast<YseDspRampDurationMode>(2));
      CHECK(yse_dsp_triggered_ramp_get_mode(p) == YSE_DSP_RAMP_DURATION_CONTINUOUS);
      CHECK(yse_dsp_triggered_ramp_is_running(p) == 0);
      const std::vector<float> trig = {0.f, 1.f, 1.f, 1.f};
      std::vector<float> out(4);
      yse_dsp_triggered_ramp_process(p, trig.data(), out.data(), 4);
      CHECK(yse_dsp_triggered_ramp_is_running(p) == 1);
      CHECK(yse_dsp_triggered_ramp_get_phase(p) > 0.f);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api envelopes: every process entry point matches the engine class") {
    const std::vector<float> gate = Gate(LENGTH, 200, 120);
    const std::vector<float> attack = Signal(LENGTH, 0.3f, 1.f);
    const std::vector<float> decay = Signal(LENGTH, 0.2f, 0.6f);
    const std::vector<float> sustain = Signal(LENGTH, 0.2f, 0.5f);
    const std::vector<float> release = Signal(LENGTH, 0.3f, 0.8f);
    const std::vector<float> ramp = Ramp(LENGTH, 150.f);

    SUBCASE("adsr, all four overloads, with the gate output") {
      for (int variant = 0; variant < 4; ++variant) {
        INFO("variant " << variant);
        // A sustain limit releases the event-triggered variants too, so the
        // gate output falls inside the run.
        auto* p = yse_dsp_adsr_create();
        SetAdsr(p);
        yse_dsp_adsr_set_max_sustain(p, 1.f);
        yse_dsp_adsr_trigger(p, 0.8f);
        std::vector<float> out(LENGTH, 7.f), g(LENGTH, 7.f);
        unsigned done = 0;
        if (variant == 0)
          done = yse_dsp_adsr_process(p, out.data(), g.data(), LENGTH);
        else if (variant == 1)
          done = yse_dsp_adsr_process_gate(p, gate.data(), out.data(), g.data(), LENGTH);
        else if (variant == 2)
          done = yse_dsp_adsr_process_signal(p, attack.data(), decay.data(), sustain.data(),
                                             release.data(), out.data(), g.data(), LENGTH);
        else
          done = yse_dsp_adsr_process_gate_signal(p, gate.data(), attack.data(), decay.data(),
                                                  sustain.data(), release.data(), out.data(),
                                                  g.data(), LENGTH);
        CHECK(done == LENGTH);

        DSP::adsr a;
        SetAdsr(a);
        a.maxSustain(1.f).trigger(0.8f);
        std::vector<DSP::buffer> s(5);
        std::vector<float> want(LENGTH), wantGate(LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) {
          DSP::buffer* r = nullptr;
          if (variant == 0)
            r = &a(n);
          else if (variant == 1)
            r = &a(Stage(s[0], gate, o, n));
          else if (variant == 2)
            r = &a(Stage(s[1], attack, o, n), Stage(s[2], decay, o, n), Stage(s[3], sustain, o, n),
                   Stage(s[4], release, o, n));
          else
            r = &a(Stage(s[0], gate, o, n), Stage(s[1], attack, o, n), Stage(s[2], decay, o, n),
                   Stage(s[3], sustain, o, n), Stage(s[4], release, o, n));
          Put(*r, want, o, n);
          Put(a.gate(), wantGate, o, n);
        });
        CheckEqual(out, want);
        CheckEqual(g, wantGate);
        REQUIRE(Moves(want));
        REQUIRE(Moves(wantGate));
        CHECK(yse_dsp_adsr_get_level(p) == a.level());
        CHECK(yse_dsp_adsr_get_stage(p) == static_cast<YseDspAdsrStage>(a.stage()));
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("curveSegments") {
      const float targets[3] = {1.f, 0.2f, 0.8f};
      const float times[3] = {2.f, 3.f, 1.f};
      const float curves[3] = {0.5f, -0.3f, 0.f};
      for (YseDspCurveShape shape : {YSE_DSP_CURVE_POWER, YSE_DSP_CURVE_EXPONENTIAL}) {
        auto* p = yse_dsp_curve_segments_create(0);
        yse_dsp_curve_segments_set_shape(p, shape);
        REQUIRE(yse_dsp_curve_segments_set(p, targets, times, curves, 3) == 3);
        std::vector<float> out(LENGTH, 7.f);
        CHECK(yse_dsp_curve_segments_process(p, out.data(), LENGTH) == LENGTH);

        DSP::curveSegments c;
        c.shape(static_cast<DSP::curveSegments::SHAPE>(shape));
        c.segments(targets, times, curves, 3);
        std::vector<float> want(LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(c(n), want, o, n); });
        CheckEqual(out, want);
        REQUIRE(Moves(want));
        CHECK(yse_dsp_curve_segments_get_value(p) == c.value());
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("lineSegments, with the activity output") {
      const float targets[3] = {0.f, 1.f, -0.5f};
      const float times[3] = {0.f, 3.f, 2.f};
      auto* p = yse_dsp_line_segments_create(0);
      REQUIRE(yse_dsp_line_segments_set(p, targets, times, 3) == 3);
      std::vector<float> out(LENGTH, 7.f), act(LENGTH, 7.f);
      CHECK(yse_dsp_line_segments_process(p, out.data(), act.data(), LENGTH) == LENGTH);

      DSP::lineSegments l;
      l.segments(targets, times, 3);
      std::vector<float> want(LENGTH), wantAct(LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        Put(l(n), want, o, n);
        Put(l.activity(), wantAct, o, n);
      });
      CheckEqual(out, want);
      CheckEqual(act, wantAct);
      REQUIRE(Moves(want));
      REQUIRE(Moves(wantAct));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("zigzag, played and with input and speed signals, with the index output") {
      const float values[4] = {0.f, 1.f, 0.5f, -1.f};
      const float deltas[4] = {0.f, 1.f, 1.5f, 0.5f};
      const std::vector<float> index = Signal(LENGTH, 1.5f, 1.5f);
      const std::vector<float> trigger = Ramp(LENGTH, 61.f);
      const std::vector<float> speed = Signal(LENGTH, 1.f, 0.5f);

      struct Variant {
        YseDspZigzagMode mode;
        const std::vector<float>* input;
        const std::vector<float>* rate;
      };
      const Variant variants[] = {
          {YSE_DSP_ZIGZAG_START, nullptr, nullptr}, {YSE_DSP_ZIGZAG_START, nullptr, &speed},
          {YSE_DSP_ZIGZAG_START, &index, nullptr},  {YSE_DSP_ZIGZAG_TRIGGER, &trigger, &speed},
          {YSE_DSP_ZIGZAG_STEP, &trigger, nullptr},
      };
      int k = 0;
      for (const Variant& v : variants) {
        INFO("variant " << k++);
        auto* p = yse_dsp_zigzag_create(0);
        yse_dsp_zigzag_set_line(p, values, deltas, 4);
        yse_dsp_zigzag_set_loop_mode(p, YSE_DSP_ZIGZAG_LOOP_ON);
        yse_dsp_zigzag_set_mode(p, v.mode);
        yse_dsp_zigzag_set_bang_delta(p, 0.25f);
        yse_dsp_zigzag_play(p);
        std::vector<float> out(LENGTH, 7.f), idx(LENGTH, 7.f);
        if (v.input == nullptr && v.rate == nullptr) {
          CHECK(yse_dsp_zigzag_process(p, out.data(), idx.data(), LENGTH) == LENGTH);
        } else {
          CHECK(yse_dsp_zigzag_process_signal(p, v.input ? v.input->data() : nullptr,
                                              v.rate ? v.rate->data() : nullptr, out.data(),
                                              idx.data(), LENGTH) == LENGTH);
        }

        DSP::zigzag z;
        z.setLine(values, deltas, 4);
        z.loopMode(DSP::zigzag::LOOP_ON);
        z.mode(static_cast<DSP::zigzag::MODE>(v.mode));
        z.bangDelta(0.25f);
        z.play();
        std::vector<DSP::buffer> s(2);
        std::vector<float> want(LENGTH), wantIdx(LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) {
          DSP::buffer* in = v.input ? &Stage(s[0], *v.input, o, n) : nullptr;
          DSP::buffer* rate = v.rate ? &Stage(s[1], *v.rate, o, n) : nullptr;
          Put(z(in, rate, n), want, o, n);
          Put(z.indices(), wantIdx, o, n);
        });
        CheckEqual(out, want);
        CheckEqual(idx, wantIdx);
        REQUIRE(Moves(want));
        REQUIRE(Moves(wantIdx));
        CHECK(yse_dsp_zigzag_get_speed(p) == z.speed());
        yse_dsp_processor_destroy(p);
      }
    }

    SUBCASE("techno, with the envelope and position outputs") {
      auto* p = yse_dsp_techno_create(8);
      DSP::techno t(8);
      yse_dsp_techno_set_length(p, 4);
      t.length(4);
      const float pitches[4] = {110.f, 220.f, 165.f, 330.f};
      for (unsigned i = 0; i < 4; ++i) {
        yse_dsp_techno_set_pitch(p, i, pitches[i]);
        yse_dsp_techno_set_amplitude(p, i, 0.25f * static_cast<float>(i + 1));
        yse_dsp_techno_set_curve(p, i, 0.5f * static_cast<float>(i));
        yse_dsp_techno_set_attack(p, i, 2.f);
        yse_dsp_techno_set_decay(p, i, 0.5f + static_cast<float>(i));
        t.pitch(i, pitches[i]).amplitude(i, 0.25f * static_cast<float>(i + 1));
        t.curve(i, 0.5f * static_cast<float>(i)).attack(i, 2.f);
        t.decay(i, 0.5f + static_cast<float>(i));
      }
      yse_dsp_techno_set_position(p, 1, 0.3f);
      t.position(1, 0.3f);
      std::vector<float> out(LENGTH, 7.f), env(LENGTH, 7.f), pos(LENGTH, 7.f);
      CHECK(yse_dsp_techno_process(p, ramp.data(), out.data(), env.data(), pos.data(), LENGTH) ==
            LENGTH);

      DSP::buffer s;
      std::vector<float> want(LENGTH), wantEnv(LENGTH), wantPos(LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        Put(t(Stage(s, ramp, o, n)), want, o, n);
        Put(t.envelope(), wantEnv, o, n);
        Put(t.stepPosition(), wantPos, o, n);
      });
      CheckEqual(out, want);
      CheckEqual(env, wantEnv);
      CheckEqual(pos, wantPos);
      REQUIRE(Moves(want));
      REQUIRE(Moves(wantEnv));
      REQUIRE(Moves(wantPos));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("updown") {
      auto* p = yse_dsp_updown_create();
      yse_dsp_updown_set_up(p, 0.5f);
      yse_dsp_updown_set_down(p, 0.7f);
      yse_dsp_updown_set_level(p, 0.8f);
      std::vector<float> out(LENGTH, 7.f);
      CHECK(yse_dsp_updown_process(p, ramp.data(), out.data(), LENGTH) == LENGTH);

      DSP::updown u;
      u.up(0.5f).down(0.7f).level(0.8f);
      DSP::buffer s;
      std::vector<float> want(LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(u(Stage(s, ramp, o, n)), want, o, n); });
      CheckEqual(out, want);
      REQUIRE(Moves(want));
      CHECK(yse_dsp_updown_get_envelope(p) == u.envelope());
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("shapeEnvelope") {
      const float values[3] = {1.f, 0.3f, 0.f};
      const float times[3] = {1.f, 2.f, 3.f};
      const float curves[3] = {0.5f, 0.f, -0.5f};
      auto* p = yse_dsp_shape_envelope_create(0);
      REQUIRE(yse_dsp_shape_envelope_set_breakpoints(p, 0.f, values, times, curves, 3) == 1);
      yse_dsp_shape_envelope_set_curved(p, 1);
      yse_dsp_shape_envelope_set_constant(p, 1);
      const std::vector<float> slow = Ramp(LENGTH, 170.f);
      std::vector<float> out(LENGTH, 7.f);
      CHECK(yse_dsp_shape_envelope_process(p, slow.data(), out.data(), LENGTH) == LENGTH);

      DSP::shapeEnvelope e;
      e.breakpoints(0.f, values, times, curves, 3);
      e.curved(true).constant(1);
      DSP::buffer s;
      std::vector<float> want(LENGTH);
      Blocks(LENGTH, [&](unsigned o, unsigned n) { Put(e(Stage(s, slow, o, n)), want, o, n); });
      CheckEqual(out, want);
      REQUIRE(Moves(want));
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("triggeredRamp, set and per-sample") {
      const std::vector<float> trig = Gate(LENGTH, 150, 5);
      const std::vector<float> dur = Signal(LENGTH, 0.5f, 1.5f);
      const std::vector<float> from = Signal(LENGTH, 0.1f, -0.5f);
      const std::vector<float> to = Signal(LENGTH, 0.1f, 1.f);
      for (int variant = 0; variant < 2; ++variant) {
        INFO("variant " << variant);
        auto* p = yse_dsp_triggered_ramp_create();
        yse_dsp_triggered_ramp_set_duration(p, 1.f);
        yse_dsp_triggered_ramp_set_curve(p, 0.4f);
        yse_dsp_triggered_ramp_set_mode(p, YSE_DSP_RAMP_DURATION_CONTINUOUS);
        std::vector<float> out(LENGTH, 7.f);
        const unsigned done =
            variant == 0
                ? yse_dsp_triggered_ramp_process(p, trig.data(), out.data(), LENGTH)
                : yse_dsp_triggered_ramp_process_signal(p, trig.data(), dur.data(), from.data(),
                                                        to.data(), out.data(), LENGTH);
        CHECK(done == LENGTH);

        DSP::triggeredRamp r;
        r.duration(1.f).curve(0.4f).mode(DSP::triggeredRamp::durationMode::continuous);
        std::vector<DSP::buffer> s(4);
        std::vector<float> want(LENGTH);
        Blocks(LENGTH, [&](unsigned o, unsigned n) {
          DSP::buffer& t = Stage(s[0], trig, o, n);
          Put(variant == 0
                  ? r(t)
                  : r(t, Stage(s[1], dur, o, n), Stage(s[2], from, o, n), Stage(s[3], to, o, n)),
              want, o, n);
        });
        CheckEqual(out, want);
        REQUIRE(Moves(want));
        CHECK(yse_dsp_triggered_ramp_get_phase(p) == r.phase());
        yse_dsp_processor_destroy(p);
      }
    }
  }

  TEST_CASE("c api envelopes: in-place processing matches out-of-place") {
    const std::vector<float> ramp = Ramp(LENGTH, 150.f);
    const std::vector<float> trig = Gate(LENGTH, 150, 5);

    std::vector<float> separate(LENGTH);
    std::vector<float> shared = ramp;
    auto* a = yse_dsp_updown_create();
    auto* b = yse_dsp_updown_create();
    CHECK(yse_dsp_updown_process(a, ramp.data(), separate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_updown_process(b, shared.data(), shared.data(), LENGTH) == LENGTH);
    CheckEqual(shared, separate);

    shared = trig;
    auto* r1 = yse_dsp_triggered_ramp_create();
    auto* r2 = yse_dsp_triggered_ramp_create();
    yse_dsp_triggered_ramp_set_duration(r1, 1.f);
    yse_dsp_triggered_ramp_set_duration(r2, 1.f);
    CHECK(yse_dsp_triggered_ramp_process(r1, trig.data(), separate.data(), LENGTH) == LENGTH);
    CHECK(yse_dsp_triggered_ramp_process(r2, shared.data(), shared.data(), LENGTH) == LENGTH);
    CheckEqual(shared, separate);

    // The adsr gate input becomes the envelope output.
    shared = trig;
    auto* e1 = yse_dsp_adsr_create();
    auto* e2 = yse_dsp_adsr_create();
    SetAdsr(e1);
    SetAdsr(e2);
    CHECK(yse_dsp_adsr_process_gate(e1, trig.data(), separate.data(), nullptr, LENGTH) == LENGTH);
    CHECK(yse_dsp_adsr_process_gate(e2, shared.data(), shared.data(), nullptr, LENGTH) == LENGTH);
    CheckEqual(shared, separate);

    for (YseDspProcessor* p : {a, b, r1, r2, e1, e2})
      yse_dsp_processor_destroy(p);
  }

  TEST_CASE("c api envelopes: started and finished cover the whole process call") {
    SUBCASE("adsr") {
      // Starts in the first block, comes to rest in the second (an AD
      // envelope of 2 + 1 + 1 ms, 192 samples); the last block sees neither.
      auto* p = yse_dsp_adsr_create();
      SetAdsr(p);
      yse_dsp_adsr_set_attack(p, 2.f);
      yse_dsp_adsr_set_decay(p, 1.f);
      yse_dsp_adsr_set_max_sustain(p, 0.f);
      yse_dsp_adsr_trigger(p, 1.f);
      std::vector<float> out(LENGTH);
      CHECK(yse_dsp_adsr_process(p, out.data(), nullptr, LENGTH) == LENGTH);

      DSP::adsr a;
      SetAdsr(a);
      a.attack(2.f).decay(1.f).maxSustain(0.f).trigger(1.f);
      unsigned startBlock = 99, endBlock = 99;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        a(n);
        if (a.started()) startBlock = o / BLOCK;
        if (a.finished()) endBlock = o / BLOCK;
      });
      // Different blocks, neither of them the last.
      REQUIRE(startBlock == 0);
      REQUIRE(endBlock == 1);
      CHECK(yse_dsp_adsr_get_started(p) == 1);
      CHECK(yse_dsp_adsr_get_finished(p) == 1);
      // A refused call leaves them as they were; the next call clears them.
      CHECK(yse_dsp_adsr_process(p, nullptr, nullptr, LENGTH) == 0);
      CHECK(yse_dsp_adsr_get_started(p) == 1);
      CHECK(yse_dsp_adsr_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_adsr_get_started(p) == 0);
      CHECK(yse_dsp_adsr_get_finished(p) == 0);
      // reset clears them, as the engine's reset clears its own.
      yse_dsp_adsr_trigger(p, 1.f);
      CHECK(yse_dsp_adsr_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_adsr_get_started(p) == 1);
      yse_dsp_adsr_reset(p);
      CHECK(yse_dsp_adsr_get_started(p) == 0);
      CHECK(yse_dsp_adsr_get_finished(p) == 0);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("curveSegments") {
      // 288 samples: the list runs out in the third block of four.
      const float targets[3] = {1.f, 0.2f, 0.8f};
      const float times[3] = {2.f, 3.f, 1.f};
      auto* p = yse_dsp_curve_segments_create(0);
      yse_dsp_curve_segments_set(p, targets, times, nullptr, 3);
      std::vector<float> out(LENGTH);
      CHECK(yse_dsp_curve_segments_process(p, out.data(), LENGTH) == LENGTH);
      DSP::curveSegments c;
      c.segments(targets, times, nullptr, 3);
      unsigned endBlock = 99;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        c(n);
        if (c.finished()) endBlock = o / BLOCK;
      });
      REQUIRE(endBlock == 2);
      CHECK(yse_dsp_curve_segments_get_finished(p) == 1);
      CHECK(yse_dsp_curve_segments_process(p, out.data(), LENGTH) == LENGTH);
      CHECK(yse_dsp_curve_segments_get_finished(p) == 0);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("lineSegments") {
      // 240 samples: the list runs out in the second block.
      const float targets[2] = {1.f, 0.f};
      const float times[2] = {3.f, 2.f};
      auto* p = yse_dsp_line_segments_create(0);
      yse_dsp_line_segments_set(p, targets, times, 2);
      std::vector<float> out(LENGTH);
      CHECK(yse_dsp_line_segments_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_line_segments_get_finished(p) == 1);
      CHECK(yse_dsp_line_segments_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_line_segments_get_finished(p) == 0);
      // A set_target jump counts in the next call.
      yse_dsp_line_segments_set_target(p, 0.5f, 0.f);
      CHECK(yse_dsp_line_segments_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_line_segments_get_finished(p) == 1);
      yse_dsp_processor_destroy(p);
    }
    SUBCASE("zigzag ends are summed over the call") {
      // A 144-sample loop: it wraps in several blocks of one call.
      const float values[4] = {0.f, 1.f, 0.5f, -1.f};
      const float deltas[4] = {0.f, 1.f, 1.5f, 0.5f};
      auto* p = yse_dsp_zigzag_create(0);
      yse_dsp_zigzag_set_line(p, values, deltas, 4);
      yse_dsp_zigzag_set_loop_mode(p, YSE_DSP_ZIGZAG_LOOP_ON);
      yse_dsp_zigzag_play(p);
      std::vector<float> out(LENGTH);
      CHECK(yse_dsp_zigzag_process(p, out.data(), nullptr, LENGTH) == LENGTH);

      DSP::zigzag z;
      z.setLine(values, deltas, 4);
      z.loopMode(DSP::zigzag::LOOP_ON);
      z.play();
      unsigned total = 0, lastBlock = 0;
      Blocks(LENGTH, [&](unsigned, unsigned n) {
        z(n);
        total += z.ends();
        lastBlock = z.ends();
      });
      REQUIRE(total > 1);
      REQUIRE(total != lastBlock);
      CHECK(yse_dsp_zigzag_get_ends(p) == total);
      CHECK(yse_dsp_zigzag_get_finished(p) == 1);
      // reset clears the count.
      auto* q = yse_dsp_zigzag_create(0);
      yse_dsp_zigzag_line(q, values, deltas, 4);
      yse_dsp_zigzag_set_loop_mode(q, YSE_DSP_ZIGZAG_LOOP_ON);
      CHECK(yse_dsp_zigzag_process(q, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_zigzag_get_ends(q) > 0);
      yse_dsp_zigzag_reset(q);
      CHECK(yse_dsp_zigzag_get_ends(q) == 0);
      yse_dsp_processor_destroy(q);
      yse_dsp_zigzag_stop(p);
      CHECK(yse_dsp_zigzag_process(p, out.data(), nullptr, LENGTH) == LENGTH);
      CHECK(yse_dsp_zigzag_get_ends(p) == 0);
      CHECK(yse_dsp_zigzag_get_finished(p) == 0);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api envelopes: the sequencer's fired events cover the whole call") {
    // A 100-sample phase cycle: events fire in every block of the call.
    const std::vector<float> phase = Ramp(LENGTH, 100.f);

    // The engine's per-block lists, laid out over the whole run.
    struct Want {
      unsigned offset;
      float phase;
      std::string text;
    };
    auto reference = [&](DSP::eventSequencer& s, unsigned& missed) {
      std::vector<Want> all;
      DSP::buffer b;
      missed = 0;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        s(Stage(b, phase, o, n), n);
        for (unsigned i = 0; i < s.fired(); ++i) {
          const auto& f = s.firedEvent(i);
          all.push_back({o + f.offset, f.phase, std::string(f.text, f.length)});
        }
        missed += s.missed();
      });
      return all;
    };

    SUBCASE("every event, with room for all") {
      auto* p = yse_dsp_event_sequencer_create(0, 0);
      Fill(p);
      yse_dsp_event_sequencer_set_play(p, 1);
      std::vector<YseDspEventFiring> fired(64);
      CHECK(yse_dsp_event_sequencer_process(p, phase.data(), fired.data(), 64, LENGTH) == LENGTH);

      DSP::eventSequencer s;
      Fill(s);
      s.play(true);
      unsigned missed = 0;
      const std::vector<Want> want = reference(s, missed);
      REQUIRE(want.size() > 8);
      REQUIRE(want.back().offset >= 3 * BLOCK);
      REQUIRE(yse_dsp_event_sequencer_get_fired(p) == want.size());
      CHECK(yse_dsp_event_sequencer_get_missed(p) == 0);
      for (std::size_t i = 0; i < want.size(); ++i) {
        INFO("event " << i);
        CHECK(fired[i].offset == want[i].offset);
        CHECK(fired[i].phase == want[i].phase);
        CHECK(fired[i].length == want[i].text.size());
        CHECK(std::string(fired[i].text) == want[i].text);
      }
      // A refused call leaves the count; the next call replaces it.
      CHECK(yse_dsp_event_sequencer_process(p, nullptr, fired.data(), 64, LENGTH) == 0);
      CHECK(yse_dsp_event_sequencer_get_fired(p) == want.size());
      yse_dsp_event_sequencer_set_play(p, 0);
      CHECK(yse_dsp_event_sequencer_process(p, phase.data(), fired.data(), 64, LENGTH) == LENGTH);
      CHECK(yse_dsp_event_sequencer_get_fired(p) == 0);
      yse_dsp_event_sequencer_set_play(p, 1);
      CHECK(yse_dsp_event_sequencer_process(p, phase.data(), nullptr, 0, LENGTH) == LENGTH);
      CHECK(yse_dsp_event_sequencer_get_fired(p) > 0);
      yse_dsp_event_sequencer_reset(p);
      CHECK(yse_dsp_event_sequencer_get_fired(p) == 0);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("a host array too small: the count is the whole, the copy stops at capacity") {
      auto* p = yse_dsp_event_sequencer_create(0, 0);
      Fill(p);
      yse_dsp_event_sequencer_set_play(p, 1);
      YseDspEventFiring fired[4];
      std::memset(fired, 0x5A, sizeof fired);
      CHECK(yse_dsp_event_sequencer_process(p, phase.data(), fired, 3, LENGTH) == LENGTH);
      CHECK(yse_dsp_event_sequencer_get_fired(p) > 3);
      CHECK(std::string(fired[0].text) == "one");
      CHECK(std::string(fired[2].text) == "three");
      CHECK(fired[3].offset == 0x5A5A5A5Au);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("a fired capacity too small per block: missed is summed over the call") {
      // A 20-sample cycle crosses many events per block.
      const std::vector<float> fast = Ramp(LENGTH, 20.f);
      auto* p = yse_dsp_event_sequencer_create(0, 2);
      Fill(p);
      yse_dsp_event_sequencer_set_play(p, 1);
      std::vector<YseDspEventFiring> fired(128);
      CHECK(yse_dsp_event_sequencer_process(p, fast.data(), fired.data(), 128, LENGTH) == LENGTH);

      DSP::eventSequencer s(DSP::eventSequencer::DEFAULT_EVENTS, 2);
      Fill(s);
      s.play(true);
      DSP::buffer b;
      unsigned firedTotal = 0, missedTotal = 0, missedLast = 0;
      Blocks(LENGTH, [&](unsigned o, unsigned n) {
        s(Stage(b, fast, o, n), n);
        firedTotal += s.fired();
        missedTotal += s.missed();
        missedLast = s.missed();
      });
      REQUIRE(missedTotal > missedLast);
      CHECK(yse_dsp_event_sequencer_get_fired(p) == firedTotal);
      CHECK(yse_dsp_event_sequencer_get_missed(p) == missedTotal);
      yse_dsp_processor_destroy(p);
    }

    SUBCASE("recording takes the phase the last call ended on") {
      auto* p = yse_dsp_event_sequencer_create(0, 0);
      yse_dsp_event_sequencer_set_overdub(p, 1);
      CHECK(yse_dsp_event_sequencer_process(p, phase.data(), nullptr, 0, LENGTH) == LENGTH);
      CHECK(yse_dsp_event_sequencer_record_event(p, "rec", 3) == 1);
      CHECK(yse_dsp_event_sequencer_get_event_phase(p, 0) == phase[LENGTH - 1]);
      yse_dsp_processor_destroy(p);
    }
  }

  TEST_CASE("c api envelopes: process, setter and list calls allocate nothing") {
    const std::vector<float> ramp = Ramp(LENGTH, 100.f);
    const std::vector<float> gate = Gate(LENGTH, 200, 120);
    const std::vector<float> knob = Signal(LENGTH, 0.3f, 1.f);
    const float targets[3] = {1.f, 0.2f, 0.8f};
    const float times[3] = {2.f, 3.f, 1.f};
    const float curves[3] = {0.5f, -0.3f, 0.f};
    const float swing[1] = {0.66f};
    std::vector<float> o1(LENGTH), o2(LENGTH), o3(LENGTH);
    std::vector<YseDspEventFiring> fired(16);
    char text[16];

    std::vector<YseDspProcessor*> all;
    for (const Named& c : AllCtors())
      all.push_back(c.make());
    YseDspProcessor* ad = all[0];
    YseDspProcessor* cs = all[1];
    YseDspProcessor* ls = all[2];
    YseDspProcessor* zz = all[3];
    YseDspProcessor* te = all[4];
    YseDspProcessor* es = all[5];
    YseDspProcessor* ud = all[6];
    YseDspProcessor* se = all[7];
    YseDspProcessor* tr = all[8];

    {
      TestHelpers::ProbeScope probe;
      SetAdsr(ad);
      yse_dsp_adsr_trigger(ad, 1.f);
      yse_dsp_adsr_process(ad, o1.data(), o2.data(), LENGTH);
      yse_dsp_adsr_process_gate_signal(ad, gate.data(), knob.data(), knob.data(), knob.data(),
                                       knob.data(), o1.data(), o2.data(), LENGTH);
      (void)yse_dsp_adsr_get_finished(ad);
      yse_dsp_adsr_reset(ad);
      yse_dsp_curve_segments_set(cs, targets, times, curves, 3);
      yse_dsp_curve_segments_set_shape(cs, YSE_DSP_CURVE_EXPONENTIAL);
      yse_dsp_curve_segments_process(cs, o1.data(), LENGTH);
      yse_dsp_curve_segments_jump(cs, 0.f);
      yse_dsp_line_segments_set(ls, targets, times, 3);
      yse_dsp_line_segments_set_target(ls, 0.5f, 0.f);
      yse_dsp_line_segments_process(ls, o1.data(), o2.data(), LENGTH);
      yse_dsp_zigzag_set_line(zz, targets, times, 3);
      yse_dsp_zigzag_append(zz, 0.3f, 1.f, 1);
      yse_dsp_zigzag_insert(zz, 1, 0.4f, 1.f);
      yse_dsp_zigzag_remove(zz, 0);
      yse_dsp_zigzag_set_loop_mode(zz, YSE_DSP_ZIGZAG_LOOP_PENDULUM);
      yse_dsp_zigzag_play(zz);
      yse_dsp_zigzag_process(zz, o1.data(), o2.data(), LENGTH);
      yse_dsp_zigzag_set_mode(zz, YSE_DSP_ZIGZAG_TRIGGER);
      yse_dsp_zigzag_process_signal(zz, ramp.data(), knob.data(), o1.data(), o2.data(), LENGTH);
      yse_dsp_techno_set_length(te, 4);
      yse_dsp_techno_set_pitch(te, 1, 220.f);
      yse_dsp_techno_repeat_positions(te, swing, 1);
      yse_dsp_techno_process(te, ramp.data(), o1.data(), o2.data(), o3.data(), LENGTH);
      // Fill() asserts; the probe region adds the same events without doctest.
      for (unsigned i = 0; i < 4; ++i)
        yse_dsp_event_sequencer_add(es, 0, PHASES[i], TEXTS[i],
                                    static_cast<unsigned>(std::strlen(TEXTS[i])));
      yse_dsp_event_sequencer_set_play(es, 1);
      yse_dsp_event_sequencer_process(es, ramp.data(), fired.data(), 16, LENGTH);
      yse_dsp_event_sequencer_set_overdub(es, 1);
      yse_dsp_event_sequencer_record_event(es, "rec", 3);
      yse_dsp_event_sequencer_remove(es, 0, 0.f, 1.f, "one", 3);
      (void)yse_dsp_event_sequencer_get_event_text(es, 0, text, sizeof text);
      yse_dsp_event_sequencer_clear_all(es);
      yse_dsp_updown_set_up(ud, 0.5f);
      yse_dsp_updown_process(ud, ramp.data(), o1.data(), LENGTH);
      yse_dsp_shape_envelope_set_breakpoints(se, 0.f, targets, times, curves, 3);
      yse_dsp_shape_envelope_set_curved(se, 1);
      yse_dsp_shape_envelope_process(se, ramp.data(), o1.data(), LENGTH);
      yse_dsp_triggered_ramp_set_duration(tr, 1.f);
      yse_dsp_triggered_ramp_set_mode(tr, YSE_DSP_RAMP_DURATION_CONTINUOUS);
      yse_dsp_triggered_ramp_process(tr, gate.data(), o1.data(), LENGTH);
      yse_dsp_triggered_ramp_process_signal(tr, gate.data(), knob.data(), knob.data(), knob.data(),
                                            o1.data(), LENGTH);
      // A refused call (wrong class) is allocation-free too.
      yse_dsp_adsr_process(tr, o1.data(), nullptr, LENGTH);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    for (YseDspProcessor* p : all)
      yse_dsp_processor_destroy(p);
  }

} // TEST_SUITE("dsp")
