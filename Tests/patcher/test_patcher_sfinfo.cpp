// Tests for the ~sfinfo patcher object (patcher/sampling/dSfinfo), issue
// #1157. They run ~sfinfo in a patcher with a sink on every outlet: the
// report for the real test fixture and for files of every sample type, the
// right-to-left order, the argument and bang, a missing file, the host's path
// resolver (issue #1255) seeing the request, a round trip, and allocation-free
// requests and deliveries.

#include <doctest/doctest.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>
#include <sndfile.hh>
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dSfinfo.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using YSE::PATCHER::dSfinfo;
using YSE::PATCHER::patcherImplementation;

namespace {

  // The real fixture: mono, 16-bit PCM, 44100 Hz, 100 frames.
  std::string Fixture() {
    return std::string(YSE_TEST_FIXTURES_DIR) + "/test_mono_44100.wav";
  }

  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  // A file of `frames` frames of silence in `format`, through libsndfile.
  bool WriteFile(const std::string& path, int format, int channels, int rate, int frames) {
    SndfileHandle file(path, SFM_WRITE, format, channels, rate);
    if (!file) return false;
    const std::vector<float> samples(static_cast<std::size_t>(channels) * frames, 0.f);
    return file.writef(samples.data(), frames) == frames;
  }

  // A host that keeps sound files somewhere else (issue #1255): it records
  // every name it is asked about and answers later with a path of its own.
  struct HostResolver : YSE::pathResolver {
    struct Asked {
      unsigned int id;
      std::string name;
      YSE::PATH_ACCESS access;
      std::string objectType;
    };
    std::mutex mutex;
    std::vector<Asked> asked;
    void Resolve(const YSE::pathRequest& r) override {
      const std::lock_guard<std::mutex> lock(mutex);
      asked.push_back({r.id, r.name, r.access, r.objectType});
    }
    std::size_t count() {
      const std::lock_guard<std::mutex> lock(mutex);
      return asked.size();
    }
    Asked last() {
      const std::lock_guard<std::mutex> lock(mutex);
      REQUIRE_FALSE(asked.empty());
      return asked.back();
    }
  };

  constexpr int OUTLETS = 6;

  // A ~sfinfo in a patcher, with an order-recording sink on every outlet
  // (tags '0' to '5'). The sinks come first so they outlive the patcher
  // (issue #967).
  struct Rig {
    std::vector<char> log;
    OrderSink sinks[OUTLETS];
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dSfinfo* info = nullptr;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_SFINFO, args);
      REQUIRE(handle != nullptr);
      info = static_cast<dSfinfo*>(p.GetObjectFromID(handle->GetID()));
      for (int i = 0; i < OUTLETS; ++i) {
        sinks[i].log = &log;
        sinks[i].tag = static_cast<char>('0' + i);
        sinkHandles.push_back(std::make_unique<YSE::pHandle>(&sinks[i]));
        p.Connect(handle, i, sinkHandles.back().get(), 0);
      }
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      info->GetInlet(0)->SetMessage(text, thread);
    }

    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      info->GetInlet(0)->SetBang(thread);
    }

    // The file is read on the background pool (WaitIdle joins it); the
    // answer is handed out at the top of the next block.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }

    void Reset() {
      log.clear();
      for (OrderSink& s : sinks) {
        s.count = 0;
        s.lastKind = OrderSink::NONE;
      }
    }

    std::string Order() const {
      return std::string(log.begin(), log.end());
    }

    const OrderSink& Out(int i) const {
      return sinks[i];
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("sfinfo: registry and shape (#1157)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFINFO, "");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~sfinfo");
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == OUTLETS);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(3) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(4) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(5) == YSE::OUT_TYPE::LIST);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_SFINFO) listed = true;
    CHECK(listed);

    dSfinfo standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
  }

  TEST_CASE("sfinfo: open reports the test fixture, right to left (#1157)") {
    Rig r("");
    r.Message("open " + Fixture());
    r.Settle();

    CHECK(r.Order() == "543210");
    CHECK(r.Out(5).lastList == "test_mono_44100.wav");
    CHECK(r.Out(4).lastList == "int16");
    CHECK(r.Out(3).lastFloat == doctest::Approx(100.0 * 1000.0 / 44100.0));
    CHECK(r.Out(2).lastFloat == doctest::Approx(44100.f));
    CHECK(r.Out(1).lastInt == 16);
    CHECK(r.Out(0).lastInt == 1);
    CHECK(std::string(r.info->FileName()) == Fixture());
    CHECK(r.info->FailedTasks() == 0);
  }

  TEST_CASE("sfinfo: the argument names the file a bang reports on (#1157)") {
    Rig r(Fixture());
    CHECK(std::string(r.info->FileName()) == Fixture());
    // Nothing is reported until asked.
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.log.empty());

    r.Bang();
    r.Settle();
    CHECK(r.Order() == "543210");
    CHECK(r.Out(0).lastInt == 1);
    CHECK(r.Out(2).lastFloat == doctest::Approx(44100.f));

    // The `bang` message does the same, and reports again.
    r.Reset();
    r.Message("bang");
    r.Settle();
    CHECK(r.Order() == "543210");
    CHECK(r.Out(4).lastList == "int16");
  }

  TEST_CASE("sfinfo: every sample type, sample size and channel count (#1157)") {
    struct Case {
      const char* file;
      int format;
      int channels;
      int rate;
      const char* type;
      int bits;
    };
    const Case cases[] = {
        {"yse_sfinfo_f32_1157.wav", SF_FORMAT_WAV | SF_FORMAT_FLOAT, 2, 22050, "float32", 32},
        {"yse_sfinfo_f64_1157.wav", SF_FORMAT_WAV | SF_FORMAT_DOUBLE, 1, 48000, "float64", 64},
        {"yse_sfinfo_i24_1157.aif", SF_FORMAT_AIFF | SF_FORMAT_PCM_24, 3, 96000, "int24", 24},
        {"yse_sfinfo_i32_1157.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_32, 2, 8000, "int32", 32},
        {"yse_sfinfo_s8_1157.aif", SF_FORMAT_AIFF | SF_FORMAT_PCM_S8, 1, 11025, "int8", 8},
        {"yse_sfinfo_u8_1157.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_U8, 1, 11025, "uint8", 8},
        {"yse_sfinfo_ulaw_1157.wav", SF_FORMAT_WAV | SF_FORMAT_ULAW, 1, 8000, "mulaw", 8},
        {"yse_sfinfo_alaw_1157.wav", SF_FORMAT_WAV | SF_FORMAT_ALAW, 2, 8000, "alaw", 8},
        {"yse_sfinfo_flac_1157.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_16, 2, 44100, "int16", 16},
        // ADPCM has no fixed sample format.
        {"yse_sfinfo_adpcm_1157.wav", SF_FORMAT_WAV | SF_FORMAT_IMA_ADPCM, 1, 22050, "other", 0},
    };
    constexpr int FRAMES = 4410;
    Rig r("");
    for (const Case& c : cases) {
      CAPTURE(c.file);
      const std::string path = TempFile(c.file);
      REQUIRE(WriteFile(path, c.format, c.channels, c.rate, FRAMES));
      // The frame count as the file states it: ADPCM rounds up to whole
      // blocks.
      sf_count_t frames = 0;
      {
        const SndfileHandle check(path);
        frames = check.frames();
      }
      r.Reset();
      r.Message("open " + path);
      r.Settle();
      CHECK(r.Order() == "543210");
      CHECK(r.Out(5).lastList == c.file);
      CHECK(r.Out(4).lastList == c.type);
      CHECK(r.Out(3).lastFloat ==
            doctest::Approx(static_cast<double>(frames) * 1000.0 / c.rate).epsilon(1e-5));
      CHECK(r.Out(2).lastFloat == doctest::Approx(static_cast<float>(c.rate)));
      CHECK(r.Out(1).lastInt == c.bits);
      CHECK(r.Out(0).lastInt == c.channels);
      Remove(path);
    }
    CHECK(r.info->FailedTasks() == 0);
  }

  TEST_CASE("sfinfo: a missing file sends nothing and is counted (#1157)") {
    const std::string missing = TempFile("yse_sfinfo_missing_1157.wav");
    Rig r("");
    r.Message("open " + missing);
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 1);

    // A bang asks again, and fails again.
    r.Bang();
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 2);

    // Once the file exists, the same bang reports it.
    REQUIRE(WriteFile(missing, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 2, 44100, 441));
    r.Bang();
    r.Settle();
    CHECK(r.Order() == "543210");
    CHECK(r.Out(0).lastInt == 2);
    CHECK(r.Out(3).lastFloat == doctest::Approx(10.f));
    CHECK(r.info->FailedTasks() == 2);
    Remove(missing);

    // A file that is not a sound file fails the same way.
    const std::string text = TempFile("yse_sfinfo_text_1157.wav");
    {
      std::FILE* f = std::fopen(text.c_str(), "wb");
      REQUIRE(f != nullptr);
      std::fputs("not a sound file", f);
      std::fclose(f);
    }
    r.Reset();
    r.Message("open " + text);
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 3);
    Remove(text);
  }

  TEST_CASE("sfinfo: without a file name, nothing is asked (#1157)") {
    Rig r("");
    r.Bang();
    r.Message("open"); // a file dialog in Max; nothing here
    r.p.FileIO()->WaitIdle();
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 0);
    CHECK(r.p.FileIO()->PendingCount() == 0);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("sfinfo: the host's path resolver places the file (#1255, #1157)") {
    HostResolver host;
    Rig r("", &host);
    // A name that exists nowhere but in the host's mind.
    r.Message("open voices/kick_1157.wav");
    r.p.FileIO()->WaitIdle();

    HostResolver::Asked a = host.last();
    CHECK(a.name == "voices/kick_1157.wav");
    CHECK(a.access == YSE::PA_READ);
    CHECK(a.objectType == "~sfinfo");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.log.empty()); // parked until the host answers

    CHECK(YSE::patcher::ResolveReply(a.id, Fixture().c_str()));
    r.Settle();
    CHECK(r.Order() == "543210");
    // The name is the patch's, the details the file the host chose.
    CHECK(r.Out(5).lastList == "kick_1157.wav");
    CHECK(r.Out(4).lastList == "int16");
    CHECK(r.Out(2).lastFloat == doctest::Approx(44100.f));
    CHECK(r.Out(0).lastInt == 1);

    // A bang asks again; a refusal fails like a missing file.
    r.Reset();
    r.Bang();
    r.p.FileIO()->WaitIdle();
    REQUIRE(host.count() == 2);
    a = host.last();
    CHECK(a.name == "voices/kick_1157.wav");
    CHECK(YSE::patcher::ResolveReply(a.id, nullptr));
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 1);
  }

  TEST_CASE("sfinfo: the argument is not asked about until a bang (#1255, #1157)") {
    HostResolver host;
    Rig r("drums/snare_1157.wav", &host);
    r.p.FileIO()->WaitIdle();
    CHECK(host.count() == 0);
    r.Bang();
    r.p.FileIO()->WaitIdle();
    REQUIRE(host.count() == 1);
    CHECK(host.last().name == "drums/snare_1157.wav");
    CHECK(YSE::patcher::ResolveReply(host.last().id, Fixture().c_str()));
    r.Settle();
    CHECK(r.Out(5).lastList == "snare_1157.wav");
    CHECK(r.Out(1).lastInt == 16);
  }

  TEST_CASE("sfinfo: without a patcher a bang does nothing (#1157)") {
    dSfinfo standalone;
    standalone.SetParams(Fixture());
    standalone.GetInlet(0)->SetBang(YSE::T_GUI);
    standalone.GetInlet(0)->SetMessage("open " + Fixture(), YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 0);
  }

  TEST_CASE("sfinfo: the argument survives a DumpJSON / ParseJSON round trip (#1157)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SFINFO, "roundtrip_1157.wav") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    CHECK(dump.find("roundtrip_1157.wav") != std::string::npos);
  }

  TEST_CASE("sfinfo: requests and answers on the audio thread allocate nothing (#1157)") {
    Rig r("");
    const std::string open = "open " + Fixture();
    // Room in the sinks' own strings and the log, so only ~sfinfo is
    // measured. Its first report is the measured one: nothing warms it up.
    r.log.reserve(64);
    for (OrderSink& s : r.sinks)
      s.lastList.reserve(1024);
    r.p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      r.Message(open, YSE::T_DSP);
      r.Bang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    r.p.FileIO()->WaitIdle();
    {
      TestHelpers::ProbeScope probe;
      r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(r.Order() == "543210543210");
  }

} // TEST_SUITE("patcher")
