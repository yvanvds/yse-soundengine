// Tests for YSE::DSP::fileStreamPlayer and YSE::DSP::fileStreamCues
// (YseEngine/dsp/sampling/), issue #1155: sound files streamed from disk
// through the engine's own streaming sound file, with cues, behind ~sfplay
// and ~sflist.
//
// The fixtures are float WAV files, so what a stream reads back is exact.
// The engine runs at 1000 Hz and the files are at 1000 Hz unless a case says
// otherwise, so a millisecond is a frame and a sample. A stream fades in over
// 200 samples when it starts and out over 200 when it stops; Fade mirrors the
// engine's arithmetic for that, so the reference output is exact too.
//
// A stream's disk fills run on the slow pool. Drain() runs an empty job
// there and waits for it: the pool has one worker, so every fill asked for
// before it has landed.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/fileStreamPlayer.hpp"
#include "headers/constants.hpp"
#include "internal/global.h"
#include "internal/threadPool.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::STREAM_BUFFERSIZE;
using YSE::DSP::fileStreamCues;
using YSE::DSP::fileStreamPlayer;
using YSE::DSP::pitchShifter;

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

  // Sample n of channel c in the ramp fixtures: never 0.
  float Ramp(long n, int c = 0) {
    return static_cast<float>(n + 1) + 100000.f * static_cast<float>(c);
  }

  // A float WAV of `frames` frames: channel c's sample n is Ramp(n, c).
  void WriteRamp(const std::string& path, long frames, int channels = 1, int rate = 1000) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, channels, rate);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames) * channels);
    for (long n = 0; n < frames; ++n)
      for (int c = 0; c < channels; ++c)
        samples[static_cast<std::size_t>(n) * channels + c] = Ramp(n, c);
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  // A float WAV sine of `hz` at `rate`, amplitude 0.5.
  void WriteSine(const std::string& path, long frames, float hz, int rate) {
    SndfileHandle h(path.c_str(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, rate);
    REQUIRE(h);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (long n = 0; n < frames; ++n)
      samples[static_cast<std::size_t>(n)] = static_cast<float>(
          0.5 * std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(n) / rate));
    REQUIRE(h.writef(samples.data(), frames) == frames);
  }

  struct emptyJob : YSE::INTERNAL::threadPoolJob {
    void run() override {}
  };

  // Every disk fill asked for so far has landed.
  void Drain() {
    emptyJob job;
    YSE::INTERNAL::Global().addSlowJob(&job);
    job.join();
  }

  // The engine's fade: the gain each sample of a start (up) or a stop (down)
  // is played at, with the same float arithmetic.
  struct Fade {
    float v;
    bool up;
    explicit Fade(bool rising) : v(rising ? 0.f : 1.f), up(rising) {}
    float next() {
      const float g = v;
      if (up) {
        v += 0.005f;
        if (v >= 1.f) v = 1.f;
      } else if (v > 0.f) {
        v -= 0.005f;
        if (v <= 0.f) v = 0.f;
      }
      return g;
    }
  };

  // Output `channel` of the last block.
  std::vector<float> Block(fileStreamPlayer& p, UInt channel = 0) {
    const float* o = p.out(channel).getPtr();
    return std::vector<float>(o, o + p.out(channel).getLength());
  }

  // Blocks until the player is done, draining between them; the samples of
  // output 0, and how many blocks it took (0 when it never finished).
  std::vector<float> PlayToDone(fileStreamPlayer& p, int maxBlocks, int* blocks = nullptr) {
    std::vector<float> all;
    for (int b = 0; b < maxBlocks; ++b) {
      p();
      const std::vector<float> o = Block(p);
      all.insert(all.end(), o.begin(), o.end());
      Drain();
      if (p.done()) {
        if (blocks != nullptr) *blocks = b + 1;
        return all;
      }
    }
    if (blocks != nullptr) *blocks = 0;
    return all;
  }

  // Positive-going zero crossings of `x` from `from`, per sample.
  double CrossingRate(const std::vector<float>& x, std::size_t from) {
    int count = 0;
    for (std::size_t i = from + 1; i < x.size(); ++i)
      if (x[i - 1] < 0.f && x[i] >= 0.f) count++;
    return static_cast<double>(count) / static_cast<double>(x.size() - from);
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── fileStreamCues ───────────────────────────────────────────────────────

  TEST_CASE("fileStreamCues: shared by name, cues by number, spans held inside the file "
            "(#1155)") {
    TempFolder dir("yse_fsp_cues_1155");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 2000, 1, 2000);

    std::shared_ptr<fileStreamCues> a = fileStreamCues::acquire("cues_1155");
    CHECK(fileStreamCues::acquire("cues_1155") == a);
    CHECK(fileStreamCues::find("cues_1155") == a);
    CHECK(fileStreamCues::acquire("") != fileStreamCues::acquire(""));
    CHECK(fileStreamCues::find("") == nullptr);

    const std::uint64_t v0 = a->version();
    CHECK(a->preload(3, dir / "a.wav", 100.f, 300.f));
    CHECK(a->preload(2, dir / "b.wav", 250.f, 0.f, 2.f)); // to the end, at 2000 Hz
    CHECK(a->version() > v0);
    // Normalized: a quarter to a half of a.wav.
    CHECK(a->preload(4, dir / "a.wav", 0.25f, 0.5f, 1.f, true));
    // Held inside the file.
    CHECK(a->preload(5, dir / "a.wav", 900.f, 5000.f));

    // Refused, and nothing changes.
    const std::uint64_t v1 = a->version();
    CHECK_FALSE(a->preload(1, dir / "a.wav")); // cue 1 is the player's open file
    CHECK_FALSE(a->preload(32768, dir / "a.wav"));
    CHECK_FALSE(a->preload(6, dir / "missing.wav"));
    CHECK_FALSE(a->preload(6, dir / "a.wav", 300.f, 200.f)); // holds nothing
    CHECK_FALSE(a->preload(6, dir / "a.wav", 0.f, 0.f, 0.f)); // speed
    CHECK_FALSE(a->preload(6, "")); // no file, and no open file
    CHECK(a->version() == v1);

    const std::vector<fileStreamCues::cue> cues = a->cues();
    REQUIRE(cues.size() == 4);
    CHECK(cues[0].number == 2);
    CHECK(cues[0].startFrame == 500);
    CHECK(cues[0].endFrame == 2000);
    CHECK(cues[0].startMs == doctest::Approx(250.f));
    CHECK(cues[0].endMs == doctest::Approx(1000.f));
    CHECK(cues[0].speed == 2.f);
    CHECK(cues[1].number == 3);
    CHECK(cues[1].startFrame == 100);
    CHECK(cues[1].endFrame == 300);
    CHECK(cues[2].number == 4);
    CHECK(cues[2].startFrame == 250);
    CHECK(cues[2].endFrame == 500);
    CHECK(cues[3].number == 5);
    CHECK(cues[3].endFrame == 1000);

    // The open file is what a preload without a file uses; closing a file
    // removes its cues and forgets it.
    CHECK(a->open(dir / "b.wav"));
    CHECK_FALSE(a->open(dir / "missing.wav"));
    CHECK(a->file() == dir / "b.wav");
    CHECK(a->preload(7, ""));
    CHECK(a->cues().back().file == dir / "b.wav");
    CHECK(a->close(dir / "a.wav") == 3);
    CHECK(a->count() == 2);
    CHECK(a->remove(7));
    CHECK_FALSE(a->remove(7));
    CHECK(a->close(dir / "b.wav") == 1);
    CHECK(a->file().empty());
    CHECK(a->preload(2, dir / "a.wav"));
    CHECK(a->clear() == 1);
    CHECK(a->count() == 0);
  }

  // ─── playing ──────────────────────────────────────────────────────────────

  TEST_CASE("fileStreamPlayer: the open file plays from its start, fading in, to its end, "
            "and done (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_open_1155");
    WriteRamp(dir / "a.wav", 1000);
    fileStreamPlayer p;
    CHECK_FALSE(p.open(dir / "missing.wav"));
    REQUIRE(p.open(dir / "a.wav"));
    CHECK(p.file() == dir / "a.wav");

    // Nothing before a play.
    p();
    CHECK_FALSE(p.hasCue(2));
    CHECK(p.hasCue(1));
    CHECK_FALSE(p.playing());
    for (float s : Block(p))
      CHECK(s == 0.f);

    p.play(1);
    int blocks = 0;
    const std::vector<float> heard = PlayToDone(p, 40, &blocks);
    REQUIRE(blocks > 0);
    REQUIRE(heard.size() >= 1000);
    Fade in(true);
    for (std::size_t k = 0; k < 1000; ++k) {
      CAPTURE(k);
      CHECK(heard[k] == doctest::Approx(Ramp(static_cast<long>(k)) * in.next()));
    }
    for (std::size_t k = 1000; k < heard.size(); ++k)
      CHECK(heard[k] == 0.f);
    // Done in the block that held the end.
    CHECK(blocks == static_cast<int>(1000 / N + 1));
    CHECK_FALSE(p.playing());
    CHECK(p.cue() == 0);
    p();
    CHECK_FALSE(p.done());

    // It plays again from the start.
    p.play(1);
    p();
    Fade again(true);
    const std::vector<float> first = Block(p);
    for (UInt k = 0; k < N; ++k)
      CHECK(first[k] == doctest::Approx(Ramp(k) * again.next()));
  }

  TEST_CASE("fileStreamPlayer: a file longer than the stream buffers plays without a gap "
            "(#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_long_1155");
    const long frames = 2 * static_cast<long>(STREAM_BUFFERSIZE) + 5000;
    WriteRamp(dir / "long.wav", frames, 2);
    fileStreamPlayer p(2);
    REQUIRE(p.open(dir / "long.wav"));
    p.play(1);
    long n = 0;
    Fade in(true);
    bool ended = false;
    for (int b = 0; b < 2000 && !ended; ++b) {
      p();
      const std::vector<float> l = Block(p, 0);
      const std::vector<float> r = Block(p, 1);
      for (UInt k = 0; k < N && n < frames; ++k, ++n) {
        const float g = in.next();
        if (l[k] != doctest::Approx(Ramp(n) * g) || r[k] != doctest::Approx(Ramp(n, 1) * g)) {
          FAIL("sample ", n, " is ", l[k], " / ", r[k]);
        }
      }
      ended = p.done();
      Drain();
    }
    CHECK(ended);
    CHECK(n == frames);
  }

  TEST_CASE("fileStreamPlayer: cues play their span at their speed; a list plays them in "
            "turn (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_cue_1155");
    WriteRamp(dir / "a.wav", 1000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 100.f, 400.f)); // the open file
    REQUIRE(p.preload(3, dir / "a.wav", 500.f, 900.f, 2.f));
    CHECK_FALSE(p.preload(4, dir / "missing.wav"));
    p();
    CHECK(p.hasCue(2));
    CHECK(p.hasCue(3));
    CHECK_FALSE(p.hasCue(4));

    SUBCASE("a span") {
      p.play(2);
      const std::vector<float> heard = PlayToDone(p, 20);
      REQUIRE(heard.size() >= 300);
      Fade in(true);
      for (std::size_t k = 0; k < 300; ++k)
        CHECK(heard[k] == doctest::Approx(Ramp(100 + static_cast<long>(k)) * in.next()));
      for (std::size_t k = 300; k < heard.size(); ++k)
        CHECK(heard[k] == 0.f);
    }
    SUBCASE("twice as fast") {
      p.play(3);
      const std::vector<float> heard = PlayToDone(p, 20);
      REQUIRE(heard.size() >= 200);
      Fade in(true);
      for (std::size_t k = 0; k < 200; ++k)
        CHECK(heard[k] == doctest::Approx(Ramp(500 + 2 * static_cast<long>(k)) * in.next()));
      for (std::size_t k = 200; k < heard.size(); ++k)
        CHECK(heard[k] == 0.f);
    }
    SUBCASE("a list, skipping a cue it has no stream for") {
      const Int list[] = {4, 3, 9, 2};
      p.play(list, 4);
      int dones = 0;
      std::vector<Int> cues;
      for (int b = 0; b < 30; ++b) {
        p();
        if (p.cue() != 0 && (cues.empty() || cues.back() != p.cue())) cues.push_back(p.cue());
        if (p.done()) dones++;
        Drain();
      }
      CHECK(cues == std::vector<Int>{3, 2});
      CHECK(dones == 1);
      CHECK_FALSE(p.playing());
    }
    SUBCASE("an unknown cue is ignored") {
      p.play(2);
      p();
      p.play(9);
      p();
      CHECK(p.cue() == 2);
    }
  }

  TEST_CASE("fileStreamPlayer: follows the live sample rate and the file's own rate (#1155)") {
    TempFolder dir("yse_fsp_rate_1155");
    WriteRamp(dir / "k1.wav", 1000, 1, 1000);
    WriteRamp(dir / "k2.wav", 2000, 1, 2000);
    ScopedSampleRate rate(1000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "k1.wav"));
    REQUIRE(p.preload(2, dir / "k2.wav"));

    SUBCASE("the engine rate doubles after the file is opened") {
      YSE::SAMPLERATE = 2000;
      p.play(1);
      for (int b = 0; b < 3; ++b)
        p();
      const std::vector<float> o = Block(p);
      // Each frame twice; the third block is past the fade (256 > 200).
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>((2 * N + k) / 2)));
    }
    SUBCASE("a 2000 Hz file at 1000 Hz plays two frames a sample") {
      p.play(2);
      for (int b = 0; b < 3; ++b)
        p();
      const std::vector<float> o = Block(p);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>(2 * (2 * N + k))));
    }
    SUBCASE("speed") {
      p.speed(0.5f);
      p.play(1);
      for (int b = 0; b < 3; ++b)
        p();
      const std::vector<float> o = Block(p);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == Ramp(static_cast<long>((2 * N + k) / 2)));
      p.speed(-1.f);
      CHECK(p.speed() == 0.f);
      p.speed(NAN);
      CHECK(p.speed() == 0.f);
    }
  }

  TEST_CASE("fileStreamPlayer: loop, loopone, and a loop of a span without a gap (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_loop_1155");
    WriteRamp(dir / "a.wav", 1000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 100.f, 300.f));
    REQUIRE(p.preload(3, "", 600.f, 700.f));
    p();

    auto loops = [&](Int cue, long first, long span) {
      p.play(cue);
      Fade in(true);
      long n = 0;
      for (int b = 0; b < 20; ++b) {
        p();
        CHECK_FALSE(p.done());
        const std::vector<float> o = Block(p);
        for (UInt k = 0; k < N; ++k, ++n) {
          CAPTURE(n);
          CHECK(o[k] == doctest::Approx(Ramp(first + n % span) * in.next()));
        }
        Drain();
      }
    };
    SUBCASE("loop") {
      p.loop(true);
      loops(2, 100, 200);
    }
    SUBCASE("loopone, the cue it names") {
      p.loopOne(3);
      loops(3, 600, 100);
    }
    SUBCASE("loopone, another cue ends") {
      p.loopOne(3);
      p.play(2);
      CHECK(PlayToDone(p, 20).size() > 0);
      CHECK_FALSE(p.playing());
    }
    SUBCASE("the whole file") {
      p.loop(true);
      loops(1, 0, 1000);
    }
  }

  TEST_CASE("fileStreamPlayer: stop and pause fade out; resume goes on; a stop is done "
            "(#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_stop_1155");
    WriteRamp(dir / "a.wav", 5000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    p.play(1);
    for (int b = 0; b < 3; ++b) {
      p();
      Drain();
    }
    const long at = 3 * static_cast<long>(N);

    SUBCASE("pause and resume") {
      p.pause();
      p();
      CHECK(p.paused());
      Fade out(false);
      std::vector<float> o = Block(p);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == doctest::Approx(Ramp(at + k) * out.next()));
      p();
      o = Block(p);
      long n = at + N;
      for (UInt k = 0; k < N; ++k) {
        const float g = out.next();
        // Past the fade the stream holds where it is.
        if (g > 0.f) {
          CHECK(o[k] == doctest::Approx(Ramp(n++) * g));
        } else {
          CHECK(o[k] == 0.f);
        }
      }
      CHECK_FALSE(p.done());
      p();
      CHECK(Block(p)[N - 1] == 0.f);
      p.resume();
      p();
      CHECK_FALSE(p.paused());
      Fade in(true);
      o = Block(p);
      for (UInt k = 0; k < N; ++k)
        CHECK(o[k] == doctest::Approx(Ramp(n + k) * in.next()));
    }
    SUBCASE("stop") {
      p.stop();
      p();
      CHECK_FALSE(p.done());
      p();
      CHECK(p.done());
      CHECK_FALSE(p.playing());
      p();
      CHECK_FALSE(p.done());
      // And it plays from the start again.
      Drain();
      p.play(1);
      p();
      CHECK(Block(p)[1] == doctest::Approx(Ramp(1) * 0.005f));
    }
    SUBCASE("stop while paused is done at once") {
      p.pause();
      p();
      p();
      p.stop();
      p();
      CHECK(p.done());
    }
  }

  TEST_CASE("fileStreamPlayer: switching cues fades the old one out as the new one fades in "
            "(#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_switch_1155");
    WriteRamp(dir / "a.wav", 5000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 3000.f));
    p.play(1);
    for (int b = 0; b < 2; ++b) {
      p();
      Drain();
    }
    p.play(2);
    p();
    CHECK(p.cue() == 2);
    CHECK_FALSE(p.done());
    const std::vector<float> o = Block(p);
    Fade out(false);
    Fade in(true);
    const long at = 2 * static_cast<long>(N);
    for (UInt k = 0; k < N; ++k)
      CHECK(o[k] == doctest::Approx(Ramp(at + k) * out.next() + Ramp(3000 + k) * in.next()));

    // The cue that plays, again: from its start, once the disk has read it.
    p.play(2);
    std::vector<float> again;
    for (int b = 0; b < 6; ++b) {
      p();
      Drain();
      CHECK(p.cue() == 2);
      const std::vector<float> o2 = Block(p);
      // The old cue's fade, then silence until the stream has read its start.
      if (b > 0 && again.empty() && o2[1] == 0.f) continue;
      if (b > 0) again.insert(again.end(), o2.begin(), o2.end());
    }
    REQUIRE(again.size() > 2);
    CHECK(again[1] == doctest::Approx(Ramp(3001) * 0.005f));
  }

  TEST_CASE("fileStreamPlayer: seek plays the open file from a start to an end; 1 plays it "
            "whole again (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_seek_1155");
    WriteRamp(dir / "a.wav", 2000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    p.seek(1500.f, 1700.f);
    std::vector<float> heard;
    for (int b = 0; b < 10 && heard.size() < 400; ++b) {
      p();
      Drain();
      // Silent until the slow pool has read from there.
      const std::vector<float> o = Block(p);
      if (heard.empty() && o[N - 1] == 0.f) continue;
      heard.insert(heard.end(), o.begin(), o.end());
      if (p.done()) break;
    }
    REQUIRE(heard.size() >= 200);
    Fade in(true);
    for (std::size_t k = 0; k < 200; ++k)
      CHECK(heard[k] == doctest::Approx(Ramp(1500 + static_cast<long>(k)) * in.next()));
    CHECK(p.done());

    // The position, per sample, in ms into the file.
    p.seek(100.f);
    for (int b = 0; b < 3; ++b) {
      p();
      Drain();
    }
    const float* w = p.position().getPtr();
    CHECK(w[0] > 100.f);
    for (UInt k = 1; k < N; ++k)
      CHECK(w[k] == doctest::Approx(w[0] + static_cast<float>(k)));

    p.play(1);
    std::vector<float> whole;
    for (int b = 0; b < 4; ++b) {
      p();
      Drain();
      const std::vector<float> o = Block(p);
      whole.insert(whole.end(), o.begin(), o.end());
    }
    // From frame 0, once the stream has read it: the first sample heard is
    // frame 1 (frame 0 is at the fade's gain of 0).
    std::size_t start = 0;
    while (start < whole.size() && whole[start] == 0.f)
      start++;
    REQUIRE(start + 1 < whole.size());
    CHECK(whole[start] == doctest::Approx(Ramp(1) * 0.005f));
    CHECK(whole[start + 1] == doctest::Approx(Ramp(2) * 0.01f));
  }

  TEST_CASE("fileStreamPlayer: channels, and modout (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_channels_1155");
    WriteRamp(dir / "stereo.wav", 1000, 2);
    WriteRamp(dir / "mono.wav", 1000, 1);
    fileStreamPlayer p(3);
    CHECK(p.channels() == 3);
    REQUIRE(p.open(dir / "stereo.wav"));
    REQUIRE(p.preload(2, dir / "mono.wav"));
    p.play(1);
    for (int b = 0; b < 3; ++b)
      p();
    for (UInt k = 0; k < N; ++k) {
      CHECK(p.out(0).getPtr()[k] == Ramp(2 * N + k, 0));
      CHECK(p.out(1).getPtr()[k] == Ramp(2 * N + k, 1));
      CHECK(p.out(2).getPtr()[k] == 0.f);
    }
    p.modout(true);
    p();
    for (UInt k = 0; k < N; ++k)
      CHECK(p.out(2).getPtr()[k] == Ramp(3 * N + k, 0));
    p.play(2);
    for (int b = 0; b < 3; ++b)
      p();
    for (UInt k = 0; k < N; ++k) {
      CHECK(p.out(0).getPtr()[k] == Ramp(2 * N + k));
      CHECK(p.out(1).getPtr()[k] == Ramp(2 * N + k));
      CHECK(p.out(2).getPtr()[k] == Ramp(2 * N + k));
    }
    // A file with more channels than outputs: the extra ones are not played.
    fileStreamPlayer mono(1);
    REQUIRE(mono.open(dir / "stereo.wav"));
    mono.play(1);
    for (int b = 0; b < 3; ++b)
      mono();
    for (UInt k = 0; k < N; ++k)
      CHECK(mono.out(0).getPtr()[k] == Ramp(2 * N + k, 0));
  }

  TEST_CASE("fileStreamPlayer: pitch shift and time stretch, through DSP::pitchShifter "
            "(#1155)") {
    // A 100 Hz sine at 8 kHz. Played twice as fast it is 200 Hz; time
    // stretched, 100 Hz again; shifted an octave down, 50 Hz.
    ScopedSampleRate rate(8000);
    TempFolder dir("yse_fsp_pitch_1155");
    WriteSine(dir / "sine.wav", 40000, 100.f, 8000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "sine.wav"));
    p.loop(true);

    auto measure = [&]() {
      p.play(1);
      std::vector<float> all;
      for (int b = 0; b < 160; ++b) {
        p();
        const std::vector<float> o = Block(p);
        all.insert(all.end(), o.begin(), o.end());
        Drain();
      }
      // Past the fade and the shifter's latency.
      return CrossingRate(all, 6000) * 8000.0;
    };

    SUBCASE("off: the stream as it is") {
      CHECK(measure() == doctest::Approx(100.0).epsilon(0.03));
    }
    SUBCASE("speed 2") {
      p.speed(2.f);
      CHECK(measure() == doctest::Approx(200.0).epsilon(0.03));
    }
    SUBCASE("speed 2, time stretched") {
      p.speed(2.f).timeStretch(true);
      CHECK(measure() == doctest::Approx(100.0).epsilon(0.05));
    }
    SUBCASE("pitchshift 0.5") {
      p.pitchShift(0.5f);
      CHECK(measure() == doctest::Approx(50.0).epsilon(0.05));
    }
    SUBCASE("pitchshiftcent 1200, in good quality") {
      p.pitchShiftCents(1200.f).quality(pitchShifter::GOOD);
      CHECK(p.quality() == pitchShifter::GOOD);
      CHECK(measure() == doctest::Approx(200.0).epsilon(0.05));
    }
  }

  TEST_CASE("fileStreamPlayer: a shared cue table, followed by sync; a cleared cue falls "
            "silent (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_shared_1155");
    WriteRamp(dir / "a.wav", 5000);
    std::shared_ptr<fileStreamCues> table = fileStreamCues::acquire("shared_1155");
    fileStreamPlayer one;
    fileStreamPlayer two;
    one.cueList(table);
    two.cueList(table);
    CHECK(one.cueList() == table);
    REQUIRE(table->preload(2, dir / "a.wav", 1000.f));
    one();
    two();
    CHECK(one.stale());
    CHECK_FALSE(one.hasCue(2));
    CHECK(one.sync());
    CHECK(two.sync());
    one();
    two();
    CHECK_FALSE(one.stale());
    CHECK(one.hasCue(2));
    CHECK(two.hasCue(2));

    // Each plays through a stream of its own.
    one.play(2);
    two.play(2);
    for (int b = 0; b < 3; ++b) {
      one();
      two();
    }
    CHECK(one.out(0).getPtr()[5] == Ramp(1000 + 2 * N + 5));
    CHECK(two.out(0).getPtr()[5] == Ramp(1000 + 2 * N + 5));

    // Another cue leaves the one that plays playing.
    REQUIRE(table->preload(3, dir / "a.wav"));
    CHECK(one.sync());
    one();
    CHECK(one.out(0).getPtr()[5] == Ramp(1000 + 3 * N + 5));
    CHECK(one.releasePending());
    one.collect();
    CHECK_FALSE(one.releasePending());

    // Cleared, it falls silent.
    one.clear(std::vector<Int>{2});
    CHECK(table->count() == 1);
    one();
    CHECK_FALSE(one.playing());
    CHECK(one.out(0).getPtr()[5] == 0.f);
    CHECK_FALSE(one.hasCue(2));

    // Back to a table of its own.
    one.cueList(nullptr);
    one();
    CHECK(one.cueList() != table);
    CHECK_FALSE(one.hasCue(3));
  }

  TEST_CASE("fileStreamPlayer: close and clear (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_close_1155");
    WriteRamp(dir / "a.wav", 1000);
    WriteRamp(dir / "b.wav", 1000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, ""));
    REQUIRE(p.preload(3, dir / "b.wav"));
    CHECK(p.close()); // the open file, and its cues
    p();
    CHECK_FALSE(p.hasCue(1));
    CHECK_FALSE(p.hasCue(2));
    CHECK(p.hasCue(3));
    CHECK(p.file().empty());
    CHECK_FALSE(p.close());
    CHECK_FALSE(p.close(dir / "missing.wav"));
    REQUIRE(p.open(dir / "a.wav"));
    CHECK(p.close(dir / "b.wav"));
    p();
    CHECK(p.hasCue(1));
    CHECK_FALSE(p.hasCue(3));
    REQUIRE(p.preload(2, dir / "b.wav"));
    p.clear();
    p();
    CHECK(p.hasCue(1));
    CHECK_FALSE(p.hasCue(2));
  }

  TEST_CASE("fileStreamPlayer: blocks and transport allocate nothing, take no lock, read no "
            "disk (#1155)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_rt_1155");
    WriteRamp(dir / "a.wav", 3000, 2);
    fileStreamPlayer p(2);
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 500.f, 900.f));
    REQUIRE(p.preload(3, "", 100.f));
    p();
    p.pitchShift(1.5f);
    p();
    p.pitchShift(1.f);
    const Int list[] = {2, 3};
    {
      TestHelpers::ProbeScope probe;
      p.play(1);
      for (int b = 0; b < 4; ++b)
        p();
      p.play(2);
      p();
      p.pause();
      p();
      p.resume();
      p();
      p.seek(200.f, 400.f);
      p();
      p.play(list, 2);
      p.loop(true).speed(1.5f).modout(true).pitchShift(2.f).timeStretch(true);
      for (int b = 0; b < 6; ++b)
        p();
      p.stop();
      for (int b = 0; b < 4; ++b)
        p();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("fileStreamPlayer: plays while another thread edits its cues (#1155)") {
    // The control side opens and frees streams while the audio thread plays:
    // every sample heard is one of the file's, or silence.
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_fsp_threads_1155");
    WriteRamp(dir / "a.wav", 3000);
    fileStreamPlayer p;
    REQUIRE(p.open(dir / "a.wav"));
    REQUIRE(p.preload(2, "", 0.f, 2000.f));
    p.loop(true);
    std::atomic<bool> run{true};
    std::thread editor([&] {
      int i = 0;
      while (run.load()) {
        if (i % 3 == 0) p.clear();
        p.preload(2 + (i % 3), "", static_cast<float>(i % 7) * 100.f);
        p.collect();
        i++;
      }
    });
    bool bad = false;
    for (int b = 0; b < 600; ++b) {
      if (b % 20 == 0) p.play(2 + (b / 20) % 3);
      p();
      const float* o = p.out(0).getPtr();
      for (UInt k = 0; k < N; ++k)
        if (o[k] < 0.f || o[k] > 3000.f) bad = true;
    }
    run.store(false);
    editor.join();
    CHECK_FALSE(bad);
  }
}
