// Tests for YSE::DSP::ampToDb, dbToAmp, msToSamples and samplesToMs and their
// scalar forms (YseEngine/dsp/operators/units.hpp), issue #1052, plus the
// base / ref tuning of midiToFreq and freqToMidi (dsp/math.hpp). Reference
// output is closed form: 20 log10 |a|, 10 ** (db / 20), ms * rate / 1000,
// samples * 1000 / rate, base * 2 ** ((note - ref) / 12).

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/math.hpp"
#include "dsp/operators/units.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  constexpr float kInf = std::numeric_limits<float>::infinity();

  bool AllFinite(YSE::DSP::buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (!std::isfinite(b.getPtr()[i])) return false;
    return true;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("AmpToDb / DbToAmp: closed form, 0 dB is an amplitude of 1 (#1052)") {
    using namespace YSE::DSP;
    CHECK(AmpToDb(1.f) == doctest::Approx(0.f));
    CHECK(AmpToDb(0.5f) == doctest::Approx(-6.0206f).epsilon(1e-4));
    CHECK(AmpToDb(2.f) == doctest::Approx(6.0206f).epsilon(1e-4));
    CHECK(AmpToDb(0.001f) == doctest::Approx(-60.f).epsilon(1e-4));
    CHECK(AmpToDb(-0.5f) == AmpToDb(0.5f)); // the sign is ignored
    CHECK(DbToAmp(0.f) == doctest::Approx(1.f));
    CHECK(DbToAmp(-6.0206f) == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(DbToAmp(20.f) == doctest::Approx(10.f).epsilon(1e-5));
    CHECK(DbToAmp(-60.f) == doctest::Approx(0.001f).epsilon(1e-4));
    for (float a : {0.01f, 0.1f, 0.25f, 0.7f, 1.f, 3.f, 100.f}) {
      CAPTURE(a);
      CHECK(DbToAmp(AmpToDb(a)) == doctest::Approx(a).epsilon(1e-5));
    }
  }

  TEST_CASE("AmpToDb / DbToAmp: silence, NaN and infinity stay finite (#1052)") {
    using namespace YSE::DSP;
    CHECK(AmpToDb(0.f) == DB_FLOOR);
    CHECK(AmpToDb(-0.f) == DB_FLOOR);
    CHECK(AmpToDb(std::nanf("")) == DB_FLOOR);
    CHECK(AmpToDb(std::numeric_limits<float>::denorm_min()) >= DB_FLOOR);
    CHECK(std::isfinite(AmpToDb(kInf)));
    CHECK(std::isfinite(AmpToDb(-kInf)));
    CHECK(DbToAmp(DB_FLOOR) == 0.f);
    CHECK(DbToAmp(-2000.f) == 0.f);
    CHECK(DbToAmp(-kInf) == 0.f);
    CHECK(DbToAmp(std::nanf("")) == 0.f);
    CHECK(std::isfinite(DbToAmp(kInf)));
    CHECK(std::isfinite(DbToAmp(1000.f)));
    CHECK(DbToAmp(AmpToDb(0.f)) == 0.f);
  }

  TEST_CASE("ampToDb / dbToAmp classes: a sine block against the scalar form (#1052)") {
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = std::sin(6.2831853f * static_cast<float>(i) / 64.f);
    in.getPtr()[3] = std::nanf("");
    YSE::DSP::ampToDb toDb;
    YSE::DSP::dbToAmp toAmp;
    YSE::DSP::buffer& db = toDb(in);
    REQUIRE(db.getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(AllFinite(db));
    for (unsigned int i = 0; i < db.getLength(); ++i) {
      CAPTURE(i);
      CHECK(db.getPtr()[i] == YSE::DSP::AmpToDb(in.getPtr()[i]));
    }
    CHECK(db.getPtr()[0] == YSE::DSP::DB_FLOOR); // sin(0)
    CHECK(db.getPtr()[16] == doctest::Approx(0.f).scale(1.f)); // the peak
    YSE::DSP::buffer& back = toAmp(db);
    CHECK(AllFinite(back));
    for (unsigned int i = 4; i < back.getLength(); ++i) {
      CAPTURE(i);
      CHECK(back.getPtr()[i] ==
            doctest::Approx(std::fabs(in.getPtr()[i])).epsilon(1e-4).scale(1.f));
    }
    CHECK(back.getPtr()[3] == 0.f); // NaN -> floor -> 0
  }

  TEST_CASE("msToSamples / samplesToMs: follow the live sample rate (#1052)") {
    SampleRateGuard guard;
    YSE::DSP::msToSamples toSamples;
    YSE::DSP::samplesToMs toMs;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(i) - 10.f;

    // Constructed at one rate, processed at others: nothing is cached.
    for (UInt rate : {44100u, 48000u, 96000u}) {
      CAPTURE(rate);
      YSE::SAMPLERATE = rate;
      const float r = static_cast<float>(rate);
      CHECK(YSE::DSP::MsToSamples(1000.f) == doctest::Approx(r));
      CHECK(YSE::DSP::SamplesToMs(r) == doctest::Approx(1000.f));
      CHECK(YSE::DSP::MsToSamples(10.f) == doctest::Approx(r / 100.f));
      YSE::DSP::buffer& samples = toSamples(in);
      YSE::DSP::buffer& ms = toMs(in);
      for (unsigned int i = 0; i < in.getLength(); ++i) {
        CAPTURE(i);
        CHECK(samples.getPtr()[i] == doctest::Approx(in.getPtr()[i] * r / 1000.f));
        CHECK(ms.getPtr()[i] == doctest::Approx(in.getPtr()[i] * 1000.f / r));
      }
    }
  }

  TEST_CASE("msToSamples / samplesToMs: non-finite input and a rate of 0 give 0 (#1052)") {
    SampleRateGuard guard;
    using namespace YSE::DSP;
    CHECK(msToSamples::process(kInf, 48000.f) == 0.f);
    CHECK(msToSamples::process(std::nanf(""), 48000.f) == 0.f);
    CHECK(samplesToMs::process(kInf, 48000.f) == 0.f);
    CHECK(samplesToMs::process(100.f, 0.f) == 0.f);
    YSE::SAMPLERATE = 0;
    CHECK(SamplesToMs(100.f) == 0.f);
    samplesToMs toMs;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 100.f;
    YSE::DSP::buffer& out = toMs(in);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(AllFinite(out));
  }

  TEST_CASE("midiToFreq / freqToMidi: base and ref retune (#1052)") {
    YSE::DSP::buffer notes(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < notes.getLength(); ++i)
      notes.getPtr()[i] = static_cast<float>(i);
    YSE::DSP::midiToFreq m2f;
    YSE::DSP::freqToMidi f2m;
    // Defaults: A4 = 440.
    CHECK(m2f.getBase() == 440.f);
    CHECK(m2f.getRef() == 69.f);
    CHECK(m2f(notes).getPtr()[69] == doctest::Approx(440.f));

    // Middle C at 256 Hz ("scientific pitch").
    m2f.setBase(256.f).setRef(60.f);
    f2m.setBase(256.f).setRef(60.f);
    YSE::DSP::buffer& freqs = m2f(notes);
    for (unsigned int i = 0; i < freqs.getLength(); ++i) {
      CAPTURE(i);
      const float expected = 256.f * std::pow(2.f, (static_cast<float>(i) - 60.f) / 12.f);
      CHECK(freqs.getPtr()[i] == doctest::Approx(expected).epsilon(1e-4));
    }
    YSE::DSP::buffer& back = f2m(freqs);
    for (unsigned int i = 0; i < back.getLength(); ++i) {
      CAPTURE(i);
      CHECK(back.getPtr()[i] == doctest::Approx(static_cast<float>(i)).epsilon(1e-4).scale(1.f));
    }

    // A nonsense base counts as 440, a non-finite ref as 69.
    m2f.setBase(-5.f).setRef(std::nanf(""));
    CHECK(m2f(notes).getPtr()[69] == doctest::Approx(440.f));
    f2m.setBase(0.f).setRef(kInf);
    YSE::DSP::buffer hz(4);
    hz = 440.f;
    CHECK(f2m(hz).getPtr()[0] == doctest::Approx(69.f));
  }

  TEST_CASE("midiToFreq / freqToMidi: no NaN or infinity leaves the classes (#1052)") {
    YSE::DSP::buffer in(8);
    const float values[] = {std::nanf(""), kInf, -kInf, 0.f, -1.f, 1e30f, -1e30f, 69.f};
    for (unsigned int i = 0; i < 8; ++i)
      in.getPtr()[i] = values[i];
    YSE::DSP::midiToFreq m2f;
    YSE::DSP::freqToMidi f2m;
    YSE::DSP::buffer& freqs = m2f(in);
    CHECK(AllFinite(freqs));
    CHECK(freqs.getPtr()[0] == 0.f); // NaN
    CHECK(freqs.getPtr()[2] == 0.f); // -inf
    YSE::DSP::buffer& notes = f2m(in);
    CHECK(AllFinite(notes));
    CHECK(notes.getPtr()[0] == -1500.f); // NaN
    CHECK(notes.getPtr()[3] == -1500.f); // 0
    CHECK(notes.getPtr()[4] == -1500.f); // below 0
    // A huge base makes the top of the range overflow: 0, not infinity.
    m2f.setBase(1e30f);
    CHECK(AllFinite(m2f(in)));
  }

  TEST_CASE("units: processing allocates nothing (#1052)") {
    SampleRateGuard guard;
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    YSE::DSP::buffer shortIn(16);
    shortIn = 2.f;
    YSE::DSP::ampToDb a;
    YSE::DSP::dbToAmp d;
    YSE::DSP::msToSamples m;
    YSE::DSP::samplesToMs s;
    YSE::DSP::midiToFreq m2f;
    YSE::DSP::freqToMidi f2m;
    m2f(in);
    f2m(in);
    TestHelpers::ProbeScope probe;
    for (int pass = 0; pass < 2; ++pass) {
      a(in);
      a(shortIn);
      d(in);
      d(shortIn);
      YSE::SAMPLERATE = pass == 0 ? 44100u : 96000u;
      m(in);
      m(shortIn);
      s(in);
      s(shortIn);
      m2f.setBase(432.f).setRef(60.f);
      m2f(in);
      f2m.setBase(432.f).setRef(60.f);
      f2m(in);
      (void)YSE::DSP::MsToSamples(1.f);
      (void)YSE::DSP::AmpToDb(0.5f);
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
