// Tests for the ~sfrecord patcher object (patcher/sampling/dSfrecord), issue
// #1156. The recording is DSP::fileRecorder's, tested in
// Tests/dsp/test_file_recorder.cpp. These show that pins, words and
// arguments reach it in rendered patches: message boxes opening a file and
// recording a ~count into it, read back afterwards; a JSON round trip of the
// arguments and a patch recording after one; the timed, looped and typed
// words; lost frames reported on the second outlet when the disk falls
// behind; the host's path resolver; a task outliving its object; and
// allocation-free messages and blocks on the audio thread.
//
// The engine runs at 1000 Hz, so a millisecond is a frame. ~count 0 0 1
// counts the samples from the first block, so a float file of it holds
// consecutive whole numbers.

#include <doctest/doctest.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/fileRecorder.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dSfrecord.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::fileRecorder;
using YSE::PATCHER::dSfrecord;
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

  struct emptyJob : YSE::INTERNAL::threadPoolJob {
    void run() override {}
  };

  // Every task asked for so far has run: the slow pool has one worker.
  void Drain() {
    emptyJob job;
    YSE::INTERNAL::Global().addSlowJob(&job);
    job.join();
  }

  // Holds the slow pool until opened: the disk falling behind.
  struct gateJob : YSE::INTERNAL::threadPoolJob {
    std::atomic<bool> open{false};
    void run() override {
      while (!open.load())
        std::this_thread::yield();
    }
  };

  struct readBack {
    int channels = 0;
    int rate = 0;
    int format = 0;
    std::vector<float> samples;
    long frames() const {
      return channels > 0 ? static_cast<long>(samples.size()) / channels : 0;
    }
    float at(long frame, int c = 0) const {
      return samples[static_cast<std::size_t>(frame) * channels + c];
    }
  };

  readBack Read(const std::string& path) {
    SndfileHandle h(path.c_str());
    readBack r;
    REQUIRE(h);
    r.channels = h.channels();
    r.rate = h.samplerate();
    r.format = h.format();
    r.samples.resize(static_cast<std::size_t>(h.frames()) * h.channels());
    REQUIRE(h.readf(r.samples.data(), h.frames()) == h.frames());
    return r;
  }

  // Whether channel c of the file counts up by one from its first frame.
  bool Counts(const readBack& r, int c = 0, float step = 1.f) {
    for (long k = 1; k < r.frames(); ++k)
      if (r.at(k, c) != r.at(k - 1, c) + step) return false;
    return true;
  }

  // [~count 0 0 1] -> [~sfrecord args] -> [~dac] (the time), its dropped
  // outlet into an int sink. The sink comes first so it outlives the patcher
  // (issue #967).
  struct Rig {
    TestHelpers::IntSink dropped;
    YSE::pHandle droppedHandle{&dropped};
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* count = nullptr;
    dSfrecord* sf = nullptr;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_SFRECORD, args);
      REQUIRE(handle != nullptr);
      count = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
      REQUIRE(count != nullptr);
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      sf = static_cast<dSfrecord*>(p.GetObjectFromID(handle->GetID()));
      for (int c = 0; c < sf->Channels(); ++c)
        p.Connect(count, 0, handle, static_cast<unsigned int>(c));
      p.Connect(handle, 0, dac, 0);
      p.Connect(handle, 1, &droppedHandle, 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(0)->SetMessage(text, thread);
    }

    void Record(int on, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(0)->SetInt(on, thread);
    }

    // A block, with the tasks it queued run.
    void Block() {
      p.Calculate(YSE::T_DSP);
      Drain();
    }

    void Blocks(int count) {
      for (int b = 0; b < count; ++b)
        Block();
    }
  };

  struct HostResolver : YSE::pathResolver {
    std::mutex mutex;
    std::vector<YSE::pathRequest> asked;
    std::vector<std::string> names;
    void Resolve(const YSE::pathRequest& r) override {
      const std::lock_guard<std::mutex> lock(mutex);
      asked.push_back(r);
      names.emplace_back(r.name);
    }
    std::pair<unsigned int, std::string> last() {
      const std::lock_guard<std::mutex> lock(mutex);
      REQUIRE_FALSE(asked.empty());
      return {asked.back().id, names.back()};
    }
    YSE::PATH_ACCESS lastAccess() {
      const std::lock_guard<std::mutex> lock(mutex);
      REQUIRE_FALSE(asked.empty());
      return asked.back().access;
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

  TEST_CASE("~sfrecord: registry and shape (#1156)") {
    int listed = 0;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_SFRECORD) listed++;
    CHECK(listed == 1);
    patcherImplementation p(1, nullptr);
    struct shape {
      const char* args;
      int channels;
      UInt ring;
    };
    for (const shape& s :
         {shape{"", 1, fileRecorder::DEFAULT_RING_FRAMES}, shape{"2", 2, 65536},
          shape{"3 8192", 3, 8192}, shape{"0 10", 1, 4096}, shape{"99", 64, 65536}}) {
      CAPTURE(s.args);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFRECORD, s.args);
      REQUIRE(h != nullptr);
      auto* sf = static_cast<dSfrecord*>(p.GetObjectFromID(h->GetID()));
      CHECK(sf->Channels() == s.channels);
      CHECK(h->GetInputs() == s.channels);
      CHECK(h->GetOutputs() == 2);
      CHECK(sf->Recorder().inputs() == static_cast<UInt>(s.channels));
      CHECK(sf->Recorder().ringFrames() == s.ring);
    }
  }

  TEST_CASE("~sfrecord: message boxes open a file and record into it; 0 closes it (#1156)") {
    // [samptype float32( [open a.wav( [1( and later [0( -> [~sfrecord 2],
    // fed by [~count]; the 1 waits for the file.
    TempFolder dir("yse_sfrecord_box_1156");
    Rig r("2");
    YSE::pHandle* type = r.p.CreateObject(YSE::OBJ::G_MESSAGE, "samptype float32");
    YSE::pHandle* open = r.p.CreateObject(YSE::OBJ::G_MESSAGE, "open " + (dir / "a.wav"));
    YSE::pHandle* one = r.p.CreateObject(YSE::OBJ::G_MESSAGE, "1");
    YSE::pHandle* zero = r.p.CreateObject(YSE::OBJ::G_MESSAGE, "0");
    for (YSE::pHandle* box : {type, open, one, zero}) {
      REQUIRE(box != nullptr);
      r.p.Connect(box, 0, r.handle, 0);
    }
    r.Block();
    type->SetBang(0);
    open->SetBang(0);
    one->SetBang(0);
    int recording = 0;
    for (int b = 0; b < 20 && recording < 6; ++b) {
      r.Block();
      if (r.sf->Recorder().recording()) recording++;
    }
    REQUIRE(recording == 6);
    // The time outlet through ~dac: 5 blocks recorded before this one.
    CHECK(r.p.output[0].getPtr()[3] == static_cast<float>(5 * N + 3));
    zero->SetBang(0);
    r.Blocks(3);
    CHECK_FALSE(r.sf->Recorder().isOpen());
    CHECK(r.p.output[0].getPtr()[3] == static_cast<float>(6 * N)); // held
    CHECK_FALSE(r.dropped.gotInt);

    const readBack f = Read(dir / "a.wav");
    CHECK(f.channels == 2);
    CHECK(f.rate == 1000);
    CHECK((f.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT);
    REQUIRE(f.frames() == 6 * static_cast<long>(N));
    CHECK(Counts(f, 0));
    CHECK(Counts(f, 1));
    CHECK(f.at(0, 0) == f.at(0, 1));
    CHECK(static_cast<long>(f.at(0)) % static_cast<long>(N) == 0);
    CHECK(r.sf->FailedTasks() == 0);
  }

  TEST_CASE("~sfrecord: record, loop and sortloop, start, stop, nchans, bitdepth and samptype "
            "by word (#1156)") {
    TempFolder dir("yse_sfrecord_words_1156");
    SUBCASE("record <ms> stops by itself") {
      Rig r("");
      r.Message("samptype float32");
      r.Message("open " + (dir / "a.aif"));
      r.Message("record 300");
      r.Blocks(6);
      CHECK_FALSE(r.sf->Recorder().isOpen());
      const readBack f = Read(dir / "a.aif");
      CHECK((f.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_AIFF);
      REQUIRE(f.frames() == 300);
      CHECK(Counts(f));
    }
    SUBCASE("loop <ms> keeps the last stretch, in order with sortloop") {
      Rig r("1 0 0 0 16 0 0 0 1");
      r.Message("samptype float32");
      r.Message("open " + (dir / "b.wav"));
      r.Message("loop 200");
      r.Blocks(8);
      r.Message("stop");
      r.Blocks(2);
      // Over 200 frames recorded: the file holds the last 200, oldest first.
      CHECK(r.p.output[0].getPtr()[0] > 400.f);
      const readBack f = Read(dir / "b.wav");
      REQUIRE(f.frames() == 200);
      CHECK(Counts(f));
    }
    SUBCASE("start <file> <channels>; nchans; open with a type") {
      Rig r("2");
      r.Message("samptype float32");
      r.Message("start " + (dir / "c.wav") + " 1");
      r.Blocks(3);
      r.Record(0);
      r.Blocks(2);
      const readBack c = Read(dir / "c.wav");
      CHECK(c.channels == 1);
      CHECK(c.frames() > 0);
      CHECK(Counts(c));

      r.Message("nchans 3");
      r.Message("open wave " + (dir / "d.snd"));
      r.Record(1);
      r.Blocks(3);
      r.Message("stop");
      r.Blocks(2);
      const readBack d = Read(dir / "d.snd");
      CHECK((d.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV);
      CHECK(d.channels == 3);
      REQUIRE(d.frames() > 0);
      CHECK(d.at(0, 2) == 0.f);
      CHECK(d.at(0, 1) == d.at(0, 0));

      SF_INFO flac{};
      flac.channels = 1;
      flac.samplerate = 1000;
      flac.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
      if (sf_format_check(&flac) != 0) {
        r.Message("nchans 0");
        r.Message("open " + (dir / "e.aiff") + " flac");
        r.Record(1);
        r.Blocks(2);
        CHECK(r.sf->Recorder().file() == dir / "e.aiff");
        r.Message("stop");
        r.Blocks(2);
        CHECK((Read(dir / "e.aiff").format & SF_FORMAT_TYPEMASK) == SF_FORMAT_FLAC);
      }
    }
    SUBCASE("bitdepth, samptype and the integer words") {
      Rig r("1 0 0 0 24");
      r.Message("open " + (dir / "f.wav"));
      r.Record(1);
      r.Blocks(2);
      r.Record(0);
      r.Blocks(2);
      CHECK((Read(dir / "f.wav").format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_24);
      r.Message("bitdepth 8");
      r.Message("dither 2");
      r.Message("quantization 1");
      r.Message("open " + (dir / "g.aif"));
      r.Record(1);
      r.Blocks(2);
      r.Record(0);
      r.Blocks(2);
      CHECK((Read(dir / "g.aif").format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_S8);
      CHECK(r.sf->Recorder().dither() == fileRecorder::DITHER::NOISE_SHAPED);
      CHECK(r.sf->Recorder().quantization() == fileRecorder::QUANTIZE::FLOOR);
      r.Message("samptype mulaw");
      r.Message("open " + (dir / "h.wav"));
      r.Record(1);
      r.Blocks(2);
      r.Record(0);
      r.Blocks(2);
      CHECK((Read(dir / "h.wav").format & SF_FORMAT_SUBMASK) == SF_FORMAT_ULAW);
      r.Message("samptype int16 1");
      r.Message("open " + (dir / "i.raw"));
      r.Blocks(1);
      CHECK(r.sf->Recorder().byteSwap());
      CHECK(r.sf->FailedTasks() == 0);
    }
    SUBCASE("ramps by argument; a failed open") {
      Rig r("1 0 100 0");
      r.Message("samptype float32");
      r.Message("open " + (dir / "missing/j.wav"));
      r.Record(1);
      r.Blocks(2);
      CHECK(r.sf->FailedTasks() == 1);
      CHECK_FALSE(r.sf->Recorder().recording());
      CHECK(r.sf->Recorder().beginRamp() == 100.f);
    }
  }

  TEST_CASE("~sfrecord: the dropped outlet reports frames lost while the disk is held up "
            "(#1156)") {
    TempFolder dir("yse_sfrecord_drop_1156");
    Rig r("1 4096");
    r.Message("samptype float32");
    r.Message("open " + (dir / "x.wav"));
    r.Record(1);
    r.Blocks(3);
    REQUIRE(r.sf->Recorder().recording());
    gateJob gate;
    YSE::INTERNAL::Global().addSlowJob(&gate);
    // 4096 frames of ring, 32 blocks: 40 more fill it and lose 8 blocks'
    // worth, less what the last task before the gate wrote.
    for (int b = 0; b < 40; ++b)
      r.p.Calculate(YSE::T_DSP);
    gate.open.store(true);
    gate.join();
    CHECK(r.dropped.gotInt);
    CHECK(r.dropped.received > 0);
    CHECK(static_cast<std::uint64_t>(r.dropped.received) == r.sf->Recorder().dropped());
    r.Record(0);
    r.Blocks(3);
    // The time holds the recording's length, lost frames included.
    const auto length = static_cast<long>(r.p.output[0].getPtr()[0]);
    CHECK(length >= 41 * static_cast<long>(N));
    const readBack f = Read(dir / "x.wav");
    CHECK(f.frames() == length - r.dropped.received);
    CHECK(Counts(f, 0)); // the frames lost were the last ones
    CHECK(r.sf->Recorder().failures() == 0);
  }

  TEST_CASE("~sfrecord: arguments survive a DumpJSON / ParseJSON round trip; a patch records "
            "after one (#1156)") {
    TempFolder dir("yse_sfrecord_json_1156");
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int ids[4] = {};
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
      YSE::pHandle* sf = src.CreateObject(YSE::OBJ::D_SFRECORD, "2 8192 0 0 24 1 1 3 1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* type = src.CreateObject(YSE::OBJ::G_MESSAGE, "samptype float32");
      YSE::pHandle* open = src.CreateObject(YSE::OBJ::G_MESSAGE, "open " + (dir / "take.wav"));
      YSE::pHandle* one = src.CreateObject(YSE::OBJ::G_MESSAGE, "1");
      YSE::pHandle* zero = src.CreateObject(YSE::OBJ::G_MESSAGE, "0");
      for (YSE::pHandle* h : {count, sf, dac, type, open, one, zero})
        REQUIRE(h != nullptr);
      src.Connect(count, 0, sf, 0);
      src.Connect(count, 0, sf, 1);
      src.Connect(sf, 0, dac, 0);
      for (YSE::pHandle* box : {type, open, one, zero})
        src.Connect(box, 0, sf, 0);
      ids[0] = type->GetID();
      ids[1] = open->GetID();
      ids[2] = one->GetID();
      ids[3] = zero->GetID();
      dump = src.DumpJSON();
    }

    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* sfh = FindType(dst, YSE::OBJ::D_SFRECORD);
    REQUIRE(sfh != nullptr);
    auto* sf = static_cast<dSfrecord*>(dst.GetObjectFromID(sfh->GetID()));
    CHECK(sf->Channels() == 2);
    CHECK(sf->Recorder().ringFrames() == 8192);
    CHECK(sf->Recorder().dither() == fileRecorder::DITHER::TRIANGULAR);
    CHECK(sf->Recorder().quantization() == fileRecorder::QUANTIZE::FLOOR);
    CHECK(sf->Recorder().sortLoop());

    // The loaded boxes drive the loaded patch.
    YSE::pHandle* boxes[4] = {};
    for (int t = 0; t < 4; ++t) {
      boxes[t] = dst.GetHandleFromID(ids[t]);
      REQUIRE(boxes[t] != nullptr);
    }
    dst.Calculate(YSE::T_DSP);
    boxes[0]->SetBang(0);
    boxes[1]->SetBang(0);
    boxes[2]->SetBang(0);
    for (int b = 0; b < 8; ++b) {
      dst.Calculate(YSE::T_DSP);
      Drain();
    }
    boxes[3]->SetBang(0);
    for (int b = 0; b < 3; ++b) {
      dst.Calculate(YSE::T_DSP);
      Drain();
    }
    // nchans 3 from the arguments: two counts and a silent channel, as
    // float32 from the message.
    const readBack f = Read(dir / "take.wav");
    CHECK(f.channels == 3);
    CHECK((f.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT);
    REQUIRE(f.frames() >= 5 * static_cast<long>(N));
    CHECK(f.frames() % static_cast<long>(N) == 0);
    CHECK(Counts(f, 0));
    CHECK(Counts(f, 1));
    for (long k = 0; k < f.frames(); ++k)
      CHECK(f.at(k, 2) == 0.f);
    CHECK(static_cast<long>(f.at(0)) % static_cast<long>(N) == 0);
  }

  TEST_CASE("~sfrecord: the host places the file it records (#1156, #1255)") {
    TempFolder dir("yse_sfrecord_resolve_1156");
    HostResolver host;
    Rig r("", &host);
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);
    r.Message("samptype float32");
    r.Message("open takes/a.wav");
    r.Record(1);
    io->WaitIdle();
    CHECK(host.last().second == "takes/a.wav");
    CHECK(host.lastAccess() == YSE::PA_WRITE);
    // The 1 waits for the host.
    r.Block();
    CHECK_FALSE(r.sf->Recorder().recording());
    CHECK(YSE::patcher::ResolveReply(host.last().first, (dir / "a.wav").c_str()));
    Drain();
    r.Blocks(3);
    CHECK(r.sf->Recorder().recording());
    CHECK(r.sf->Recorder().file() == dir / "a.wav");
    r.Record(0);
    r.Blocks(2);
    CHECK(Read(dir / "a.wav").frames() > 0);

    // A refusal fails like a folder that is not there.
    r.Message("open nowhere/b.wav");
    io->WaitIdle();
    CHECK(YSE::patcher::ResolveReply(host.last().first, nullptr));
    r.Blocks(2);
    CHECK(r.sf->FailedTasks() == 1);
    CHECK_FALSE(r.sf->Recorder().isOpen());
  }

  TEST_CASE("~sfrecord: a recording outlives its object's deletion; no patcher, no task "
            "(#1156)") {
    TempFolder dir("yse_sfrecord_delete_1156");
    {
      Rig r("");
      r.Message("samptype float32");
      r.Message("open " + (dir / "a.wav"));
      r.Record(1);
      r.Blocks(4);
      r.p.DeleteObject(r.handle);
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    // The recorder finished the file when the last holder let go.
    const readBack f = Read(dir / "a.wav");
    CHECK(f.frames() >= static_cast<long>(N));
    CHECK(Counts(f));

    dSfrecord standalone;
    standalone.GetInlet(0)->SetMessage("open " + (dir / "b.wav"), YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 1);
    CHECK_FALSE(standalone.Recorder().isOpen());
  }

  TEST_CASE("~sfrecord: words, numbers and blocks on the audio thread allocate nothing "
            "(#1156)") {
    TempFolder dir("yse_sfrecord_rt_1156");
    Rig r("2");
    // Built before the probe: the strings themselves allocate.
    const std::string open = "open " + (dir / "a.wav");
    const std::string start = "start " + (dir / "b.wav") + " 2";
    const std::vector<std::string> words = {
        "samptype float32", "bitdepth 24",    "format 16", "beginramp 10", "endramp 5",
        "dither 1",         "quantization 1", "nchans 2",  "sortloop 1",   "record 50",
        "loop 100",         "stop",           "print",     "resample 2",   "nonsense"};
    for (int round = 0; round < 3; ++round) {
      CAPTURE(round);
      {
        TestHelpers::ProbeScope probe;
        r.Message(open, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Record(1, YSE::T_DSP);
        r.Message(start, YSE::T_DSP);
        r.sf->GetInlet(0)->SetFloat(0.f, YSE::T_DSP);
        r.Record(1, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      {
        // The blocks that deliver the opens, start, record, queue the disk
        // writes and stop.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 4; ++block)
          r.p.Calculate(YSE::T_DSP);
        r.Record(0, YSE::T_DSP);
        for (int block = 0; block < 4; ++block)
          r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    CHECK(r.p.FileIO()->Dropped() < 100);
  }
}
