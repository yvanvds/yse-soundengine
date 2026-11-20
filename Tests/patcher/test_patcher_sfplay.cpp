// Tests for the ~sfplay and ~sflist patcher objects
// (patcher/sampling/dSfplay, dSflist), issue #1155. The streaming and the cue
// tables are DSP::fileStreamPlayer's and DSP::fileStreamCues', tested in
// Tests/dsp/test_file_stream_player.cpp. These show that pins, words and
// arguments reach them, in rendered patches heard through a ~dac: message
// boxes opening a file and playing it with the done bang counted, cues,
// lists, seek, pause and loop by word, an ~sflist shared by name and
// followed when it changes, a JSON round trip of the arguments and of the
// embedded files, the host's path resolver, a task outliving its object, and
// allocation-free messages and blocks on the audio thread.
//
// The fixtures are float WAV files at 1000 Hz and the engine runs at 1000 Hz,
// so a millisecond is a frame and a sample: sample n of channel c is
// n + 1 + 100000 c. A stream fades in over its first 200 samples, so a check
// on what is heard looks past them, or mirrors the fade.

#include <doctest/doctest.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/fileStreamPlayer.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dSflist.h"
#include "patcher/sampling/dSfplay.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::fileStreamCues;
using YSE::PATCHER::dSflist;
using YSE::PATCHER::dSfplay;
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

  float Ramp(long n, int c = 0) {
    return static_cast<float>(n + 1) + 100000.f * static_cast<float>(c);
  }

  void WriteRamp(const std::string& path, long frames, int channels = 1) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, channels, 1000);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames) * channels);
    for (long n = 0; n < frames; ++n)
      for (int c = 0; c < channels; ++c)
        samples[static_cast<std::size_t>(n) * channels + c] = Ramp(n, c);
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

  // The engine's fade-in gain, sample by sample.
  struct FadeIn {
    float v = 0.f;
    float next() {
      const float g = v;
      v += 0.005f;
      if (v >= 1.f) v = 1.f;
      return g;
    }
  };

  YSE::pHandle* FindType(patcherImplementation& p, const char* type) {
    for (unsigned int i = 0; i < p.Objects(); ++i) {
      YSE::pHandle* h = p.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == type) return h;
    }
    return nullptr;
  }

  // [~sfplay args] -> [~dac], its done outlet into a bang sink. The sink
  // comes first so it outlives the patcher (issue #967).
  struct Rig {
    TestHelpers::BangSink done;
    YSE::pHandle doneHandle{&done};
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dSfplay* sf = nullptr;

    explicit Rig(const std::string& args, YSE::pathResolver* resolver = nullptr) {
      if (resolver != nullptr) p.SetPathResolver(resolver, 0);
      handle = p.CreateObject(YSE::OBJ::D_SFPLAY, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      sf = static_cast<dSfplay*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, dac, 0);
      if (sf->Channels() > 1) p.Connect(handle, 1, dac, 1);
      const int doneOutlet = sf->Channels() + (sf->HasPosition() ? 1 : 0);
      p.Connect(handle, static_cast<unsigned int>(doneOutlet), &doneHandle, 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(0)->SetMessage(text, thread);
    }

    void Cue(int cue, YSE::THREAD thread = YSE::T_GUI) {
      sf->GetInlet(0)->SetInt(cue, thread);
    }

    // The tasks and fills land, then a block delivers them.
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

    // Blocks until the done bang, at most `most`; the samples heard.
    std::vector<float> UntilDone(int most) {
      std::vector<float> heard;
      const int before = done.bangCount;
      for (int b = 0; b < most && done.bangCount == before; ++b) {
        const std::vector<float> o = Block();
        heard.insert(heard.end(), o.begin(), o.end());
      }
      return heard;
    }
  };

  // [~sfplay args] alone, so a dump holds nothing from outside the patcher.
  struct Bare {
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* handle = nullptr;
    dSfplay* sf = nullptr;

    explicit Bare(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_SFPLAY, args);
      REQUIRE(handle != nullptr);
      sf = static_cast<dSfplay*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text) {
      sf->GetInlet(0)->SetMessage(text, YSE::T_GUI);
    }

    void Settle() {
      Drain();
      p.Calculate(YSE::T_DSP);
      Drain();
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

  TEST_CASE("~sfplay / ~sflist: registry and shape; a first argument that is a number is the "
            "channel count (#1155)") {
    int listed = 0;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_SFPLAY || name == YSE::OBJ::D_SFLIST) listed++;
    CHECK(listed == 2);
    patcherImplementation p(1, nullptr);
    struct shape {
      const char* args;
      int channels;
      bool position;
      unsigned int outlets;
    };
    for (const shape& s : {shape{"", 1, false, 2}, shape{"2", 2, false, 3},
                           shape{"3 0 1", 3, true, 5}, shape{"list_shape_1155 2 0 1", 2, true, 4},
                           shape{"list_shape_1155", 1, false, 2}, shape{"0", 1, false, 2}}) {
      CAPTURE(s.args);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SFPLAY, s.args);
      REQUIRE(h != nullptr);
      auto* sf = static_cast<dSfplay*>(p.GetObjectFromID(h->GetID()));
      CHECK(sf->Channels() == s.channels);
      CHECK(sf->HasPosition() == s.position);
      CHECK(h->GetOutputs() == static_cast<int>(s.outlets));
      CHECK(h->GetInputs() == 2);
      CHECK(sf->Player().channels() == static_cast<UInt>(s.channels));
    }
    YSE::pHandle* list = p.CreateObject(YSE::OBJ::D_SFLIST, "list_shape_1155");
    REQUIRE(list != nullptr);
    CHECK(list->GetOutputs() == 0);
    CHECK(list->GetInputs() == 1);
    auto* sl = static_cast<dSflist*>(p.GetObjectFromID(list->GetID()));
    CHECK(sl->Cues() == fileStreamCues::find("list_shape_1155"));
  }

  TEST_CASE("~sfplay: message boxes open a file and play it to the end; done bangs (#1155)") {
    // [open a.wav( and [1( -> [~sfplay] -> [~dac], sent together: the 1
    // waits for the file.
    TempFolder dir("yse_sfplay_box_1155");
    WriteRamp(dir / "a.wav", 600);
    TestHelpers::BangSink done;
    YSE::pHandle doneHandle(&done);
    ScopedSampleRate rate(1000);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* open = p.CreateObject(YSE::OBJ::G_MESSAGE, "open " + (dir / "a.wav"));
    YSE::pHandle* one = p.CreateObject(YSE::OBJ::G_MESSAGE, "1");
    YSE::pHandle* sf = p.CreateObject(YSE::OBJ::D_SFPLAY, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(open != nullptr);
    REQUIRE(one != nullptr);
    REQUIRE(sf != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(open, 0, sf, 0);
    p.Connect(one, 0, sf, 0);
    p.Connect(sf, 0, dac, 0);
    p.Connect(sf, 1, &doneHandle, 0);
    p.Calculate(YSE::T_DSP);

    open->SetBang(0);
    one->SetBang(0);
    std::vector<float> heard;
    for (int b = 0; b < 20 && done.bangCount == 0; ++b) {
      Drain();
      p.Calculate(YSE::T_DSP);
      const float* o = p.output[0].getPtr();
      if (heard.empty() && o[N - 1] == 0.f) continue; // the file not open yet
      heard.insert(heard.end(), o, o + N);
    }
    CHECK(done.bangCount == 1);
    REQUIRE(heard.size() >= 600);
    FadeIn in;
    for (std::size_t k = 0; k < 600; ++k) {
      CAPTURE(k);
      CHECK(heard[k] == doctest::Approx(Ramp(static_cast<long>(k)) * in.next()));
    }
    for (std::size_t k = 600; k < heard.size(); ++k)
      CHECK(heard[k] == 0.f);
    p.Calculate(YSE::T_DSP);
    CHECK(done.bangCount == 1);
  }

  TEST_CASE("~sfplay: preload, cue numbers, lists, seek, pause, loop and speed by word "
            "(#1155)") {
    TempFolder dir("yse_sfplay_words_1155");
    WriteRamp(dir / "a.wav", 3000);
    Rig r("");
    r.Message("open " + (dir / "a.wav"));
    r.Message("preload 2 1000 1500");
    r.Message("preload 3 " + (dir / "a.wav") + " 2000 2100 0 2");
    r.Message("preloadn 4 0.5 0.6");
    r.Settle();
    REQUIRE(r.sf->Player().hasCue(2));
    REQUIRE(r.sf->Player().hasCue(3));
    REQUIRE(r.sf->Player().hasCue(4));
    CHECK(r.sf->FailedTasks() == 0);

    SUBCASE("a cue number") {
      r.Cue(2);
      r.Block();
      r.Block();
      const std::vector<float> o = r.Block();
      CHECK(o[0] == Ramp(1000 + 2 * N));
      const std::vector<float> rest = r.UntilDone(10);
      CHECK(r.done.bangCount == 1);
    }
    SUBCASE("a cue at its own speed, and preloadn") {
      r.Cue(3);
      CHECK(r.UntilDone(5).size() <= 2 * N); // 100 ms at twice the speed
      CHECK(r.done.bangCount == 1);
      r.Cue(4);
      r.Block();
      r.Block();
      CHECK(r.Block()[0] == Ramp(1500 + 2 * N));
    }
    SUBCASE("a list") {
      r.Message("4 2");
      int dones = 0;
      std::vector<int> cues;
      for (int b = 0; b < 20; ++b) {
        r.Block();
        const int c = r.sf->Player().cue();
        if (c != 0 && (cues.empty() || cues.back() != c)) cues.push_back(c);
        dones = r.done.bangCount;
      }
      CHECK(cues == std::vector<int>{4, 2});
      CHECK(dones == 1);
    }
    SUBCASE("seek, pause and resume, 0") {
      r.Message("seek 1000 2900");
      r.Block();
      r.Block();
      r.Block();
      CHECK(r.sf->Player().cue() == 1);
      const float at = r.Block()[0];
      CHECK(at > 1000.f);
      CHECK(at < 2900.f);
      r.Message("pause");
      r.Block();
      r.Block();
      CHECK(r.Block()[5] == 0.f);
      CHECK(r.sf->Player().paused());
      r.Message("resume");
      r.Block();
      r.Block();
      CHECK(r.Block()[5] > at);
      r.Cue(0);
      r.Block();
      r.Block();
      CHECK(r.done.bangCount == 1);
      CHECK_FALSE(r.sf->Player().playing());
    }
    SUBCASE("loop, loopone and speed") {
      r.Message("loop 1");
      r.Message("speed 2");
      r.Cue(3);
      for (int b = 0; b < 10; ++b)
        r.Block();
      CHECK(r.done.bangCount == 0);
      CHECK(r.sf->Player().loop());
      CHECK(r.sf->Player().speed() == 2.f);
      const std::vector<float> o = r.Block();
      for (UInt k = 0; k < N; ++k) {
        // Cue 3 is frames 2000 to 2099 at 2 x 2 frames a sample.
        CHECK(o[k] >= Ramp(2000));
        CHECK(o[k] <= Ramp(2099));
      }
      r.Message("loop 0");
      r.Message("loopone 3");
      for (int b = 0; b < 4; ++b)
        r.Block();
      CHECK(r.done.bangCount == 0);
      r.Message("loopone 0");
      r.UntilDone(4);
      CHECK(r.done.bangCount == 1);
      // The speed inlet.
      r.sf->GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
      r.Block();
      CHECK(r.sf->Player().speed() == 0.5f);
    }
    SUBCASE("pitch words reach the class") {
      r.Message("pitchshift 2");
      r.Message("pitchshiftcent 100");
      r.Message("timestretch 1");
      r.Message("quality better");
      r.Message("modout 1");
      r.Block();
      CHECK(r.sf->Player().pitchShift() == 2.f);
      CHECK(r.sf->Player().pitchShiftCents() == 100.f);
      CHECK(r.sf->Player().timeStretch());
      CHECK(r.sf->Player().quality() == YSE::DSP::pitchShifter::BETTER);
      CHECK(r.sf->Player().modout());
    }
    SUBCASE("clear, fclose, and what fails") {
      r.Message("clear 3");
      r.Settle();
      CHECK_FALSE(r.sf->Player().hasCue(3));
      CHECK(r.sf->Player().hasCue(2));
      r.Message("fclose");
      r.Settle();
      CHECK_FALSE(r.sf->Player().hasCue(1));
      CHECK_FALSE(r.sf->Player().hasCue(2)); // a cue of the open file
      CHECK(r.sf->Player().hasCue(4) == false);
      r.Message("open " + (dir / "missing.wav"));
      r.Message("preload 5 " + (dir / "missing.wav"));
      r.Settle();
      CHECK(r.sf->FailedTasks() == 2);
      r.Message("open"); // a dialog in Max: nothing here
      r.Message("nonsense 3");
      r.Cue(7); // no such cue
      r.Settle();
      CHECK(r.sf->FailedTasks() == 2);
      CHECK_FALSE(r.sf->Player().playing());
    }
  }

  TEST_CASE("~sfplay plays the cues of the ~sflist it names, and follows its changes "
            "(#1155)") {
    TempFolder dir("yse_sfplay_sflist_1155");
    WriteRamp(dir / "a.wav", 3000);
    WriteRamp(dir / "b.wav", 3000, 2);
    Rig r("strings_1155 2");
    YSE::pHandle* lh = r.p.CreateObject(YSE::OBJ::D_SFLIST, "strings_1155");
    REQUIRE(lh != nullptr);
    auto* list = static_cast<dSflist*>(r.p.GetObjectFromID(lh->GetID()));
    CHECK(list->Cues() == r.sf->Player().cueList());

    lh->SetListData(0, "open " + (dir / "a.wav"));
    lh->SetListData(0, "preload 2 500");
    lh->SetListData(0, "preload 3 " + (dir / "b.wav") + " 1000");
    r.Settle(); // the list fills; the ~sfplay sees it changed
    r.Settle(); // and opens its streams
    CHECK(list->FailedTasks() == 0);
    REQUIRE(r.sf->Player().hasCue(2));
    REQUIRE(r.sf->Player().hasCue(3));

    r.Cue(3);
    r.Block();
    r.Block();
    std::vector<float> left = r.Block(0);
    std::vector<float> right(r.p.output[1].getPtr(), r.p.output[1].getPtr() + N);
    CHECK(left[0] == Ramp(1000 + 2 * N, 0));
    CHECK(right[0] == Ramp(1000 + 2 * N, 1));

    // A cue removed from the list falls silent in the ~sfplay.
    lh->SetListData(0, "clear 3");
    r.Settle();
    r.Settle();
    CHECK_FALSE(r.sf->Player().hasCue(3));
    CHECK_FALSE(r.sf->Player().playing());

    // set: back to cues of its own, then to the list again.
    r.Message("set");
    r.Settle();
    CHECK_FALSE(r.sf->Player().hasCue(2));
    r.Message("set strings_1155");
    r.Settle();
    CHECK(r.sf->Player().hasCue(2));

    // fclose on the list removes the open file's cues.
    lh->SetListData(0, "fclose");
    r.Settle();
    r.Settle();
    CHECK(list->Cues()->count() == 0);
    CHECK_FALSE(r.sf->Player().hasCue(2));
  }

  TEST_CASE("~sfplay / ~sflist: arguments survive a DumpJSON / ParseJSON round trip; embed "
            "saves the files and cues (#1155)") {
    TempFolder dir("yse_sfplay_json_1155");
    WriteRamp(dir / "a.wav", 3000);
    std::string dump;
    {
      Bare r("2 0 1");
      dump = r.p.DumpJSON();
      r.Message("open " + (dir / "a.wav"));
      r.Message("loop 1");
      r.Settle();
      CHECK(r.p.DumpJSON() == dump); // messages are not saved without embed
    }
    CHECK(dump.find("\"2 0 1\"") != std::string::npos);
    {
      patcherImplementation dst(2, nullptr);
      dst.ParseJSON(dump);
      CHECK(dst.DumpJSON() == dump);
      YSE::pHandle* h = FindType(dst, YSE::OBJ::D_SFPLAY);
      REQUIRE(h != nullptr);
      auto* sf = static_cast<dSfplay*>(dst.GetObjectFromID(h->GetID()));
      CHECK(sf->Channels() == 2);
      CHECK(sf->HasPosition());
      CHECK(sf->Player().file().empty());
    }

    // With embed: the open file and the object's own cues; the ~sflist its
    // cues.
    {
      Bare r("");
      YSE::pHandle* lh = r.p.CreateObject(YSE::OBJ::D_SFLIST, "embed_list_1155");
      REQUIRE(lh != nullptr);
      r.Message("open " + (dir / "a.wav"));
      r.Message("preload 2 100 200 0 2");
      r.Message("embed 1");
      lh->SetListData(0, "preload 5 " + (dir / "a.wav") + " 700");
      lh->SetListData(0, "embed 1");
      r.Settle();
      dump = r.p.DumpJSON();
    }
    REQUIRE(fileStreamCues::find("embed_list_1155") == nullptr);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    auto* sf =
        static_cast<dSfplay*>(dst.GetObjectFromID(FindType(dst, YSE::OBJ::D_SFPLAY)->GetID()));
    CHECK(sf->Player().file() == dir / "a.wav");
    const std::vector<fileStreamCues::cue> cues = sf->Player().cueList()->cues();
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].number == 2);
    CHECK(cues[0].startFrame == 100);
    CHECK(cues[0].endFrame == 200);
    CHECK(cues[0].speed == 2.f);
    std::shared_ptr<fileStreamCues> list = fileStreamCues::find("embed_list_1155");
    REQUIRE(list != nullptr);
    REQUIRE(list->count() == 1);
    CHECK(list->cues()[0].startFrame == 700);
  }

  TEST_CASE("~sfplay: in a rendered patch after a JSON round trip, the embedded file and cue "
            "play through ~dac (#1155)") {
    // [2( -> [~sfplay] -> [~dac], with a.wav and cue 2 embedded. After the
    // round trip nothing holds the file: the patch opens it itself.
    TempFolder dir("yse_sfplay_render_1155");
    WriteRamp(dir / "a.wav", 2000, 2);
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int cueID = 0;
    unsigned int oneID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* two = src.CreateObject(YSE::OBJ::G_MESSAGE, "2");
      YSE::pHandle* one = src.CreateObject(YSE::OBJ::G_MESSAGE, "1");
      YSE::pHandle* sf = src.CreateObject(YSE::OBJ::D_SFPLAY, "2");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(two != nullptr);
      REQUIRE(one != nullptr);
      REQUIRE(sf != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(two, 0, sf, 0);
      src.Connect(one, 0, sf, 0);
      src.Connect(sf, 0, dac, 0);
      src.Connect(sf, 1, dac, 1);
      sf->SetListData(0, "open " + (dir / "a.wav"));
      sf->SetListData(0, "preload 2 1200 1800");
      sf->SetListData(0, "embed 1");
      Drain();
      src.Calculate(YSE::T_DSP);
      cueID = two->GetID();
      oneID = one->GetID();
      dump = src.DumpJSON();
    }

    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* two = dst.GetHandleFromID(cueID);
    YSE::pHandle* one = dst.GetHandleFromID(oneID);
    REQUIRE(two != nullptr);
    REQUIRE(one != nullptr);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[5] == 0.f);

    two->SetBang(0);
    std::vector<float> left;
    std::vector<float> right;
    for (int b = 0; b < 8; ++b) {
      dst.Calculate(YSE::T_DSP);
      Drain();
      left.insert(left.end(), dst.output[0].getPtr(), dst.output[0].getPtr() + N);
      right.insert(right.end(), dst.output[1].getPtr(), dst.output[1].getPtr() + N);
    }
    FadeIn in;
    for (std::size_t k = 0; k < 600; ++k) {
      CAPTURE(k);
      const float g = in.next();
      CHECK(left[k] == doctest::Approx(Ramp(1200 + static_cast<long>(k), 0) * g));
      CHECK(right[k] == doctest::Approx(Ramp(1200 + static_cast<long>(k), 1) * g));
    }
    for (std::size_t k = 600; k < left.size(); ++k)
      CHECK(left[k] == 0.f);

    one->SetBang(0);
    for (int b = 0; b < 3; ++b)
      dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == Ramp(2 * N));
  }

  TEST_CASE("~sfplay: the host places the files it opens (#1155, #1255)") {
    TempFolder dir("yse_sfplay_resolve_1155");
    WriteRamp(dir / "a.wav", 2000);
    HostResolver host;
    Rig r("", &host);
    YSE::PATCHER::fileScheduler* io = r.p.FileIO();
    REQUIRE(io != nullptr);
    r.Message("open songs/a.wav");
    r.Cue(1);
    io->WaitIdle();
    CHECK(host.last().second == "songs/a.wav");
    // The 1 waits for the host.
    r.Block();
    CHECK_FALSE(r.sf->Player().playing());
    CHECK(YSE::patcher::ResolveReply(host.last().first, (dir / "a.wav").c_str()));
    Drain();
    r.Block();
    CHECK(r.sf->Player().playing());
    CHECK(r.sf->Player().file() == dir / "a.wav");

    r.Message("preload 2 songs/a.wav 1000");
    io->WaitIdle();
    CHECK(host.last().second == "songs/a.wav");
    CHECK(YSE::patcher::ResolveReply(host.last().first, (dir / "a.wav").c_str()));
    r.Settle();
    CHECK(r.sf->Player().hasCue(2));

    // A refusal fails like a missing file.
    r.Message("open nowhere.wav");
    io->WaitIdle();
    CHECK(YSE::patcher::ResolveReply(host.last().first, nullptr));
    r.Settle();
    CHECK(r.sf->FailedTasks() == 1);
    CHECK(r.sf->Player().file() == dir / "a.wav");
  }

  TEST_CASE("~sfplay: a task in flight survives the object's deletion; no patcher, no task "
            "(#1155)") {
    TempFolder dir("yse_sfplay_delete_1155");
    WriteRamp(dir / "a.wav", 2000);
    {
      Rig r("");
      r.Message("open " + (dir / "a.wav"));
      r.Message("preload 2 100");
      r.p.DeleteObject(r.handle);
      Drain();
      r.p.Calculate(YSE::T_DSP);
      Drain();
    }
    dSfplay standalone;
    standalone.GetInlet(0)->SetMessage("open " + (dir / "a.wav"), YSE::T_GUI);
    CHECK(standalone.FailedTasks() == 1);
    CHECK(standalone.Player().file().empty());
  }

  TEST_CASE("~sfplay: words, numbers and blocks on the audio thread allocate nothing "
            "(#1155)") {
    TempFolder dir("yse_sfplay_rt_1155");
    WriteRamp(dir / "a.wav", 3000, 2);
    Rig r("2 0 1");
    YSE::pHandle* lh = r.p.CreateObject(YSE::OBJ::D_SFLIST, "rt_list_1155");
    REQUIRE(lh != nullptr);
    r.Message("open " + (dir / "a.wav"));
    r.Message("preload 2 500 900");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string preload = "preload 3 " + (dir / "a.wav") + " 1000";
    const std::string listPreload = "preload 2 " + (dir / "a.wav");
    const std::vector<std::string> words = {
        "2 1",           "seek 100 300",   "pause",    "resume",       "loop 1",
        "loopone 2",     "speed 1.5",      "modout 1", "pitchshift 2", "pitchshiftcent 50",
        "timestretch 1", "quality good",   "embed 1",  "clear 3",      "set rt_list_1155",
        "set",           "fclose nothing", "nonsense"};
    for (int round = 0; round < 3; ++round) {
      CAPTURE(round);
      {
        TestHelpers::ProbeScope probe;
        r.Message(preload, YSE::T_DSP);
        lh->SetListData(0, listPreload); // a ~sflist message
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Cue(2, YSE::T_DSP);
        r.Cue(0, YSE::T_DSP);
        r.sf->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      Drain();
      {
        // The blocks that deliver the tasks, take the new streams, play them,
        // ask for the old ones to be freed and for a changed list to be
        // opened again, and flush the done bang.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 6; ++block)
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
