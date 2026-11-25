// Tests for YSE::DSP::filePlaylist (YseEngine/dsp/sampling/), issue #1158:
// a list of sound files, each a clip with a selection and settings of its
// own, played from disk one at a time through a fileStreamPlayer, behind
// ~playlist. The streaming itself is tested in test_file_stream_player.cpp.
//
// The fixtures are float WAV files, so what a stream reads back is exact.
// The engine runs at 1000 Hz and the files are at 1000 Hz unless a case says
// otherwise, so a millisecond is a frame and a sample. A clip fades in over
// 200 samples when it starts; Fade mirrors the engine's arithmetic for that.
// Drain() waits for the slow pool, which fills the streams.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/filePlaylist.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::filePlaylist;
using YSE::DSP::fileStreamPlayer;

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

  // Sample n of a ramp fixture: never 0; `offset` tells files apart.
  float Ramp(long n, float offset = 0.f) {
    return static_cast<float>(n + 1) + offset;
  }

  void WriteRamp(const std::string& path, long frames, float offset = 0.f, int rate = 1000) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, rate);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (long n = 0; n < frames; ++n)
      samples[static_cast<std::size_t>(n)] = Ramp(n, offset);
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  struct emptyJob : YSE::INTERNAL::threadPoolJob {
    void run() override {}
  };

  // Every disk fill asked for so far has landed: the slow pool has one
  // worker.
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

  std::vector<float> Out(filePlaylist& l) {
    const float* o = l.out(0).getPtr();
    return std::vector<float>(o, o + l.out(0).getLength());
  }

  std::vector<float> Head(filePlaylist& l) {
    const float* o = l.position().getPtr();
    return std::vector<float>(o, o + l.position().getLength());
  }

  bool Silent(const std::vector<float>& x) {
    for (float v : x)
      if (v != 0.f) return false;
    return true;
  }

  // What blocks report, block by block.
  struct heard {
    std::vector<float> samples;
    std::vector<Int> started;
    std::vector<Int> done;
    std::vector<Int> looped;
    int blocks = 0;
  };

  // `blocks` blocks (or until done when `untilDone`), draining between them.
  heard Run(filePlaylist& l, int blocks, bool untilDone = false) {
    heard h;
    for (int b = 0; b < blocks; ++b) {
      l();
      const std::vector<float> o = Out(l);
      h.samples.insert(h.samples.end(), o.begin(), o.end());
      h.started.push_back(l.started());
      h.done.push_back(l.done());
      h.looped.push_back(l.looped());
      h.blocks++;
      Drain();
      if (untilDone && l.done() != 0) break;
    }
    return h;
  }

  int Count(const std::vector<Int>& events, Int clip) {
    int n = 0;
    for (Int e : events)
      if (e == clip) n++;
    return n;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filePlaylist: append, move, remove and clear edit the list; selections and "
            "settings per clip (#1158)") {
    TempFolder dir("yse_fpl_list_1158");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 2000, 0.f, 2000); // a second at 2000 Hz
    WriteRamp(dir / "c.wav", 500);
    filePlaylist l;

    const std::uint64_t v0 = l.version();
    CHECK(l.append(dir / "a.wav") == 1);
    CHECK(l.append(dir / "b.wav") == 2);
    CHECK(l.append(dir / "c.wav", 1, "first") == 1); // at the front
    CHECK(l.append(dir / "missing.wav") == 0);
    CHECK(l.append("") == 0);
    CHECK(l.version() == v0 + 3);
    REQUIRE(l.count() == 3);

    std::vector<filePlaylist::clip> c = l.clips();
    REQUIRE(c.size() == 3);
    CHECK(c[0].name == "first");
    CHECK(c[1].name == "a.wav");
    CHECK(c[2].name == "b.wav");
    CHECK(c[1].file == dir / "a.wav");
    CHECK(c[0].lengthMs == doctest::Approx(500.f));
    CHECK(c[1].lengthMs == doctest::Approx(1000.f));
    CHECK(c[2].lengthMs == doctest::Approx(1000.f));
    CHECK(c[2].startMs == 0.f);
    CHECK(c[2].endMs == doctest::Approx(1000.f));
    CHECK_FALSE(c[2].selected);
    for (int k = 0; k < filePlaylist::SETTINGS; ++k)
      CHECK(std::isnan(c[1].settings[k]));

    // Moving: to the end, and back to the front.
    CHECK(l.move(1, 3));
    CHECK(l.clips()[2].name == "first");
    CHECK(l.move(3, 1));
    CHECK(l.clips()[0].name == "first");
    CHECK(l.clips()[1].name == "a.wav");
    CHECK_FALSE(l.move(0, 1));
    CHECK_FALSE(l.move(1, 4));

    // Selections: fractions, milliseconds, every clip, cleared.
    CHECK(l.selection(2, 0.25f, 0.5f));
    CHECK(l.selectionMs(3, 100.f, 600.f));
    c = l.clips();
    CHECK(c[1].selected);
    CHECK(c[1].startMs == doctest::Approx(250.f));
    CHECK(c[1].endMs == doctest::Approx(500.f));
    CHECK(c[2].startMs == doctest::Approx(100.f));
    CHECK(c[2].endMs == doctest::Approx(600.f));
    const std::uint64_t v1 = l.version();
    CHECK_FALSE(l.selection(4, 0.f, 1.f));
    CHECK_FALSE(l.selectionMs(1, 300.f, 200.f)); // holds nothing
    CHECK_FALSE(l.selection(1, std::numeric_limits<float>::quiet_NaN(), 1.f));
    CHECK(l.version() == v1);
    CHECK(l.selection(0, 0.5f, 0.f)); // every clip, to the end
    c = l.clips();
    CHECK(c[0].startMs == doctest::Approx(250.f));
    CHECK(c[0].endMs == doctest::Approx(500.f));
    CHECK(c[1].startMs == doctest::Approx(500.f));
    CHECK(c[2].startMs == doctest::Approx(500.f));
    CHECK(c[2].endMs == doctest::Approx(1000.f));
    CHECK(l.clearSelection(2));
    c = l.clips();
    CHECK_FALSE(c[1].selected);
    CHECK(c[1].startMs == 0.f);
    CHECK(c[1].endMs == doctest::Approx(1000.f));
    CHECK(c[2].selected);

    // Settings of a clip's own.
    CHECK(l.setting(2, filePlaylist::SPEED, 0.5f));
    CHECK(l.setting(2, filePlaylist::QUALITY, 2.7f));
    CHECK_FALSE(l.setting(2, filePlaylist::PITCHSHIFT, 0.f));
    CHECK_FALSE(l.setting(2, filePlaylist::QUALITY, 4.f));
    CHECK_FALSE(l.setting(2, filePlaylist::SPEED, INFINITY));
    CHECK_FALSE(l.setting(9, filePlaylist::LOOP, 1.f));
    CHECK(l.setting(0, filePlaylist::LOOP, 5.f)); // every clip, 0 or 1
    CHECK(l.setting(1, filePlaylist::LOOP, std::numeric_limits<float>::quiet_NaN()));
    c = l.clips();
    CHECK(c[1].settings[filePlaylist::SPEED] == 0.5f);
    CHECK(c[1].settings[filePlaylist::QUALITY] == 2.f);
    CHECK(c[1].settings[filePlaylist::LOOP] == 1.f);
    CHECK(c[2].settings[filePlaylist::LOOP] == 1.f);
    CHECK(std::isnan(c[0].settings[filePlaylist::LOOP]));

    // Removing and clearing.
    CHECK(l.remove(2));
    CHECK_FALSE(l.remove(3));
    c = l.clips();
    REQUIRE(c.size() == 2);
    CHECK(c[1].name == "b.wav");
    CHECK(l.clear() == 2);
    CHECK(l.count() == 0);
    CHECK(l.clear() == 0);
    CHECK_FALSE(l.selection(0, 0.f, 1.f));
    CHECK_FALSE(l.setting(0, filePlaylist::LOOP, 1.f));
    // A clip number freed is used again.
    CHECK(l.append(dir / "a.wav") == 1);
  }

  TEST_CASE("filePlaylist: a clip plays from its start, fading in, to its end; started, done and "
            "the playhead (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_play_1158");
    WriteRamp(dir / "a.wav", 600);
    WriteRamp(dir / "b.wav", 400, 10000.f);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.append(dir / "b.wav") == 2);
    l();
    CHECK(l.clipCount() == 2);
    REQUIRE(l.clipAt(2) != nullptr);
    CHECK(l.clipAt(2)->name == "b.wav");
    CHECK(l.clipAt(3) == nullptr);
    CHECK(l.playing() == 0);
    CHECK(Silent(Out(l)));
    CHECK(Silent(Head(l)));

    l.play(9); // not a clip
    l();
    CHECK(l.started() == 0);

    l.play(2);
    l();
    CHECK(l.started() == 2);
    CHECK(l.playing() == 2);
    const std::vector<float> h = Head(l);
    for (UInt k = 0; k < N; ++k)
      CHECK(h[k] == doctest::Approx(2.f + static_cast<float>(k) / 400.f));
    CHECK(l.where() == doctest::Approx(static_cast<float>(N - 1) / 400.f));
    std::vector<float> samples = Out(l);
    Drain();
    const heard rest = Run(l, 20, true);
    samples.insert(samples.end(), rest.samples.begin(), rest.samples.end());
    CHECK(Count(rest.started, 2) == 0);
    CHECK(rest.done.back() == 2);
    CHECK(rest.blocks + 1 == static_cast<int>(400 / N + 1)); // done in the block of the end
    FadeIn in;
    for (std::size_t k = 0; k < 400; ++k) {
      CAPTURE(k);
      CHECK(samples[k] == doctest::Approx(Ramp(static_cast<long>(k), 10000.f) * in.next()));
    }
    for (std::size_t k = 400; k < samples.size(); ++k)
      CHECK(samples[k] == 0.f);
    CHECK(l.playing() == 0);
    l();
    CHECK(l.done() == 0);
    CHECK(Silent(Head(l)));
  }

  TEST_CASE("filePlaylist: a selection plays its span; next plays the clip after, the first "
            "after the last (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_next_1158");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 300, 10000.f);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.append(dir / "b.wav") == 2);
    REQUIRE(l.selectionMs(1, 100.f, 300.f));

    // Nothing played yet: the first.
    l.next();
    heard h = Run(l, 10, true);
    CHECK(h.started.front() == 1);
    CHECK(h.done.back() == 1);
    REQUIRE(h.samples.size() >= 200);
    FadeIn in;
    for (std::size_t k = 0; k < 200; ++k)
      CHECK(h.samples[k] == doctest::Approx(Ramp(100 + static_cast<long>(k)) * in.next()));
    for (std::size_t k = 200; k < h.samples.size(); ++k)
      CHECK(h.samples[k] == 0.f);

    l.next();
    h = Run(l, 10, true);
    CHECK(h.started.front() == 2);
    CHECK(h.done.back() == 2);
    l.next(); // past the last: the first
    h = Run(l, 10, true);
    CHECK(h.started.front() == 1);

    // next while a clip plays: the one after it, at once.
    l.play(1);
    l();
    l.next();
    l();
    CHECK(l.started() == 2);
    CHECK(l.playing() == 2);
  }

  TEST_CASE("filePlaylist: loop, a clip's own loop, and looped() once per loop, forward and "
            "backward (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_loop_1158");
    WriteRamp(dir / "a.wav", 1000);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.selectionMs(1, 100.f, 400.f)); // 300 samples

    auto loops = [&]() {
      l.play(1);
      const heard h = Run(l, 20);
      FadeIn in;
      for (std::size_t n = 0; n < h.samples.size(); ++n) {
        CAPTURE(n);
        CHECK(h.samples[n] == doctest::Approx(Ramp(100 + static_cast<long>(n % 300)) * in.next()));
      }
      CHECK(Count(h.done, 1) == 0);
      // The loop is reported in the block that reaches the selection's end.
      for (int b = 0; b < h.blocks; ++b) {
        CAPTURE(b);
        const long before = static_cast<long>(N) * b / 300;
        const long after = static_cast<long>(N) * (b + 1) / 300;
        CHECK(h.looped[static_cast<std::size_t>(b)] == (after > before ? 1 : 0));
      }
    };
    SUBCASE("the playlist loops") {
      l.loop(true);
      loops();
    }
    SUBCASE("the clip's own loop") {
      REQUIRE(l.setting(1, filePlaylist::LOOP, 1.f));
      loops();
    }
    SUBCASE("the clip's own loop off wins over the playlist's") {
      l.loop(true);
      REQUIRE(l.setting(1, filePlaylist::LOOP, 0.f));
      l.play(1);
      const heard h = Run(l, 20, true);
      CHECK(h.done.back() == 1);
      CHECK(Count(h.looped, 1) == 0);
    }
    SUBCASE("backward") {
      l.loop(true).speed(-1.f);
      l.play(1);
      // Silent while the selection's end is read; then from its end down,
      // a loop each time the playhead jumps back up.
      int jumps = 0;
      int reported = 0;
      float last = 0.f;
      for (int b = 0; b < 40; ++b) {
        l();
        reported += static_cast<int>(l.looped() != 0);
        // While it waits for the disk it is silent, its playhead not yet
        // moving with it.
        if (Silent(Out(l))) {
          last = 0.f;
        } else {
          for (float v : Head(l)) {
            if (last != 0.f && v > last) jumps++;
            last = v;
          }
        }
        Drain();
      }
      CHECK(jumps > 10);
      CHECK(reported == jumps);
    }
  }

  TEST_CASE("filePlaylist: a clip's own speed, the playlist's for the others, at the live "
            "sample rate (#1158)") {
    TempFolder dir("yse_fpl_speed_1158");
    WriteRamp(dir / "a.wav", 2000);
    ScopedSampleRate rate(1000);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.append(dir / "a.wav") == 2);
    REQUIRE(l.setting(2, filePlaylist::SPEED, 2.f));
    auto third = [&](Int clip) {
      l.play(clip);
      for (int b = 0; b < 3; ++b) {
        l();
        Drain();
      }
      return Out(l); // past the fade: 256 > 200
    };
    SUBCASE("the clip's own") {
      l.speed(0.5f); // not the clip's
      const std::vector<float> o = third(2);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>(2 * (2 * N + k))));
    }
    SUBCASE("the playlist's") {
      l.speed(0.5f);
      const std::vector<float> o = third(1);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>((2 * N + k) / 2)));
    }
    SUBCASE("the engine rate doubles") {
      YSE::SAMPLERATE = 2000;
      const std::vector<float> o = third(1);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>((2 * N + k) / 2)));
    }
    SUBCASE("the clip follows the playlist again") {
      REQUIRE(l.setting(2, filePlaylist::SPEED, std::numeric_limits<float>::quiet_NaN()));
      const std::vector<float> o = third(2);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>(2 * N + k)));
    }
  }

  TEST_CASE("filePlaylist: the list changes while a clip plays (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_edit_1158");
    WriteRamp(dir / "a.wav", 3000);
    WriteRamp(dir / "b.wav", 3000, 10000.f);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.append(dir / "b.wav") == 2);
    l.play(2);
    for (int b = 0; b < 3; ++b) {
      l();
      Drain();
    }
    const long played = 3 * static_cast<long>(N);

    // The clip goes on where it was, past the fade.
    auto goesOn = [&]() {
      l();
      const std::vector<float> o = Out(l);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(played + static_cast<long>(k), 10000.f));
      CHECK(l.done() == 0);
      CHECK(l.started() == 0);
    };
    SUBCASE("moved: it plays on under its new number") {
      REQUIRE(l.move(2, 1));
      goesOn();
      CHECK(l.playing() == 1);
      CHECK(Head(l)[0] == doctest::Approx(1.f + static_cast<float>(played) / 3000.f));
    }
    SUBCASE("another clip removed") {
      REQUIRE(l.remove(1));
      goesOn();
      CHECK(l.playing() == 1);
    }
    SUBCASE("another clip appended, and its selection changed") {
      REQUIRE(l.append(dir / "a.wav", 1) == 1);
      REQUIRE(l.selectionMs(1, 10.f, 20.f));
      goesOn();
      CHECK(l.playing() == 3);
    }
    SUBCASE("removed: silent at once, without done") {
      REQUIRE(l.remove(2));
      l();
      CHECK(Silent(Out(l)));
      CHECK(l.done() == 0);
      CHECK(l.playing() == 0);
      l();
      CHECK(l.started() == 0);
    }
    SUBCASE("cleared") {
      CHECK(l.clear() == 2);
      l();
      CHECK(Silent(Out(l)));
      CHECK(l.playing() == 0);
    }
    SUBCASE("its selection changed: it plays again from the new start") {
      REQUIRE(l.selectionMs(2, 1000.f, 2000.f));
      const heard h = Run(l, 8); // before it reaches the new end
      CHECK(Count(h.started, 2) == 1);
      CHECK(Count(h.done, 2) == 0);
      std::size_t from = 0;
      while (from < h.samples.size() && h.samples[from] == 0.f)
        from++;
      from = from / N * N; // the block it started in, fading in from 0
      REQUIRE(h.samples.size() - from >= 600);
      FadeIn in;
      for (std::size_t k = 0; k < 600; ++k) {
        CAPTURE(k);
        CHECK(h.samples[from + k] ==
              doctest::Approx(Ramp(1000 + static_cast<long>(k), 10000.f) * in.next()));
      }
    }
  }

  TEST_CASE("filePlaylist: pause, resume and stop (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_pause_1158");
    WriteRamp(dir / "a.wav", 3000);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    l.play(1);
    Run(l, 3);
    l.pause();
    heard h = Run(l, 4);
    CHECK(Count(h.done, 1) == 0);
    CHECK(l.paused());
    CHECK(l.playing() == 1);
    CHECK(Silent(Out(l)));
    const float held = l.where();
    l();
    CHECK(l.where() == held);
    l.resume();
    l();
    CHECK_FALSE(l.paused());
    CHECK(l.where() > held);
    l(); // back to full volume
    l();
    l.stop();
    h = Run(l, 4, true);
    CHECK(h.done.back() == 1);
    CHECK(h.blocks == 2); // the 200-sample fade
    CHECK(l.playing() == 0);
  }

  TEST_CASE("filePlaylist: progress through the selection every 50 ms of play (#1158)") {
    TempFolder dir("yse_fpl_progress_1158");
    WriteRamp(dir / "a.wav", 3000);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.selectionMs(1, 100.f, 2100.f));

    SUBCASE("off") {
      ScopedSampleRate rate(1000);
      l.play(1);
      for (int b = 0; b < 5; ++b) {
        l();
        CHECK_FALSE(l.progressDue());
      }
    }
    SUBCASE("at 1000 Hz, every block: 50 ms is less than one") {
      ScopedSampleRate rate(1000);
      l.reportProgress(true);
      l.play(1);
      for (int b = 0; b < 8; ++b) {
        CAPTURE(b);
        l();
        CHECK(l.progressDue());
        CHECK(l.progress() ==
              doctest::Approx(static_cast<float>(N * b + N - 1) / 2000.f).epsilon(1e-4));
        Drain();
      }
      l.pause();
      for (int b = 0; b < 4; ++b)
        l();
      CHECK_FALSE(l.progressDue());
    }
    SUBCASE("at 44100 Hz, every 2205 samples from the first block") {
      ScopedSampleRate rate(44100);
      l.reportProgress(true);
      l.play(1);
      double wait = 0.0;
      for (int b = 0; b < 60; ++b) {
        CAPTURE(b);
        l();
        wait -= N;
        bool due = false;
        if (wait <= 0.0) {
          due = true;
          wait += 2205.0;
        }
        CHECK(l.progressDue() == due);
        Drain();
      }
    }
  }

  TEST_CASE("filePlaylist: blocks, transport and settings allocate nothing, take no lock, read "
            "no disk (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_rt_1158");
    WriteRamp(dir / "a.wav", 3000);
    WriteRamp(dir / "b.wav", 500);
    filePlaylist l(2);
    REQUIRE(l.append(dir / "a.wav") == 1);
    REQUIRE(l.append(dir / "b.wav") == 2);
    REQUIRE(l.selectionMs(1, 200.f, 900.f));
    REQUIRE(l.setting(2, filePlaylist::SPEED, 1.5f));
    l();
    l.pitchShift(1.5f);
    l();
    l.pitchShift(1.f);
    {
      TestHelpers::ProbeScope probe;
      l.reportProgress(true);
      l.play(1);
      for (int b = 0; b < 4; ++b)
        l();
      l.next();
      l();
      l.pause();
      l();
      l.resume();
      l();
      l.loop(true).speed(-1.f).pitchShift(2.f).timeStretch(true).quality(
          YSE::DSP::pitchShifter::GOOD);
      l.play(1);
      for (int b = 0; b < 6; ++b)
        l();
      CHECK(l.clipAt(1) != nullptr);
      CHECK(l.clipCount() == 2);
      l.stop();
      for (int b = 0; b < 4; ++b)
        l();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("filePlaylist: plays while another thread edits the list (#1158)") {
    // Every sample heard is one of the file's, or silence.
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fpl_threads_1158");
    WriteRamp(dir / "a.wav", 3000);
    filePlaylist l(1);
    REQUIRE(l.append(dir / "a.wav") == 1);
    l.loop(true);
    std::atomic<bool> run{true};
    std::thread editor([&] {
      int i = 0;
      while (run.load()) {
        if (l.count() > 3) l.remove(1);
        l.append(dir / "a.wav", i % 3);
        l.selectionMs(static_cast<Int>(1 + i % 2), static_cast<float>(i % 7) * 100.f, 2500.f);
        l.move(1, static_cast<Int>(l.count()));
        l.setting(0, filePlaylist::SPEED, i % 2 == 0 ? 1.f : 0.5f);
        l.collect();
        (void)l.clips();
        i++;
      }
    });
    bool bad = false;
    for (int b = 0; b < 600; ++b) {
      if (b % 20 == 0) l.play(1 + (b / 20) % 3);
      if (b % 50 == 0) l.next();
      l();
      const float* o = l.out(0).getPtr();
      for (UInt k = 0; k < N; ++k)
        if (o[k] < 0.f || o[k] > 3000.f) bad = true;
      const float* h = l.position().getPtr();
      for (UInt k = 0; k < N; ++k)
        if (h[k] < 0.f || h[k] >= static_cast<float>(filePlaylist::MAX_CLIPS + 1)) bad = true;
    }
    run.store(false);
    editor.join();
    CHECK_FALSE(bad);
  }

  TEST_CASE("fileStreamPlayer: looped() counts the loops of the cue that plays (#1158)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_looped_1158");
    WriteRamp(dir / "a.wav", 1000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 100.f, 150.f)); // 50 samples: two or three loops a block
    p.loop(true);
    p.play(2);
    long total = 0;
    for (int b = 0; b < 10; ++b) {
      p();
      Drain();
      CHECK(p.looped() >= 2);
      CHECK(p.looped() <= 3);
      total += p.looped();
    }
    CHECK(total == static_cast<long>(10 * N) / 50);
    p.loop(false);
    p();
    p();
    CHECK(p.looped() == 0);
  }
}
