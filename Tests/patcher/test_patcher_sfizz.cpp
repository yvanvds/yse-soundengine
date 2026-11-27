// Tests for the ~sfizz patcher object (patcher/sampling/dSfizz), issue #1159.
// The instrument and its voices are DSP::sfzSampler's, tested in
// Tests/dsp/test_sfz_sampler.cpp. These show that pins, words and arguments
// reach it, in rendered patches heard through a ~dac: message boxes reading
// an instrument and playing a key at a velocity, the left and right outlets,
// MIDI events, lists, the voice count, volume, base / ref / mid, scale and a
// .scl file, readagain, a JSON round trip of the arguments and of the file a
// read loaded, the host's path resolver, a task outliving its object, and
// allocation-free words and blocks on the audio thread.
//
// The fixtures are float WAV files at 1000 Hz and the engine runs at 1000 Hz.
// "level.wav" holds 0.5 everywhere and its region is panned 50 right, so the
// left outlet hears 0.25 and the right 0.5. "line.wav" holds its frame number,
// so what it plays rises by the playback speed per sample.

#include <doctest/doctest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/sfzSampler.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dSfizz.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::PATCHER::dSfizz;
using YSE::PATCHER::patcherImplementation;

namespace {

  namespace fs = std::filesystem;
  constexpr UInt N = STANDARD_BUFFERSIZE;

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

  void WriteWav(const std::string& path, long frames, bool line) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, 1000);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (long n = 0; n < frames; ++n)
      samples[static_cast<std::size_t>(n)] = line ? static_cast<float>(n) : 0.5f;
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  void WriteText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  // level.sfz (0.5, panned 50 right, released over 10 ms) and line.sfz (the
  // frame numbers, every key, recorded at key 60), velocity ignored.
  void WriteInstruments(const TempFolder& dir) {
    WriteWav(dir / "level.wav", 20000, false);
    WriteWav(dir / "line.wav", 20000, true);
    WriteText(dir / "level.sfz", "<region> sample=level.wav pitch_keycenter=60 amp_veltrack=0 "
                                 "pan=50 ampeg_release=0.01\n");
    WriteText(dir / "line.sfz", "<region> sample=line.wav pitch_keycenter=60 amp_veltrack=0\n");
  }

  struct emptyJob : YSE::INTERNAL::threadPoolJob {
    void run() override {}
  };

  // Every task asked for so far has landed: the slow pool has one worker.
  void Drain() {
    emptyJob job;
    YSE::INTERNAL::Global().addSlowJob(&job);
    job.join();
  }

  YSE::pHandle* FindType(patcherImplementation& p, const char* type) {
    for (unsigned int i = 0; i < p.Objects(); ++i) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == type) return h;
    }
    return nullptr;
  }

  // [~sfizz args] -> [~dac 1 2].
  struct Rig {
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dSfizz* sf = nullptr;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_SFIZZ, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      sf = static_cast<dSfizz*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, dac, 0);
      p.Connect(handle, 1, dac, 1);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(0)->SetMessage(text, thread);
    }

    void Key(int key, int velocity, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(1)->SetInt(velocity, thread);
      sf->GetInlet(0)->SetInt(key, thread);
    }

    // The tasks land, then a block delivers them.
    void Settle() {
      Drain();
      p.Calculate(YSE::T_DSP);
      Drain();
    }

    // A block, then what it played on `channel`.
    std::vector<float> Block(UInt channel = 0) {
      p.Calculate(YSE::T_DSP);
      Drain();
      const float* o = p.output[channel].getPtr();
      return std::vector<float>(o, o + N);
    }

    // The playback speed of what sounds: the slope of the third block.
    float Speed() {
      Block();
      Block();
      const std::vector<float> o = Block();
      return o[N - 1] - o[N - 2];
    }
  };

  struct HostResolver : YSE::pathResolver {
    std::mutex mutex;
    std::vector<std::pair<unsigned int, std::string>> asked;
    void Resolve(const YSE::pathRequest& r) override {
      const std::lock_guard<std::mutex> lock(mutex);
      asked.emplace_back(r.id, r.name);
    }
    std::pair<unsigned int, std::string> last() {
      const std::lock_guard<std::mutex> lock(mutex);
      REQUIRE_FALSE(asked.empty());
      return asked.back();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~sfizz: registry and shape; the arguments, a first one that is a number the voice "
            "count (#1159)") {
    int listed = 0;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_SFIZZ) listed++;
    CHECK(listed == 1);
    ScopedSampleRate rate(1000);
    patcherImplementation p(1, nullptr);
    struct shape {
      const char* args;
      UInt voices;
      float volume;
      float base;
      int ref;
      int mid;
    };
    for (const shape& s : {shape{"", 64, 0.f, 440.f, 69, 60}, shape{"16", 16, 0.f, 440.f, 69, 60},
                           shape{"16 -6.5", 16, -6.5f, 440.f, 69, 60},
                           shape{"8 -3 432.5 57 48", 8, -3.f, 432.5f, 57, 48},
                           shape{"0 0 -5 200 300", 1, 0.f, 440.f, 127, 127},
                           shape{"nothing_1159.sfz 4 -1 450 70", 4, -1.f, 450.f, 70, 60}}) {
      CAPTURE(s.args);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFIZZ, s.args);
      REQUIRE(h != nullptr);
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 2);
      auto* sf = static_cast<dSfizz*>(p.GetObjectFromID(h->GetID()));
      CHECK(sf->Sampler().voices() == s.voices);
      p.Calculate(YSE::T_DSP);
      CHECK(sf->Sampler().volume() == doctest::Approx(s.volume));
      CHECK(sf->Sampler().base() == doctest::Approx(s.base));
      CHECK(sf->Sampler().reference() == s.ref);
      CHECK(sf->Sampler().middle() == s.mid);
      CHECK_FALSE(sf->Sampler().loaded());
      CHECK(sf->FailedTasks() == (std::string(s.args).find(".sfz") != std::string::npos ? 1 : 0));
    }
  }

  TEST_CASE("~sfizz: message boxes read an instrument and play a key at a velocity, left and "
            "right (#1159)") {
    // [read level.sfz( [100( [60( -> [~sfizz] -> [~dac], sent together: the
    // key waits for the instrument.
    TempFolder dir("yse_sfizz_box_1159");
    WriteInstruments(dir);
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* read = p.CreateObject(YSE::OBJ::G_MESSAGE, "read " + (dir / "level.sfz"));
    YSE::pHandle* vel = p.CreateObject(YSE::OBJ::G_MESSAGE, "100");
    YSE::pHandle* key = p.CreateObject(YSE::OBJ::G_MESSAGE, "60");
    YSE::pHandle* off = p.CreateObject(YSE::OBJ::G_MESSAGE, "0");
    YSE::pHandle* sf = p.CreateObject(YSE::OBJ::D_SFIZZ, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(read != nullptr);
    REQUIRE(vel != nullptr);
    REQUIRE(key != nullptr);
    REQUIRE(off != nullptr);
    REQUIRE(sf != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(read, 0, sf, 0);
    p.Connect(vel, 0, sf, 1);
    p.Connect(off, 0, sf, 1);
    p.Connect(key, 0, sf, 0);
    p.Connect(sf, 0, dac, 0);
    p.Connect(sf, 1, dac, 1);
    p.Calculate(YSE::T_DSP);

    read->SetBang(0);
    vel->SetBang(0);
    key->SetBang(0);
    bool heard = false;
    for (int b = 0; b < 20 && !heard; ++b) {
      Drain();
      p.Calculate(YSE::T_DSP);
      heard = p.output[1].getPtr()[N - 1] != 0.f;
    }
    REQUIRE(heard);
    p.Calculate(YSE::T_DSP);
    for (UInt k = 0; k < N; ++k) {
      CHECK(p.output[0].getPtr()[k] == doctest::Approx(0.25f));
      CHECK(p.output[1].getPtr()[k] == doctest::Approx(0.5f));
    }
    auto* object = static_cast<dSfizz*>(p.GetObjectFromID(sf->GetID()));
    CHECK(object->Sampler().active() == 1);

    // [0( into the velocity, then [60( again: the key is released.
    off->SetBang(0);
    key->SetBang(0);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[1].getPtr()[0] < 0.5f);
    CHECK(p.output[1].getPtr()[N - 1] == 0.f);
    CHECK(object->Sampler().active() == 0);
    CHECK(object->FailedTasks() == 0);
  }

  TEST_CASE("~sfizz: midievent, lists, floats and the voice count (#1159)") {
    TempFolder dir("yse_sfizz_words_1159");
    WriteInstruments(dir);
    Rig r("");
    r.Message("read " + (dir / "level.sfz"));
    r.Settle();
    REQUIRE(r.sf->Sampler().loaded());

    SUBCASE("note on and off by midievent, on any channel") {
      r.Message("midievent 145 60 100");
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
      r.Message("midievent 129 60 0");
      CHECK(r.Block(1)[N - 1] == 0.f);
    }
    SUBCASE("a float velocity is 0 to 1") {
      r.Message("midievent 144 60 0.5");
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
      r.Message("midievent 144 60 0.");
      CHECK(r.Block(1)[N - 1] == 0.f);
    }
    SUBCASE("the sustain pedal by midievent") {
      r.Message("midievent 176 64 127");
      r.Message("midievent 144 60 100");
      r.Block();
      r.Message("midievent 128 60 0");
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
      r.Message("midievent 176 64 0");
      CHECK(r.Block(1)[N - 1] == 0.f);
      r.Message("midievent 1 2"); // no channel message: ignored
      r.Message("midievent 144");
      CHECK(r.Block(1)[N - 1] == 0.f);
    }
    SUBCASE("a list is key and velocity; a float key is truncated") {
      r.sf->GetInlet(0)->SetList("60 90", YSE::T_GUI);
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
      r.sf->GetInlet(0)->SetList("60 0", YSE::T_GUI);
      CHECK(r.Block(1)[N - 1] == 0.f);
      r.sf->GetInlet(1)->SetFloat(80.7f, YSE::T_GUI);
      r.sf->GetInlet(0)->SetFloat(60.9f, YSE::T_GUI);
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
      r.sf->GetInlet(0)->SetList("60.2 0", YSE::T_GUI);
      CHECK(r.Block(1)[N - 1] == 0.f);
    }
    SUBCASE("the velocity starts at 0: a key alone is a release") {
      r.sf->GetInlet(0)->SetInt(60, YSE::T_GUI);
      CHECK(r.Block(1)[N - 1] == 0.f);
      CHECK(r.sf->Sampler().active() == 0);
    }
    SUBCASE("voices: a further key takes the oldest over") {
      r.Message("voices 2");
      r.Settle();
      CHECK(r.sf->Sampler().voices() == 2);
      r.Key(60, 100);
      r.Key(60, 100);
      r.Key(60, 100);
      r.Block();
      CHECK(r.sf->Sampler().active() == 2);
      CHECK(r.p.output[1].getPtr()[N - 1] == doctest::Approx(1.f));
    }
    CHECK(r.sf->FailedTasks() == 0);
    CHECK(r.sf->DroppedEvents() == 0);
  }

  TEST_CASE("~sfizz: volume, base, ref, mid, scale and scalename (#1159)") {
    TempFolder dir("yse_sfizz_tuning_1159");
    WriteInstruments(dir);
    WriteText(dir / "penta.scl", "! penta.scl\n!\nPentatonic\n 5\n!\n 200.0\n 400.0\n 700.0\n"
                                 " 900.0\n 2/1\n");
    WriteText(dir / "bad.scl", "Not a scale\n many\n");
    Rig r("");
    r.Message("read " + (dir / "line.sfz"));
    r.Settle();
    REQUIRE(r.sf->Sampler().loaded());
    auto play = [&](int key) {
      r.Message("midievent 176 120 0"); // all sound off
      r.Key(key, 100);
      return r.Speed();
    };
    CHECK(play(72) == doctest::Approx(2.f));

    SUBCASE("volume in dB") {
      r.Message("read " + (dir / "level.sfz"));
      r.Settle();
      r.Message("volume -6.0206");
      r.Key(60, 100);
      for (int b = 0; b < 10; ++b)
        r.Block();
      CHECK(r.Block(1)[N - 1] == doctest::Approx(0.25f).epsilon(1e-3));
      CHECK(r.sf->Sampler().volume() == doctest::Approx(-6.0206f));
    }
    SUBCASE("base and ref") {
      r.Message("base 880");
      CHECK(play(60) == doctest::Approx(2.f));
      r.Message("base 440");
      r.Message("ref 57");
      CHECK(play(60) == doctest::Approx(2.f));
      r.Message("base -3"); // ignored
      r.Block();
      CHECK(r.sf->Sampler().base() == 440.f);
    }
    SUBCASE("scale: cents with a point, ratios without") {
      r.Message("scale 5 200. 400. 3/2 900. 2");
      CHECK(play(60) == doctest::Approx(0.5f));
      CHECK(r.sf->Sampler().scaleSize() == 5);
      CHECK(r.sf->Sampler().pitch(69) == doctest::Approx(440.f));
      CHECK(r.sf->Sampler().pitch(63) ==
            doctest::Approx(440.0 * std::pow(2.0, (701.955 - 2100.0) / 1200.0)));
      r.Message("mid 62");
      r.Block();
      CHECK(r.sf->Sampler().middle() == 62);
      r.Message("scale 3 100.");
      r.Block();
      CHECK(r.sf->Sampler().scaleSize() == 0); // too short: equal temperament
      r.Message("scale");
      CHECK(play(60) == doctest::Approx(1.f));
    }
    SUBCASE("scalename reads a .scl file in the background") {
      r.Message("scalename " + (dir / "penta.scl"));
      r.Settle();
      CHECK(r.sf->Sampler().scaleSize() == 5);
      CHECK(play(60) == doctest::Approx(0.5f));
      r.Message("scalename " + (dir / "bad.scl"));
      r.Message("scalename " + (dir / "nowhere.scl"));
      r.Settle();
      CHECK(r.sf->FailedTasks() == 2);
      CHECK(r.sf->Sampler().scaleSize() == 5);
    }
    SUBCASE("readagain reads the file again; a read that fails keeps the instrument") {
      const std::uint64_t before = r.sf->Sampler().version();
      r.Message("readagain");
      r.Settle();
      CHECK(r.sf->Sampler().version() == before + 1);
      r.Message("read " + (dir / "missing.sfz"));
      r.Message("read");
      r.Settle();
      CHECK(r.sf->FailedTasks() == 1);
      CHECK(r.sf->Sampler().file() == dir / "line.sfz");
    }
  }

  TEST_CASE("~sfizz: arguments and the file a read loaded survive a DumpJSON / ParseJSON round "
            "trip, then the patch plays (#1159)") {
    TempFolder dir("yse_sfizz_json_1159");
    WriteInstruments(dir);
    ScopedSampleRate rate(1000);
    const std::string args = dir / "line.sfz" + std::string(" 8 -6 432 57 48");
    std::string dump;
    {
      patcherImplementation p(2, nullptr);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFIZZ, args);
      REQUIRE(h != nullptr);
      auto* sf = static_cast<dSfizz*>(p.GetObjectFromID(h->GetID()));
      CHECK(sf->Sampler().loaded()); // the argument's file, read on the control thread
      CHECK(sf->Sampler().file() == dir / "line.sfz");
      dump = p.DumpJSON();
      CHECK(dump.find("\"file\"") == std::string::npos); // the argument's file is the argument
    }
    {
      patcherImplementation dst(2, nullptr);
      dst.ParseJSON(dump);
      CHECK(dst.DumpJSON() == dump);
      YSE::pHandle* h = FindType(dst, YSE::OBJ::D_SFIZZ);
      REQUIRE(h != nullptr);
      auto* sf = static_cast<dSfizz*>(dst.GetObjectFromID(h->GetID()));
      CHECK(sf->Sampler().voices() == 8);
      CHECK(sf->Sampler().file() == dir / "line.sfz");
      dst.Calculate(YSE::T_DSP);
      CHECK(sf->Sampler().volume() == doctest::Approx(-6.f));
      CHECK(sf->Sampler().base() == doctest::Approx(432.f));
      CHECK(sf->Sampler().reference() == 57);
      CHECK(sf->Sampler().middle() == 48);
    }

    // A read in place of the argument's file is saved, and loaded back; the
    // restored patch plays it through ~dac.
    {
      Rig r(dir / "line.sfz");
      r.Message("read " + (dir / "level.sfz"));
      r.Settle();
      REQUIRE(r.sf->Sampler().file() == dir / "level.sfz");
      dump = r.p.DumpJSON();
    }
    CHECK(dump.find("level.sfz") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* h = FindType(dst, YSE::OBJ::D_SFIZZ);
    REQUIRE(h != nullptr);
    auto* sf = static_cast<dSfizz*>(dst.GetObjectFromID(h->GetID()));
    CHECK(sf->Sampler().file() == dir / "level.sfz");
    sf->GetInlet(0)->SetList("60 100", YSE::T_GUI);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[N - 1] == doctest::Approx(0.25f));
    CHECK(dst.output[1].getPtr()[N - 1] == doctest::Approx(0.5f));
  }

  TEST_CASE("~sfizz: the host places the files it reads (#1159, #1255)") {
    TempFolder dir("yse_sfizz_resolve_1159");
    WriteInstruments(dir);
    HostResolver host;
    Rig r("", &host);
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);
    r.Message("read instruments/level.sfz");
    r.Key(60, 100);
    io->WaitIdle();
    CHECK(host.last().second == "instruments/level.sfz");
    // The key waits for the host.
    CHECK(r.Block(1)[N - 1] == 0.f);
    CHECK(YSE::patcher::ResolveReply(host.last().first, (dir / "level.sfz").c_str()));
    Drain();
    r.Block();
    CHECK(r.Block(1)[N - 1] == doctest::Approx(0.5f));
    CHECK(r.sf->Sampler().file() == dir / "level.sfz");

    // A refusal fails like a missing file.
    r.Message("read nowhere.sfz");
    io->WaitIdle();
    CHECK(YSE::patcher::ResolveReply(host.last().first, nullptr));
    r.Settle();
    CHECK(r.sf->FailedTasks() == 1);
    CHECK(r.sf->Sampler().file() == dir / "level.sfz");

    // The file argument asks the host too.
    HostResolver second;
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    p.SetPathResolver(&second, 0);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFIZZ, "instruments/line.sfz 4");
    REQUIRE(h != nullptr);
    p.FileIO()->WaitIdle();
    CHECK(second.last().second == "instruments/line.sfz");
    CHECK(YSE::patcher::ResolveReply(second.last().first, (dir / "line.sfz").c_str()));
    Drain();
    p.Calculate(YSE::T_DSP);
    auto* sf = static_cast<dSfizz*>(p.GetObjectFromID(h->GetID()));
    CHECK(sf->Sampler().file() == dir / "line.sfz");
    CHECK(sf->Sampler().voices() == 4);
  }

  TEST_CASE("~sfizz: a task in flight survives the object's deletion; no patcher, no task "
            "(#1159)") {
    TempFolder dir("yse_sfizz_delete_1159");
    WriteInstruments(dir);
    {
      Rig r("");
      r.Message("read " + (dir / "level.sfz"));
      r.Message("voices 3");
      r.p.DeleteObject(r.handle);
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    dSfizz standalone;
    standalone.GetInlet(0)->SetMessage("read " + (dir / "level.sfz"), YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 1);
    CHECK_FALSE(standalone.Sampler().loaded());
  }

  TEST_CASE("~sfizz: words, keys and blocks on the audio thread allocate nothing (#1159)") {
    TempFolder dir("yse_sfizz_rt_1159");
    WriteInstruments(dir);
    WriteText(dir / "penta.scl", "Pentatonic\n5\n200.0\n400.0\n700.0\n900.0\n2/1\n");
    Rig r("");
    r.Message("read " + (dir / "level.sfz"));
    r.Settle();
    REQUIRE(r.sf->Sampler().loaded());
    // Built before the probe: the strings themselves allocate.
    const std::string read = "read " + (dir / "level.sfz");
    const std::string scalename = "scalename " + (dir / "penta.scl");
    const std::vector<std::string> words = {"midievent 144 60 100",
                                            "midievent 176 64 127",
                                            "midievent 128 60 0",
                                            "midievent 224 0 80",
                                            "midievent 144 61 0.7",
                                            "midievent 176 64 0",
                                            "volume -3",
                                            "base 432",
                                            "ref 60",
                                            "mid 48",
                                            "scale 4 100. 300. 5/4 2",
                                            "voices 5",
                                            "readagain",
                                            "nonsense 1",
                                            "midievent 176 123 0"};
    for (int round = 0; round < 3; ++round) {
      CAPTURE(round);
      {
        TestHelpers::ProbeScope probe;
        r.Message(read, YSE::T_DSP);
        r.Message(scalename, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Key(60, 90, YSE::T_DSP);
        r.sf->GetInlet(0)->SetList("62 70", YSE::T_DSP);
        r.sf->GetInlet(0)->SetFloat(64.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      {
        // The blocks that deliver the tasks, take the new pools, play the
        // keys and ask for the old pools to be freed.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 6; ++block)
          r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    CHECK(r.sf->DroppedEvents() == 0);
  }
}
