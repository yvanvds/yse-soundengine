// Tests for the scalar utility functions in YseEngine/dsp/math_functions.h.
// These are pure computations with no audio-device or system dependencies.
//
// Amplitude reference convention used throughout the engine:
//   100 dB  ↔  RMS amplitude 1.0
//   100 dB  ↔  power 1.0
// (LOGTEN = ln(10) ≈ 2.302585)

#include <doctest/doctest.h>
#include "dsp/math_functions.h"

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
        CHECK(YSE::DSP::rmsToDb(YSE::DSP::dbToRms(db)) ==
              doctest::Approx(db).epsilon(1e-4f));
    }
}

TEST_CASE("dbToPow / powToDb round-trip") {
    const float kbs[] = {50.0f, 80.0f, 100.0f, 120.0f, 140.0f};
    for (float db : kbs) {
        CHECK(YSE::DSP::powToDb(YSE::DSP::dbToPow(db)) ==
              doctest::Approx(db).epsilon(1e-4f));
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
    float a[]   = {1.0f, -1.0f, 0.5f};
    float b[]   = {0.5f,  0.5f, 0.5f};
    float out[3] = {};
    YSE::DSP::maximum(a, b, out, 3);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.5f);
    CHECK(out[2] == 0.5f);
}

TEST_CASE("maximum: scalar clamps negative values up to floor") {
    float in[]   = {1.0f, -1.0f, 0.0f, 0.5f};
    float out[4] = {};
    YSE::DSP::maximum(in, 0.0f, out, 4);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == 0.0f);
    CHECK(out[3] == 0.5f);
}

TEST_CASE("minimum: element-wise selects smaller of two arrays") {
    float a[]   = {1.0f, -1.0f, 0.5f};
    float b[]   = {0.5f,  0.5f, 0.5f};
    float out[3] = {};
    YSE::DSP::minimum(a, b, out, 3);
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == -1.0f);
    CHECK(out[2] == 0.5f);
}

TEST_CASE("minimum: scalar clamps positive values down to ceiling") {
    float in[]   = {1.0f, -1.0f, 0.0f, 0.5f};
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
    ptr[0] = 0.3f; ptr[1] = 0.7f; ptr[2] = 0.5f; ptr[3] = -0.9f;
    CHECK(YSE::DSP::getMaxAmplitude(buf) == doctest::Approx(0.7f));
}

TEST_CASE("getMaxAmplitude: all-negative buffer returns zero") {
    YSE::DSP::buffer buf(3);
    float* ptr = buf.getPtr();
    ptr[0] = -0.3f; ptr[1] = -0.7f; ptr[2] = -0.5f;
    CHECK(YSE::DSP::getMaxAmplitude(buf) == 0.0f);
}

TEST_CASE("getMaxAmplitude: pointer overload matches buffer overload") {
    float data[] = {0.1f, 0.9f, 0.4f};
    CHECK(YSE::DSP::getMaxAmplitude(data, 3) == doctest::Approx(0.9f));
}

TEST_CASE("sqrtFunc: approximates square root to within 8 mantissa bits") {
    float in[]  = {4.0f, 9.0f, 0.25f, 16.0f};
    float out[4] = {};
    YSE::DSP::sqrtFunc(in, out, 4);
    CHECK(out[0] == doctest::Approx(2.0f).epsilon(0.005f));
    CHECK(out[1] == doctest::Approx(3.0f).epsilon(0.005f));
    CHECK(out[2] == doctest::Approx(0.5f).epsilon(0.005f));
    CHECK(out[3] == doctest::Approx(4.0f).epsilon(0.005f));
}

TEST_CASE("sqrtFunc: negative input returns zero") {
    float in[]  = {-1.0f};
    float out[1] = {999.0f};
    YSE::DSP::sqrtFunc(in, out, 1);
    CHECK(out[0] == 0.0f);
}

} // TEST_SUITE("dsp")
