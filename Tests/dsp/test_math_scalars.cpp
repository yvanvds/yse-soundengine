// Tests for the scalar utility functions in YseEngine/dsp/math_functions.h.
// These are pure computations with no audio-device or system dependencies.
//
// Amplitude reference convention used throughout the engine:
//   100 dB  ↔  RMS amplitude 1.0
//   100 dB  ↔  power 1.0
// (LOGTEN = ln(10) ≈ 2.302585)

#include <doctest/doctest.h>
#include "dsp/math_functions.h"

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
