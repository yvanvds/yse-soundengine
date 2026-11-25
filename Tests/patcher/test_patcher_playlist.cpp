// Tests for the ~playlist patcher object (patcher/sampling/dPlaylist), issue
// #1158. The list and its playback are DSP::filePlaylist's, tested in
// Tests/dsp/test_file_playlist.cpp. These show that pins, words and
// parameters reach it, in rendered patches heard through a ~dac: message
// boxes appending files and playing one with its notifications, every word,
// getcontent's lines, the GUI value a host draws from, a JSON round trip of
// the parameters and the clips played after it, the host's path resolver, a
// task outliving its object, and allocation-free messages and blocks on the
// audio thread.
//
// The fixtures are float WAV files at 1000 Hz and the engine runs at 1000 Hz,
// so a millisecond is a frame and a sample. A clip fades in over its first
// 200 samples; FadeIn mirrors the engine's arithmetic.

#include <doctest/doctest.h>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/filePlaylist.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dPlaylist.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::filePlaylist;
using YSE::PATCHER::dPlaylist;
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

  float Ramp(long n, float offset = 0.f) {
    return static_cast<float>(n + 1) + offset;
  }

  void WriteRamp(const std::string& path, long frames, float offset = 0.f) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, 1000);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (long n = 0; n < frames; ++n)
      samples[static_cast<std::size_t>(n)] = Ramp(n, offset);
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  struct emptyJob : YSE::INTERNAL::threadPoolJob {
    void run() override {}
  };

  // Every disk fill and task asked for so far has landed: the slow pool has
  // one worker.
  void Drain() {
    emptyJob job;
    YSE::INTERNAL::Global().addSlowJob(&job);
    job.join();
  }

  struct FadeIn {
    float v = 0.f;
    float next() {
      const float g = v;
      v += 0.005f;
      if (v >= 1.f) v = 1.f;
      return g;
    }
  };

  std::vector<std::string> Words(const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    std::string w;
    while (in >> w)
      out.push_back(w);
    return out;
  }

  YSE::pHandle* FindType(patcherImplementation& p, const char* type) {
    for (unsigned int i = 0; i < p.Objects(); ++i) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == type) return h;
    }
    return nullptr;
  }

  // Every list that arrives, in order.
  struct LogSink : TestHelpers::SinkBase {
    std::vector<std::string> lists;
    LogSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lists.push_back(v); });
    }
    const char* Type() const override {
      return "log_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    bool Has(const std::string& line) const {
      for (const std::string& l : lists)
        if (l == line) return true;
      return false;
    }
    int CountStarting(const std::string& word) const {
      int n = 0;
      for (const std::string& l : lists)
        if (l.rfind(word, 0) == 0) n++;
      return n;
    }
  };

  // [~playlist args] -> [~dac], its notifications and content into log
  // sinks (unless `sinks` is false: they record by allocating). The sinks
  // come first so they outlive the patcher (issue #967).
  struct Rig {
    LogSink notes;
    LogSink content;
    YSE::pHandle notesHandle{&notes};
    YSE::pHandle contentHandle{&content};
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dPlaylist* pl = nullptr;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr,
                 bool sinks = true) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_PLAYLIST, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      pl = static_cast<dPlaylist*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, dac, 0);
      if (pl->Channels() > 1) p.Connect(handle, 1, dac, 1);
      if (sinks) {
        p.Connect(handle, static_cast<unsigned int>(pl->Channels() + 1), &notesHandle, 0);
        p.Connect(handle, static_cast<unsigned int>(pl->Channels() + 2), &contentHandle, 0);
      }
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      pl->GetInlet(0)->SetMessage(text, thread);
    }

    void Clip(int clip, YSE::THREAD thread = YSE::T_GUI) {
      pl->GetInlet(0)->SetInt(clip, thread);
    }

    // The tasks and fills land, then a block delivers them.
    void Settle() {
      Drain();
      p.Calculate(YSE::T_DSP);
      Drain();
    }

    std::vector<float> Block(UInt channel = 0) {
      p.Calculate(YSE::T_DSP);
      Drain();
      const float* o = p.output[channel].getPtr();
      return std::vector<float>(o, o + N);
    }

    std::vector<std::string> Names() {
      std::vector<std::string> out;
      for (const filePlaylist::clip& c : pl->Playlist().clips())
        out.push_back(c.name);
      return out;
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

  TEST_CASE("~playlist: registry and shape; channelcount sets the signal outlets (#1158)") {
    int listed = 0;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_PLAYLIST) listed++;
    CHECK(listed == 1);
    patcherImplementation p(1, nullptr);
    struct shape {
      const char* args;
      int channels;
    };
    for (const shape& s :
         {shape{"", 2}, shape{"1", 1}, shape{"4", 4}, shape{"0", 1}, shape{"99", 64}}) {
      CAPTURE(s.args);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PLAYLIST, s.args);
      REQUIRE(h != nullptr);
      auto* pl = static_cast<dPlaylist*>(p.GetObjectFromID(h->GetID()));
      CHECK(pl->Channels() == s.channels);
      // The channels, the playhead, the notifications and the content.
      CHECK(h->GetOutputs() == s.channels + 3);
      CHECK(h->GetInputs() == 1);
      CHECK(pl->Playlist().channels() == static_cast<UInt>(s.channels));
    }
  }

  TEST_CASE("~playlist: message boxes append files and play one to its end; start and done "
            "notifications (#1158)") {
    // [append a.wav( [append b.wav( [2( -> [~playlist 1] -> [~dac], sent
    // together: the 2 waits for the files.
    TempFolder dir("yse_playlist_box_1158");
    WriteRamp(dir / "a.wav", 600);
    WriteRamp(dir / "b.wav", 400, 10000.f);
    LogSink notes;
    YSE::pHandle notesHandle(&notes);
    ScopedSampleRate rate(1000);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::G_MESSAGE, "append " + (dir / "a.wav"));
    YSE::pHandle* second = p.CreateObject(YSE::OBJ::G_MESSAGE, "append " + (dir / "b.wav"));
    YSE::pHandle* two = p.CreateObject(YSE::OBJ::G_MESSAGE, "2");
    YSE::pHandle* pl = p.CreateObject(YSE::OBJ::D_PLAYLIST, "1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(two != nullptr);
    REQUIRE(pl != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(first, 0, pl, 0);
    p.Connect(second, 0, pl, 0);
    p.Connect(two, 0, pl, 0);
    p.Connect(pl, 0, dac, 0);
    p.Connect(pl, 2, &notesHandle, 0);
    p.Calculate(YSE::T_DSP);

    first->SetBang(0);
    second->SetBang(0);
    two->SetBang(0);
    std::vector<float> heard;
    for (int b = 0; b < 20 && !notes.Has("done 2 b.wav"); ++b) {
      Drain();
      p.Calculate(YSE::T_DSP);
      const float* o = p.output[0].getPtr();
      if (heard.empty() && o[N - 1] == 0.f) continue; // the files not open yet
      heard.insert(heard.end(), o, o + N);
    }
    CHECK(notes.lists == std::vector<std::string>{"start 2 b.wav", "done 2 b.wav"});
    REQUIRE(heard.size() >= 400);
    FadeIn in;
    for (std::size_t k = 0; k < 400; ++k) {
      CAPTURE(k);
      CHECK(heard[k] == doctest::Approx(Ramp(static_cast<long>(k), 10000.f) * in.next()));
    }
    for (std::size_t k = 400; k < heard.size(); ++k)
      CHECK(heard[k] == 0.f);
  }

  TEST_CASE("~playlist: append, move, remove, selection, setclip, next, pause, loop, the "
            "reports and getcontent by word (#1158)") {
    TempFolder dir("yse_playlist_words_1158");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 500, 10000.f);
    WriteRamp(dir / "c.wav", 300, 20000.f);
    Rig r("1");
    r.Message("append " + (dir / "a.wav"));
    r.Message("append " + (dir / "b.wav"));
    r.Message("append " + (dir / "c.wav") + " 1");
    r.Settle();
    CHECK(r.Names() == std::vector<std::string>{"c.wav", "a.wav", "b.wav"});
    r.Message("move 1 3");
    r.Settle();
    CHECK(r.Names() == std::vector<std::string>{"a.wav", "b.wav", "c.wav"});
    r.Message("remove 2");
    r.Settle();
    CHECK(r.Names() == std::vector<std::string>{"a.wav", "c.wav"});
    CHECK(r.pl->FailedTasks() == 0);
    r.Message("remove 9");
    r.Message("append " + (dir / "missing.wav"));
    r.Settle();
    CHECK(r.pl->FailedTasks() == 2);

    r.Message("selectionms 1 100 300");
    r.Message("selection 2 0.5 1");
    r.Settle();
    std::vector<filePlaylist::clip> c = r.pl->Playlist().clips();
    CHECK(c[0].startMs == doctest::Approx(100.f));
    CHECK(c[0].endMs == doctest::Approx(300.f));
    CHECK(c[1].startMs == doctest::Approx(150.f));
    CHECK(c[1].selected);
    r.Message("selection 0.1 0.2"); // every clip
    r.Settle();
    c = r.pl->Playlist().clips();
    CHECK(c[0].startMs == doctest::Approx(100.f));
    CHECK(c[0].endMs == doctest::Approx(200.f));
    CHECK(c[1].startMs == doctest::Approx(30.f));
    r.Message("selection 2"); // clears clip 2's
    r.Message("selectionms 1 100 300");
    r.Settle();
    c = r.pl->Playlist().clips();
    CHECK_FALSE(c[1].selected);
    CHECK(c[1].endMs == doctest::Approx(300.f));

    r.Message("setclip 1 loop 1");
    r.Message("setclip 2 speed 2");
    r.Message("setclip 2 quality best");
    r.Message("setclip 2 nonsense 1");
    r.Message("setclip 2 pitchshift 0"); // refused by the class
    r.Settle();
    c = r.pl->Playlist().clips();
    CHECK(c[0].settings[filePlaylist::LOOP] == 1.f);
    CHECK(c[1].settings[filePlaylist::SPEED] == 2.f);
    CHECK(c[1].settings[filePlaylist::QUALITY] == 3.f);
    CHECK(std::isnan(c[1].settings[filePlaylist::PITCHSHIFT]));
    r.Message("setclip 2 quality"); // follows the playlist again
    r.Settle();
    CHECK(std::isnan(r.pl->Playlist().clips()[1].settings[filePlaylist::QUALITY]));

    // Clip 1 loops its selection, reporting each loop.
    r.Message("loopreport 1");
    r.Clip(1);
    std::vector<float> heard;
    for (int b = 0; b < 8; ++b) {
      const std::vector<float> o = r.Block();
      heard.insert(heard.end(), o.begin(), o.end());
    }
    FadeIn in;
    for (std::size_t n = 0; n < heard.size(); ++n) {
      CAPTURE(n);
      CHECK(heard[n] == doctest::Approx(Ramp(100 + static_cast<long>(n % 200)) * in.next()));
    }
    CHECK(r.notes.lists.front() == "start 1 a.wav");
    CHECK(r.notes.CountStarting("loopnotify 1 a.wav") == static_cast<int>(8 * N / 200));
    CHECK(r.notes.CountStarting("done") == 0);

    // next: clip 2 at its own speed 2, 300 frames in 150 samples.
    r.notes.lists.clear();
    r.Message("next");
    for (int b = 0; b < 4; ++b)
      r.Block();
    CHECK(r.notes.lists == std::vector<std::string>{"start 2 c.wav", "done 2 c.wav"});

    // Pause, resume, the playlist's speed, progress.
    r.notes.lists.clear();
    r.Message("speed 0.5");
    r.Message("reportprogress 1");
    r.Message("loopreport 0");
    r.Clip(1);
    r.Block();
    CHECK(r.pl->Playlist().speed() == 0.5f);
    CHECK(r.notes.lists.front() == "start 1 a.wav");
    CHECK(r.notes.CountStarting("progress 1 ") == 1);
    r.Message("pause");
    r.Block();
    CHECK(r.pl->Playlist().paused());
    r.Message("resume");
    r.Block();
    CHECK_FALSE(r.pl->Playlist().paused());
    CHECK(r.notes.CountStarting("loopnotify") == 0);
    r.Clip(0);
    for (int b = 0; b < 3; ++b)
      r.Block();
    CHECK(r.notes.lists.back() == "done 1 a.wav");
    CHECK(r.pl->Playlist().playing() == 0);

    // getcontent: the count, then a line per clip with its own settings.
    r.Message("getcontent");
    r.Block();
    REQUIRE(r.content.lists.size() == 3);
    CHECK(r.content.lists[0] == "clips 2");
    const std::vector<std::string> one = Words(r.content.lists[1]);
    REQUIRE(one.size() == 9);
    CHECK(one[0] == "clip");
    CHECK(one[1] == "1");
    CHECK(one[2] == "a.wav");
    CHECK(one[3] == dir / "a.wav");
    CHECK(std::stof(one[4]) == doctest::Approx(1000.f));
    CHECK(std::stof(one[5]) == doctest::Approx(100.f));
    CHECK(std::stof(one[6]) == doctest::Approx(300.f));
    CHECK(one[7] == "loop");
    CHECK(std::stof(one[8]) == 1.f);
    const std::vector<std::string> two = Words(r.content.lists[2]);
    REQUIRE(two.size() == 9);
    CHECK(two[2] == "c.wav");
    CHECK(std::stof(two[6]) == doctest::Approx(300.f));
    CHECK(two[7] == "speed");
    CHECK(std::stof(two[8]) == 2.f);

    r.Message("clear");
    r.Settle();
    CHECK(r.pl->Playlist().count() == 0);
  }

  TEST_CASE("~playlist: the GUI value is the list and the playhead, for the host to draw "
            "(#1158)") {
    TempFolder dir("yse_playlist_gui_1158");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 500);
    Rig r("");
    CHECK(r.pl->GetGuiValueCount() == dPlaylist::HEAD_CELLS);
    r.Message("append " + (dir / "a.wav"));
    r.Message("append " + (dir / "b.wav"));
    r.Message("selectionms 2 100 400");
    r.Settle();
    r.Clip(1);
    r.Block();
    r.Block();
    CHECK(r.pl->GetGuiValueCount() == dPlaylist::HEAD_CELLS + 2 * dPlaylist::CLIP_CELLS);
    const std::vector<std::string> cells = Words(r.pl->GetGuiValue());
    REQUIRE(cells.size() == dPlaylist::HEAD_CELLS + 2 * dPlaylist::CLIP_CELLS);
    CHECK(cells[0] == std::to_string(r.pl->Playlist().version()));
    CHECK(cells[1] == "2");
    CHECK(cells[2] == "1");
    CHECK(cells[3] == "0");
    CHECK(std::stof(cells[4]) == doctest::Approx(static_cast<float>(2 * N - 1) / 1000.f));
    CHECK(cells[5] == "a.wav");
    CHECK(std::stof(cells[6]) == doctest::Approx(1000.f));
    CHECK(cells[9] == "b.wav");
    CHECK(std::stof(cells[11]) == doctest::Approx(100.f));
    CHECK(std::stof(cells[12]) == doctest::Approx(400.f));
    CHECK(r.pl->GetGuiValueAt(9) == "b.wav");
    CHECK(r.pl->GetGuiValueAt(99).empty());
    r.Message("pause");
    r.Block();
    CHECK(r.pl->GetGuiValueAt(3) == "1");
  }

  TEST_CASE("~playlist: parameters and clips survive a DumpJSON / ParseJSON round trip; the "
            "rendered patch plays a restored clip's selection at its own settings (#1158)") {
    TempFolder dir("yse_playlist_json_1158");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 2000, 10000.f);
    ScopedSampleRate rate(1000);

    // An empty list saves nothing but its parameters.
    {
      patcherImplementation bare(1, nullptr);
      REQUIRE(bare.CreateObject(YSE::OBJ::D_PLAYLIST, "1 1 0.5 0 1 0 2 1 0") != nullptr);
      const std::string dump = bare.DumpJSON();
      CHECK(dump.find("\"1 1 0.5 0 1 0 2 1 0\"") != std::string::npos);
      CHECK(dump.find("clips") == std::string::npos);
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(dump);
      CHECK(dst.DumpJSON() == dump);
      auto* pl = static_cast<dPlaylist*>(
          dst.GetObjectFromID(FindType(dst, YSE::OBJ::D_PLAYLIST)->GetID()));
      CHECK(pl->Channels() == 1);
      dst.Calculate(YSE::T_DSP);
      CHECK(pl->Playlist().loop());
      CHECK(pl->Playlist().speed() == 0.5f);
      CHECK(pl->Playlist().quality() == YSE::DSP::pitchShifter::BETTER);
    }

    // [2( -> [~playlist 2] -> [~dac], with b's selection and its own loop.
    std::string dump;
    unsigned int twoID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* two = src.CreateObject(YSE::OBJ::G_MESSAGE, "2");
      YSE::pHandle* pl = src.CreateObject(YSE::OBJ::D_PLAYLIST, "2");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(two != nullptr);
      REQUIRE(pl != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(two, 0, pl, 0);
      src.Connect(pl, 0, dac, 0);
      src.Connect(pl, 1, dac, 1);
      pl->SetListData(0, "append " + (dir / "a.wav"));
      pl->SetListData(0, "append " + (dir / "b.wav"));
      // At most fileScheduler::CAPACITY requests in flight: settled between.
      Drain();
      src.Calculate(YSE::T_DSP);
      Drain();
      pl->SetListData(0, "selectionms 2 1200 1500");
      pl->SetListData(0, "setclip 2 loop 1");
      pl->SetListData(0, "setclip 2 speed 1");
      Drain();
      src.Calculate(YSE::T_DSP);
      Drain();
      twoID = two->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("clips") != std::string::npos);

    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    auto* pl =
        static_cast<dPlaylist*>(dst.GetObjectFromID(FindType(dst, YSE::OBJ::D_PLAYLIST)->GetID()));
    const std::vector<filePlaylist::clip> c = pl->Playlist().clips();
    REQUIRE(c.size() == 2);
    CHECK(c[0].file == dir / "a.wav");
    CHECK_FALSE(c[0].selected);
    CHECK(c[1].name == "b.wav");
    CHECK(c[1].selected);
    CHECK(c[1].startMs == doctest::Approx(1200.f));
    CHECK(c[1].endMs == doctest::Approx(1500.f));
    CHECK(c[1].settings[filePlaylist::LOOP] == 1.f);
    CHECK(c[1].settings[filePlaylist::SPEED] == 1.f);
    CHECK(std::isnan(c[1].settings[filePlaylist::PITCHSHIFT]));

    YSE::pHandle* two = dst.GetHandleFromID(twoID);
    REQUIRE(two != nullptr);
    dst.Calculate(YSE::T_DSP);
    two->SetBang(0);
    std::vector<float> left;
    for (int b = 0; b < 10; ++b) {
      dst.Calculate(YSE::T_DSP);
      Drain();
      left.insert(left.end(), dst.output[0].getPtr(), dst.output[0].getPtr() + N);
    }
    FadeIn in;
    for (std::size_t k = 0; k < left.size(); ++k) {
      CAPTURE(k);
      CHECK(left[k] ==
            doctest::Approx(Ramp(1200 + static_cast<long>(k % 300), 10000.f) * in.next()));
    }
  }

  TEST_CASE("~playlist: the host places the files it appends (#1158, #1255)") {
    TempFolder dir("yse_playlist_resolve_1158");
    WriteRamp(dir / "a.wav", 2000);
    HostResolver host;
    Rig r("", &host);
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);
    r.Message("append songs/a.wav");
    r.Clip(1);
    io->WaitIdle();
    CHECK(host.last().second == "songs/a.wav");
    // The 1 waits for the host.
    r.Block();
    CHECK(r.pl->Playlist().playing() == 0);
    CHECK(YSE::patcher::ResolveReply(host.last().first, (dir / "a.wav").c_str()));
    Drain();
    r.Block();
    CHECK(r.pl->Playlist().playing() == 1);
    const std::vector<filePlaylist::clip> c = r.pl->Playlist().clips();
    REQUIRE(c.size() == 1);
    CHECK(c[0].file == dir / "a.wav");
    CHECK(c[0].name == "a.wav");

    // A refusal fails like a missing file.
    r.Message("append nowhere.wav");
    io->WaitIdle();
    CHECK(YSE::patcher::ResolveReply(host.last().first, nullptr));
    r.Settle();
    CHECK(r.pl->FailedTasks() == 1);
    CHECK(r.pl->Playlist().count() == 1);
  }

  TEST_CASE("~playlist: a task in flight survives the object's deletion; no patcher, no task "
            "(#1158)") {
    TempFolder dir("yse_playlist_delete_1158");
    WriteRamp(dir / "a.wav", 2000);
    {
      Rig r("");
      r.Message("append " + (dir / "a.wav"));
      r.Message("selection 1 0.1 0.2");
      r.p.DeleteObject(r.handle);
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    dPlaylist standalone;
    standalone.GetInlet(0)->SetMessage("append " + (dir / "a.wav"), YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 1);
    CHECK(standalone.Playlist().count() == 0);
  }

  TEST_CASE("~playlist: words, numbers and blocks on the audio thread allocate nothing "
            "(#1158)") {
    TempFolder dir("yse_playlist_rt_1158");
    WriteRamp(dir / "a.wav", 3000);
    WriteRamp(dir / "b.wav", 300);
    Rig r("2", nullptr, false);
    r.Message("append " + (dir / "a.wav"));
    r.Message("append " + (dir / "b.wav"));
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string append = "append " + (dir / "b.wav") + " 1";
    const std::vector<std::string> words = {"next",
                                            "pause",
                                            "resume",
                                            "loop 1",
                                            "speed 1.5",
                                            "timestretch 1",
                                            "pitchshift 2",
                                            "pitchshiftcent 50",
                                            "quality good",
                                            "loopreport 1",
                                            "reportprogress 1",
                                            "getcontent",
                                            "selection 1 0.1 0.5",
                                            "selectionms 100 300",
                                            "selection 2",
                                            "setclip 1 speed 2",
                                            "setclip 1 quality best",
                                            "setclip 1 loop",
                                            "move 1 2",
                                            "remove 9",
                                            "2 3",
                                            "nonsense"};
    for (int round = 0; round < 3; ++round) {
      CAPTURE(round);
      {
        TestHelpers::ProbeScope probe;
        r.Message(append, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Clip(2, YSE::T_DSP);
        r.Clip(0, YSE::T_DSP);
        r.Clip(1, YSE::T_DSP);
        r.pl->GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      {
        // The blocks that deliver the tasks, take the new list and streams,
        // play, loop, report, answer getcontent, and ask for what they let
        // go to be freed.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 8; ++block)
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
