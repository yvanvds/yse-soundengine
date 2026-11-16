// Tests for the ~polybuffer patcher object (patcher/sampling/dPolybuffer),
// issue #1139. The collection itself is DSP::sampleBufferSet's and is tested
// in Tests/dsp/test_sample_buffer_set.cpp. These show that the messages reach
// the set as background tasks in the order sent, that the queries answer out
// of the left outlet and the changes bang the right one, that embed saves and
// restores the buffers, that the host's resolver places every file, and that
// the buffers it fills are the ones ~play plays by name.
//
// The fixtures are float WAV files at 1000 Hz (a frame per millisecond):
// file k's sample n is 100 k + n.

#include <doctest/doctest.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/sampleBufferSet.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dPolybuffer.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::sampleBufferSet;
using YSE::PATCHER::dPolybuffer;
using YSE::PATCHER::patcherImplementation;

namespace {

  namespace fs = std::filesystem;

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
    std::string str() const {
      return path.string();
    }
  };

  // A float WAV at 1000 Hz, one channel, sample n = 100 k + n.
  void WriteFixture(const std::string& path, int k, UInt frames) {
    auto b = sampleBuffer::create(1, frames, 1000.f);
    std::vector<float> values(frames);
    for (UInt n = 0; n < frames; ++n)
      values[n] = 100.f * static_cast<float>(k) + static_cast<float>(n);
    b->setSamples(0, 0, values.data(), frames);
    REQUIRE(b->write(path.c_str()));
  }

  std::vector<float> Channel(const sampleBuffer& b) {
    std::vector<float> out(b.frames());
    out.resize(b.getSamples(0, 0, out.data(), b.frames()));
    return out;
  }

  std::vector<float> Expected(int k, UInt frames) {
    std::vector<float> out(frames);
    for (UInt n = 0; n < frames; ++n)
      out[n] = 100.f * static_cast<float>(k) + static_cast<float>(n);
    return out;
  }

  // Every list it receives, in order.
  struct ListLog : TestHelpers::SinkBase {
    std::vector<std::string> lines;
    ListLog() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "list_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A ~polybuffer in a patcher, a log on its answers and a sink on its bang.
  // The sinks come first so they outlive the patcher (issue #967).
  struct Rig {
    ListLog info;
    TestHelpers::BangSink done;
    YSE::pHandle infoHandle{&info};
    YSE::pHandle doneHandle{&done};
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle;
    dPolybuffer* poly;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_POLYBUFFER, args);
      REQUIRE(handle != nullptr);
      poly = static_cast<dPolybuffer*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, &infoHandle, 0);
      p.Connect(handle, 1, &doneHandle, 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      poly->GetInlet(0)->SetMessage(text, thread);
    }

    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }

    // A query's answer: every line it sent.
    std::vector<std::string> Ask(const std::string& query) {
      info.lines.clear();
      Message(query);
      Settle();
      return info.lines;
    }

    sampleBufferSet& Set() {
      return *poly->Set();
    }
  };

  struct HostResolver : YSE::pathResolver {
    struct Asked {
      unsigned int id;
      std::string name;
      YSE::PATH_ACCESS access;
    };
    std::mutex mutex;
    std::vector<Asked> asked;
    void Resolve(const YSE::pathRequest& r) override {
      const std::lock_guard<std::mutex> lock(mutex);
      asked.push_back({r.id, r.name, r.access});
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

  YSE::pHandle* FindType(patcherImplementation& p, const char* type) {
    for (unsigned int i = 0; i < p.Objects(); ++i) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == type) return h;
    }
    return nullptr;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ────────────────────────────────────────────────────────────────

  TEST_CASE("~polybuffer: registry and shape (#1139)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "shape_1139");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~polybuffer");
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_POLYBUFFER) listed = true;
    CHECK(listed);

    dPolybuffer standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    REQUIRE(standalone.Set() != nullptr);
    CHECK(standalone.Set()->name().rfind("polybuffer-", 0) == 0);
  }

  TEST_CASE("~polybuffer: the name argument; objects with one name share the set (#1139)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "shared_1139");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "shared_1139 0 1");
    YSE::pHandle* c = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    auto* pa = static_cast<dPolybuffer*>(p.GetObjectFromID(a->GetID()));
    auto* pb = static_cast<dPolybuffer*>(p.GetObjectFromID(b->GetID()));
    auto* pc = static_cast<dPolybuffer*>(p.GetObjectFromID(c->GetID()));
    CHECK(pa->Set()->name() == "shared_1139");
    CHECK(pa->Set() == pb->Set());
    CHECK(sampleBufferSet::find("shared_1139") == pa->Set());
    CHECK(pc->Set()->name().rfind("polybuffer-", 0) == 0);
    CHECK(pc->Set() != pa->Set());
  }

  // ─── messages ─────────────────────────────────────────────────────────────

  TEST_CASE("~polybuffer: append, appendempty and the queries, in the order sent (#1139)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_append_1139");
    WriteFixture(dir / "kick.wav", 1, 30);
    WriteFixture(dir / "snare.wav", 2, 20);
    Rig r("append_1139");

    r.Message("append " + (dir / "kick.wav"));
    r.Message("append " + (dir / "snare.wav"));
    r.Message("appendempty 50 2");
    r.Message("getcount"); // after the three: they run in order
    r.Settle();
    CHECK(r.done.bangCount == 3);
    REQUIRE(r.info.lines.size() == 1);
    CHECK(r.info.lines[0] == "count 3");

    REQUIRE(r.Set().count() == 3);
    CHECK(sampleBufferRegistry::find("append_1139.1") == r.Set().buffer(1));
    CHECK(Channel(*r.Set().buffer(2)) == Expected(2, 20));
    CHECK(r.Set().buffer(3)->frames() == 50);
    CHECK(r.Set().buffer(3)->channels() == 2);

    CHECK(r.Ask("getbufferlist") ==
          std::vector<std::string>{"bufferlist append_1139.1 append_1139.2 append_1139.3", "done"});
    CHECK(r.Ask("getshortname") == std::vector<std::string>{"append_1139.1 kick",
                                                            "append_1139.2 snare",
                                                            "append_1139.3 none", "done"});
    CHECK(r.Ask("getsize") ==
          std::vector<std::string>{"size " + std::to_string((30 + 20 + 100) * sizeof(float))});
    CHECK(r.Ask("dump") ==
          std::vector<std::string>{"1 append_1139.1 " + (dir / "kick.wav") + " 30 1 1000",
                                   "2 append_1139.2 " + (dir / "snare.wav") + " 20 1 1000",
                                   "3 append_1139.3 none 50 2 1000"});
    // Queries bang nothing.
    CHECK(r.done.bangCount == 3);

    // appendempty without arguments: an empty buffer of one channel.
    r.Message("appendempty");
    r.Settle();
    CHECK(r.Set().buffer(4)->frames() == 0);
    CHECK(r.Set().buffer(4)->channels() == 1);
  }

  TEST_CASE("~polybuffer: readfolder, writetofolder and clear (#1139)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_folder_1139");
    WriteFixture(dir / "tom10.wav", 3, 10);
    WriteFixture(dir / "tom9.wav", 2, 10);
    WriteFixture(dir / "tom1.wav", 1, 10);
    Rig r("folder_1139");

    r.Message("readfolder " + dir.str());
    r.Settle();
    CHECK(r.done.bangCount == 1);
    CHECK(r.Ask("getshortname") == std::vector<std::string>{"folder_1139.1 tom1",
                                                            "folder_1139.2 tom9",
                                                            "folder_1139.3 tom10", "done"});

    TempFolder out("yse_pb_folder_out_1139");
    r.Message("writetofolder " + out.str());
    r.Settle();
    CHECK(r.done.bangCount == 2);
    CHECK(fs::exists(out.path / "tom10.wav"));

    auto held = sampleBufferRegistry::find("folder_1139.2");
    REQUIRE(held != nullptr);
    r.Message("clear");
    r.Message("getcount");
    r.Settle();
    CHECK(r.done.bangCount == 3);
    CHECK(r.info.lines.back() == "count 0");
    CHECK(held->frames() == 0);

    // The folder written reads back in the same order.
    r.Message("readfolder " + out.str());
    r.Settle();
    CHECK(r.Set().count() == 3);
    CHECK(Channel(*r.Set().buffer(3)) == Expected(3, 10));
    CHECK(r.Set().buffer(2) == held);
  }

  TEST_CASE("~polybuffer: send passes a ~buffer message to one buffer or all (#1139)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_send_1139");
    WriteFixture(dir / "a.wav", 4, 8);
    Rig r("send_1139");
    r.Message("appendempty 4");
    r.Message("appendempty 4");
    r.Message("appendempty 4");
    r.Settle(); // a patcher holds at most four messages in flight
    r.Message("send 0 fill 0.5");
    r.Message("send 2 fill 0.25");
    r.Message("send 3 sizeinsamps 6");
    r.Settle();
    CHECK(r.done.bangCount == 6);
    CHECK(Channel(*r.Set().buffer(1)) == std::vector<float>(4, 0.5f));
    CHECK(Channel(*r.Set().buffer(2)) == std::vector<float>(4, 0.25f));
    CHECK(r.Set().buffer(3)->frames() == 6);

    r.Message("send 1 replace " + (dir / "a.wav"));
    r.Message("send 0 normalize 2");
    r.Settle();
    CHECK(r.done.bangCount == 8);
    CHECK(r.Set().buffer(1)->frames() == 8);
    CHECK(Channel(*r.Set().buffer(1))[7] == doctest::Approx(2.f));
    CHECK(Channel(*r.Set().buffer(2))[0] == doctest::Approx(2.f));

    // A buffer the set does not have fails, counted, without a bang.
    r.Message("send 9 fill 1");
    r.Settle();
    CHECK(r.done.bangCount == 8);
    CHECK(r.poly->FailedTasks() == 1);

    // Malformed sends and queries through send do nothing at all.
    const std::uint64_t version = r.Set().buffer(1)->version();
    r.Message("send");
    r.Message("send x fill 1");
    r.Message("send -1 fill 1");
    r.Message("send 1");
    r.Message("send 1 frobnicate");
    r.Message("send 1 getsamps");
    r.Message("send 1 fill");
    r.Settle();
    CHECK(r.done.bangCount == 8);
    CHECK(r.info.lines.empty());
    CHECK(r.Set().buffer(1)->version() == version);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~polybuffer: failures and unknown messages (#1139)") {
    Rig r("fail_1139");
    r.Message("append no_such_file_1139.wav");
    r.Message("readfolder no_such_folder_1139");
    r.Settle();
    CHECK(r.done.bangCount == 0);
    CHECK(r.poly->FailedTasks() == 2);
    CHECK(r.Set().count() == 0);

    // The dialogs, the window and the mouse are the host's.
    r.Message("append");
    r.Message("readfolder");
    r.Message("writetofolder");
    r.Message("open");
    r.Message("wclose");
    r.Message("drag");
    r.Message("mouse 1 2");
    r.Message("frobnicate");
    r.Message("");
    r.Settle();
    CHECK(r.done.bangCount == 0);
    CHECK(r.info.lines.empty());
    CHECK(r.p.FileIO()->PendingCount() == 0);
    CHECK(r.poly->Dropped() == 0);
  }

  TEST_CASE("~polybuffer: an answer longer than the record is cut and counted (#1139)") {
    Rig r("trunc_1139");
    // From the host: 1200 buffers, more names than an answer holds.
    for (int i = 0; i < 1200; ++i)
      r.Set().appendEmpty(0.f);
    const std::vector<std::string> list = r.Ask("getbufferlist");
    REQUIRE(list.size() == 2);
    CHECK(list[0].size() <= dPolybuffer::REPLY_CAPACITY);
    CHECK(list[0].rfind("bufferlist trunc_1139.1 trunc_1139.2 ", 0) == 0);
    CHECK(list[1] == "done");
    CHECK(r.poly->Truncated() == 1);
    const std::vector<std::string> names = r.Ask("getshortname");
    CHECK(names.size() < 1201);
    CHECK(names.back() == "done");
    CHECK(r.poly->Truncated() == 2);
    CHECK(r.Ask("getcount") == std::vector<std::string>{"count 1200"});
  }

  TEST_CASE("~polybuffer: a task in flight survives the object's deletion (#1139)") {
    auto host = sampleBufferSet::acquire("deleted_1139");
    {
      Rig r("deleted_1139");
      r.Message("appendempty 10");
      r.p.DeleteObject(r.handle);
      r.Settle();
      r.p.Calculate(YSE::T_DSP);
    }
    CHECK(host->count() == 1);
  }

  TEST_CASE("~polybuffer: without a patcher a message does nothing (#1139)") {
    dPolybuffer standalone;
    standalone.SetParams("standalone_1139");
    standalone.GetInlet(0)->SetMessage("appendempty 10", YSE::T_GUI);
    CHECK(standalone.Set()->count() == 0);
    CHECK(standalone.Dropped() == 0);
  }

  // ─── embed and JSON ───────────────────────────────────────────────────────

  TEST_CASE("~polybuffer: arguments survive a DumpJSON / ParseJSON round trip; without embed "
            "no buffers are saved (#1139)") {
    std::string dump;
    {
      // No sinks: their cords would be in the dump but not in the copy.
      patcherImplementation src(1, nullptr);
      YSE::pHandle* h = src.CreateObject(YSE::OBJ::D_POLYBUFFER, "roundtrip_1139 0 1");
      REQUIRE(h != nullptr);
      h->SetListData(0, "appendempty 10");
      src.FileIO()->WaitIdle();
      src.Calculate(YSE::T_DSP);
      REQUIRE(sampleBufferSet::find("roundtrip_1139")->count() == 1);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("roundtrip_1139 0 1") != std::string::npos);
    CHECK(dump.find("\"buffers\"") == std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* h = FindType(dst, YSE::OBJ::D_POLYBUFFER);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "roundtrip_1139 0 1");
    auto* poly = static_cast<dPolybuffer*>(dst.GetObjectFromID(h->GetID()));
    CHECK(poly->Set()->name() == "roundtrip_1139");
    CHECK(poly->Set()->count() == 0);
  }

  TEST_CASE("~polybuffer: embed restores files and empty buffers in order; a set already "
            "loaded is kept (#1139)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_embed_1139");
    WriteFixture(dir / "one.wav", 1, 12);
    WriteFixture(dir / "two.wav", 2, 7);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* h = src.CreateObject(YSE::OBJ::D_POLYBUFFER, "embed_1139 1");
      REQUIRE(h != nullptr);
      h->SetListData(0, "append " + (dir / "one.wav"));
      h->SetListData(0, "appendempty 25 2");
      h->SetListData(0, "append " + (dir / "two.wav"));
      src.FileIO()->WaitIdle();
      src.Calculate(YSE::T_DSP);
      REQUIRE(sampleBufferSet::find("embed_1139")->count() == 3);
      dump = src.DumpJSON();
    }
    REQUIRE(sampleBufferSet::find("embed_1139") == nullptr);
    CHECK(dump.find("one.wav") != std::string::npos);

    {
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(dump);
      auto set = sampleBufferSet::find("embed_1139");
      REQUIRE(set != nullptr);
      REQUIRE(set->count() == 3);
      CHECK(Channel(*set->buffer(1)) == Expected(1, 12));
      CHECK(set->buffer(2)->frames() == 25);
      CHECK(set->buffer(2)->channels() == 2);
      CHECK(Channel(*set->buffer(3)) == Expected(2, 7));
      CHECK(dst.DumpJSON() == dump);

      // A second patch opening the same collection joins it, adding nothing.
      patcherImplementation again(1, nullptr);
      again.ParseJSON(dump);
      CHECK(set->count() == 3);
    }

    // A file gone missing keeps its place: the next one stays third.
    fs::remove(dir.path / "one.wav");
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    auto set = sampleBufferSet::find("embed_1139");
    REQUIRE(set != nullptr);
    REQUIRE(set->count() == 3);
    CHECK(set->buffer(1)->frames() == 12); // the placeholder's length
    CHECK(Channel(*set->buffer(3)) == Expected(2, 7));
    auto* poly = static_cast<dPolybuffer*>(
        dst.GetObjectFromID(FindType(dst, YSE::OBJ::D_POLYBUFFER)->GetID()));
    CHECK(poly->FailedTasks() == 1);
  }

  // ─── the host's path resolver (issue #1255) ───────────────────────────────

  TEST_CASE("~polybuffer: the host places every file and folder (#1139, #1255)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_resolve_1139");
    WriteFixture(dir / "kick.wav", 1, 9);
    TempFolder out("yse_pb_resolve_out_1139");
    HostResolver host;
    Rig r("resolve_1139", &host);
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);

    r.Message("append drums/kick.wav");
    io->WaitIdle();
    HostResolver::Asked a = host.last();
    CHECK(a.name == "drums/kick.wav");
    CHECK(a.access == YSE::PA_READ);
    CHECK(YSE::patcher::ResolveReply(a.id, (dir / "kick.wav").c_str()));
    r.Settle();
    CHECK(r.done.bangCount == 1);
    CHECK(Channel(*r.Set().buffer(1)) == Expected(1, 9));

    r.Message("readfolder drums");
    io->WaitIdle();
    a = host.last();
    CHECK(a.name == "drums");
    CHECK(a.access == YSE::PA_READ);
    CHECK(YSE::patcher::ResolveReply(a.id, dir.str().c_str()));
    r.Settle();
    CHECK(r.Set().count() == 2);

    r.Message("writetofolder kit");
    io->WaitIdle();
    a = host.last();
    CHECK(a.name == "kit");
    CHECK(a.access == YSE::PA_WRITE);
    CHECK(YSE::patcher::ResolveReply(a.id, out.str().c_str()));
    r.Settle();
    CHECK(fs::exists(out.path / "kick.wav"));

    r.Message("send 2 write two.wav");
    io->WaitIdle();
    a = host.last();
    CHECK(a.name == "two.wav");
    CHECK(a.access == YSE::PA_WRITE);
    CHECK(YSE::patcher::ResolveReply(a.id, (out / "second.wav").c_str()));
    r.Settle();
    CHECK(fs::exists(out.path / "second.wav"));
    CHECK(r.done.bangCount == 4);

    // A refusal fails like a missing file.
    r.Message("append nowhere.wav");
    io->WaitIdle();
    CHECK(YSE::patcher::ResolveReply(host.last().id, nullptr));
    r.Settle();
    CHECK(r.poly->FailedTasks() == 1);
    CHECK(r.Set().count() == 2);
  }

  TEST_CASE("~polybuffer: an embedded patch loads its files through the host, one at a "
            "time, and bangs when done (#1139, #1255)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_restore_1139");
    WriteFixture(dir / "a.wav", 5, 6);
    WriteFixture(dir / "b.wav", 6, 4);
    std::string dump;
    {
      Rig r("restore_1139 1");
      r.Message("append " + (dir / "a.wav"));
      r.Message("append " + (dir / "b.wav"));
      r.Settle();
      dump = r.p.DumpJSON();
    }
    REQUIRE(sampleBufferSet::find("restore_1139") == nullptr);

    TestHelpers::BangSink done;
    YSE::pHandle doneHandle{&done};
    HostResolver host;
    patcherImplementation dst(1, nullptr);
    dst.SetPathResolver(&host, 0);
    dst.ParseJSON(dump);
    YSE::pHandle* h = FindType(dst, YSE::OBJ::D_POLYBUFFER);
    REQUIRE(h != nullptr);
    dst.Connect(h, 1, &doneHandle, 0);
    auto set = sampleBufferSet::find("restore_1139");
    REQUIRE(set != nullptr);
    // The places are there at once, the samples follow.
    REQUIRE(set->count() == 2);
    CHECK(set->buffer(1)->frames() == 6);
    CHECK(Channel(*set->buffer(1)) == std::vector<float>(6, 0.f));

    YSE::PATCHER::fileScheduler* io = dst.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    REQUIRE(host.count() == 1); // one at a time
    HostResolver::Asked a = host.last();
    CHECK(a.name == (dir / "a.wav"));
    CHECK(YSE::patcher::ResolveReply(a.id, a.name.c_str()));
    io->WaitIdle();
    dst.Calculate(YSE::T_DSP);
    CHECK(Channel(*set->buffer(1)) == Expected(5, 6));
    CHECK(done.bangCount == 0);

    io->WaitIdle();
    REQUIRE(host.count() == 2);
    a = host.last();
    CHECK(a.name == (dir / "b.wav"));
    CHECK(YSE::patcher::ResolveReply(a.id, a.name.c_str()));
    io->WaitIdle();
    dst.Calculate(YSE::T_DSP);
    CHECK(Channel(*set->buffer(2)) == Expected(6, 4));
    CHECK(done.bangCount == 1);
    dst.Disconnect(h, 1, &doneHandle, 0);
  }

  // ─── the patch a user builds ──────────────────────────────────────────────

  TEST_CASE("~polybuffer: a message box loads a folder that ~play plays by name (#1139)") {
    // [readfolder <dir>( -> [~polybuffer kit] ; [start 0( -> [~play kit.3]
    // -> [~dac]. The ~play exists before the buffer is filled.
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_patch_1139");
    WriteFixture(dir / "s1.wav", 1, 300);
    WriteFixture(dir / "s2.wav", 2, 300);
    WriteFixture(dir / "s3.wav", 3, 300);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* load = p.CreateObject(YSE::OBJ::G_MESSAGE, "readfolder " + dir.str());
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "kit_patch_1139");
    YSE::pHandle* start = p.CreateObject(YSE::OBJ::G_MESSAGE, "start 0");
    YSE::pHandle* play = p.CreateObject(YSE::OBJ::D_PLAY, "kit_patch_1139.3");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(load != nullptr);
    REQUIRE(poly != nullptr);
    REQUIRE(start != nullptr);
    REQUIRE(play != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(load, 0, poly, 0);
    p.Connect(start, 0, play, 0);
    p.Connect(play, 0, dac, 0);
    p.Calculate(YSE::T_DSP);

    load->SetBang(0);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    REQUIRE(sampleBufferSet::find("kit_patch_1139")->count() == 3);

    start->SetBang(0);
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == 300.f + static_cast<float>(i));
    }
  }

  TEST_CASE("~polybuffer: in a rendered patch after a JSON round trip, the embedded kit "
            "plays through ~play (#1139)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_pb_patch_json_1139");
    WriteFixture(dir / "kick.wav", 1, 300);
    WriteFixture(dir / "snare.wav", 2, 300);
    std::string dump;
    unsigned int startID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* poly = src.CreateObject(YSE::OBJ::D_POLYBUFFER, "kit_json_1139 1");
      YSE::pHandle* start = src.CreateObject(YSE::OBJ::G_MESSAGE, "start 0");
      YSE::pHandle* play = src.CreateObject(YSE::OBJ::D_PLAY, "kit_json_1139.2");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(poly != nullptr);
      REQUIRE(start != nullptr);
      REQUIRE(play != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(start, 0, play, 0);
      src.Connect(play, 0, dac, 0);
      poly->SetListData(0, "readfolder " + dir.str());
      src.FileIO()->WaitIdle();
      src.Calculate(YSE::T_DSP);
      startID = start->GetID();
      dump = src.DumpJSON();
    }
    // Nothing holds the kit any more: the patch must bring it back itself.
    REQUIRE(sampleBufferSet::find("kit_json_1139") == nullptr);
    REQUIRE(sampleBufferRegistry::find("kit_json_1139.2") == nullptr);

    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* start = dst.GetHandleFromID(startID);
    REQUIRE(start != nullptr);
    for (int i = 0; i < 3; ++i)
      dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[5] == 0.f);

    start->SetBang(0);
    dst.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(dst.output[0].getPtr()[i] == 200.f + static_cast<float>(i));
    }
  }

  // ─── real time ────────────────────────────────────────────────────────────

  TEST_CASE("~polybuffer: messages and answers on the audio thread allocate nothing (#1139)") {
    ScopedSampleRate rate(1000);
    TestHelpers::ListSink info;
    TestHelpers::BangSink done;
    YSE::pHandle infoHandle{&info};
    YSE::pHandle doneHandle{&done};
    patcherImplementation p(1, nullptr);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_POLYBUFFER, "rt_1139");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &infoHandle, 0);
    p.Connect(h, 1, &doneHandle, 0);
    auto* poly = static_cast<dPolybuffer*>(p.GetObjectFromID(h->GetID()));
    // Built outside the probe: the strings themselves may allocate.
    const std::vector<std::string> messages = {"appendempty 10 2", "send 0 fill 0.5",
                                               "send 1 normalize"};
    const std::string count = "getcount";
    info.received.reserve(64);
    p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages)
        poly->GetInlet(0)->SetMessage(m, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    p.FileIO()->WaitIdle();
    {
      TestHelpers::ProbeScope probe;
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(done.bangCount == 3);
    {
      TestHelpers::ProbeScope probe;
      poly->GetInlet(0)->SetMessage(count, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    p.FileIO()->WaitIdle();
    {
      // The answer's line is sent from the reserved string.
      TestHelpers::ProbeScope probe;
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(info.received == "count 1");
    CHECK(poly->Set()->buffer(1)->frames() == 10);
    p.Disconnect(h, 0, &infoHandle, 0);
    p.Disconnect(h, 1, &doneHandle, 0);
  }

} // TEST_SUITE("patcher")
