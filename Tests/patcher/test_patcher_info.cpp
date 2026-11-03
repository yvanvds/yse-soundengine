// Tests for the ~info patcher object (patcher/sampling/dInfo), issue #1138.
// The details themselves are DSP::sampleBuffer::info's and are tested in
// Tests/dsp/test_sampleBuffer.cpp. These run ~info in a patcher next to a
// ~buffer: what each outlet sends for a file with loops and for one without,
// the right-to-left order, set, a buffer the host made, a ~buffer read that a
// bang follows, a round trip, and allocation-free bangs and deliveries.

#include <doctest/doctest.h>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dInfo.h"
#include "support/alloc_probe.hpp"
#include "support/sample_fixtures.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dInfo;
using YSE::PATCHER::patcherImplementation;

namespace {

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

  constexpr int OUTLETS = 10;

  // A ~info in a patcher, with an order-recording sink on every outlet
  // (tags '0' to '9'), and room for a ~buffer beside it. The sinks come first
  // so they outlive the patcher (issue #967).
  struct Rig {
    std::vector<char> log;
    OrderSink sinks[OUTLETS];
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dInfo* info = nullptr;
    YSE::pHandle* bufferHandle = nullptr;

    explicit Rig(const std::string& args, const std::string& bufferArgs = std::string()) {
      // The ~buffer first, so its file argument is loaded before ~info asks.
      if (!bufferArgs.empty()) {
        bufferHandle = p.CreateObject(YSE::OBJ::D_BUFFER, bufferArgs);
        REQUIRE(bufferHandle != nullptr);
      }
      handle = p.CreateObject(YSE::OBJ::D_INFO, args);
      REQUIRE(handle != nullptr);
      info = static_cast<dInfo*>(p.GetObjectFromID(handle->GetID()));
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

    void BufferMessage(const std::string& text) {
      REQUIRE(bufferHandle != nullptr);
      p.GetObjectFromID(bufferHandle->GetID())->GetInlet(0)->SetMessage(text, YSE::T_GUI);
    }

    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      info->GetInlet(0)->SetBang(thread);
    }

    // The tasks run on the background pool (WaitIdle joins them); their
    // results are handed out at the top of the next block.
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

  TEST_CASE("info: registry and shape (#1138)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_INFO, "shape_1138");
    REQUIRE(h != nullptr);
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == OUTLETS);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);
    for (int i = 2; i <= 6; ++i)
      CHECK(h->OutputDataType(i) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(7) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(8) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(9) == YSE::OUT_TYPE::LIST);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_INFO) listed = true;
    CHECK(listed);

    dInfo standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
  }

  TEST_CASE("info: a bang reports a loaded file's details, right to left (#1138)") {
    const std::string path = TempFile("yse_info_patch_1138.wav");
    // 20 kHz, 1000 frames (50 ms); loops at 5..10 ms and 15..20 ms.
    REQUIRE(
        TestHelpers::WavWithLoops(path, 2, 1000, 20000, 64, 12, {{1, 100, 200}, {1, 300, 400}}));
    Rig r("loaded_1138", "loaded_1138 " + path);
    r.Bang();
    r.Settle();

    CHECK(r.Order() == "9876543210");
    CHECK(r.Out(9).lastList == path);
    CHECK(r.Out(8).lastInt == 2);
    CHECK(r.Out(7).lastList == "yse_info_patch_1138.wav");
    CHECK(r.Out(6).lastFloat == doctest::Approx(50.f));
    CHECK(r.Out(5).lastFloat == doctest::Approx(20.f));
    CHECK(r.Out(4).lastFloat == doctest::Approx(15.f));
    CHECK(r.Out(3).lastFloat == doctest::Approx(10.f));
    CHECK(r.Out(2).lastFloat == doctest::Approx(5.f));
    // A WAV smpl chunk has no key, velocity or gain: full ranges, gain 1.
    CHECK(r.Out(1).lastList == "64 12 0 127 0 127 1");
    CHECK(r.Out(0).lastFloat == doctest::Approx(20000.f));
    Remove(path);
  }

  TEST_CASE("info: an AIFF instrument chunk comes out as one list (#1138)") {
    const std::string path = TempFile("yse_info_patch_1138.aif");
    TestHelpers::FixtureInstrument inst;
    inst.baseNote = 57;
    inst.detune = -7;
    inst.keyLow = 40;
    inst.keyHigh = 70;
    inst.velocityLow = 10;
    inst.velocityHigh = 100;
    inst.gain = 3;
    inst.sustain = {1, 1000, 3000};
    inst.release = {0, 0, 0}; // none
    REQUIRE(TestHelpers::AiffWithInstrument(path, 1, 10000, 10000, inst));
    Rig r("aiff_1138", "aiff_1138 " + path);
    r.Message("bang");
    r.Settle();

    CHECK(r.Out(1).lastList == "57 -7 40 70 10 100 3");
    CHECK(r.Out(2).lastFloat == doctest::Approx(100.f));
    CHECK(r.Out(3).lastFloat == doctest::Approx(300.f));
    // The release loop has no play mode, so neither of its outlets sends.
    CHECK(r.Out(4).count == 0);
    CHECK(r.Out(5).count == 0);
    CHECK(r.Out(6).lastFloat == doctest::Approx(1000.f));
    CHECK(r.Out(8).lastInt == 1);
    Remove(path);
  }

  TEST_CASE("info: a buffer with no file sends only rate, length and channels (#1138)") {
    Rig r("nofile_1138", "nofile_1138 250 2");
    r.Bang();
    r.Settle();
    CHECK(r.Order() == "860");
    CHECK(r.Out(8).lastInt == 2);
    CHECK(r.Out(6).lastFloat == doctest::Approx(250.f).epsilon(0.001));
    CHECK(r.Out(0).lastFloat == doctest::Approx(static_cast<float>(YSE::SAMPLERATE)));
  }

  TEST_CASE("info: a buffer the host made and filled is reported (#1138)") {
    auto host = sampleBufferRegistry::acquire("host_1138");
    host->setSize(4410, 3);
    host->setSampleRate(44100.f);
    Rig r("host_1138");
    r.Bang();
    r.Settle();
    CHECK(r.Out(8).lastInt == 3);
    CHECK(r.Out(6).lastFloat == doctest::Approx(100.f));
    CHECK(r.Out(0).lastFloat == doctest::Approx(44100.f));
  }

  TEST_CASE("info: set picks another buffer; an unknown or no name sends nothing (#1138)") {
    auto a = sampleBufferRegistry::acquire("set_a_1138");
    a->setSize(10, 1);
    a->setSampleRate(1000.f);
    auto b = sampleBufferRegistry::acquire("set_b_1138");
    b->setSize(20, 4);
    b->setSampleRate(1000.f);

    Rig r("set_a_1138");
    CHECK(std::string(r.info->BufferName()) == "set_a_1138");
    r.Bang();
    r.Settle();
    CHECK(r.Out(8).lastInt == 1);
    CHECK(r.Out(6).lastFloat == doctest::Approx(10.f));

    r.Message("set set_b_1138");
    r.Bang();
    r.Settle();
    CHECK(r.Out(8).lastInt == 4);
    CHECK(r.Out(6).lastFloat == doctest::Approx(20.f));

    r.Reset();
    r.Message("set no_such_buffer_1138");
    r.Bang();
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 1);

    r.Message("set");
    CHECK(std::string(r.info->BufferName()).empty());
    r.Bang();
    r.Settle();
    CHECK(r.log.empty());
    CHECK(r.info->FailedTasks() == 1); // no name: nothing was asked
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("info: a buffer created after the object is found at the next bang (#1138)") {
    Rig r("later_1138");
    r.Bang();
    r.Settle();
    CHECK(r.log.empty());
    auto later = sampleBufferRegistry::acquire("later_1138");
    later->setSize(5, 2);
    r.Bang();
    r.Settle();
    CHECK(r.Out(8).lastInt == 2);
  }

  TEST_CASE("info: a bang sent after a ~buffer read reports the new file (#1138)") {
    const std::string first = TempFile("yse_info_first_1138.wav");
    const std::string second = TempFile("yse_info_second_1138.wav");
    REQUIRE(TestHelpers::WavWithLoops(first, 1, 100, 10000, 60, 0, {}));
    REQUIRE(TestHelpers::WavWithLoops(second, 2, 300, 10000, 72, 0, {{1, 10, 20}}));
    Rig r("reread_1138", "reread_1138 " + first);

    // In one burst: the ~buffer task runs first, so the bang sees its file.
    r.BufferMessage("replace " + second);
    r.Bang();
    r.Settle();
    CHECK(r.Out(9).lastList == second);
    CHECK(r.Out(8).lastInt == 2);
    CHECK(r.Out(6).lastFloat == doctest::Approx(30.f));
    CHECK(r.Out(2).lastFloat == doctest::Approx(1.f));
    CHECK(r.Out(1).lastList == "72 0 0 127 0 127 1");
    Remove(first);
    Remove(second);
  }

  TEST_CASE("info: wired to ~buffer's loaded outlet, every load is reported (#1138)") {
    // The patch a user builds: [~buffer drums] -> [~info drums].
    const std::string path = TempFile("yse_info_wired_1138.wav");
    REQUIRE(TestHelpers::WavWithLoops(path, 2, 2000, 20000, 60, 0, {{1, 200, 400}}));
    Rig r("wired_1138", "wired_1138");
    r.p.Connect(r.bufferHandle, 0, r.handle, 0);

    r.BufferMessage("read " + path + " 0 -1 0");
    r.Settle(); // the read finishes and bangs ~info, which asks
    r.Settle(); // the answer comes out
    CHECK(r.Out(9).lastList == path);
    CHECK(r.Out(8).lastInt == 2);
    CHECK(r.Out(6).lastFloat == doctest::Approx(100.f));
    CHECK(r.Out(2).lastFloat == doctest::Approx(10.f));
    CHECK(r.Out(3).lastFloat == doctest::Approx(20.f));
    CHECK(r.Out(0).lastFloat == doctest::Approx(20000.f));
    Remove(path);
  }

  TEST_CASE("info: without a patcher a bang does nothing (#1138)") {
    dInfo standalone;
    standalone.SetParams("standalone_1138");
    standalone.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 0);
  }

  TEST_CASE("info: the argument survives a DumpJSON / ParseJSON round trip (#1138)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_INFO, "roundtrip_1138") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    CHECK(dump.find("roundtrip_1138") != std::string::npos);
  }

  TEST_CASE("info: bangs and their answers on the audio thread allocate nothing (#1138)") {
    const std::string path = TempFile("yse_info_rt_1138.wav");
    REQUIRE(TestHelpers::WavWithLoops(path, 1, 1000, 10000, 60, 0, {{1, 10, 20}, {1, 30, 40}}));
    Rig r("rt_1138", "rt_1138 " + path);
    const std::string set = "set rt_1138";
    // Room in the sinks' own strings and the log, so only ~info is measured.
    // Its first report is the measured one: nothing warms ~info up.
    r.log.reserve(64);
    for (OrderSink& s : r.sinks)
      s.lastList.reserve(1024);
    r.p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      r.Message(set, YSE::T_DSP);
      r.Bang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    r.p.FileIO()->WaitIdle();
    {
      // Every outlet fires, the path and the instrument list included.
      TestHelpers::ProbeScope probe;
      r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(r.Order() == "9876543210");
    Remove(path);
  }

} // TEST_SUITE("patcher")
