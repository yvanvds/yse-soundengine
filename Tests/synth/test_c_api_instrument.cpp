// C-API instrument surface tests (issue #178) — exercises the sampler / VA / FM
// instrument voices and their loadable assets (YseEngine/c_api/yse_instrument.*
// and the #178 growth of yse_synth.*) entirely through the flat C ABI, with no
// engine C++ headers. Mirrors the DSP-level coverage in test_sampler_voice.cpp
// / test_va_voice.cpp / test_dx7_sysex.cpp at the C surface.
//
// The engine is driven offline (yse_system_init_offline + render_offline), so no
// audio hardware is needed and the suite runs in CI. Because it calls
// yse_system_close() it lives in its own TEST_SUITE("instrumentcapi") and its
// own ctest process (see Tests/CMakeLists.txt) — the same isolation the
// synthcapi / midisynth / playersynth suites use, keeping System::close()'s
// process-global teardown out of the crowded combined suite (#298/#304).
//
// Coverage against the #178 acceptance:
//   - SFZ fixture loads and plays through the sampler voice group,
//   - a VA patch is dialled in via the per-parameter setters and rendered,
//   - a DX7 bank fixture is imported, enumerated, a patch selected, and rendered,
//   - handle lifetime is enforced: double free and use-after-destroy are logged
//     no-ops, not crashes,
//   - PARITY: every VA vaParams field and every FM headline param has a C setter
//     (exercised here; the full FM voice is reached via DX7 patch import).

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "yse_c/yse_instrument.h"
#include "yse_c/yse_synth.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

using namespace std::chrono_literals;

namespace {

  std::string fixturesDir() {
    return std::string(YSE_TEST_FIXTURES_DIR);
  }

  // Pump the engine offline until the synth has cloned `expected` voices, or the
  // budget expires. Same drain helper the synthcapi suite uses.
  bool drainUntilVoices(YseSystem* sys, YseSynth* syn, int expected,
                        std::chrono::milliseconds budget = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
      if (yse_synth_get_num_voices(syn) >= expected) return true;
      std::this_thread::sleep_for(2ms);
    }
    return yse_synth_get_num_voices(syn) >= expected;
  }

  void render(YseSystem* sys, int blocks) {
    for (int i = 0; i < blocks; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
    }
  }

  void drainFor(YseSystem* sys, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
      std::this_thread::sleep_for(2ms);
    }
  }

  // ─── DX7 SysEx fixture generation (raw bytes, no engine headers) ────────────
  // Deterministically build a valid 32-voice packed bulk dump whose slot 0 is an
  // audible single-carrier voice named "YSE Capi". The packed byte layout is the
  // documented DX7 format (see test_dx7_sysex.cpp::packVoice, the importer's
  // inverse); we hand-roll it here so the TU stays pure-C-ABI. No copyrighted
  // factory bank is committed.

  void packCarrierVoice(uint8_t out[128]) {
    for (int i = 0; i < 128; ++i)
      out[i] = 0;
    // OP1 (index 0) is the carrier: instant envelope holding at full level.
    uint8_t* d = out; // op 0 base = 0
    d[0] = d[1] = d[2] = d[3] = 99; // egRate R1..R4
    d[4] = d[5] = d[6] = 99; // egLevel L1..L3 (sustain high)
    d[7] = 0; // L4 (post-release)
    d[12] = static_cast<uint8_t>((0 & 0x07) | ((7 & 0x0f) << 3)); // rateScaling|detune(centre)
    d[14] = 99; // outputLevel
    d[15] = static_cast<uint8_t>((0 & 0x01) | ((1 & 0x1f) << 1)); // oscMode ratio, coarse 1
    // OP2..OP6 stay silent (outputLevel 0), so only OP1 sounds — a clean sine.

    uint8_t* g = out + 102; // global block
    g[0] = g[1] = g[2] = g[3] = 99; // pitch EG rates (instant)
    g[4] = g[5] = g[6] = g[7] = 50; // pitch EG levels (50 = centre, no bend)
    g[8] = 0; // algorithm 0 (DX7 algo 1 — OP1 carrier)
    g[15] = 24; // transpose 24 = no transpose
    const char name[10] = {'Y', 'S', 'E', ' ', 'C', 'a', 'p', 'i', ' ', ' '};
    for (int i = 0; i < 10; ++i)
      g[16 + i] = static_cast<uint8_t>(name[i]);
  }

  uint8_t sysexChecksum(const uint8_t* p, size_t n) {
    unsigned sum = 0;
    for (size_t i = 0; i < n; ++i)
      sum += p[i];
    return static_cast<uint8_t>((0x80u - (sum & 0x7fu)) & 0x7fu);
  }

  // Write a framed 32-voice packed bulk dump to `path`. Returns false on I/O
  // failure. slot 0 = carrier, slots 1..31 = zeroed (silent) voices.
  bool writeDx7Fixture(const std::string& path) {
    std::vector<uint8_t> payload(4096, 0);
    packCarrierVoice(payload.data()); // slot 0
    std::vector<uint8_t> msg;
    msg.push_back(0xF0);
    msg.push_back(0x43);
    msg.push_back(0x00); // sub-status / channel
    msg.push_back(0x09); // format: 32-voice packed
    msg.push_back(static_cast<uint8_t>((4096 >> 7) & 0x7f)); // byte-count MS
    msg.push_back(static_cast<uint8_t>(4096 & 0x7f)); // byte-count LS
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(sysexChecksum(payload.data(), payload.size()));
    msg.push_back(0xF7);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(msg.data()), static_cast<std::streamsize>(msg.size()));
    return static_cast<bool>(f);
  }

} // namespace

TEST_SUITE("instrumentcapi") {

  // ─── NULL-handle safety across the whole new surface (no engine) ───────────

  TEST_CASE("c-api instrument: NULL handles are null-safe") {
    // Loaders reject NULL / missing input with a NULL return, not a crash.
    CHECK(yse_sfz_load(nullptr) == nullptr);
    CHECK(yse_sfz_load("this/does/not/exist.sfz") == nullptr);
    CHECK(yse_sfz_load_config(nullptr) == nullptr);
    CHECK(yse_dx7_import_sysex(nullptr) == nullptr);
    CHECK(yse_dx7_import_sysex("this/does/not/exist.syx") == nullptr);

    // Query / destroy of NULL asset handles are safe no-ops.
    CHECK(yse_sfz_is_valid(nullptr) == 0);
    CHECK(yse_dx7_get_patch_count(nullptr) == 0);
    char buf[32];
    CHECK(yse_dx7_get_patch_name(nullptr, 0, buf, sizeof(buf)) == 0);
    yse_sfz_destroy(nullptr);
    yse_dx7_destroy(nullptr);

    // add-voices reject NULL / invalid arguments.
    CHECK(yse_synth_add_voices_sampler(nullptr, nullptr, 4, 0, 0, 127) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_synth_add_voices_va(nullptr, 4, 0, 0, 127) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_synth_add_voices_fm(nullptr, 4, 0, 0, 127) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_synth_fm_set_patch(nullptr, nullptr, 0) == YSE_ERR_INVALID_HANDLE);

    // Every VA + FM setter is a null-safe no-op on a NULL synth.
    yse_synth_va_set_osc_wave(nullptr, 0, YSE_VA_SAW);
    yse_synth_va_set_osc_detune(nullptr, 0, 0.1f);
    yse_synth_va_set_osc_level(nullptr, 0, 1.0f);
    yse_synth_va_set_osc_pulse_width(nullptr, 0, 0.5f);
    yse_synth_va_set_wavetable_position(nullptr, 0.5f);
    yse_synth_va_set_cutoff(nullptr, 1000.f);
    yse_synth_va_set_resonance(nullptr, 0.2f);
    yse_synth_va_set_key_tracking(nullptr, 0.5f);
    yse_synth_va_set_filter_env_amount(nullptr, 1.f);
    yse_synth_va_set_filter_vel_amount(nullptr, 1.f);
    yse_synth_va_set_amp_attack(nullptr, 0.01f);
    yse_synth_va_set_amp_decay(nullptr, 0.1f);
    yse_synth_va_set_amp_sustain(nullptr, 0.8f);
    yse_synth_va_set_amp_release(nullptr, 0.2f);
    yse_synth_va_set_amp_vel_amount(nullptr, 0.5f);
    yse_synth_va_set_filter_attack(nullptr, 0.01f);
    yse_synth_va_set_filter_decay(nullptr, 0.1f);
    yse_synth_va_set_filter_sustain(nullptr, 0.5f);
    yse_synth_va_set_filter_release(nullptr, 0.2f);
    yse_synth_va_set_lfo_type(nullptr, YSE_LFO_SINE);
    yse_synth_va_set_lfo_rate(nullptr, 5.f);
    yse_synth_va_set_lfo_to_pitch(nullptr, 0.1f);
    yse_synth_va_set_lfo_to_cutoff(nullptr, 0.5f);
    yse_synth_va_set_lfo_to_wavetable(nullptr, 0.3f);
    yse_synth_va_set_gain(nullptr, 0.8f);
    float cycle[4] = {0.f, 1.f, 0.f, -1.f};
    yse_synth_va_load_wavetable(nullptr, 0, cycle, 4);
    yse_synth_fm_set_algorithm(nullptr, 5);
    yse_synth_fm_set_feedback(nullptr, 4);
    yse_synth_fm_set_transpose(nullptr, 24);
    yse_synth_fm_set_lfo_speed(nullptr, 40);
    yse_synth_fm_set_lfo_delay(nullptr, 0);
    yse_synth_fm_set_lfo_waveform(nullptr, 4);
    yse_synth_fm_set_lfo_pitch_mod_depth(nullptr, 20);
    yse_synth_fm_set_lfo_amp_mod_depth(nullptr, 0);
    yse_synth_fm_set_pitch_mod_sens(nullptr, 3);
    yse_synth_fm_set_op_output_level(nullptr, 0, 99);
    yse_synth_fm_set_op_freq_coarse(nullptr, 0, 1);
    yse_synth_fm_set_op_freq_fine(nullptr, 0, 0);
    yse_synth_fm_set_op_detune(nullptr, 0, 7);
    yse_synth_fm_set_op_osc_mode(nullptr, 0, 0);
    yse_synth_fm_set_op_enabled(nullptr, 0, 1);
  }

  // ─── handle lifetime: double-free / use-after-destroy are logged no-ops ────
  // No engine required — the assets are independent of engine lifetime.

  TEST_CASE("c-api instrument: SFZ handle lifetime is enforced") {
    YseSfzInstrument* inst = yse_sfz_load((fixturesDir() + "/sfz_dedup.sfz").c_str());
    REQUIRE(inst != nullptr);
    CHECK(yse_sfz_is_valid(inst) == 1);

    yse_sfz_destroy(inst);
    // Use-after-destroy: a logged no-op, not a crash — the handle is retired.
    CHECK(yse_sfz_is_valid(inst) == 0);
    // Double free: a logged no-op, not a double delete.
    yse_sfz_destroy(inst);
    yse_sfz_destroy(inst);
  }

  TEST_CASE("c-api instrument: DX7 bank handle lifetime is enforced") {
    const std::string path = "capi_dx7_lifetime.syx";
    REQUIRE(writeDx7Fixture(path));

    YseDx7Bank* bank = yse_dx7_import_sysex(path.c_str());
    REQUIRE(bank != nullptr);
    CHECK(yse_dx7_get_patch_count(bank) == 32);
    char name[32] = {0};
    CHECK(yse_dx7_get_patch_name(bank, 0, name, sizeof(name)) > 0);
    CHECK(std::string(name) == "YSE Capi");

    yse_dx7_destroy(bank);
    // Use-after-destroy: queries fall back to safe zero/empty, no crash.
    CHECK(yse_dx7_get_patch_count(bank) == 0);
    CHECK(yse_dx7_get_patch_name(bank, 0, name, sizeof(name)) == 0);
    // Double free is a logged no-op.
    yse_dx7_destroy(bank);
    yse_dx7_destroy(bank);

    std::remove(path.c_str());
  }

  TEST_CASE("c-api instrument: a destroyed instrument cannot be added to a synth") {
    // add-voices with an already-destroyed instrument handle is rejected (logged),
    // not a use-after-free. No engine needed to reach the guard.
    YseSfzInstrument* inst = yse_sfz_load((fixturesDir() + "/sfz_dedup.sfz").c_str());
    REQUIRE(inst != nullptr);
    yse_sfz_destroy(inst);

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    CHECK(yse_synth_add_voices_sampler(syn, inst, 4, 0, 0, 127) == YSE_ERR_INVALID_ARGUMENT);
    yse_synth_destroy(syn);
  }

  // ─── SFZ sampler: fixture loads, attaches, and plays ───────────────────────

  TEST_CASE("c-api instrument: SFZ sampler loads a fixture and plays") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return; // unavailable → skip

    // sfz_dedup.sfz names the real test_mono_44100.wav (audible PCM).
    YseSfzInstrument* inst = yse_sfz_load((fixturesDir() + "/sfz_dedup.sfz").c_str());
    REQUIRE(inst != nullptr);
    CHECK(yse_sfz_is_valid(inst) == 1);

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sampler(syn, inst, 4, 0, 0, 127) == YSE_OK);
    // The voice group retains its own share — safe to drop the handle now.
    yse_sfz_destroy(inst);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.9f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 4));
    CHECK(yse_synth_get_num_voices(syn) == 4);

    // Play a couple of notes across the key range and render.
    yse_synth_note_on(syn, 1, 60, 1.0f);
    yse_synth_note_on(syn, 1, 72, 0.8f);
    render(sys, 6);
    yse_synth_all_notes_off(syn, 0);
    render(sys, 8);

    CHECK(yse_synth_is_valid(syn) == 1);
    CHECK(yse_synth_get_num_voices(syn) == 4);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

  // ─── VA voice: full parameter set dialled from C, then rendered ────────────

  TEST_CASE("c-api instrument: VA patch set through every setter and rendered") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    // Channel 1 with the full key range — the notes below play on channel 1, so
    // this also exercises the #390 channel window through a real render.
    REQUIRE(yse_synth_add_voices_va(syn, 6, 1, 0, 127) == YSE_OK);

    // PARITY: exercise every vaParams field through its C setter. Setters are
    // glitch-free atomics, so this may run before or during play.
    for (int osc = 0; osc < 3; ++osc) {
      yse_synth_va_set_osc_wave(syn, osc, YSE_VA_SAW);
      yse_synth_va_set_osc_detune(syn, osc, 0.05f * osc);
      yse_synth_va_set_osc_level(syn, osc, osc == 0 ? 1.0f : 0.4f);
      yse_synth_va_set_osc_pulse_width(syn, osc, 0.5f);
    }
    yse_synth_va_set_osc_wave(syn, 2, YSE_VA_WAVETABLE);
    yse_synth_va_set_wavetable_position(syn, 0.3f);
    yse_synth_va_set_cutoff(syn, 4000.f);
    yse_synth_va_set_resonance(syn, 0.3f);
    yse_synth_va_set_key_tracking(syn, 0.5f);
    yse_synth_va_set_filter_env_amount(syn, 1.5f);
    yse_synth_va_set_filter_vel_amount(syn, 0.5f);
    yse_synth_va_set_amp_attack(syn, 0.005f);
    yse_synth_va_set_amp_decay(syn, 0.05f);
    yse_synth_va_set_amp_sustain(syn, 0.8f);
    yse_synth_va_set_amp_release(syn, 0.1f);
    yse_synth_va_set_amp_vel_amount(syn, 0.6f);
    yse_synth_va_set_filter_attack(syn, 0.01f);
    yse_synth_va_set_filter_decay(syn, 0.08f);
    yse_synth_va_set_filter_sustain(syn, 0.4f);
    yse_synth_va_set_filter_release(syn, 0.15f);
    yse_synth_va_set_lfo_type(syn, YSE_LFO_SINE);
    yse_synth_va_set_lfo_rate(syn, 4.f);
    yse_synth_va_set_lfo_to_pitch(syn, 0.1f);
    yse_synth_va_set_lfo_to_cutoff(syn, 0.5f);
    yse_synth_va_set_lfo_to_wavetable(syn, 0.2f);
    yse_synth_va_set_gain(syn, 0.8f);
    // A single-cycle table into the morph bank (setup-thread, before play).
    std::vector<float> cycle(64);
    for (int i = 0; i < 64; ++i)
      cycle[i] = (i < 32) ? 1.f : -1.f; // square-ish
    yse_synth_va_load_wavetable(syn, 0, cycle.data(), cycle.size());

    // Out-of-range osc index is ignored, not a crash.
    yse_synth_va_set_osc_level(syn, 99, 1.0f);
    yse_synth_va_set_osc_level(syn, -1, 1.0f);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 6));

    yse_synth_note_on(syn, 1, 57, 1.0f); // A3
    yse_synth_note_on(syn, 1, 60, 0.8f);
    render(sys, 6);
    // Live tweak while sounding — must stay glitch-free (no crash / no invalidation).
    yse_synth_va_set_cutoff(syn, 8000.f);
    yse_synth_va_set_resonance(syn, 0.6f);
    render(sys, 4);
    yse_synth_all_notes_off(syn, 0);
    render(sys, 8);

    CHECK(yse_synth_is_valid(syn) == 1);
    CHECK(yse_synth_get_num_voices(syn) == 6);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

  // ─── FM voice: DX7 bank imported, patch selected, headline params, rendered ─

  TEST_CASE("c-api instrument: DX7 bank imported, patch selected, and rendered") {
    const std::string path = "capi_dx7_render.syx";
    REQUIRE(writeDx7Fixture(path));

    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) {
      std::remove(path.c_str());
      return;
    }

    YseDx7Bank* bank = yse_dx7_import_sysex(path.c_str());
    REQUIRE(bank != nullptr);
    CHECK(yse_dx7_get_patch_count(bank) == 32);
    char name[32] = {0};
    CHECK(yse_dx7_get_patch_name(bank, 0, name, sizeof(name)) > 0);
    CHECK(std::string(name) == "YSE Capi");

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_fm(syn, 4, 0, 0, 127) == YSE_OK);

    // Select the imported patch — the way the full 155-parameter DX7 voice is
    // reached from C. Out-of-range / destroyed-bank guards return errors.
    REQUIRE(yse_synth_fm_set_patch(syn, bank, 0) == YSE_OK);
    CHECK(yse_synth_fm_set_patch(syn, bank, 999) == YSE_ERR_INVALID_ARGUMENT);

    // PARITY: the headline FM params, reachable directly from C.
    yse_synth_fm_set_algorithm(syn, 0);
    yse_synth_fm_set_feedback(syn, 3);
    yse_synth_fm_set_transpose(syn, 24);
    yse_synth_fm_set_lfo_speed(syn, 35);
    yse_synth_fm_set_lfo_delay(syn, 0);
    yse_synth_fm_set_lfo_waveform(syn, 4);
    yse_synth_fm_set_lfo_pitch_mod_depth(syn, 10);
    yse_synth_fm_set_lfo_amp_mod_depth(syn, 0);
    yse_synth_fm_set_pitch_mod_sens(syn, 3);
    for (int op = 0; op < 6; ++op) {
      yse_synth_fm_set_op_output_level(syn, op, op == 0 ? 99 : 0);
      yse_synth_fm_set_op_freq_coarse(syn, op, 1);
      yse_synth_fm_set_op_freq_fine(syn, op, 0);
      yse_synth_fm_set_op_detune(syn, op, 7);
      yse_synth_fm_set_op_osc_mode(syn, op, 0);
      yse_synth_fm_set_op_enabled(syn, op, 1);
    }
    // Out-of-range op index is ignored, not a crash.
    yse_synth_fm_set_op_output_level(syn, 99, 50);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 4));

    yse_synth_note_on(syn, 1, 57, 1.0f);
    render(sys, 8);
    yse_synth_all_notes_off(syn, 0);
    render(sys, 8);

    CHECK(yse_synth_is_valid(syn) == 1);
    CHECK(yse_synth_get_num_voices(syn) == 4);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    yse_dx7_destroy(bank);
    drainFor(sys, 300ms);
    yse_system_close(sys);
    std::remove(path.c_str());
  }

  // ─── one-region samplerConfig convenience creator ──────────────────────────

  TEST_CASE("c-api instrument: samplerConfig creator builds a one-region instrument") {
    YseSamplerConfig cfg = {};
    cfg.name = "capi-oneshot";
    std::string wav = fixturesDir() + "/test_mono_44100.wav";
    cfg.file = wav.c_str();
    cfg.root = 60;
    cfg.low = 0;
    cfg.high = 127;
    cfg.attack = 0.0f;
    cfg.release = 0.1f;
    cfg.max_length = 5.0f;

    YseSfzInstrument* inst = yse_sfz_load_config(&cfg);
    REQUIRE(inst != nullptr);
    CHECK(yse_sfz_is_valid(inst) == 1);
    yse_sfz_destroy(inst);
  }

} // TEST_SUITE("instrumentcapi")
