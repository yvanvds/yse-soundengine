// Tests for the ~buffer patcher object (patcher/sampling/dBuffer), issue
// #1039. The edits themselves are DSP::sampleBuffer's and are tested in
// Tests/dsp/test_sampleBuffer.cpp. These show that the arguments and the
// messages reach the buffer, that every message runs as a background task in
// the order it was sent, and that a patch and the C++ host share buffers by
// name.

#include <doctest/doctest.h>
#include <cmath>
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
#include "patcher/sampling/dBuffer.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dBuffer;
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

  std::vector<float> Channel(const sampleBuffer& b, UInt c) {
    std::vector<float> out(b.frames());
    out.resize(b.getSamples(c, 0, out.data(), b.frames()));
    return out;
  }

  // A ~buffer in a patcher, with a sink on each outlet. The sinks come first
  // so they outlive the patcher (issue #967).
  struct Rig {
    TestHelpers::BangSink loaded;
    TestHelpers::ListSink info;
    YSE::pHandle loadedHandle{&loaded};
    YSE::pHandle infoHandle{&info};
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle;
    dBuffer* buffer;

    explicit Rig(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_BUFFER, args);
      REQUIRE(handle != nullptr);
      buffer = static_cast<dBuffer*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, &loadedHandle, 0);
      p.Connect(handle, 1, &infoHandle, 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      buffer->GetInlet(0)->SetMessage(text, thread);
    }

    // The tasks run on the background pool (WaitIdle joins them); their
    // results are handed out at the top of the next block.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }

    // A query's answer, as sent out of outlet 1.
    std::string Ask(const std::string& query) {
      info.gotList = false;
      Message(query);
      Settle();
      return info.gotList ? info.received : std::string();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ────────────────────────────────────────────────────────────────

  TEST_CASE("buffer: registry and shape (#1039)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_BUFFER, "shape_1039");
    REQUIRE(h != nullptr);
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_BUFFER) listed = true;
    CHECK(listed);

    dBuffer standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.IsDSPObject());
  }

  // ─── arguments ────────────────────────────────────────────────────────────

  TEST_CASE("buffer: the arguments name, size and shape the buffer (#1039)") {
    Rig r("args_1039 500 2");
    const auto& b = r.buffer->Buffer();
    CHECK(b->name() == "args_1039");
    CHECK(sampleBufferRegistry::find("args_1039") == b);
    CHECK(b->channels() == 2);
    CHECK(b->frames() == YSE::SAMPLERATE / 2);
  }

  TEST_CASE("buffer: no name is a private buffer (#1039)") {
    Rig r("");
    REQUIRE(r.buffer->Buffer() != nullptr);
    CHECK(r.buffer->Buffer()->name().empty());
    CHECK(r.buffer->Buffer()->frames() == 0);
  }

  TEST_CASE("buffer: a file argument is loaded when the object is created (#1039)") {
    const std::string path = TempFile("yse_buffer_arg_1039.wav");
    {
      auto source = sampleBuffer::create(2, 64, 22050.f);
      source->fill(0.25f);
      REQUIRE(source->write(path.c_str()));
    }
    SUBCASE("the file's channels") {
      Rig r("filearg_1039 " + path);
      const auto& b = r.buffer->Buffer();
      CHECK(b->channels() == 2);
      CHECK(b->frames() == 64);
      CHECK(b->sampleRate() == doctest::Approx(22050.f));
      CHECK(Channel(*b, 1)[63] == doctest::Approx(0.25f));
    }
    SUBCASE("a channel argument wins") {
      Rig r("filearg_1039 " + path + " 0 1");
      CHECK(r.buffer->Buffer()->channels() == 1);
      CHECK(Channel(*r.buffer->Buffer(), 0)[0] == doctest::Approx(0.5f));
    }
    SUBCASE("a missing file is counted, the buffer stays as it was") {
      Rig r("filearg_1039 no_such_file_1039.wav 100");
      CHECK(r.buffer->FailedTasks() == 1);
      CHECK(r.buffer->Buffer()->frames() == YSE::SAMPLERATE / 10);
    }
    Remove(path);
  }

  // ─── sharing with the host ────────────────────────────────────────────────

  TEST_CASE("buffer: a patch sees what the host put in a buffer, and the reverse (#1039)") {
    auto host = sampleBufferRegistry::acquire("shared_1039");
    host->setSize(300, 2);
    host->fill(0.5f);

    {
      // A ~buffer with only the name adopts the host's contents.
      Rig r("shared_1039");
      CHECK(r.buffer->Buffer() == host);
      CHECK(r.Ask("getsamps") == "samps 300");
      CHECK(r.Ask("getchans") == "chans 2");

      // And what the patch does, the host sees.
      r.Message("sizeinsamps 10 1");
      r.Message("fill 3");
      r.Settle();
      CHECK(host->channels() == 1);
      CHECK(Channel(*host, 0) == std::vector<float>(10, 3.f));
    }

    // The patch is gone; the host still holds the buffer and its contents.
    CHECK(sampleBufferRegistry::find("shared_1039") == host);
    CHECK(host->frames() == 10);
  }

  TEST_CASE("buffer: a buffer the patch created is found by the host (#1039)") {
    Rig r("made_by_patch_1039 250");
    auto found = sampleBufferRegistry::find("made_by_patch_1039");
    REQUIRE(found != nullptr);
    CHECK(found->frames() == YSE::SAMPLERATE / 4);
  }

  // ─── messages ─────────────────────────────────────────────────────────────

  TEST_CASE("buffer: the edit messages reach the buffer (#1039)") {
    Rig r("edits_1039");
    const auto& b = r.buffer->Buffer();
    r.Message("sizeinsamps 4");
    r.Message("fill 2");
    r.Settle();
    CHECK(Channel(*b, 0) == std::vector<float>(4, 2.f));

    r.Message("normalize 0.5");
    r.Message("apply offset 1");
    r.Settle();
    CHECK(Channel(*b, 0) == std::vector<float>(4, 1.5f));

    r.Message("apply gain 2");
    r.Message("clear");
    r.Settle();
    CHECK(Channel(*b, 0) == std::vector<float>(4, 0.f));

    r.Message("sizeinsamps 9");
    r.Message("fill 1");
    r.Message("apply hanning");
    r.Settle();
    auto w = Channel(*b, 0);
    CHECK(w[0] == doctest::Approx(0.f));
    CHECK(w[4] == doctest::Approx(1.f));

    r.Message("fill sin 1");
    r.Message("crop_samples 2 3");
    r.Settle();
    REQUIRE(b->frames() == 1);
    CHECK(Channel(*b, 0)[0] == doctest::Approx(std::sin(2.0 * 3.14159265 * 2 / 9)));

    r.Message("sr 1000");
    r.Message("setsize 5 2");
    r.Settle();
    CHECK(b->sampleRate() == doctest::Approx(1000.f));
    CHECK(b->frames() == 5);
    CHECK(b->channels() == 2);
    CHECK(r.Ask("getsize") == "size 5");
    CHECK(r.Ask("getsr") == "sr 1000");
  }

  TEST_CASE("buffer: messages run in the order they were sent (#1039)") {
    // Four tasks in one burst (the scheduler's capacity), with the query last:
    // it must see all three edits before it.
    Rig r("order_1039 0");
    r.Message("sizeinsamps 100");
    r.Message("fill 1");
    r.Message("crop_samples 10 30");
    r.Message("getsamps");
    r.Settle();
    REQUIRE(r.info.gotList);
    CHECK(r.info.received == "samps 20");
  }

  TEST_CASE("buffer: duplicate copies another buffer by name (#1039)") {
    auto source = sampleBufferRegistry::acquire("dup_source_1039");
    source->setSize(3, 2);
    source->fill(4.f);
    Rig r("dup_target_1039");
    r.Message("duplicate dup_source_1039");
    r.Settle();
    CHECK(r.buffer->Buffer()->channels() == 2);
    CHECK(Channel(*r.buffer->Buffer(), 1) == std::vector<float>(3, 4.f));

    r.Message("duplicate no_such_buffer_1039");
    r.Settle();
    CHECK(r.buffer->FailedTasks() == 1);
  }

  TEST_CASE("buffer: write, read and replace through files, with a bang when loaded (#1039)") {
    const std::string path = TempFile("yse_buffer_msg_1039.wav");
    Rig r("files_1039");
    r.Message("sizeinsamps 32 2");
    r.Message("fill 0.75");
    r.Message("write " + path);
    r.Settle();
    CHECK_FALSE(r.loaded.gotBang); // a write has no outlet
    REQUIRE(std::filesystem::exists(path));

    r.Message("clear");
    r.Message("sizeinsamps 4 1");
    r.Message("read " + path);
    r.Settle();
    CHECK(r.loaded.bangCount == 1);
    // read keeps the buffer's one channel and mixes both file channels in.
    CHECK(r.buffer->Buffer()->channels() == 1);
    CHECK(r.buffer->Buffer()->frames() == 32);
    CHECK(Channel(*r.buffer->Buffer(), 0)[0] == doctest::Approx(1.5f));

    r.Message("replace " + path);
    r.Settle();
    CHECK(r.loaded.bangCount == 2);
    CHECK(r.buffer->Buffer()->channels() == 2);

    r.Message("clear");
    r.Message("readagain");
    r.Settle();
    CHECK(r.loaded.bangCount == 3);
    CHECK(Channel(*r.buffer->Buffer(), 1)[31] == doctest::Approx(0.75f));
    Remove(path);
  }

  TEST_CASE("buffer: a failed read sends no bang and changes nothing (#1039)") {
    Rig r("fail_1039 0");
    r.Message("sizeinsamps 7");
    r.Message("read no_such_file_1039.wav");
    r.Settle();
    CHECK_FALSE(r.loaded.gotBang);
    CHECK(r.buffer->FailedTasks() == 1);
    CHECK(r.buffer->Buffer()->frames() == 7);
  }

  TEST_CASE("buffer: malformed and unknown messages are ignored (#1039)") {
    Rig r("ignored_1039 0");
    const std::uint64_t before = r.buffer->Buffer()->version();
    r.Message("frobnicate");
    r.Message("fill");
    r.Message("fill banana");
    r.Message("apply nonsense");
    r.Message("sizeinsamps -3");
    r.Message("crop 5");
    r.Message("read");
    r.Message("readagain"); // nothing read yet
    r.Settle();
    CHECK(r.buffer->Buffer()->version() == before);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("buffer: a task in flight survives the object's deletion (#1039)") {
    // The task finds the buffer by id, never through the object.
    auto host = sampleBufferRegistry::acquire("deleted_1039");
    host->setSize(5);
    {
      Rig r("deleted_1039");
      r.Message("fill 9");
      r.p.DeleteObject(r.handle);
      r.Settle();
      r.p.Calculate(YSE::T_DSP);
    }
    CHECK(Channel(*host, 0) == std::vector<float>(5, 9.f));
  }

  TEST_CASE("buffer: without a patcher a message does nothing (#1039)") {
    dBuffer standalone;
    standalone.SetParams("standalone_1039 0");
    standalone.GetInlet(0)->SetMessage("fill 1", YSE::T_GUI);
    CHECK(standalone.Buffer()->version() == 1); // only the size argument
  }

  TEST_CASE("buffer: arguments survive a DumpJSON / ParseJSON round trip (#1039)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_BUFFER, "roundtrip_1039 100 2") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    auto b = sampleBufferRegistry::find("roundtrip_1039");
    REQUIRE(b != nullptr);
    CHECK(b->channels() == 2);
  }

  TEST_CASE("buffer: messages on the audio thread allocate nothing (#1039)") {
    Rig r("rt_1039 10");
    // Built outside the probe: the strings themselves may allocate.
    const std::vector<std::string> messages = {
        "fill 0.5",
        "apply kaiser 4.5 half",
        "normalize",
        "getsamps",
    };
    r.p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages)
        r.Message(m, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    r.p.FileIO()->WaitIdle();
    {
      // The completions, including the query's answer.
      TestHelpers::ProbeScope probe;
      r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(r.info.received == "samps " + std::to_string(YSE::SAMPLERATE / 100));
  }

} // TEST_SUITE("patcher")
