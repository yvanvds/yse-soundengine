// C-API channel-DSP, effect-module, and send/return surface tests (issue #166,
// part of epic #146). Exercises the surface added in #166 entirely through the
// flat C ABI (no engine C++ headers), mirroring the C++ coverage in
// test_channel_dsp.cpp / test_channel_sends.cpp and the module suites at the C
// surface.
//
// Two things are proven here:
//   1. A C-only end-to-end render: a channel with a parametric-EQ insert sends
//      into a plate-reverb return under an offline engine (no audio hardware),
//      and the whole graph renders block after block with finite output. This
//      is the issue's headline acceptance item.
//   2. Parameter parity: every new C++ module parameter (feedbackDelay, chorus,
//      plateReverb, parametricEQ, compressor) is reachable and round-trips
//      through the C setters/getters, and the channel insert get/set + send
//      wiring mirror the engine semantics from docs/design/send_return_buses.md.
//
// The engine is driven offline (yse_system_init_offline + render_offline), so
// it runs on headless CI. It lives in its own TEST_SUITE("channelcapi") and its
// own ctest process (see Tests/CMakeLists.txt) because init_offline() drives
// process-global engine state — the same isolation the sendstress / synthcapi
// suites use.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "yse_c/yse_channel.h"
#include "yse_c/yse_dsp.h"
#include "yse_c/yse_dsp_modules.h"
#include "yse_c/yse_enums.h"
#include "yse_c/yse_patcher.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

using namespace std::chrono_literals;

namespace {

  // Init the offline engine once for the whole channelcapi process. Mirrors the
  // sendstress suite's ensureOffline(); no audio hardware needed.
  bool ensureOffline() {
    static bool done = false;
    static bool ok = false;
    if (!done) {
      YseSystem* sys = yse_system_get();
      yse_system_close(sys); // normalize regardless of starting state
      ok = (yse_system_init_offline(sys) == YSE_OK);
      done = true;
    }
    return ok;
  }

  // Offline analogue of the channel suite's drainChannels(): update() flags the
  // control-plane work the audio callback body runs, render_offline() runs
  // blocks, and a short sleep lets the slow pool execute queued setup() jobs so
  // freshly created channels / returns / sounds reach OBJECT_READY.
  void pump(int iterations = 20) {
    YseSystem* sys = yse_system_get();
    for (int i = 0; i < iterations; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 2);
      std::this_thread::sleep_for(2ms);
    }
  }

  // A looping in-memory sine tone loaded through the C buffer API — a real AC
  // signal source built entirely from C (no dspSourceObject subclassing, which
  // is out of scope for #166). File-scope so it outlives every sound built from
  // it (the load_buffer lifetime contract).
  YseDspBuffer* makeToneBuffer() {
    const unsigned int len = 2048;
    YseDspBuffer* buf = yse_dsp_buffer_create(len, 0);
    if (!buf) return nullptr;
    std::vector<float> tone(len);
    for (unsigned int i = 0; i < len; ++i)
      tone[i] = 0.25f * std::sin(2.0f * 3.14159265f * 4.0f * static_cast<float>(i) /
                                 static_cast<float>(len));
    yse_dsp_buffer_write(buf, 0, tone.data(), len);
    return buf;
  }

} // namespace

TEST_SUITE("channelcapi") {

  // ─── Headline acceptance: C-only insert + send-to-plate-return render ────────

  TEST_CASE("c-api channel: insert chain + send to a plate return renders end to end") {
    REQUIRE(ensureOffline());

    // A source channel with a parametric-EQ insert (the DAW insert slot).
    YseChannel* src = yse_channel_create("capi_src", yse_channel_master());
    REQUIRE(src != nullptr);
    CHECK(yse_channel_is_return(src) == 0);

    YseDspObject* eq = yse_dsp_eq_create();
    REQUIRE(eq != nullptr);
    yse_dsp_eq_set_gain(eq, YSE_EQ_LOW_SHELF, 6.0f);
    yse_channel_set_dsp(src, eq);
    CHECK(yse_channel_get_dsp(src) == eq); // insert round-trips through the C handle

    // A plate-reverb return bus (excluded from the mix tree; folds into master).
    YseChannel* verb = yse_channel_create_return("capi_verb", 4);
    REQUIRE(verb != nullptr);
    CHECK(yse_channel_is_return(verb) == 1);
    YseDspObject* plate = yse_dsp_plate_reverb_create();
    REQUIRE(plate != nullptr);
    yse_dsp_plate_reverb_set_decay(plate, 0.7f);
    yse_channel_set_dsp(verb, plate);

    // Drive the channel/return lifecycle to READY, then attach a looping tone
    // and start it (create/load does not auto-play).
    pump();
    YseDspBuffer* tone = makeToneBuffer();
    REQUIRE(tone != nullptr);
    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_sound_load_buffer(snd, tone, src, /*loop*/ 1, /*volume*/ 1.0f) == YSE_OK);
    pump();
    yse_sound_play(snd);
    pump();

    // Wire a post-fader send from the source into the plate return.
    yse_channel_send(src, 0, verb, 0.5f, /*pre_fader*/ 0);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.5f));

    // Render enough blocks to build the plate tail. The whole graph must render
    // without crashing and stay finite.
    pump();
    for (int i = 0; i < 8; ++i)
      yse_system_render_offline(yse_system_get(), 16);

    CHECK(std::isfinite(yse_channel_get_peak_linear_post(src)));
    CHECK(std::isfinite(yse_channel_get_peak_linear_post(verb)));
    CHECK(yse_channel_get_peak_linear_post(verb) >= 0.f);
    CHECK(std::isfinite(yse_channel_get_peak_linear_post(yse_channel_master())));

    // A send level is a modulation target — continuous writes must be accepted.
    for (int t = 0; t < 16; ++t)
      yse_channel_set_send_level(src, 0, 0.3f + 0.2f * std::sin(0.2f * static_cast<float>(t)));
    pump(2);
    CHECK(std::isfinite(yse_channel_get_peak_linear_post(verb)));

    // Tear down: detach the inserts before the returns/channels fall away, stop
    // and free the sound, then let the delete jobs drain before the buffer dies.
    yse_channel_clear_send(src, 0);
    yse_sound_stop(snd);
    pump();
    yse_channel_set_dsp(src, nullptr);
    yse_channel_set_dsp(verb, nullptr);
    yse_sound_destroy(snd);
    yse_channel_destroy(verb);
    yse_channel_destroy(src);
    pump();
    yse_dsp_object_destroy(eq);
    yse_dsp_object_destroy(plate);
    yse_dsp_buffer_destroy(tone);
  }

  // ─── Send/return wiring validation through the C surface ─────────────────────

  TEST_CASE("c-api channel: send wiring validation mirrors the engine rules") {
    REQUIRE(ensureOffline());

    YseChannel* src = yse_channel_create("capi_wsrc", yse_channel_master());
    YseChannel* notReturn = yse_channel_create("capi_notret", yse_channel_master());
    YseChannel* ret = yse_channel_create_return("capi_wret", 4);
    REQUIRE(src);
    REQUIRE(notReturn);
    REQUIRE(ret);
    pump();

    // Send to a non-return target is rejected (level stays 0).
    yse_channel_send(src, 0, notReturn, 0.5f, 0);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.f));

    // An out-of-range slot is rejected.
    yse_channel_send(src, 99, ret, 0.5f, 0);
    CHECK(yse_channel_get_send_level(src, 99) == doctest::Approx(0.f));

    // A valid send reports its level; set_send_level updates it; clear detaches.
    yse_channel_send(src, 0, ret, 0.4f, 0);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.4f));
    yse_channel_set_send_level(src, 0, 0.7f);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.7f));
    yse_channel_clear_send(src, 0);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.f));

    // return→return is allowed (DAG); a cycle is rejected at wiring time.
    YseChannel* ret2 = yse_channel_create_return("capi_wret2", 4);
    REQUIRE(ret2);
    pump();
    yse_channel_send(ret, 0, ret2, 0.5f, 0); // legal edge
    CHECK(yse_channel_get_send_level(ret, 0) == doctest::Approx(0.5f));
    yse_channel_send(ret2, 0, ret, 0.5f, 0); // would close a cycle
    CHECK(yse_channel_get_send_level(ret2, 0) == doctest::Approx(0.f));

    yse_channel_clear_send(ret, 0);
    pump();
    yse_channel_destroy(ret2);
    yse_channel_destroy(ret);
    yse_channel_destroy(notReturn);
    yse_channel_destroy(src);
    pump();
  }

  // Null-safety: every void mutator is a no-op and every getter a zero on NULL.
  TEST_CASE("c-api channel: send/insert surface is null-safe") {
    CHECK(yse_channel_is_return(nullptr) == 0);
    CHECK(yse_channel_get_send_level(nullptr, 0) == doctest::Approx(0.f));
    CHECK(yse_channel_get_dsp(nullptr) == nullptr);
    // No crash:
    yse_channel_send(nullptr, 0, nullptr, 1.f, 0);
    yse_channel_set_send_level(nullptr, 0, 1.f);
    yse_channel_clear_send(nullptr, 0);
    yse_channel_set_dsp(nullptr, nullptr);
    CHECK(yse_channel_create_return(nullptr, 4) == nullptr);
    CHECK(yse_channel_create_with_sends(nullptr, nullptr, 4) == nullptr);
  }

  // ─── Module parameter parity (every new C++ param reachable from C) ──────────

  TEST_CASE("c-api dsp: feedback delay parameters round-trip (#160)") {
    YseDspObject* d = yse_dsp_feedback_delay_create();
    REQUIRE(d != nullptr);
    yse_dsp_feedback_delay_set_time(d, 120.0f);
    CHECK(yse_dsp_feedback_delay_get_time(d) == doctest::Approx(120.0f));
    yse_dsp_feedback_delay_set_feedback(d, 0.6f);
    CHECK(yse_dsp_feedback_delay_get_feedback(d) == doctest::Approx(0.6f));
    yse_dsp_feedback_delay_set_damping(d, 3000.0f);
    CHECK(yse_dsp_feedback_delay_get_damping(d) == doctest::Approx(3000.0f));
    yse_dsp_feedback_delay_set_crossfeed(d, 0.5f);
    CHECK(yse_dsp_feedback_delay_get_crossfeed(d) == doctest::Approx(0.5f));
    // inherited dspObject surface reaches the same handle
    yse_dsp_object_set_impact(d, 0.8f);
    CHECK(yse_dsp_object_get_impact(d) == doctest::Approx(0.8f));
    yse_dsp_object_destroy(d);
  }

  TEST_CASE("c-api dsp: chorus/flanger parameters round-trip (#161)") {
    YseDspObject* c = yse_dsp_chorus_create();
    REQUIRE(c != nullptr);
    yse_dsp_chorus_set_mode(c, YSE_CHORUS_MODE_FLANGER);
    CHECK(yse_dsp_chorus_get_mode(c) == YSE_CHORUS_MODE_FLANGER);
    yse_dsp_chorus_set_rate(c, 0.8f);
    CHECK(yse_dsp_chorus_get_rate(c) == doctest::Approx(0.8f));
    yse_dsp_chorus_set_depth(c, 0.5f);
    CHECK(yse_dsp_chorus_get_depth(c) == doctest::Approx(0.5f));
    yse_dsp_chorus_set_feedback(c, -0.4f);
    CHECK(yse_dsp_chorus_get_feedback(c) == doctest::Approx(-0.4f));
    yse_dsp_chorus_set_spread(c, 0.7f);
    CHECK(yse_dsp_chorus_get_spread(c) == doctest::Approx(0.7f));
    yse_dsp_object_destroy(c);
  }

  TEST_CASE("c-api dsp: plate reverb parameters round-trip (#162)") {
    YseDspObject* p = yse_dsp_plate_reverb_create();
    REQUIRE(p != nullptr);
    yse_dsp_plate_reverb_set_decay(p, 0.85f);
    CHECK(yse_dsp_plate_reverb_get_decay(p) == doctest::Approx(0.85f));
    yse_dsp_plate_reverb_set_damping(p, 4000.0f);
    CHECK(yse_dsp_plate_reverb_get_damping(p) == doctest::Approx(4000.0f));
    yse_dsp_plate_reverb_set_predelay(p, 20.0f);
    CHECK(yse_dsp_plate_reverb_get_predelay(p) == doctest::Approx(20.0f));
    yse_dsp_object_destroy(p);
  }

  TEST_CASE("c-api dsp: parametric EQ parameters round-trip, per band (#163)") {
    YseDspObject* e = yse_dsp_eq_create();
    REQUIRE(e != nullptr);
    const YseEqBand bands[] = {YSE_EQ_LOW_SHELF, YSE_EQ_PEAK_1, YSE_EQ_PEAK_2, YSE_EQ_HIGH_SHELF};
    for (YseEqBand b : bands) {
      yse_dsp_eq_set_frequency(e, b, 1000.0f);
      CHECK(yse_dsp_eq_get_frequency(e, b) == doctest::Approx(1000.0f));
      yse_dsp_eq_set_gain(e, b, 3.0f);
      CHECK(yse_dsp_eq_get_gain(e, b) == doctest::Approx(3.0f));
      yse_dsp_eq_set_q(e, b, 1.2f);
      CHECK(yse_dsp_eq_get_q(e, b) == doctest::Approx(1.2f));
    }
    yse_dsp_object_destroy(e);
  }

  TEST_CASE("c-api dsp: compressor parameters round-trip (#163)") {
    YseDspObject* c = yse_dsp_compressor_create();
    REQUIRE(c != nullptr);
    yse_dsp_compressor_set_detector(c, YSE_COMPRESSOR_DETECT_RMS);
    CHECK(yse_dsp_compressor_get_detector(c) == YSE_COMPRESSOR_DETECT_RMS);
    yse_dsp_compressor_set_threshold(c, -18.0f);
    CHECK(yse_dsp_compressor_get_threshold(c) == doctest::Approx(-18.0f));
    yse_dsp_compressor_set_ratio(c, 4.0f);
    CHECK(yse_dsp_compressor_get_ratio(c) == doctest::Approx(4.0f));
    yse_dsp_compressor_set_attack(c, 10.0f);
    CHECK(yse_dsp_compressor_get_attack(c) == doctest::Approx(10.0f));
    yse_dsp_compressor_set_release(c, 120.0f);
    CHECK(yse_dsp_compressor_get_release(c) == doctest::Approx(120.0f));
    yse_dsp_compressor_set_makeup(c, 6.0f);
    CHECK(yse_dsp_compressor_get_makeup(c) == doctest::Approx(6.0f));
    // read-only meter is reachable and finite
    CHECK(std::isfinite(yse_dsp_compressor_get_gain_reduction_db(c)));
    yse_dsp_object_destroy(c);
  }

  TEST_CASE("c-api dsp: morphing reverb presets + morph round-trip (#326/#369)") {
    YseDspObject* m = yse_dsp_morphing_reverb_create();
    REQUIRE(m != nullptr);

    // Defaults (per #326): A = GENERIC, B = HALL, morph = 0 (pure A).
    CHECK(yse_dsp_morphing_reverb_get_morph(m) == doctest::Approx(0.0f));

    // Named-preset endpoints resolve to the shared preset table.
    yse_dsp_morphing_reverb_set_preset_a(m, YSE_REVERB_CAVE);
    yse_dsp_morphing_reverb_set_preset_b(m, YSE_REVERB_OFF);
    YseReverbPresetValues a{};
    YseReverbPresetValues b{};
    yse_dsp_morphing_reverb_get_preset_a(m, &a);
    yse_dsp_morphing_reverb_get_preset_b(m, &b);
    CHECK(a.roomsize == doctest::Approx(1.0f)); // CAVE
    CHECK(a.wet == doctest::Approx(0.7f));
    CHECK(a.dry == doctest::Approx(0.3f));
    CHECK(a.early_time[0] == doctest::Approx(100.0f));
    CHECK(a.early_gain[0] == doctest::Approx(0.8f));
    CHECK(b.dry == doctest::Approx(1.0f)); // OFF: dry pass-through
    CHECK(b.wet == doctest::Approx(0.0f));

    // Custom endpoint values round-trip field-for-field through the mirror
    // struct (send/return flavour: fully wet).
    YseReverbPresetValues custom{};
    custom.roomsize = 0.42f;
    custom.damp = 0.3f;
    custom.dry = 0.0f;
    custom.wet = 1.0f;
    custom.mod_frequency = 2.5f;
    custom.mod_width = 8.0f;
    for (int i = 0; i < 4; ++i) {
      custom.early_time[i] = 100.0f * static_cast<float>(i + 1);
      custom.early_gain[i] = 0.1f * static_cast<float>(i + 1);
    }
    yse_dsp_morphing_reverb_set_preset_b_values(m, &custom);
    YseReverbPresetValues got{};
    yse_dsp_morphing_reverb_get_preset_b(m, &got);
    CHECK(got.roomsize == doctest::Approx(0.42f));
    CHECK(got.damp == doctest::Approx(0.3f));
    CHECK(got.dry == doctest::Approx(0.0f));
    CHECK(got.wet == doctest::Approx(1.0f));
    CHECK(got.mod_frequency == doctest::Approx(2.5f));
    CHECK(got.mod_width == doctest::Approx(8.0f));
    for (int i = 0; i < 4; ++i) {
      CHECK(got.early_time[i] == doctest::Approx(100.0f * static_cast<float>(i + 1)));
      CHECK(got.early_gain[i] == doctest::Approx(0.1f * static_cast<float>(i + 1)));
    }

    // Morph control input (per #326) is stored and clamped to [0, 1].
    yse_dsp_morphing_reverb_set_morph(m, 0.25f);
    CHECK(yse_dsp_morphing_reverb_get_morph(m) == doctest::Approx(0.25f));
    yse_dsp_morphing_reverb_set_morph(m, 7.5f);
    CHECK(yse_dsp_morphing_reverb_get_morph(m) == doctest::Approx(1.0f));
    yse_dsp_morphing_reverb_set_morph(m, -2.0f);
    CHECK(yse_dsp_morphing_reverb_get_morph(m) == doctest::Approx(0.0f));

    // Inherited dspObject surface reaches the same handle.
    yse_dsp_object_set_bypass(m, 1);
    CHECK(yse_dsp_object_get_bypass(m) == 1);

    yse_dsp_object_destroy(m);
  }

  // ─── patcher-as-insert (#167 module, #370 C API) ─────────────────────────────

  TEST_CASE("c-api dsp: patcher insert wraps a graph and renders on a channel (#167/#370)") {
    REQUIRE(ensureOffline());

    // Build a passthrough patcher graph (~adc -> ~dac, stereo) entirely through
    // the C ABI, then wrap it as a chainable insert. The graph is function-scoped
    // so it outlives the insert that borrows it.
    YsePatcher* graph = yse_patcher_create();
    REQUIRE(graph != nullptr);
    yse_patcher_init(graph, 2);
    YsePHandle* adc = yse_patcher_create_object(graph, "~adc", nullptr);
    YsePHandle* dac = yse_patcher_create_object(graph, "~dac", nullptr);
    REQUIRE(adc != nullptr);
    REQUIRE(dac != nullptr);
    yse_patcher_connect(graph, adc, 0, dac, 0);
    yse_patcher_connect(graph, adc, 1, dac, 1);

    YseDspObject* insert = yse_dsp_patcher_insert_create(graph);
    REQUIRE(insert != nullptr);

    // The inherited dspObject surface reaches the same handle.
    yse_dsp_object_set_bypass(insert, 1);
    CHECK(yse_dsp_object_get_bypass(insert) == 1);
    yse_dsp_object_set_bypass(insert, 0);

    // Attach the insert to a channel; it round-trips through the C handle.
    YseChannel* ch = yse_channel_create("capi_pi", yse_channel_master());
    REQUIRE(ch != nullptr);
    yse_channel_set_dsp(ch, insert);
    CHECK(yse_channel_get_dsp(ch) == insert);

    // Drive a real tone through the channel and render: the patcher insert now
    // runs on the audio path and the whole graph must stay finite.
    pump();
    YseDspBuffer* tone = makeToneBuffer();
    REQUIRE(tone != nullptr);
    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_sound_load_buffer(snd, tone, ch, /*loop*/ 1, /*volume*/ 1.0f) == YSE_OK);
    pump();
    yse_sound_play(snd);
    pump();
    for (int i = 0; i < 8; ++i)
      yse_system_render_offline(yse_system_get(), 16);

    CHECK(std::isfinite(yse_channel_get_peak_linear_post(ch)));
    CHECK(std::isfinite(yse_channel_get_peak_linear_post(yse_channel_master())));

    // Tear down: detach the insert before the channel/sound fall away, then free
    // the insert before the patcher it borrows, then the buffer.
    yse_sound_stop(snd);
    pump();
    yse_channel_set_dsp(ch, nullptr);
    yse_sound_destroy(snd);
    yse_channel_destroy(ch);
    pump();
    yse_dsp_object_destroy(insert);
    yse_patcher_destroy(graph);
    yse_dsp_buffer_destroy(tone);
  }

  // A NULL patcher has no graph to wrap, so the creator reports failure rather
  // than handing back an insert that can never render.
  TEST_CASE("c-api dsp: patcher insert create rejects a NULL patcher (#370)") {
    CHECK(yse_dsp_patcher_insert_create(nullptr) == nullptr);
  }

  // Module setters/getters are null-safe like the rest of the module surface.
  TEST_CASE("c-api dsp: new module setters/getters are null-safe") {
    yse_dsp_feedback_delay_set_time(nullptr, 1.f);
    yse_dsp_chorus_set_rate(nullptr, 1.f);
    yse_dsp_plate_reverb_set_decay(nullptr, 1.f);
    yse_dsp_eq_set_gain(nullptr, YSE_EQ_PEAK_1, 1.f);
    yse_dsp_compressor_set_ratio(nullptr, 1.f);
    CHECK(yse_dsp_feedback_delay_get_time(nullptr) == doctest::Approx(0.f));
    CHECK(yse_dsp_chorus_get_mode(nullptr) == YSE_CHORUS_MODE_CHORUS);
    CHECK(yse_dsp_plate_reverb_get_decay(nullptr) == doctest::Approx(0.f));
    CHECK(yse_dsp_eq_get_gain(nullptr, YSE_EQ_PEAK_1) == doctest::Approx(0.f));
    CHECK(yse_dsp_compressor_get_gain_reduction_db(nullptr) == doctest::Approx(0.f));

    // morphing reverb (#326/#369)
    yse_dsp_morphing_reverb_set_preset_a(nullptr, YSE_REVERB_HALL);
    yse_dsp_morphing_reverb_set_preset_b_values(nullptr, nullptr);
    yse_dsp_morphing_reverb_set_morph(nullptr, 0.5f);
    CHECK(yse_dsp_morphing_reverb_get_morph(nullptr) == doctest::Approx(0.f));
    YseReverbPresetValues z; // getter must zero-fill on a NULL handle
    yse_dsp_morphing_reverb_get_preset_a(nullptr, &z);
    CHECK(z.roomsize == doctest::Approx(0.f));
    CHECK(z.wet == doctest::Approx(0.f));
    yse_dsp_morphing_reverb_get_preset_a(nullptr, nullptr); // no crash
  }

} // TEST_SUITE("channelcapi")
