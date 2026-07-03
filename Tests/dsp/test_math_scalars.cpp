// Tests for the scalar utility functions in YseEngine/dsp/math_functions.h
// and for the buffer-call wrapper classes in YseEngine/dsp/math.hpp
// (clip, rSqrt, sqrt, wrap, dbToRms, dbToPow, rmsToDb, powToDb, pow, exp, log,
// abs, inverter). These are pure computations with no audio-device or system
// dependencies.
//
// Amplitude reference convention used throughout the engine:
//   100 dB  ↔  RMS amplitude 1.0
//   100 dB  ↔  power 1.0
// (LOGTEN = ln(10) ≈ 2.302585)

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/math_functions.h"
#include "dsp/math.hpp"

TEST_SUITE("dsp") {

  TEST_CASE("dbToRms: reference level 100 dB = 1.0") {
    CHECK(YSE::DSP::dbToRms(100.0f) == doctest::Approx(1.0f).epsilon(1e-5f));
  }

  TEST_CASE("dbToRms: -20 dB from reference gives 0.1") {
    // exp(ln10 * 0.05 * (80 - 100)) = exp(-ln10) = 0.1
    CHECK(YSE::DSP::dbToRms(80.0f) == doctest::Approx(0.1f).epsilon(1e-5f));
  }

  TEST_CASE("dbToRms: zero and negative inputs clamp to zero") {
    CHECK(YSE::DSP::dbToRms(0.0f) == 0.0f);
    CHECK(YSE::DSP::dbToRms(-10.0f) == 0.0f);
  }

  TEST_CASE("rmsToDb: RMS 1.0 = 100 dB") {
    CHECK(YSE::DSP::rmsToDb(1.0f) == doctest::Approx(100.0f).epsilon(1e-4f));
  }

  TEST_CASE("rmsToDb: RMS 0.1 = 80 dB") {
    // 100 + 20/ln10 * ln(0.1) = 100 - 20 = 80
    CHECK(YSE::DSP::rmsToDb(0.1f) == doctest::Approx(80.0f).epsilon(1e-4f));
  }

  TEST_CASE("rmsToDb: zero and negative inputs clamp to zero") {
    CHECK(YSE::DSP::rmsToDb(0.0f) == 0.0f);
    CHECK(YSE::DSP::rmsToDb(-1.0f) == 0.0f);
  }

  TEST_CASE("dbToPow: reference level 100 dB = power 1.0") {
    CHECK(YSE::DSP::dbToPow(100.0f) == doctest::Approx(1.0f).epsilon(1e-5f));
  }

  TEST_CASE("dbToPow: 90 dB = power 0.1") {
    // exp(ln10 * 0.1 * (90 - 100)) = exp(-ln10) = 0.1
    CHECK(YSE::DSP::dbToPow(90.0f) == doctest::Approx(0.1f).epsilon(1e-5f));
  }

  TEST_CASE("dbToPow: zero and negative inputs clamp to zero") {
    CHECK(YSE::DSP::dbToPow(0.0f) == 0.0f);
    CHECK(YSE::DSP::dbToPow(-5.0f) == 0.0f);
  }

  TEST_CASE("powToDb: power 1.0 = 100 dB") {
    CHECK(YSE::DSP::powToDb(1.0f) == doctest::Approx(100.0f).epsilon(1e-4f));
  }

  TEST_CASE("powToDb: power 0.1 = 90 dB") {
    // 100 + 10/ln10 * ln(0.1) = 100 - 10 = 90
    CHECK(YSE::DSP::powToDb(0.1f) == doctest::Approx(90.0f).epsilon(1e-4f));
  }

  TEST_CASE("powToDb: zero and negative inputs clamp to zero") {
    CHECK(YSE::DSP::powToDb(0.0f) == 0.0f);
    CHECK(YSE::DSP::powToDb(-1.0f) == 0.0f);
  }

  TEST_CASE("dbToRms / rmsToDb round-trip") {
    // Verify the two functions are genuine inverses over a range well away from
    // the clamping boundary (below ~0 dB rmsToDb clamps to 0).
    const float kbs[] = {50.0f, 80.0f, 100.0f, 120.0f, 140.0f};
    for (float db : kbs) {
      CHECK(YSE::DSP::rmsToDb(YSE::DSP::dbToRms(db)) == doctest::Approx(db).epsilon(1e-4f));
    }
  }

  TEST_CASE("dbToPow / powToDb round-trip") {
    const float kbs[] = {50.0f, 80.0f, 100.0f, 120.0f, 140.0f};
    for (float db : kbs) {
      CHECK(YSE::DSP::powToDb(YSE::DSP::dbToPow(db)) == doctest::Approx(db).epsilon(1e-4f));
    }
  }

  TEST_CASE("RMS and power: dbToPow(x) == dbToRms(x)^2") {
    // Power is proportional to amplitude squared; the engine's dB formulas
    // use a 100-dB reference so this identity holds exactly.
    for (float db : {60.0f, 80.0f, 100.0f, 110.0f}) {
      float rms = YSE::DSP::dbToRms(db);
      CHECK(YSE::DSP::dbToPow(db) == doctest::Approx(rms * rms).epsilon(1e-5f));
    }
  }

  TEST_CASE("dbToPow: values above 870 dB are clamped to the 870 dB result") {
    // Both 870 and 1000 dB overflow float to +inf; the clamp ensures they produce
    // the same result. Use direct == because Approx does not handle infinity.
    CHECK(YSE::DSP::dbToPow(900.0f) == YSE::DSP::dbToPow(870.0f));
  }

  TEST_CASE("dbToRms: values above 485 dB are clamped") {
    CHECK(YSE::DSP::dbToRms(485.0f) == doctest::Approx(YSE::DSP::dbToRms(600.0f)).epsilon(1e-5f));
    CHECK(YSE::DSP::dbToRms(485.0f) > 0.0f);
  }

  TEST_CASE("maximum: element-wise selects larger of two arrays") {
    float a[] = {1.0f, -1.0f, 0.5f};
    float b[] = {0.5f, 0.5f, 0.5f};
    float out[3] = {};
    YSE::DSP::maximum(a, b, out, 3);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.5f);
    CHECK(out[2] == 0.5f);
  }

  TEST_CASE("maximum: scalar clamps negative values up to floor") {
    float in[] = {1.0f, -1.0f, 0.0f, 0.5f};
    float out[4] = {};
    YSE::DSP::maximum(in, 0.0f, out, 4);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 0.5f);
  }

  TEST_CASE("minimum: element-wise selects smaller of two arrays") {
    float a[] = {1.0f, -1.0f, 0.5f};
    float b[] = {0.5f, 0.5f, 0.5f};
    float out[3] = {};
    YSE::DSP::minimum(a, b, out, 3);
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == -1.0f);
    CHECK(out[2] == 0.5f);
  }

  TEST_CASE("minimum: scalar clamps positive values down to ceiling") {
    float in[] = {1.0f, -1.0f, 0.0f, 0.5f};
    float out[4] = {};
    YSE::DSP::minimum(in, 0.0f, out, 4);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == -1.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 0.0f);
  }

  TEST_CASE("getMaxAmplitude: returns largest positive value from buffer") {
    YSE::DSP::buffer buf(4);
    float* ptr = buf.getPtr();
    ptr[0] = 0.3f;
    ptr[1] = 0.7f;
    ptr[2] = 0.5f;
    ptr[3] = -0.9f;
    CHECK(YSE::DSP::getMaxAmplitude(buf) == doctest::Approx(0.7f));
  }

  TEST_CASE("getMaxAmplitude: all-negative buffer returns zero") {
    YSE::DSP::buffer buf(3);
    float* ptr = buf.getPtr();
    ptr[0] = -0.3f;
    ptr[1] = -0.7f;
    ptr[2] = -0.5f;
    CHECK(YSE::DSP::getMaxAmplitude(buf) == 0.0f);
  }

  TEST_CASE("getMaxAmplitude: pointer overload matches buffer overload") {
    float data[] = {0.1f, 0.9f, 0.4f};
    CHECK(YSE::DSP::getMaxAmplitude(data, 3) == doctest::Approx(0.9f));
  }

  TEST_CASE("sqrtFunc: approximates square root to within 8 mantissa bits") {
    float in[] = {4.0f, 9.0f, 0.25f, 16.0f};
    float out[4] = {};
    YSE::DSP::sqrtFunc(in, out, 4);
    CHECK(out[0] == doctest::Approx(2.0f).epsilon(0.005f));
    CHECK(out[1] == doctest::Approx(3.0f).epsilon(0.005f));
    CHECK(out[2] == doctest::Approx(0.5f).epsilon(0.005f));
    CHECK(out[3] == doctest::Approx(4.0f).epsilon(0.005f));
  }

  TEST_CASE("sqrtFunc: negative input returns zero") {
    float in[] = {-1.0f};
    float out[1] = {999.0f};
    YSE::DSP::sqrtFunc(in, out, 1);
    CHECK(out[0] == 0.0f);
  }

  // --- clip (buffer-call class) ---

  TEST_CASE("clip: default range is [-1, 1] and clamps both sides") {
    YSE::DSP::buffer in(5);
    in.getPtr()[0] = -2.0f;
    in.getPtr()[1] = -0.5f;
    in.getPtr()[2] = 0.0f;
    in.getPtr()[3] = 0.5f;
    in.getPtr()[4] = 2.0f;
    YSE::DSP::clip c;
    YSE::DSP::buffer& out = c(in);
    REQUIRE(out.getLength() == 5u);
    CHECK(out.getPtr()[0] == -1.0f);
    CHECK(out.getPtr()[1] == -0.5f);
    CHECK(out.getPtr()[2] == 0.0f);
    CHECK(out.getPtr()[3] == 0.5f);
    CHECK(out.getPtr()[4] == 1.0f);
  }

  TEST_CASE("clip: set() applies new low and high") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = -5.0f;
    in.getPtr()[1] = 2.5f;
    in.getPtr()[2] = 10.0f;
    YSE::DSP::clip c;
    c.set(0.0f, 5.0f);
    YSE::DSP::buffer& out = c(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 2.5f);
    CHECK(out.getPtr()[2] == 5.0f);
  }

  TEST_CASE("clip: setLow / setHigh adjust bounds independently") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = -10.0f;
    in.getPtr()[1] = 10.0f;
    YSE::DSP::clip c;
    c.setLow(-2.0f).setHigh(3.0f);
    YSE::DSP::buffer& out = c(in);
    CHECK(out.getPtr()[0] == -2.0f);
    CHECK(out.getPtr()[1] == 3.0f);
  }

  TEST_CASE("clip: resizes its output buffer when input length changes") {
    YSE::DSP::clip c;
    YSE::DSP::buffer small(4);
    small = 0.5f;
    YSE::DSP::buffer& outSmall = c(small);
    CHECK(outSmall.getLength() == 4u);
    YSE::DSP::buffer big(16);
    big = 0.5f;
    YSE::DSP::buffer& outBig = c(big);
    CHECK(outBig.getLength() == 16u);
  }

  // --- rSqrt / sqrt (buffer-call classes) ---

  TEST_CASE("rSqrt: reciprocal sqrt approximates 1/sqrt(x)") {
    YSE::DSP::buffer in(4);
    in.getPtr()[0] = 1.0f;
    in.getPtr()[1] = 4.0f;
    in.getPtr()[2] = 16.0f;
    in.getPtr()[3] = 100.0f;
    YSE::DSP::rSqrt r;
    YSE::DSP::buffer& out = r(in);
    CHECK(out.getPtr()[0] == doctest::Approx(1.0f).epsilon(0.005f));
    CHECK(out.getPtr()[1] == doctest::Approx(0.5f).epsilon(0.005f));
    CHECK(out.getPtr()[2] == doctest::Approx(0.25f).epsilon(0.005f));
    CHECK(out.getPtr()[3] == doctest::Approx(0.1f).epsilon(0.005f));
  }

  TEST_CASE("rSqrt: negative input returns zero") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = -1.0f;
    in.getPtr()[1] = 4.0f;
    YSE::DSP::rSqrt r;
    YSE::DSP::buffer& out = r(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == doctest::Approx(0.5f).epsilon(0.005f));
  }

  TEST_CASE("sqrt class: approximates sqrt(x)") {
    YSE::DSP::buffer in(4);
    in.getPtr()[0] = 4.0f;
    in.getPtr()[1] = 9.0f;
    in.getPtr()[2] = 0.25f;
    in.getPtr()[3] = 16.0f;
    YSE::DSP::sqrt s;
    YSE::DSP::buffer& out = s(in);
    CHECK(out.getPtr()[0] == doctest::Approx(2.0f).epsilon(0.005f));
    CHECK(out.getPtr()[1] == doctest::Approx(3.0f).epsilon(0.005f));
    CHECK(out.getPtr()[2] == doctest::Approx(0.5f).epsilon(0.005f));
    CHECK(out.getPtr()[3] == doctest::Approx(4.0f).epsilon(0.005f));
  }

  TEST_CASE("sqrt class: negative input returns zero") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = -4.0f;
    in.getPtr()[1] = 9.0f;
    YSE::DSP::sqrt s;
    YSE::DSP::buffer& out = s(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == doctest::Approx(3.0f).epsilon(0.005f));
  }

  // --- wrap ---

  TEST_CASE("wrap: returns fractional part of positive values") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = 1.25f;
    in.getPtr()[1] = 2.75f;
    in.getPtr()[2] = 0.5f;
    YSE::DSP::wrap w;
    YSE::DSP::buffer& out = w(in);
    CHECK(out.getPtr()[0] == doctest::Approx(0.25f).epsilon(1e-5f));
    CHECK(out.getPtr()[1] == doctest::Approx(0.75f).epsilon(1e-5f));
    CHECK(out.getPtr()[2] == doctest::Approx(0.5f).epsilon(1e-5f));
  }

  TEST_CASE("wrap: negative values produce a positive fractional part") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = -1.25f; // -1.25 - (-2) = 0.75
    in.getPtr()[1] = -0.5f; // -0.5  - (-1) = 0.5
    YSE::DSP::wrap w;
    YSE::DSP::buffer& out = w(in);
    CHECK(out.getPtr()[0] == doctest::Approx(0.75f).epsilon(1e-5f));
    CHECK(out.getPtr()[1] == doctest::Approx(0.5f).epsilon(1e-5f));
  }

  // --- dbToRms / rmsToDb / dbToPow / powToDb (buffer-call classes) ---

  TEST_CASE("dbToRms class: 100 dB → 1.0 across a buffer") {
    YSE::DSP::buffer in(3);
    in = 100.0f;
    class YSE::DSP::dbToRms d2r;
    YSE::DSP::buffer& out = d2r(in);
    for (unsigned i = 0; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(1.0f).epsilon(1e-4f));
  }

  TEST_CASE("dbToRms class: <=0 dB clamps to 0 and >485 dB is clamped at the 485 value") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = -10.0f;
    in.getPtr()[1] = 0.0f;
    in.getPtr()[2] = 1000.0f;
    class YSE::DSP::dbToRms d2r;
    YSE::DSP::buffer& out = d2r(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 0.0f);
    CHECK(out.getPtr()[2] > 0.0f); // clamped, not zero
  }

  TEST_CASE("rmsToDb class: round-trip with dbToRms class") {
    YSE::DSP::buffer in(4);
    in.getPtr()[0] = 50.0f;
    in.getPtr()[1] = 80.0f;
    in.getPtr()[2] = 100.0f;
    in.getPtr()[3] = 120.0f;
    class YSE::DSP::dbToRms d2r;
    class YSE::DSP::rmsToDb r2d;
    YSE::DSP::buffer& rms = d2r(in);
    YSE::DSP::buffer& db = r2d(rms);
    CHECK(db.getPtr()[0] == doctest::Approx(50.0f).epsilon(1e-3f));
    CHECK(db.getPtr()[1] == doctest::Approx(80.0f).epsilon(1e-3f));
    CHECK(db.getPtr()[2] == doctest::Approx(100.0f).epsilon(1e-3f));
    CHECK(db.getPtr()[3] == doctest::Approx(120.0f).epsilon(1e-3f));
  }

  TEST_CASE("rmsToDb class: non-positive amplitude maps to 0 dB") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = 0.0f;
    in.getPtr()[1] = -0.5f;
    class YSE::DSP::rmsToDb r2d;
    YSE::DSP::buffer& out = r2d(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 0.0f);
  }

  TEST_CASE("rmsToDb class: extremely small positive amplitude clamps to 0 dB") {
    // 100 + 20/ln10 * ln(1e-12) ≈ -140, the function clamps g<0 to 0
    YSE::DSP::buffer in(1);
    in.getPtr()[0] = 1e-12f;
    class YSE::DSP::rmsToDb r2d;
    YSE::DSP::buffer& out = r2d(in);
    CHECK(out.getPtr()[0] == 0.0f);
  }

  TEST_CASE("dbToPow class: 100 dB → 1.0 across a buffer") {
    YSE::DSP::buffer in(2);
    in = 100.0f;
    class YSE::DSP::dbToPow d2p;
    YSE::DSP::buffer& out = d2p(in);
    for (unsigned i = 0; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(1.0f).epsilon(1e-4f));
  }

  TEST_CASE("dbToPow class: clamping branches (<=0 → 0, >870 → clamped value)") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = -5.0f;
    in.getPtr()[1] = 0.0f;
    in.getPtr()[2] = 2000.0f;
    class YSE::DSP::dbToPow d2p;
    YSE::DSP::buffer& out = d2p(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 0.0f);
    CHECK(out.getPtr()[2] > 0.0f);
  }

  TEST_CASE("powToDb class: round-trip with dbToPow class") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = 50.0f;
    in.getPtr()[1] = 100.0f;
    in.getPtr()[2] = 150.0f;
    class YSE::DSP::dbToPow d2p;
    class YSE::DSP::powToDb p2d;
    YSE::DSP::buffer& pow = d2p(in);
    YSE::DSP::buffer& db = p2d(pow);
    CHECK(db.getPtr()[0] == doctest::Approx(50.0f).epsilon(1e-3f));
    CHECK(db.getPtr()[1] == doctest::Approx(100.0f).epsilon(1e-3f));
    CHECK(db.getPtr()[2] == doctest::Approx(150.0f).epsilon(1e-3f));
  }

  TEST_CASE("powToDb class: non-positive power maps to 0 dB") {
    YSE::DSP::buffer in(2);
    in.getPtr()[0] = 0.0f;
    in.getPtr()[1] = -1.0f;
    class YSE::DSP::powToDb p2d;
    YSE::DSP::buffer& out = p2d(in);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 0.0f);
  }

  TEST_CASE("powToDb class: extremely small positive power clamps to 0 dB") {
    YSE::DSP::buffer in(1);
    in.getPtr()[0] = 1e-12f;
    class YSE::DSP::powToDb p2d;
    YSE::DSP::buffer& out = p2d(in);
    CHECK(out.getPtr()[0] == 0.0f);
  }

  // --- pow / exp / log / abs ---

  TEST_CASE("pow class: element-wise base^exponent") {
    YSE::DSP::buffer base(3), exponent(3);
    base.getPtr()[0] = 2.0f;
    base.getPtr()[1] = 3.0f;
    base.getPtr()[2] = 5.0f;
    exponent.getPtr()[0] = 3.0f;
    exponent.getPtr()[1] = 2.0f;
    exponent.getPtr()[2] = 0.0f;
    YSE::DSP::pow p;
    YSE::DSP::buffer& out = p(base, exponent);
    CHECK(out.getPtr()[0] == doctest::Approx(8.0f).epsilon(1e-4f));
    CHECK(out.getPtr()[1] == doctest::Approx(9.0f).epsilon(1e-4f));
    CHECK(out.getPtr()[2] == doctest::Approx(1.0f).epsilon(1e-4f));
  }

  TEST_CASE("pow class: non-positive base produces 0") {
    YSE::DSP::buffer base(2), exponent(2);
    base.getPtr()[0] = -2.0f;
    base.getPtr()[1] = 0.0f;
    exponent.getPtr()[0] = 3.0f;
    exponent.getPtr()[1] = 2.0f;
    YSE::DSP::pow p;
    YSE::DSP::buffer& out = p(base, exponent);
    CHECK(out.getPtr()[0] == 0.0f);
    CHECK(out.getPtr()[1] == 0.0f);
  }

  TEST_CASE("exp class: element-wise e^x") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = 0.0f;
    in.getPtr()[1] = 1.0f;
    in.getPtr()[2] = 2.0f;
    YSE::DSP::exp e;
    YSE::DSP::buffer& out = e(in);
    CHECK(out.getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-5f));
    CHECK(out.getPtr()[1] == doctest::Approx(std::exp(1.0f)).epsilon(1e-5f));
    CHECK(out.getPtr()[2] == doctest::Approx(std::exp(2.0f)).epsilon(1e-5f));
  }

  TEST_CASE("log class: log of in1 with base in2") {
    YSE::DSP::buffer in1(2), in2(2);
    in1.getPtr()[0] = 8.0f;
    in2.getPtr()[0] = 2.0f; // log2(8) = 3
    in1.getPtr()[1] = 100.0f;
    in2.getPtr()[1] = 10.0f; // log10(100) = 2
    YSE::DSP::log l;
    YSE::DSP::buffer& out = l(in1, in2);
    CHECK(out.getPtr()[0] == doctest::Approx(3.0f).epsilon(1e-4f));
    CHECK(out.getPtr()[1] == doctest::Approx(2.0f).epsilon(1e-4f));
  }

  TEST_CASE("log class: f<=0 falls back to natural log of f") {
    // The branch in math.cpp:329 returns ::log(f) when f<=0 (NaN/-inf path).
    YSE::DSP::buffer in1(1), in2(1);
    in1.getPtr()[0] = -1.0f;
    in2.getPtr()[0] = 10.0f;
    YSE::DSP::log l;
    YSE::DSP::buffer& out = l(in1, in2);
    // log(-1) is NaN — we just verify the branch ran (i.e. result is not the
    // natural-log change-of-base value that would be produced for f>0).
    CHECK_FALSE(out.getPtr()[0] > 0.0f);
  }

  TEST_CASE("log class: g<=0 falls back to natural log of f") {
    // The branch in math.cpp:330 returns ::log(f) when g<=0.
    YSE::DSP::buffer in1(1), in2(1);
    in1.getPtr()[0] = std::exp(1.0f); // ln(e) = 1
    in2.getPtr()[0] = 0.0f; // forces the g<=0 branch
    YSE::DSP::log l;
    YSE::DSP::buffer& out = l(in1, in2);
    CHECK(out.getPtr()[0] == doctest::Approx(1.0f).epsilon(1e-4f));
  }

  TEST_CASE("abs class: returns absolute value of each sample") {
    YSE::DSP::buffer in(4);
    in.getPtr()[0] = -3.0f;
    in.getPtr()[1] = 0.0f;
    in.getPtr()[2] = 1.5f;
    in.getPtr()[3] = -7.25f;
    YSE::DSP::abs a;
    YSE::DSP::buffer& out = a(in);
    CHECK(out.getPtr()[0] == 3.0f);
    CHECK(out.getPtr()[1] == 0.0f);
    CHECK(out.getPtr()[2] == 1.5f);
    CHECK(out.getPtr()[3] == 7.25f);
  }

  // --- inverter ---

  TEST_CASE("inverter: default (zeroToOne=false) negates each sample (large buffer covers unrolled "
            "loop)") {
    // > 8 samples so both the unrolled 8-at-a-time loop and the tail run.
    constexpr unsigned N = 11;
    YSE::DSP::buffer in(N);
    for (unsigned i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i + 1);
    YSE::DSP::inverter inv;
    YSE::DSP::buffer& out = inv(in);
    for (unsigned i = 0; i < N; ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(-static_cast<float>(i + 1)).epsilon(1e-6f));
  }

  TEST_CASE("inverter: zeroToOne=true computes 1 - sample (large buffer covers unrolled loop)") {
    constexpr unsigned N = 11;
    YSE::DSP::buffer in(N);
    for (unsigned i = 0; i < N; ++i)
      in.getPtr()[i] = 0.1f * static_cast<float>(i);
    YSE::DSP::inverter inv;
    YSE::DSP::buffer& out = inv(in, /*zeroToOne=*/true);
    for (unsigned i = 0; i < N; ++i)
      CHECK(out.getPtr()[i] == doctest::Approx(1.0f - 0.1f * static_cast<float>(i)).epsilon(1e-5f));
  }

  TEST_CASE("inverter: small buffer (length < 8) only exercises the tail loop") {
    YSE::DSP::buffer in(3);
    in.getPtr()[0] = 0.25f;
    in.getPtr()[1] = -0.5f;
    in.getPtr()[2] = 1.0f;
    YSE::DSP::inverter inv;
    YSE::DSP::buffer& out = inv(in);
    CHECK(out.getPtr()[0] == doctest::Approx(-0.25f));
    CHECK(out.getPtr()[1] == doctest::Approx(0.5f));
    CHECK(out.getPtr()[2] == doctest::Approx(-1.0f));
  }

} // TEST_SUITE("dsp")
