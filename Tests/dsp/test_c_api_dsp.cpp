// C-API boundary tests for the two lowest-coverage DSP translation units under
// YseEngine/c_api/ (issue #568, the follow-up half of #417 / epic #420):
// yse_dsp.cpp (buffer + drawableBuffer + fileBuffer + wavetable) and
// yse_dsp_modules.cpp (the effect-module control surface).
//
// Same recipe as test_c_api_surface.cpp: the assertions are about the *boundary
// contract*, not DSP behaviour, because that is what a binding breaks on
// silently.
//
//   * NULL-handle handling — every entry point is called with a NULL handle.
//     Void setters must no-op; queries must return zero / false / NULL; the
//     YseStatus-returning subclass entry points must report
//     YSE_ERR_INVALID_HANDLE and record a last_error.
//   * Ownership — create/destroy pairs for all four buffer subclasses and all
//     sixteen module types, plus destroy(NULL).
//   * Round-trips — every module setter/getter pair, the enum-valued ones in
//     both directions, and the reverb preset-values struct field by field.
//   * Bulk I/O clamping — yse_dsp_buffer_read / _write return the number of
//     samples actually copied and refuse an out-of-range offset.
//
// Neither TU needs a live engine: buffers and effect modules are plain heap
// objects. The cases live in TEST_SUITE("capilowcov") and its dedicated ctest
// process anyway (see Tests/CMakeLists.txt) so they share it with the sibling
// #568 files that DO drive yse_system_init_offline().

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "yse_c/yse_common.h"
#include "yse_c/yse_dsp.h"
#include "yse_c/yse_dsp_modules.h"
#include "yse_c/yse_enums.h"
#include "yse_c/yse_patcher.h"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

namespace {

  std::string fixture(const char* name) {
    return std::string(YSE_TEST_FIXTURES_DIR) + "/" + name;
  }

  // Every no-arg module constructor in yse_dsp_modules.h, so the ownership and
  // inherited-control-surface cases can sweep the whole family.
  using ModuleCtor = YseDspObject* (*)();

  const ModuleCtor kModuleCtors[] = {
      &yse_dsp_lowpass_create,
      &yse_dsp_highpass_create,
      &yse_dsp_bandpass_create,
      &yse_dsp_basic_delay_create,
      &yse_dsp_lowpass_delay_create,
      &yse_dsp_highpass_delay_create,
      &yse_dsp_phaser_create,
      &yse_dsp_ring_modulator_create,
      &yse_dsp_difference_create,
      &yse_dsp_feedback_delay_create,
      &yse_dsp_chorus_create,
      &yse_dsp_plate_reverb_create,
      &yse_dsp_eq_create,
      &yse_dsp_compressor_create,
      &yse_dsp_morphing_reverb_create,
  };

} // namespace

TEST_SUITE("capilowcov") {

  // ═══ yse_dsp.cpp — buffers ════════════════════════════════════════════════

  TEST_CASE("c-api dsp buffer: every entry point is NULL-safe") {
    // Queries return zero / false on a NULL handle.
    CHECK(yse_dsp_buffer_length(nullptr) == 0u);
    CHECK(yse_dsp_buffer_length_ms(nullptr) == 0u);
    CHECK(yse_dsp_buffer_length_sec(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_dsp_buffer_is_silent(nullptr) == 0);
    CHECK(yse_dsp_buffer_max_value(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_dsp_buffer_get_back(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_dsp_buffer_sample_rate_adjustment(nullptr) == doctest::Approx(0.0f));

    // Void setters no-op.
    yse_dsp_buffer_set_sample_rate_adjustment(nullptr, 2.0f);
    yse_dsp_buffer_resize(nullptr, 16, 0.0f);
    yse_dsp_buffer_fill(nullptr, 1.0f);
    yse_dsp_buffer_add_scalar(nullptr, 1.0f);
    yse_dsp_buffer_mul_scalar(nullptr, 2.0f);
    yse_dsp_buffer_destroy(nullptr);

    // Bulk I/O copies nothing without a handle, and nothing without a host
    // array either.
    float scratch[4] = {0.f, 0.f, 0.f, 0.f};
    CHECK(yse_dsp_buffer_read(nullptr, 0, scratch, 4) == 0u);
    CHECK(yse_dsp_buffer_write(nullptr, 0, scratch, 4) == 0u);

    // Subclass entry points report an invalid handle rather than crashing.
    CHECK(yse_dsp_buffer_draw_line(nullptr, 0, 4, 0.f, 1.f) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_buffer_draw_flat(nullptr, 0, 4, 1.f) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_buffer_load_file(nullptr, "nope.wav", 0) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_buffer_save_file(nullptr, "nope.wav") == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_wavetable_create_saw(nullptr, 8, 64) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_wavetable_create_square(nullptr, 8, 64) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_dsp_wavetable_create_triangle(nullptr, 8, 64) == YSE_ERR_INVALID_HANDLE);

    // The last failure above recorded a message for the C client.
    CHECK(std::string(yse_last_error()).find("wavetable") != std::string::npos);
    yse_clear_last_error();
  }

  TEST_CASE("c-api dsp buffer: all four subclasses construct and destroy") {
    YseDspBuffer* plain = yse_dsp_buffer_create(128, 0);
    YseDspBuffer* drawable = yse_dsp_drawable_buffer_create(128, 0);
    YseDspBuffer* file = yse_dsp_file_buffer_create(128, 0);
    YseDspBuffer* table = yse_dsp_wavetable_create(128);

    REQUIRE(plain != nullptr);
    REQUIRE(drawable != nullptr);
    REQUIRE(file != nullptr);
    REQUIRE(table != nullptr);

    // Length excludes the overflow tail — the wavetable ctor requests one
    // wrap-around sample, so its reported length still matches the request.
    CHECK(yse_dsp_buffer_length(plain) == 128u);
    CHECK(yse_dsp_buffer_length(drawable) == 128u);
    CHECK(yse_dsp_buffer_length(file) == 128u);
    CHECK(yse_dsp_buffer_length(table) == 128u);

    yse_dsp_buffer_destroy(plain);
    yse_dsp_buffer_destroy(drawable);
    yse_dsp_buffer_destroy(file);
    yse_dsp_buffer_destroy(table);
  }

  TEST_CASE("c-api dsp buffer: length queries and sample-rate adjustment round-trip") {
    YseDspBuffer* buf = yse_dsp_buffer_create(512, 0);
    REQUIRE(buf != nullptr);

    CHECK(yse_dsp_buffer_length(buf) == 512u);
    // ms / sec are derived from the same sample count at the engine rate, so
    // they must agree with each other without hard-coding a sample rate. Both
    // truncate toward zero, so compare the truncated values.
    const float sec = yse_dsp_buffer_length_sec(buf);
    CHECK(sec > 0.0f);
    CHECK(yse_dsp_buffer_length_ms(buf) == static_cast<unsigned int>(sec * 1000.0f));

    yse_dsp_buffer_set_sample_rate_adjustment(buf, 0.5f);
    CHECK(yse_dsp_buffer_sample_rate_adjustment(buf) == doctest::Approx(0.5f));

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: fill / scalar math / silence / peak") {
    YseDspBuffer* buf = yse_dsp_buffer_create(64, 0);
    REQUIRE(buf != nullptr);

    // A freshly constructed buffer is zeroed.
    CHECK(yse_dsp_buffer_is_silent(buf) == 1);
    CHECK(yse_dsp_buffer_max_value(buf) == doctest::Approx(0.0f));

    yse_dsp_buffer_fill(buf, 0.25f);
    CHECK(yse_dsp_buffer_is_silent(buf) == 0);
    CHECK(yse_dsp_buffer_max_value(buf) == doctest::Approx(0.25f));
    CHECK(yse_dsp_buffer_get_back(buf) == doctest::Approx(0.25f));

    yse_dsp_buffer_add_scalar(buf, 0.25f);
    CHECK(yse_dsp_buffer_max_value(buf) == doctest::Approx(0.5f));

    yse_dsp_buffer_mul_scalar(buf, 2.0f);
    CHECK(yse_dsp_buffer_max_value(buf) == doctest::Approx(1.0f));

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: read / write clamp to the buffer length") {
    YseDspBuffer* buf = yse_dsp_buffer_create(8, 0);
    REQUIRE(buf != nullptr);

    const float in[8] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
    CHECK(yse_dsp_buffer_write(buf, 0, in, 8) == 8u);

    float out[8] = {};
    CHECK(yse_dsp_buffer_read(buf, 0, out, 8) == 8u);
    for (int i = 0; i < 8; ++i)
      CHECK(out[i] == doctest::Approx(in[i]));

    // A count that runs past the end is clamped to what fits.
    CHECK(yse_dsp_buffer_read(buf, 6, out, 8) == 2u);
    CHECK(out[0] == doctest::Approx(6.0f));
    CHECK(out[1] == doctest::Approx(7.0f));
    CHECK(yse_dsp_buffer_write(buf, 6, in, 8) == 2u);

    // An offset at or past the end copies nothing at all.
    CHECK(yse_dsp_buffer_read(buf, 8, out, 1) == 0u);
    CHECK(yse_dsp_buffer_read(buf, 99, out, 1) == 0u);
    CHECK(yse_dsp_buffer_write(buf, 8, in, 1) == 0u);
    CHECK(yse_dsp_buffer_write(buf, 99, in, 1) == 0u);

    // NULL host array with a live handle is refused on both directions.
    CHECK(yse_dsp_buffer_read(buf, 0, nullptr, 4) == 0u);
    CHECK(yse_dsp_buffer_write(buf, 0, nullptr, 4) == 0u);

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: resize grows with the fill value and shrinks") {
    YseDspBuffer* buf = yse_dsp_buffer_create(4, 0);
    REQUIRE(buf != nullptr);
    yse_dsp_buffer_fill(buf, 1.0f);

    yse_dsp_buffer_resize(buf, 8, -0.5f);
    CHECK(yse_dsp_buffer_length(buf) == 8u);
    float out[8] = {};
    CHECK(yse_dsp_buffer_read(buf, 0, out, 8) == 8u);
    CHECK(out[0] == doctest::Approx(1.0f));
    CHECK(out[7] == doctest::Approx(-0.5f)); // newly added samples

    yse_dsp_buffer_resize(buf, 2, 0.0f);
    CHECK(yse_dsp_buffer_length(buf) == 2u);

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: drawable draw_line / draw_flat shape the buffer") {
    YseDspBuffer* buf = yse_dsp_drawable_buffer_create(16, 0);
    REQUIRE(buf != nullptr);

    // `stop` is an exclusive bound (drawableBuffer::drawLine writes [start, stop)),
    // so a full-buffer ramp asks for stop == length.
    CHECK(yse_dsp_buffer_draw_line(buf, 0, 16, 0.0f, 1.0f) == YSE_OK);
    float out[16] = {};
    REQUIRE(yse_dsp_buffer_read(buf, 0, out, 16) == 16u);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[8] > out[4]); // monotonically rising ramp
    CHECK(out[15] > out[8]);
    CHECK(out[15] <= 1.0f);

    CHECK(yse_dsp_buffer_draw_flat(buf, 0, 16, 0.75f) == YSE_OK);
    REQUIRE(yse_dsp_buffer_read(buf, 0, out, 16) == 16u);
    CHECK(out[0] == doctest::Approx(0.75f));
    CHECK(out[15] == doctest::Approx(0.75f));

    // An empty or inverted range is a no-op rather than an out-of-bounds write.
    CHECK(yse_dsp_buffer_draw_flat(buf, 8, 8, 0.0f) == YSE_OK);
    CHECK(yse_dsp_buffer_draw_line(buf, 12, 4, 0.0f, 1.0f) == YSE_OK);
    REQUIRE(yse_dsp_buffer_read(buf, 0, out, 16) == 16u);
    CHECK(out[8] == doctest::Approx(0.75f));

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: fileBuffer load and save") {
    YseDspBuffer* buf = yse_dsp_file_buffer_create(8, 0);
    REQUIRE(buf != nullptr);

    // NULL filename is an argument error, distinct from an invalid handle.
    CHECK(yse_dsp_buffer_load_file(buf, nullptr, 0) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(yse_dsp_buffer_save_file(buf, nullptr) == YSE_ERR_INVALID_ARGUMENT);

    // A missing file reports FILE_NOT_FOUND and names the path in last_error.
    yse_clear_last_error();
    CHECK(yse_dsp_buffer_load_file(buf, "definitely_not_here.wav", 0) == YSE_ERR_FILE_NOT_FOUND);
    CHECK(std::string(yse_last_error()).find("definitely_not_here.wav") != std::string::npos);
    yse_clear_last_error();

    // The bundled mono fixture loads: the buffer is resized to the file's frame
    // count and the sample-rate adjustment is set from the file's rate.
    const std::string wav = fixture("test_mono_44100.wav");
    REQUIRE(yse_dsp_buffer_load_file(buf, wav.c_str(), 0) == YSE_OK);
    CHECK(yse_dsp_buffer_length(buf) > 8u); // grew past the constructed length
    CHECK(yse_dsp_buffer_sample_rate_adjustment(buf) > 0.0f);

    // Asking for a channel the mono file does not have fails cleanly.
    CHECK(yse_dsp_buffer_load_file(buf, wav.c_str(), 1) == YSE_ERR_FILE_NOT_FOUND);
    yse_clear_last_error();

    // save() is currently a stub in the engine (fileBuffer::save writes nothing
    // and returns true — the JUCE writer it replaced was never ported, issue
    // #580). Only the C-side contract is asserted here; a round-trip through
    // the file would be asserting the stub.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "yse_c_api_filebuffer_568";
    CHECK(yse_dsp_buffer_save_file(buf, tmp.string().c_str()) == YSE_OK);
    std::error_code ec;
    std::filesystem::remove(tmp.string() + ".wav", ec); // best effort

    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api dsp buffer: wavetable generators fill the table") {
    YseDspBuffer* table = yse_dsp_wavetable_create(64);
    REQUIRE(table != nullptr);

    CHECK(yse_dsp_wavetable_create_saw(table, 8, 64) == YSE_OK);
    CHECK(yse_dsp_buffer_is_silent(table) == 0);
    CHECK(yse_dsp_buffer_max_value(table) > 0.0f);

    CHECK(yse_dsp_wavetable_create_square(table, 8, 64) == YSE_OK);
    CHECK(yse_dsp_buffer_is_silent(table) == 0);

    CHECK(yse_dsp_wavetable_create_triangle(table, 8, 64) == YSE_OK);
    CHECK(yse_dsp_buffer_is_silent(table) == 0);

    yse_dsp_buffer_destroy(table);
  }

  // ═══ yse_dsp_modules.cpp — effect modules ═════════════════════════════════

  TEST_CASE("c-api dsp module: every no-arg constructor yields a destroyable handle") {
    for (ModuleCtor make : kModuleCtors) {
      YseDspObject* obj = make();
      REQUIRE(obj != nullptr);
      yse_dsp_object_destroy(obj);
    }
    // The two parameterised constructors as well.
    YseDspObject* sweep = yse_dsp_sweep_create(YSE_SWEEP_TRIANGLE);
    REQUIRE(sweep != nullptr);
    yse_dsp_object_destroy(sweep);

    YseDspObject* gran = yse_dsp_granulator_create(4096, 8);
    REQUIRE(gran != nullptr);
    yse_dsp_object_destroy(gran);

    yse_dsp_object_destroy(nullptr); // destroy(NULL) is a no-op
  }

  TEST_CASE("c-api dsp module: patcher_insert_create rejects a NULL patcher") {
    yse_clear_last_error();
    CHECK(yse_dsp_patcher_insert_create(nullptr) == nullptr);
    CHECK(std::string(yse_last_error()).find("patcher is NULL") != std::string::npos);
    yse_clear_last_error();

    // A live patcher is accepted and the insert borrows it (destroy the insert
    // first — it holds a reference to the patcher, not ownership of it).
    YsePatcher* patch = yse_patcher_create();
    REQUIRE(patch != nullptr);
    yse_patcher_init(patch, 1);
    YseDspObject* insert = yse_dsp_patcher_insert_create(patch);
    CHECK(insert != nullptr);
    yse_dsp_object_destroy(insert);
    yse_patcher_destroy(patch);
  }

  TEST_CASE("c-api dsp module: inherited dspObject surface round-trips on every module") {
    for (ModuleCtor make : kModuleCtors) {
      YseDspObject* obj = make();
      REQUIRE(obj != nullptr);

      CHECK(yse_dsp_object_get_bypass(obj) == 0);
      yse_dsp_object_set_bypass(obj, 1);
      CHECK(yse_dsp_object_get_bypass(obj) == 1);
      yse_dsp_object_set_bypass(obj, 0);
      CHECK(yse_dsp_object_get_bypass(obj) == 0);

      yse_dsp_object_set_impact(obj, 0.6f);
      CHECK(yse_dsp_object_get_impact(obj) == doctest::Approx(0.6f));

      yse_dsp_object_set_lfo_type(obj, YSE_LFO_SINE);
      CHECK(yse_dsp_object_get_lfo_type(obj) == YSE_LFO_SINE);
      yse_dsp_object_set_lfo_frequency(obj, 3.5f);
      CHECK(yse_dsp_object_get_lfo_frequency(obj) == doctest::Approx(3.5f));
      yse_dsp_object_set_lfo_type(obj, YSE_LFO_NONE);
      CHECK(yse_dsp_object_get_lfo_type(obj) == YSE_LFO_NONE);

      yse_dsp_object_destroy(obj);
    }
  }

  TEST_CASE("c-api dsp module: link(NULL) detaches the forward edge") {
    YseDspObject* head = yse_dsp_lowpass_create();
    YseDspObject* next = yse_dsp_highpass_create();
    REQUIRE(head != nullptr);
    REQUIRE(next != nullptr);

    yse_dsp_object_link(head, next);
    yse_dsp_object_link(head, nullptr); // #391: NULL detaches rather than no-ops
    yse_dsp_object_link(nullptr, next); // NULL head is a plain no-op

    yse_dsp_object_destroy(head);
    yse_dsp_object_destroy(next);
  }

  TEST_CASE("c-api dsp module: the inherited surface is NULL-safe") {
    yse_dsp_object_set_bypass(nullptr, 1);
    CHECK(yse_dsp_object_get_bypass(nullptr) == 0);
    yse_dsp_object_set_impact(nullptr, 1.0f);
    CHECK(yse_dsp_object_get_impact(nullptr) == doctest::Approx(0.0f));
    yse_dsp_object_set_lfo_type(nullptr, YSE_LFO_SINE);
    CHECK(yse_dsp_object_get_lfo_type(nullptr) == YSE_LFO_NONE);
    yse_dsp_object_set_lfo_frequency(nullptr, 2.0f);
    CHECK(yse_dsp_object_get_lfo_frequency(nullptr) == doctest::Approx(0.0f));
  }

  TEST_CASE("c-api dsp module: filter frequency / Q round-trips") {
    YseDspObject* lp = yse_dsp_lowpass_create();
    YseDspObject* hp = yse_dsp_highpass_create();
    YseDspObject* bp = yse_dsp_bandpass_create();
    REQUIRE(lp != nullptr);
    REQUIRE(hp != nullptr);
    REQUIRE(bp != nullptr);

    yse_dsp_lowpass_set_frequency(lp, 800.0f);
    CHECK(yse_dsp_lowpass_get_frequency(lp) == doctest::Approx(800.0f));
    yse_dsp_highpass_set_frequency(hp, 1200.0f);
    CHECK(yse_dsp_highpass_get_frequency(hp) == doctest::Approx(1200.0f));
    yse_dsp_bandpass_set_frequency(bp, 2000.0f);
    CHECK(yse_dsp_bandpass_get_frequency(bp) == doctest::Approx(2000.0f));
    yse_dsp_bandpass_set_q(bp, 4.0f);
    CHECK(yse_dsp_bandpass_get_q(bp) == doctest::Approx(4.0f));

    // NULL handles: setters no-op, getters return zero.
    yse_dsp_lowpass_set_frequency(nullptr, 100.f);
    CHECK(yse_dsp_lowpass_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_highpass_set_frequency(nullptr, 100.f);
    CHECK(yse_dsp_highpass_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_bandpass_set_frequency(nullptr, 100.f);
    CHECK(yse_dsp_bandpass_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_bandpass_set_q(nullptr, 1.f);
    CHECK(yse_dsp_bandpass_get_q(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(lp);
    yse_dsp_object_destroy(hp);
    yse_dsp_object_destroy(bp);
  }

  TEST_CASE("c-api dsp module: sweep filter speed / depth / frequency round-trip") {
    YseDspObject* sweep = yse_dsp_sweep_create(YSE_SWEEP_SAW);
    REQUIRE(sweep != nullptr);

    yse_dsp_sweep_set_speed(sweep, 0.5f);
    CHECK(yse_dsp_sweep_get_speed(sweep) == doctest::Approx(0.5f));
    // depth and frequency are 0-100 scale positions, not Hz — values outside
    // the range are clamped by the engine.
    yse_dsp_sweep_set_depth(sweep, 12);
    CHECK(yse_dsp_sweep_get_depth(sweep) == 12);
    yse_dsp_sweep_set_frequency(sweep, 55);
    CHECK(yse_dsp_sweep_get_frequency(sweep) == 55);
    yse_dsp_sweep_set_frequency(sweep, 4000);
    CHECK(yse_dsp_sweep_get_frequency(sweep) == 100);

    yse_dsp_sweep_set_speed(nullptr, 1.f);
    CHECK(yse_dsp_sweep_get_speed(nullptr) == doctest::Approx(0.0f));
    yse_dsp_sweep_set_depth(nullptr, 1);
    CHECK(yse_dsp_sweep_get_depth(nullptr) == 0);
    yse_dsp_sweep_set_frequency(nullptr, 1);
    CHECK(yse_dsp_sweep_get_frequency(nullptr) == 0);

    yse_dsp_object_destroy(sweep);
  }

  TEST_CASE("c-api dsp module: delay taps and filtered-delay cutoffs round-trip") {
    YseDspObject* basic = yse_dsp_basic_delay_create();
    REQUIRE(basic != nullptr);

    yse_dsp_basic_delay_set_tap(basic, YSE_DELAY_TAP_FIRST, 120.0f, 0.5f);
    CHECK(yse_dsp_basic_delay_get_time(basic, YSE_DELAY_TAP_FIRST) == doctest::Approx(120.0f));
    CHECK(yse_dsp_basic_delay_get_gain(basic, YSE_DELAY_TAP_FIRST) == doctest::Approx(0.5f));
    yse_dsp_basic_delay_set_tap(basic, YSE_DELAY_TAP_SECOND, 240.0f, 0.25f);
    CHECK(yse_dsp_basic_delay_get_time(basic, YSE_DELAY_TAP_SECOND) == doctest::Approx(240.0f));
    CHECK(yse_dsp_basic_delay_get_gain(basic, YSE_DELAY_TAP_SECOND) == doctest::Approx(0.25f));

    yse_dsp_basic_delay_set_tap(nullptr, YSE_DELAY_TAP_FIRST, 1.f, 1.f);
    CHECK(yse_dsp_basic_delay_get_time(nullptr, YSE_DELAY_TAP_FIRST) == doctest::Approx(0.0f));
    CHECK(yse_dsp_basic_delay_get_gain(nullptr, YSE_DELAY_TAP_FIRST) == doctest::Approx(0.0f));

    YseDspObject* lpd = yse_dsp_lowpass_delay_create();
    YseDspObject* hpd = yse_dsp_highpass_delay_create();
    REQUIRE(lpd != nullptr);
    REQUIRE(hpd != nullptr);
    yse_dsp_lowpass_delay_set_frequency(lpd, 900.0f);
    CHECK(yse_dsp_lowpass_delay_get_frequency(lpd) == doctest::Approx(900.0f));
    yse_dsp_highpass_delay_set_frequency(hpd, 300.0f);
    CHECK(yse_dsp_highpass_delay_get_frequency(hpd) == doctest::Approx(300.0f));

    yse_dsp_lowpass_delay_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_lowpass_delay_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_highpass_delay_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_highpass_delay_get_frequency(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(basic);
    yse_dsp_object_destroy(lpd);
    yse_dsp_object_destroy(hpd);
  }

  TEST_CASE("c-api dsp module: phaser / ring modulator / difference round-trip") {
    YseDspObject* ph = yse_dsp_phaser_create();
    YseDspObject* rm = yse_dsp_ring_modulator_create();
    YseDspObject* diff = yse_dsp_difference_create();
    REQUIRE(ph != nullptr);
    REQUIRE(rm != nullptr);
    REQUIRE(diff != nullptr);

    yse_dsp_phaser_set_frequency(ph, 0.4f);
    CHECK(yse_dsp_phaser_get_frequency(ph) == doctest::Approx(0.4f));
    // range is clamped to [0, 0.5] — above that the all-pass cascade is unstable.
    yse_dsp_phaser_set_range(ph, 0.4f);
    CHECK(yse_dsp_phaser_get_range(ph) == doctest::Approx(0.4f));
    yse_dsp_phaser_set_range(ph, 0.9f);
    CHECK(yse_dsp_phaser_get_range(ph) == doctest::Approx(0.5f));

    yse_dsp_ring_modulator_set_frequency(rm, 55.0f);
    CHECK(yse_dsp_ring_modulator_get_frequency(rm) == doctest::Approx(55.0f));

    yse_dsp_difference_set_frequency(diff, 110.0f);
    CHECK(yse_dsp_difference_get_frequency(diff) == doctest::Approx(110.0f));
    yse_dsp_difference_set_amplitude(diff, 0.3f);
    CHECK(yse_dsp_difference_get_amplitude(diff) == doctest::Approx(0.3f));

    yse_dsp_phaser_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_phaser_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_phaser_set_range(nullptr, 1.f);
    CHECK(yse_dsp_phaser_get_range(nullptr) == doctest::Approx(0.0f));
    yse_dsp_ring_modulator_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_ring_modulator_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_difference_set_frequency(nullptr, 1.f);
    CHECK(yse_dsp_difference_get_frequency(nullptr) == doctest::Approx(0.0f));
    yse_dsp_difference_set_amplitude(nullptr, 1.f);
    CHECK(yse_dsp_difference_get_amplitude(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(ph);
    yse_dsp_object_destroy(rm);
    yse_dsp_object_destroy(diff);
  }

  TEST_CASE("c-api dsp module: granulator grain parameters round-trip") {
    YseDspObject* g = yse_dsp_granulator_create(8192, 16);
    REQUIRE(g != nullptr);

    yse_dsp_granulator_set_grain_frequency(g, 40);
    CHECK(yse_dsp_granulator_get_grain_frequency(g) == 40u);
    yse_dsp_granulator_set_grain_length(g, 2048, 256);
    CHECK(yse_dsp_granulator_get_grain_length(g) == 2048u);
    yse_dsp_granulator_set_grain_transpose(g, 1.5f, 0.1f);
    CHECK(yse_dsp_granulator_get_grain_transpose(g) == doctest::Approx(1.5f));
    yse_dsp_granulator_set_gain(g, 0.7f);
    CHECK(yse_dsp_granulator_get_gain(g) == doctest::Approx(0.7f));

    yse_dsp_granulator_set_grain_frequency(nullptr, 1);
    CHECK(yse_dsp_granulator_get_grain_frequency(nullptr) == 0u);
    yse_dsp_granulator_set_grain_length(nullptr, 1, 1);
    CHECK(yse_dsp_granulator_get_grain_length(nullptr) == 0u);
    yse_dsp_granulator_set_grain_transpose(nullptr, 1.f, 0.f);
    CHECK(yse_dsp_granulator_get_grain_transpose(nullptr) == doctest::Approx(0.0f));
    yse_dsp_granulator_set_gain(nullptr, 1.f);
    CHECK(yse_dsp_granulator_get_gain(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(g);
  }

  TEST_CASE("c-api dsp module: feedback delay parameters round-trip") {
    YseDspObject* d = yse_dsp_feedback_delay_create();
    REQUIRE(d != nullptr);

    yse_dsp_feedback_delay_set_time(d, 250.0f);
    CHECK(yse_dsp_feedback_delay_get_time(d) == doctest::Approx(250.0f));
    yse_dsp_feedback_delay_set_feedback(d, 0.45f);
    CHECK(yse_dsp_feedback_delay_get_feedback(d) == doctest::Approx(0.45f));
    yse_dsp_feedback_delay_set_damping(d, 4000.0f);
    CHECK(yse_dsp_feedback_delay_get_damping(d) == doctest::Approx(4000.0f));
    yse_dsp_feedback_delay_set_crossfeed(d, 0.2f);
    CHECK(yse_dsp_feedback_delay_get_crossfeed(d) == doctest::Approx(0.2f));

    yse_dsp_feedback_delay_set_time(nullptr, 1.f);
    CHECK(yse_dsp_feedback_delay_get_time(nullptr) == doctest::Approx(0.0f));
    yse_dsp_feedback_delay_set_feedback(nullptr, 1.f);
    CHECK(yse_dsp_feedback_delay_get_feedback(nullptr) == doctest::Approx(0.0f));
    yse_dsp_feedback_delay_set_damping(nullptr, 1.f);
    CHECK(yse_dsp_feedback_delay_get_damping(nullptr) == doctest::Approx(0.0f));
    yse_dsp_feedback_delay_set_crossfeed(nullptr, 1.f);
    CHECK(yse_dsp_feedback_delay_get_crossfeed(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(d);
  }

  TEST_CASE("c-api dsp module: chorus mode enum and parameters round-trip") {
    YseDspObject* c = yse_dsp_chorus_create();
    REQUIRE(c != nullptr);

    yse_dsp_chorus_set_mode(c, YSE_CHORUS_MODE_FLANGER);
    CHECK(yse_dsp_chorus_get_mode(c) == YSE_CHORUS_MODE_FLANGER);
    yse_dsp_chorus_set_mode(c, YSE_CHORUS_MODE_CHORUS);
    CHECK(yse_dsp_chorus_get_mode(c) == YSE_CHORUS_MODE_CHORUS);

    yse_dsp_chorus_set_rate(c, 0.8f);
    CHECK(yse_dsp_chorus_get_rate(c) == doctest::Approx(0.8f));
    yse_dsp_chorus_set_depth(c, 0.35f);
    CHECK(yse_dsp_chorus_get_depth(c) == doctest::Approx(0.35f));
    yse_dsp_chorus_set_feedback(c, 0.15f);
    CHECK(yse_dsp_chorus_get_feedback(c) == doctest::Approx(0.15f));
    yse_dsp_chorus_set_spread(c, 0.9f);
    CHECK(yse_dsp_chorus_get_spread(c) == doctest::Approx(0.9f));

    // NULL handle falls back to the documented default mode, not garbage.
    yse_dsp_chorus_set_mode(nullptr, YSE_CHORUS_MODE_FLANGER);
    CHECK(yse_dsp_chorus_get_mode(nullptr) == YSE_CHORUS_MODE_CHORUS);
    yse_dsp_chorus_set_rate(nullptr, 1.f);
    CHECK(yse_dsp_chorus_get_rate(nullptr) == doctest::Approx(0.0f));
    yse_dsp_chorus_set_depth(nullptr, 1.f);
    CHECK(yse_dsp_chorus_get_depth(nullptr) == doctest::Approx(0.0f));
    yse_dsp_chorus_set_feedback(nullptr, 1.f);
    CHECK(yse_dsp_chorus_get_feedback(nullptr) == doctest::Approx(0.0f));
    yse_dsp_chorus_set_spread(nullptr, 1.f);
    CHECK(yse_dsp_chorus_get_spread(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(c);
  }

  TEST_CASE("c-api dsp module: plate reverb parameters round-trip") {
    YseDspObject* p = yse_dsp_plate_reverb_create();
    REQUIRE(p != nullptr);

    yse_dsp_plate_reverb_set_decay(p, 0.6f);
    CHECK(yse_dsp_plate_reverb_get_decay(p) == doctest::Approx(0.6f));
    yse_dsp_plate_reverb_set_damping(p, 5000.0f);
    CHECK(yse_dsp_plate_reverb_get_damping(p) == doctest::Approx(5000.0f));
    yse_dsp_plate_reverb_set_predelay(p, 25.0f);
    CHECK(yse_dsp_plate_reverb_get_predelay(p) == doctest::Approx(25.0f));

    yse_dsp_plate_reverb_set_decay(nullptr, 1.f);
    CHECK(yse_dsp_plate_reverb_get_decay(nullptr) == doctest::Approx(0.0f));
    yse_dsp_plate_reverb_set_damping(nullptr, 1.f);
    CHECK(yse_dsp_plate_reverb_get_damping(nullptr) == doctest::Approx(0.0f));
    yse_dsp_plate_reverb_set_predelay(nullptr, 1.f);
    CHECK(yse_dsp_plate_reverb_get_predelay(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(p);
  }

  TEST_CASE("c-api dsp module: parametric EQ is addressed per band") {
    YseDspObject* eq = yse_dsp_eq_create();
    REQUIRE(eq != nullptr);

    const YseEqBand bands[] = {YSE_EQ_LOW_SHELF, YSE_EQ_PEAK_1, YSE_EQ_PEAK_2, YSE_EQ_HIGH_SHELF};
    float hz = 100.0f;
    for (YseEqBand band : bands) {
      yse_dsp_eq_set_frequency(eq, band, hz);
      CHECK(yse_dsp_eq_get_frequency(eq, band) == doctest::Approx(hz));
      yse_dsp_eq_set_gain(eq, band, -3.0f);
      CHECK(yse_dsp_eq_get_gain(eq, band) == doctest::Approx(-3.0f));
      yse_dsp_eq_set_q(eq, band, 1.4f);
      CHECK(yse_dsp_eq_get_q(eq, band) == doctest::Approx(1.4f));
      hz *= 4.0f;
    }
    // Bands are independent: setting the last one did not disturb the first.
    CHECK(yse_dsp_eq_get_frequency(eq, YSE_EQ_LOW_SHELF) == doctest::Approx(100.0f));

    yse_dsp_eq_set_frequency(nullptr, YSE_EQ_LOW_SHELF, 1.f);
    CHECK(yse_dsp_eq_get_frequency(nullptr, YSE_EQ_LOW_SHELF) == doctest::Approx(0.0f));
    yse_dsp_eq_set_gain(nullptr, YSE_EQ_LOW_SHELF, 1.f);
    CHECK(yse_dsp_eq_get_gain(nullptr, YSE_EQ_LOW_SHELF) == doctest::Approx(0.0f));
    yse_dsp_eq_set_q(nullptr, YSE_EQ_LOW_SHELF, 1.f);
    CHECK(yse_dsp_eq_get_q(nullptr, YSE_EQ_LOW_SHELF) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(eq);
  }

  TEST_CASE("c-api dsp module: compressor parameters and detector enum round-trip") {
    YseDspObject* c = yse_dsp_compressor_create();
    REQUIRE(c != nullptr);

    yse_dsp_compressor_set_detector(c, YSE_COMPRESSOR_DETECT_RMS);
    CHECK(yse_dsp_compressor_get_detector(c) == YSE_COMPRESSOR_DETECT_RMS);
    yse_dsp_compressor_set_detector(c, YSE_COMPRESSOR_DETECT_PEAK);
    CHECK(yse_dsp_compressor_get_detector(c) == YSE_COMPRESSOR_DETECT_PEAK);

    yse_dsp_compressor_set_threshold(c, -18.0f);
    CHECK(yse_dsp_compressor_get_threshold(c) == doctest::Approx(-18.0f));
    yse_dsp_compressor_set_ratio(c, 4.0f);
    CHECK(yse_dsp_compressor_get_ratio(c) == doctest::Approx(4.0f));
    yse_dsp_compressor_set_attack(c, 5.0f);
    CHECK(yse_dsp_compressor_get_attack(c) == doctest::Approx(5.0f));
    yse_dsp_compressor_set_release(c, 120.0f);
    CHECK(yse_dsp_compressor_get_release(c) == doctest::Approx(120.0f));
    yse_dsp_compressor_set_makeup(c, 2.0f);
    CHECK(yse_dsp_compressor_get_makeup(c) == doctest::Approx(2.0f));
    // Nothing has been rendered, so the meter reads no reduction yet.
    CHECK(yse_dsp_compressor_get_gain_reduction_db(c) == doctest::Approx(0.0f));

    yse_dsp_compressor_set_detector(nullptr, YSE_COMPRESSOR_DETECT_RMS);
    CHECK(yse_dsp_compressor_get_detector(nullptr) == YSE_COMPRESSOR_DETECT_PEAK);
    yse_dsp_compressor_set_threshold(nullptr, 1.f);
    CHECK(yse_dsp_compressor_get_threshold(nullptr) == doctest::Approx(0.0f));
    yse_dsp_compressor_set_ratio(nullptr, 1.f);
    CHECK(yse_dsp_compressor_get_ratio(nullptr) == doctest::Approx(0.0f));
    yse_dsp_compressor_set_attack(nullptr, 1.f);
    CHECK(yse_dsp_compressor_get_attack(nullptr) == doctest::Approx(0.0f));
    yse_dsp_compressor_set_release(nullptr, 1.f);
    CHECK(yse_dsp_compressor_get_release(nullptr) == doctest::Approx(0.0f));
    yse_dsp_compressor_set_makeup(nullptr, 1.f);
    CHECK(yse_dsp_compressor_get_makeup(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_dsp_compressor_get_gain_reduction_db(nullptr) == doctest::Approx(0.0f));

    yse_dsp_object_destroy(c);
  }

  TEST_CASE("c-api dsp module: morphing reverb presets round-trip field by field") {
    YseDspObject* r = yse_dsp_morphing_reverb_create();
    REQUIRE(r != nullptr);

    // Named presets on both slots.
    yse_dsp_morphing_reverb_set_preset_a(r, YSE_REVERB_HALL);
    yse_dsp_morphing_reverb_set_preset_b(r, YSE_REVERB_CAVE);
    YseReverbPresetValues hall{};
    YseReverbPresetValues cave{};
    yse_dsp_morphing_reverb_get_preset_a(r, &hall);
    yse_dsp_morphing_reverb_get_preset_b(r, &cave);
    // Two different named presets must not decode to the same values.
    CHECK((hall.roomsize != cave.roomsize || hall.damp != cave.damp || hall.wet != cave.wet ||
           hall.dry != cave.dry));

    // Explicit values on both slots — the C mirror struct is copied field by
    // field, so every field has to survive the trip.
    YseReverbPresetValues custom{};
    custom.roomsize = 0.71f;
    custom.damp = 0.33f;
    custom.dry = 0.42f;
    custom.wet = 0.58f;
    custom.mod_frequency = 1.25f;
    custom.mod_width = 0.02f;
    for (int i = 0; i < 4; ++i) {
      custom.early_time[i] = 0.01f * static_cast<float>(i + 1);
      custom.early_gain[i] = 0.1f * static_cast<float>(i + 1);
    }

    yse_dsp_morphing_reverb_set_preset_a_values(r, &custom);
    yse_dsp_morphing_reverb_set_preset_b_values(r, &custom);
    YseReverbPresetValues backA{};
    YseReverbPresetValues backB{};
    yse_dsp_morphing_reverb_get_preset_a(r, &backA);
    yse_dsp_morphing_reverb_get_preset_b(r, &backB);
    for (const YseReverbPresetValues* back : {&backA, &backB}) {
      CHECK(back->roomsize == doctest::Approx(custom.roomsize));
      CHECK(back->damp == doctest::Approx(custom.damp));
      CHECK(back->dry == doctest::Approx(custom.dry));
      CHECK(back->wet == doctest::Approx(custom.wet));
      CHECK(back->mod_frequency == doctest::Approx(custom.mod_frequency));
      CHECK(back->mod_width == doctest::Approx(custom.mod_width));
      for (int i = 0; i < 4; ++i) {
        CHECK(back->early_time[i] == doctest::Approx(custom.early_time[i]));
        CHECK(back->early_gain[i] == doctest::Approx(custom.early_gain[i]));
      }
    }

    yse_dsp_morphing_reverb_set_morph(r, 0.4f);
    CHECK(yse_dsp_morphing_reverb_get_morph(r) == doctest::Approx(0.4f));

    // NULL handle / NULL out-parameter contracts.
    yse_dsp_morphing_reverb_set_preset_a(nullptr, YSE_REVERB_HALL);
    yse_dsp_morphing_reverb_set_preset_b(nullptr, YSE_REVERB_HALL);
    yse_dsp_morphing_reverb_set_preset_a_values(nullptr, &custom);
    yse_dsp_morphing_reverb_set_preset_b_values(nullptr, &custom);
    yse_dsp_morphing_reverb_set_preset_a_values(r, nullptr);
    yse_dsp_morphing_reverb_set_preset_b_values(r, nullptr);
    yse_dsp_morphing_reverb_get_preset_a(r, nullptr); // must not write anywhere
    yse_dsp_morphing_reverb_get_preset_b(r, nullptr);
    yse_dsp_morphing_reverb_set_morph(nullptr, 1.0f);
    CHECK(yse_dsp_morphing_reverb_get_morph(nullptr) == doctest::Approx(0.0f));

    // A NULL handle with a live out-parameter zero-fills rather than leaving
    // the caller's struct untouched.
    YseReverbPresetValues dirty{};
    dirty.roomsize = 9.0f;
    yse_dsp_morphing_reverb_get_preset_a(nullptr, &dirty);
    CHECK(dirty.roomsize == doctest::Approx(0.0f));
    dirty.roomsize = 9.0f;
    yse_dsp_morphing_reverb_get_preset_b(nullptr, &dirty);
    CHECK(dirty.roomsize == doctest::Approx(0.0f));

    // The custom values survived every NULL-argument call above unchanged.
    YseReverbPresetValues after{};
    yse_dsp_morphing_reverb_get_preset_a(r, &after);
    CHECK(after.roomsize == doctest::Approx(custom.roomsize));

    yse_dsp_object_destroy(r);
  }

} // TEST_SUITE("capilowcov")
