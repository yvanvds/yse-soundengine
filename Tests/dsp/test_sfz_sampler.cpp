// Tests for YSE::DSP::sfzSampler (dsp/sampling/sfzSampler), issue #1159: a
// polyphonic SFZ instrument played by keys and MIDI messages, the engine of
// the patcher's ~sfizz.
//
// Most instruments are built in code from synthetic samples, so what is heard
// has a closed form: a constant sample plays as that constant (the cubic
// kernel of a constant is the constant, and an attack of 0 reaches full level
// on the first sample), and a sample whose value is its frame number plays a
// line whose slope is the playback speed. One case loads an .sfz file and a
// WAV file written to a temporary folder.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/sfzSampler.hpp"
#include "dsp/sfzModel.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "synth/samplerVoice.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sfzRegion;
using YSE::DSP::sfzSampler;
using YSE::SYNTH::residentSample;
using YSE::SYNTH::samplerInstrument;

namespace {

  namespace fs = std::filesystem;
  constexpr UInt N = STANDARD_BUFFERSIZE;

  // A sample of `frames` frames, value gen(frame), appended to `inst`.
  template <typename Fn> int AddSample(samplerInstrument& inst, long frames, Fn gen) {
    residentSample rs;
    YSE::DSP::fileBuffer b;
    b.resize(static_cast<UInt>(frames));
    float* p = b.getPtr();
    for (long i = 0; i < frames; ++i)
      p[i] = gen(i);
    rs.frames = frames;
    rs.loaded = true;
    rs.channels.push_back(b);
    inst.samples.push_back(std::move(rs));
    return static_cast<int>(inst.samples.size()) - 1;
  }

  // A region on `sample` for keys lo..hi, at full level at once, velocity
  // ignored, released over `release` seconds.
  sfzRegion Region(int sample, int lo, int hi, float release = 0.005f) {
    sfzRegion r;
    r.sampleIndex = sample;
    r.lokey = lo;
    r.hikey = hi;
    r.pitchKeycenter = 60;
    r.ampVeltrack = 0.f;
    r.egRelease = release;
    return r;
  }

  std::shared_ptr<samplerInstrument> Finish(std::shared_ptr<samplerInstrument> inst) {
    inst->model.valid = true;
    REQUIRE(inst->valid());
    return inst;
  }

  // One constant sample per entry, each on its own key from 60 up, with its
  // own pan.
  std::shared_ptr<samplerInstrument> Constants(const std::vector<float>& values,
                                               const std::vector<float>& pans = {},
                                               float release = 0.005f) {
    auto inst = std::make_shared<samplerInstrument>();
    for (std::size_t i = 0; i < values.size(); ++i) {
      const float v = values[i];
      const int s = AddSample(*inst, 200000, [v](long) { return v; });
      sfzRegion r = Region(s, 60 + static_cast<int>(i), 60 + static_cast<int>(i), release);
      if (i < pans.size()) r.pan = pans[i];
      inst->model.regions.push_back(r);
    }
    return Finish(inst);
  }

  // A sample whose value is its frame number, on every key: what is heard
  // rises by the playback speed per sample.
  std::shared_ptr<samplerInstrument> Line() {
    auto inst = std::make_shared<samplerInstrument>();
    const int s = AddSample(*inst, 400000, [](long i) { return static_cast<float>(i); });
    inst->model.regions.push_back(Region(s, 0, 127));
    return Finish(inst);
  }

  // The playback speed of what sounds: the slope of the third block.
  float Speed(sfzSampler& s) {
    s();
    s();
    const float* o = s().getPtr();
    return o[N - 1] - o[N - 2];
  }

  struct TempFolder {
    fs::path path;
    explicit TempFolder(const char* name) : path(fs::temp_directory_path() / name) {
      std::error_code ec;
      fs::remove_all(path, ec);
      fs::create_directories(path);
    }
    ~TempFolder() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    std::string operator/(const char* file) const {
      return (path / file).string();
    }
  };

  void WriteConstant(const std::string& path, float value, long frames, int rate) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, rate);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames), value);
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  void WriteText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  template <typename... Counters> void waitForProgress(const Counters&... counters) {
    while (!((counters.load(std::memory_order_relaxed) > 0) && ...))
      std::this_thread::yield();
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("sfzSampler: silent without an instrument, and for a key no region plays (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s;
    CHECK(s.voices() == sfzSampler::DEFAULT_VOICES);
    CHECK_FALSE(s.loaded());
    s.noteOn(60, 1.f);
    const float* l = s().getPtr();
    for (UInt k = 0; k < N; ++k)
      CHECK(l[k] == 0.f);
    CHECK(s.active() == 0);

    REQUIRE(s.instrument(Constants({0.5f})));
    CHECK(s.loaded());
    CHECK(s.regions() == 1);
    s.noteOn(70, 1.f); // no region on 70
    s();
    for (UInt k = 0; k < N; ++k) {
      CHECK(s.left().getPtr()[k] == 0.f);
      CHECK(s.right().getPtr()[k] == 0.f);
    }
    CHECK(s.active() == 0);
    CHECK_FALSE(s.instrument(nullptr));
    CHECK_FALSE(s.instrument(std::make_shared<samplerInstrument>()));
  }

  TEST_CASE("sfzSampler: a key plays its region from the first sample; pan by the balance "
            "law (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s;
    REQUIRE(s.instrument(Constants({0.5f, 0.5f, 0.5f, 0.5f}, {0.f, 50.f, -100.f, -25.f})));
    struct expect {
      int key;
      float left;
      float right;
    };
    for (const expect& e : {expect{60, 0.5f, 0.5f}, expect{61, 0.25f, 0.5f}, expect{62, 0.5f, 0.f},
                            expect{63, 0.5f, 0.375f}}) {
      CAPTURE(e.key);
      s.allSoundOff();
      s.noteOn(e.key, 1.f);
      s();
      for (UInt k = 0; k < N; ++k) {
        CHECK(s.left().getPtr()[k] == doctest::Approx(e.left));
        CHECK(s.right().getPtr()[k] == doctest::Approx(e.right));
      }
      CHECK(s.active() == 1);
    }
  }

  TEST_CASE("sfzSampler: volume in dB, smoothed over 10 ms at the live rate (#1159)") {
    for (UInt r : {1000u, 2000u}) {
      CAPTURE(r);
      ScopedSampleRate rate(r);
      sfzSampler s;
      REQUIRE(s.instrument(Constants({1.f})));
      s.volume(-20.f); // before the first block: at once
      CHECK(s.volume() == -20.f);
      s.noteOn(60, 1.f);
      s();
      CHECK(s.left().getPtr()[0] == doctest::Approx(0.1f));
      CHECK(s.right().getPtr()[N - 1] == doctest::Approx(0.1f));
      s.volume(0.f);
      s();
      // One pole: g(k) = 1 - 0.9 (1 - a)^(k + 1), a = 1 - exp(-1 / (0.01 rate)).
      const double a = 1.0 - std::exp(-1.0 / (0.01 * r));
      for (UInt k = 0; k < N; k += 7) {
        CAPTURE(k);
        const double g = 1.0 - 0.9 * std::pow(1.0 - a, static_cast<double>(k + 1));
        CHECK(s.left().getPtr()[k] == doctest::Approx(g).epsilon(1e-4));
      }
      s.volume(std::nanf(""));
      CHECK(s.volume() == 0.f);
    }
  }

  TEST_CASE("sfzSampler: a released key fades by its region's release, then frees its "
            "voice (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s;
    REQUIRE(s.instrument(Constants({0.5f}, {}, 0.1f))); // 100 samples at 1000 Hz
    s.noteOn(60, 1.f);
    s();
    s.noteOff(60);
    s();
    for (UInt k = 0; k < 99; ++k) {
      CAPTURE(k);
      CHECK(s.left().getPtr()[k] ==
            doctest::Approx(0.5f * (1.f - static_cast<float>(k + 1) / 100.f)).epsilon(1e-3));
    }
    for (UInt k = 101; k < N; ++k)
      CHECK(s.left().getPtr()[k] == 0.f);
    CHECK(s.active() == 0);

    // A key on and off before a block still sounds, then releases.
    s.noteOn(60, 1.f);
    s.noteOn(60, 0.f); // velocity 0 is a note off
    s();
    CHECK(s.left().getPtr()[0] == doctest::Approx(0.5f));
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.5f));
    CHECK(s.active() == 1);
    s();
    CHECK(s.left().getPtr()[0] < 0.5f);
    CHECK(s.left().getPtr()[N - 1] == 0.f);
    CHECK(s.active() == 0);
  }

  TEST_CASE("sfzSampler: a further note takes a free voice, else the oldest released, else the "
            "oldest (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s(2);
    CHECK(s.voices() == 2);
    REQUIRE(s.instrument(Constants({0.1f, 0.f, 0.2f, 0.f, 0.4f}, {}, 10.f)));
    s.noteOn(60, 1.f);
    s.noteOn(62, 1.f);
    s();
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.3f));

    SUBCASE("the oldest released, though a held one is older") {
      s.noteOff(62); // releasing over 10 s: still sounding
      s.noteOn(64, 1.f);
      s();
      // 62 was taken over: 60 and 64 sound.
      CHECK(s.left().getPtr()[0] == doctest::Approx(0.5f));
      CHECK(s.active() == 2);
    }
    SUBCASE("all held: the oldest") {
      s.noteOn(64, 1.f);
      s();
      // 60 was taken over: 62 and 64 sound.
      CHECK(s.left().getPtr()[0] == doctest::Approx(0.6f));
      CHECK(s.active() == 2);
    }
    SUBCASE("all sound off silences at once") {
      s.allSoundOff();
      s();
      CHECK(s.left().getPtr()[0] == 0.f);
      CHECK(s.active() == 0);
    }
  }

  TEST_CASE("sfzSampler: the sustain pedal holds released keys until it comes up; MIDI "
            "messages (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s;
    REQUIRE(s.instrument(Constants({0.5f, 0.25f})));
    s.midi(0xB3, 64, 127); // sustain on, channel 4: channels are ignored
    CHECK(s.sustain());
    s.midi(0x90, 60, 127);
    s();
    s.midi(0x80, 60, 0);
    s();
    s();
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.5f));
    s.midi(0xB0, 64, 0); // pedal up: released
    s();
    CHECK(s.left().getPtr()[N - 1] == 0.f);
    CHECK(s.active() == 0);
    CHECK_FALSE(s.sustain());

    SUBCASE("note on at velocity 0 is a note off") {
      s.midi(0x90, 61, 100);
      s();
      CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.25f));
      s.midi(0x90, 61, 0);
      s();
      CHECK(s.active() == 0);
    }
    SUBCASE("all notes off (123) releases through the pedal") {
      s.midi(0xB0, 64, 127);
      s.midi(0x90, 60, 100);
      s.midi(0x90, 61, 100);
      s();
      CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.75f));
      s.midi(0xB0, 123, 0);
      s();
      CHECK(s.active() == 0);
      CHECK_FALSE(s.sustain());
    }
    SUBCASE("all sound off (120)") {
      s.midi(0x90, 60, 100);
      s();
      s.midi(0xB0, 120, 0);
      s();
      CHECK(s.left().getPtr()[0] == 0.f);
    }
    SUBCASE("reset (121): the pedal up, the wheel centred") {
      s.midi(0xB0, 64, 127);
      s.midi(0xE0, 0, 0);
      CHECK(s.pitchBend() == -1.f);
      s.midi(0xB0, 121, 0);
      CHECK_FALSE(s.sustain());
      CHECK(s.pitchBend() == 0.f);
    }
  }

  TEST_CASE("sfzSampler: keys, the wheel and the tuning set the playback speed (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s;
    REQUIRE(s.instrument(Line()));

    struct expect {
      const char* what;
      int key;
      double speed;
    };
    auto play = [&](int key) {
      s.allSoundOff();
      s.noteOn(key, 1.f);
      return Speed(s);
    };

    CHECK(play(60) == doctest::Approx(1.0));
    CHECK(play(72) == doctest::Approx(2.0));
    CHECK(play(48) == doctest::Approx(0.5));

    SUBCASE("the wheel: two semitones either way, on a sounding note") {
      s.allSoundOff();
      s.noteOn(60, 1.f);
      s();
      s.pitchBend(1.f);
      CHECK(Speed(s) == doctest::Approx(std::pow(2.0, 2.0 / 12.0)).epsilon(1e-4));
      s.midi(0xE0, 0, 0x40); // centre
      CHECK(Speed(s) == doctest::Approx(1.0));
      s.midi(0xE0, 0, 0); // full down
      CHECK(Speed(s) == doctest::Approx(std::pow(2.0, -2.0 / 12.0)).epsilon(1e-4));
      s.pitchBend(5.f);
      CHECK(s.pitchBend() == 1.f);
    }
    SUBCASE("base and ref move every key") {
      s.base(880.f);
      CHECK(s.pitch(69) == doctest::Approx(880.f));
      CHECK(play(60) == doctest::Approx(2.0));
      s.base(440.f).reference(57);
      CHECK(s.pitch(57) == doctest::Approx(440.f));
      CHECK(play(60) == doctest::Approx(2.0));
      s.base(-1.f);
      CHECK(s.base() == 440.f);
    }
    SUBCASE("a scale: degrees above mid in cents, the last the period") {
      const float pentatonic[] = {200.f, 400.f, 700.f, 900.f, 1200.f};
      s.scale(pentatonic, 5);
      CHECK(s.scaleSize() == 5);
      CHECK(s.middle() == 60);
      // Key 69 is 9 steps above 60: a period and degree 4, 2100 cents.
      CHECK(s.pitch(69) == doctest::Approx(440.f));
      CHECK(s.pitch(60) == doctest::Approx(440.0 * std::pow(2.0, -2100.0 / 1200.0)));
      CHECK(s.pitch(61) == doctest::Approx(440.0 * std::pow(2.0, -1900.0 / 1200.0)));
      CHECK(s.pitch(59) == doctest::Approx(440.0 * std::pow(2.0, -2400.0 / 1200.0)));
      CHECK(s.pitch(65) == doctest::Approx(440.0 * std::pow(2.0, -900.0 / 1200.0)));
      // The region was recorded at key 60 in equal temperament (261.6 Hz).
      CHECK(play(60) == doctest::Approx(0.5));
      CHECK(play(61) == doctest::Approx(0.5 * std::pow(2.0, 200.0 / 1200.0)));
      s.middle(62);
      // Now key 69 is 7 steps above 62: a period and degree 2, 1600 cents.
      CHECK(s.pitch(62) == doctest::Approx(440.0 * std::pow(2.0, -1600.0 / 1200.0)));
      s.scale(nullptr, 0);
      CHECK(s.scaleSize() == 0);
      CHECK(play(60) == doctest::Approx(1.0));
      const float noPeriod[] = {100.f, 0.f};
      s.scale(noPeriod, 2);
      CHECK(s.scaleSize() == 0);
    }
  }

  TEST_CASE("sfzSampler: Scala pitches and .scl text (#1159)") {
    Flt c = 0.f;
    auto pitch = [&](const char* text) {
      return sfzSampler::scalaPitch(text, std::char_traits<char>::length(text), c);
    };
    CHECK(pitch("100.0"));
    CHECK(c == doctest::Approx(100.f));
    CHECK(pitch("-5.5"));
    CHECK(c == doctest::Approx(-5.5f));
    CHECK(pitch("3/2"));
    CHECK(c == doctest::Approx(701.955f));
    CHECK(pitch("2"));
    CHECK(c == doctest::Approx(1200.f));
    CHECK_FALSE(pitch("abc"));
    CHECK_FALSE(pitch("0/1"));
    CHECK_FALSE(pitch("3/"));
    CHECK_FALSE(pitch("1/0"));
    CHECK_FALSE(pitch("100.0x"));

    Flt degrees[8] = {};
    const std::string scl = "! test.scl\n!\nA pentatonic scale\n 5\n!\n 200.0\n 400.0 cents\n"
                            " 3/2\r\n 900.\n 2/1\n";
    REQUIRE(sfzSampler::scalaDegrees(scl, degrees, 8) == 5);
    CHECK(degrees[0] == doctest::Approx(200.f));
    CHECK(degrees[1] == doctest::Approx(400.f));
    CHECK(degrees[2] == doctest::Approx(701.955f));
    CHECK(degrees[3] == doctest::Approx(900.f));
    CHECK(degrees[4] == doctest::Approx(1200.f));
    CHECK(sfzSampler::scalaDegrees(scl, degrees, 3) == 3);
    CHECK(sfzSampler::scalaDegrees("title\n3\n100.0\n200.0\n", degrees, 8) == 0); // short
    CHECK(sfzSampler::scalaDegrees("title\nmany\n100.0\n", degrees, 8) == 0);
    CHECK(sfzSampler::scalaDegrees("", degrees, 8) == 0);
  }

  TEST_CASE("sfzSampler: loads an .sfz file and its samples; reload, voices and collect "
            "(#1159)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_sfz_sampler_1159");
    WriteConstant(dir / "a.wav", 0.25f, 4000, 1000);
    WriteText(dir / "a.sfz", "<region> sample=a.wav pitch_keycenter=60 amp_veltrack=0\n");
    sfzSampler s;
    CHECK(s.version() == 0);
    REQUIRE(s.load(dir / "a.sfz"));
    CHECK(s.loaded());
    CHECK(s.regions() == 1);
    CHECK(s.file() == dir / "a.sfz");
    CHECK(s.version() == 1);
    s.noteOn(60, 1.f);
    s();
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.25f));
    CHECK(s.right().getPtr()[N - 1] == doctest::Approx(0.25f));
    CHECK(s.collectPending() == false); // there was no pool before

    // A file that cannot be read changes nothing.
    CHECK_FALSE(s.load(dir / "missing.sfz"));
    CHECK(s.version() == 1);
    CHECK(s.file() == dir / "a.sfz");

    // A new file plays from the next block; the notes of the old one stop.
    WriteConstant(dir / "a.wav", 0.5f, 4000, 1000);
    REQUIRE(s.reload());
    CHECK(s.version() == 2);
    s();
    CHECK(s.left().getPtr()[0] == 0.f);
    CHECK(s.collectPending());
    s.collect();
    CHECK_FALSE(s.collectPending());
    s.noteOn(60, 1.f);
    s();
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.5f));

    CHECK(s.voices(8));
    CHECK_FALSE(s.voices(8));
    CHECK(s.voices() == 8);
    CHECK(s.version() == 3);
    CHECK(s.voices(0)); // clamped to 1
    CHECK(s.voices() == 1);
    s();
    s.noteOn(60, 1.f);
    s.noteOn(60, 1.f);
    s();
    CHECK(s.active() == 1);
    CHECK(s.left().getPtr()[N - 1] == doctest::Approx(0.5f));

    sfzSampler none;
    CHECK_FALSE(none.reload());
  }

  TEST_CASE("sfzSampler: keys, MIDI, settings and blocks allocate nothing (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s(8);
    REQUIRE(s.instrument(Constants({0.5f, 0.25f, 0.125f}, {10.f, -10.f, 0.f}, 0.01f)));
    s();
    const float scale[] = {100.f, 300.f, 500.f, 1200.f};
    {
      TestHelpers::ProbeScope probe;
      for (int round = 0; round < 3; ++round) {
        s.noteOn(60, 1.f);
        s.midi(0x90, 61, 90);
        s.midi(0xB0, 64, 127);
        s();
        s.noteOff(60);
        s.midi(0x80, 61, 0);
        s.midi(0xE0, 10, 80);
        s.volume(-3.f).base(432.f).reference(60).middle(48).scale(scale, 4);
        s();
        s.midi(0xB0, 64, 0);
        s.noteOn(62, 0.5f);
        for (int i = 0; i < 9; ++i)
          s.noteOn(60 + i % 3, 1.f); // past the voice count: takes voices over
        s();
        s.allNotesOff();
        s();
        s.allSoundOff();
        s(64);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(s.left().getLength() == 64);
  }

  TEST_CASE("sfzSampler: plays while another thread changes the voices (#1159)") {
    ScopedSampleRate rate(1000);
    sfzSampler s(4);
    REQUIRE(s.instrument(Constants({0.5f})));
    std::atomic<int> blocks{0};
    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0};
    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        s.allSoundOff(); // one voice at a time
        s.noteOn(60, 1.f);
        const float* o = s().getPtr();
        // Every sample is the region's level or silence (a new pool).
        for (UInt k = 0; k < N; ++k)
          if (o[k] != 0.f && std::fabs(o[k] - 0.5f) > 1e-6f) wrong.fetch_add(1);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });
    // The writer starts once the render thread has played: a loaded runner
    // must not let the churn finish before a single block.
    waitForProgress(blocks);
    for (int i = 0; i < 40; ++i) {
      s.voices(static_cast<UInt>(2 + i % 5));
      s.collect();
    }
    const int before = blocks.load();
    while (blocks.load() < before + 2)
      std::this_thread::yield();
    stop.store(true);
    render.join();
    s.collect();
    CHECK(blocks.load() > 0);
    CHECK(wrong.load() == 0);
  }
}
