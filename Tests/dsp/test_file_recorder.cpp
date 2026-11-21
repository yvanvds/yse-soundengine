// Tests for YSE::DSP::fileRecorder (YseEngine/dsp/sampling/), issue #1156:
// signals recorded to a sound file through a lock-free ring, written to disk
// by write() on another thread, behind ~sfrecord.
//
// The engine runs at 1000 Hz unless a case says otherwise, so a millisecond
// is a frame. Most files are 32-bit float, so what is read back is exactly
// what was recorded; the integer cases compute the quantized values.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sndfile.hh>
#include "dsp/sampling/fileRecorder.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::fileRecorder;

namespace {

  namespace fs = std::filesystem;
  constexpr UInt N = STANDARD_BUFFERSIZE;
  using ST = fileRecorder::SAMPLE_TYPE;
  using FT = fileRecorder::FILE_TYPE;

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

  // Sample n of input c: never 0, distinct per channel.
  float Value(long n, int c = 0) {
    return static_cast<float>(n + 1) + 100000.f * static_cast<float>(c);
  }

  // A recorder fed blocks whose sample n is f(n, c), n counted over every
  // block fed.
  struct Feeder {
    std::vector<YSE::DSP::buffer> blocks;
    std::vector<YSE::DSP::buffer*> pointers;
    long next = 0;
    explicit Feeder(int channels)
      : blocks(static_cast<std::size_t>(channels), YSE::DSP::buffer(N)) {
      for (auto& b : blocks)
        pointers.push_back(&b);
    }
    template <typename F> YSE::DSP::buffer& Block(fileRecorder& rec, F f, UInt length = N) {
      for (std::size_t c = 0; c < blocks.size(); ++c) {
        if (blocks[c].getLength() != length) blocks[c].resize(length);
        for (UInt i = 0; i < length; ++i)
          blocks[c].getPtr()[i] = f(next + static_cast<long>(i), static_cast<int>(c));
      }
      next += length;
      return rec(pointers.data(), static_cast<UInt>(pointers.size()));
    }
    YSE::DSP::buffer& Block(fileRecorder& rec) {
      return Block(rec, [](long n, int c) { return Value(n, c); });
    }
  };

  struct readBack {
    int channels = 0;
    int rate = 0;
    int format = 0;
    std::vector<float> samples; // interleaved
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

  // The file as integers at full scale, for the integer types.
  std::vector<int> ReadInts(const std::string& path) {
    SndfileHandle h(path.c_str());
    REQUIRE(h);
    std::vector<int> v(static_cast<std::size_t>(h.frames()) * h.channels());
    REQUIRE(h.readf(v.data(), h.frames()) == h.frames());
    return v;
  }

  fileRecorder& FloatFile(fileRecorder& rec) {
    return rec.sampleType(ST::FLOAT32);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("fileRecorder: records its inputs into the file, exactly, and closes it on a stop "
            "(#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_basic_1156");
    fileRecorder rec(2);
    FloatFile(rec);
    CHECK_FALSE(rec.isOpen());
    REQUIRE(rec.open(dir / "a.wav"));
    CHECK(rec.isOpen());
    CHECK(rec.file() == dir / "a.wav");
    Feeder feed(2);
    feed.Block(rec); // not recording yet
    rec.record(true);
    for (int b = 0; b < 5; ++b) {
      feed.Block(rec);
      if (b == 2) rec.write(); // part of it on the way
    }
    CHECK(rec.recording());
    rec.record(false);
    feed.Block(rec);
    CHECK_FALSE(rec.recording());
    CHECK(rec.pending());
    CHECK(rec.isOpen()); // until the stop is written
    CHECK(rec.write() == 5 * N - 3 * N);
    CHECK_FALSE(rec.pending());
    CHECK_FALSE(rec.isOpen());
    CHECK(rec.file().empty());
    CHECK(rec.framesWritten() == 5 * N);
    CHECK(rec.dropped() == 0);

    const readBack r = Read(dir / "a.wav");
    CHECK(r.channels == 2);
    CHECK(r.rate == 1000);
    CHECK((r.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV);
    CHECK((r.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT);
    REQUIRE(r.frames() == 5 * N);
    for (long k = 0; k < r.frames(); ++k) {
      CAPTURE(k);
      CHECK(r.at(k, 0) == Value(N + k, 0));
      CHECK(r.at(k, 1) == Value(N + k, 1));
    }

    // Each open records once: a second start without one does nothing.
    rec.record(true);
    feed.Block(rec);
    CHECK_FALSE(rec.recording());
    CHECK_FALSE(rec.pending());
  }

  TEST_CASE("fileRecorder: the time output; a stop with nothing recording closes the open file, "
            "empty (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_time_1156");
    fileRecorder rec;
    FloatFile(rec);
    Feeder feed(1);
    // No file: a start does nothing.
    rec.record(true);
    CHECK(feed.Block(rec).getPtr()[5] == 0.f);
    CHECK_FALSE(rec.recording());

    REQUIRE(rec.open(dir / "t.wav"));
    rec.record(true);
    YSE::DSP::buffer& t0 = feed.Block(rec);
    for (UInt i = 0; i < N; ++i)
      CHECK(t0.getPtr()[i] == static_cast<float>(i));
    YSE::DSP::buffer& t1 = feed.Block(rec);
    CHECK(t1.getPtr()[0] == static_cast<float>(N));
    rec.record(false);
    YSE::DSP::buffer& t2 = feed.Block(rec);
    for (UInt i = 0; i < N; ++i)
      CHECK(t2.getPtr()[i] == static_cast<float>(2 * N)); // the length, held
    rec.write();

    // At 2000 Hz a frame is half a millisecond.
    {
      ScopedSampleRate faster(2000);
      REQUIRE(rec.open(dir / "t2.wav"));
      rec.record(true);
      YSE::DSP::buffer& t3 = feed.Block(rec);
      CHECK(t3.getPtr()[10] == 5.f);
    }
    rec.close();
    CHECK(Read(dir / "t2.wav").rate == 2000);

    REQUIRE(rec.open(dir / "empty.wav"));
    rec.record(false);
    feed.Block(rec);
    rec.write();
    CHECK_FALSE(rec.isOpen());
    CHECK(Read(dir / "empty.wav").frames() == 0);
  }

  TEST_CASE("fileRecorder: recordFor records exactly that long; the begin and end ramps "
            "(#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_ramps_1156");
    fileRecorder rec;
    FloatFile(rec);
    Feeder feed(1);
    auto one = [](long, int) { return 1.f; };

    SUBCASE("timed") {
      REQUIRE(rec.open(dir / "d.wav"));
      rec.recordFor(300.f);
      for (int b = 0; b < 4; ++b)
        feed.Block(rec);
      CHECK_FALSE(rec.recording());
      rec.write();
      CHECK_FALSE(rec.isOpen());
      const readBack r = Read(dir / "d.wav");
      REQUIRE(r.frames() == 300);
      for (long k = 0; k < 300; ++k)
        CHECK(r.at(k) == Value(k));
    }
    SUBCASE("ramps on a timed recording") {
      REQUIRE(rec.open(dir / "r.wav"));
      rec.beginRamp(50.f).endRamp(40.f);
      rec.recordFor(200.f);
      for (int b = 0; b < 2; ++b)
        feed.Block(rec, one);
      rec.write();
      const readBack r = Read(dir / "r.wav");
      REQUIRE(r.frames() == 200);
      for (long k = 0; k < 200; ++k) {
        CAPTURE(k);
        float g = 1.f;
        if (k < 50) g = static_cast<float>(static_cast<double>(k) / 50.0);
        const long left = 200 - k;
        if (left <= 40) g *= static_cast<float>(static_cast<double>(left - 1) / 40.0);
        CHECK(r.at(k) == g);
      }
      CHECK(r.at(199) == 0.f);
    }
    SUBCASE("a stop records on while it fades out") {
      REQUIRE(rec.open(dir / "s.wav"));
      rec.endRamp(200.f);
      rec.record(true);
      feed.Block(rec, one);
      rec.record(false);
      feed.Block(rec, one);
      CHECK(rec.recording()); // fading
      feed.Block(rec, one);
      CHECK_FALSE(rec.recording());
      rec.write();
      const readBack r = Read(dir / "s.wav");
      REQUIRE(r.frames() == static_cast<long>(N) + 200);
      for (long k = 0; k < static_cast<long>(N); ++k)
        CHECK(r.at(k) == 1.f);
      for (long k = 0; k < 200; ++k)
        CHECK(r.at(N + k) == static_cast<float>(static_cast<double>(199 - k) / 200.0));
    }
  }

  TEST_CASE("fileRecorder: loop keeps the last stretch; sortloop puts it in order (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_loop_1156");
    for (const bool sorted : {false, true}) {
      CAPTURE(sorted);
      fileRecorder rec(2);
      FloatFile(rec).sortLoop(sorted);
      const std::string path = dir / (sorted ? "sorted.wav" : "circle.wav");
      REQUIRE(rec.open(path));
      Feeder feed(2);
      rec.loop(300.f);
      for (int b = 0; b < 10; ++b) {
        feed.Block(rec);
        if (b % 3 == 0) rec.write();
      }
      rec.record(false);
      feed.Block(rec);
      rec.write();
      CHECK(rec.framesWritten() == 300);
      const readBack r = Read(path);
      REQUIRE(r.frames() == 300);
      // 1280 frames recorded: the file held the last 300, in a circle.
      const long total = 10 * N;
      for (long k = 0; k < 300; ++k) {
        CAPTURE(k);
        // Frame n of the recording is in slot n % 300; sorted, the oldest
        // first.
        const long n =
            sorted ? total - 300 + k
                   : (k < total % 300 ? total - total % 300 + k : total - total % 300 - 300 + k);
        CHECK(r.at(k, 0) == Value(n, 0));
        CHECK(r.at(k, 1) == Value(n, 1));
      }
    }
    // Shorter than the loop: what was recorded, as it came.
    fileRecorder rec;
    FloatFile(rec).sortLoop(true);
    REQUIRE(rec.open(dir / "short.wav"));
    Feeder feed(1);
    rec.loop(1000.f);
    feed.Block(rec);
    rec.record(false);
    feed.Block(rec);
    rec.write();
    const readBack r = Read(dir / "short.wav");
    REQUIRE(r.frames() == N);
    CHECK(r.at(7) == Value(7));
  }

  TEST_CASE("fileRecorder: integer samples, quantized and dithered here (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_int_1156");
    // Values between the 16-bit steps, both signs, and past full scale.
    auto signal = [](long n, int) {
      const float values[] = {0.25f,
                              -0.25f,
                              0.1234567f,
                              -0.1234567f,
                              0.99999f,
                              1.5f,
                              -1.5f,
                              (0.5f + 0.4f) / 32768.f,
                              (-0.5f - 0.4f) / 32768.f,
                              0.f};
      return values[n % 10];
    };
    auto expected = [&](long n, int bits, bool floor) {
      const double scale = std::ldexp(1.0, bits - 1);
      const double x = static_cast<double>(signal(n, 0)) * scale;
      double q = floor ? std::floor(x) : std::floor(x + 0.5);
      q = std::min(std::max(q, -scale), scale - 1.0);
      return static_cast<long long>(q);
    };
    struct form {
      const char* file;
      ST type;
      int bits;
      bool floor;
      int major;
    };
    for (const form f : {form{"a.wav", ST::INT16, 16, false, SF_FORMAT_WAV},
                         form{"b.aif", ST::INT24, 24, false, SF_FORMAT_AIFF},
                         form{"c.wav", ST::INT8, 8, true, SF_FORMAT_WAV},
                         form{"d.aiff", ST::INT32, 32, true, SF_FORMAT_AIFF},
                         form{"e.xyz", ST::INT16, 16, true, SF_FORMAT_AIFF}}) {
      CAPTURE(f.file);
      fileRecorder rec;
      rec.sampleType(f.type).quantization(f.floor ? fileRecorder::QUANTIZE::FLOOR
                                                  : fileRecorder::QUANTIZE::ROUND);
      REQUIRE(rec.open(dir / f.file));
      Feeder feed(1);
      rec.record(true);
      feed.Block(rec, signal);
      rec.record(false);
      feed.Block(rec, signal);
      rec.write();
      SndfileHandle h((dir / f.file).c_str());
      CHECK((h.format() & SF_FORMAT_TYPEMASK) == f.major);
      const std::vector<int> v = ReadInts(dir / f.file);
      REQUIRE(v.size() == N);
      for (long k = 0; k < static_cast<long>(N); ++k) {
        CAPTURE(k);
        const long long stored =
            static_cast<long long>(v[static_cast<std::size_t>(k)]) >> (32 - f.bits);
        CHECK(stored == expected(k, f.bits, f.floor));
      }
    }

    // Dither: within a step and a half of the value, varying, and right on
    // average; noise shaping too.
    for (const auto mode : {fileRecorder::DITHER::TRIANGULAR, fileRecorder::DITHER::NOISE_SHAPED}) {
      CAPTURE(static_cast<int>(mode));
      fileRecorder rec;
      rec.sampleType(ST::INT16).dither(mode);
      REQUIRE(rec.open(dir / "dither.wav"));
      Feeder feed(1);
      const float level = 0.3f / 32768.f; // under a step
      rec.record(true);
      for (int b = 0; b < 40; ++b)
        feed.Block(rec, [&](long, int) { return level; });
      rec.record(false);
      feed.Block(rec);
      rec.write();
      const std::vector<int> v = ReadInts(dir / "dither.wav");
      REQUIRE(v.size() == 40 * N);
      double sum = 0.0;
      bool varied = false;
      for (const int s : v) {
        const int q = s >> 16;
        CHECK(std::abs(static_cast<double>(q) - 0.3) <= 4.0);
        sum += q;
        varied = varied || q != v[0] >> 16;
      }
      CHECK(varied);
      CHECK(sum / static_cast<double>(v.size()) == doctest::Approx(0.3).epsilon(0.1));
    }
  }

  TEST_CASE("fileRecorder: file types by extension and by name; float64, mu-law, raw byte "
            "order (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_types_1156");
    struct form {
      const char* file;
      FT type;
      ST sample;
      int major;
      int sub;
    };
    for (const form f : {form{"a.wav", FT::AUTO, ST::FLOAT64, SF_FORMAT_WAV, SF_FORMAT_DOUBLE},
                         form{"b.AIF", FT::AUTO, ST::FLOAT32, SF_FORMAT_AIFF, SF_FORMAT_FLOAT},
                         form{"c.take", FT::AUTO, ST::INT16, SF_FORMAT_AIFF, SF_FORMAT_PCM_16},
                         form{"d.au", FT::AUTO, ST::MULAW, SF_FORMAT_AU, SF_FORMAT_ULAW},
                         form{"e.caf", FT::AUTO, ST::ALAW, SF_FORMAT_CAF, SF_FORMAT_ALAW},
                         form{"f.w64", FT::AUTO, ST::INT24, SF_FORMAT_W64, SF_FORMAT_PCM_24},
                         form{"g.aif", FT::WAV, ST::INT16, SF_FORMAT_WAV, SF_FORMAT_PCM_16}}) {
      CAPTURE(f.file);
      fileRecorder rec;
      rec.sampleType(f.sample);
      REQUIRE(rec.open(dir / f.file, f.type));
      Feeder feed(1);
      rec.record(true);
      feed.Block(rec, [](long n, int) { return static_cast<float>(n % 7) * 0.125f - 0.375f; });
      rec.record(false);
      feed.Block(rec);
      rec.write();
      const readBack r = Read(dir / f.file);
      CHECK((r.format & SF_FORMAT_TYPEMASK) == f.major);
      CHECK((r.format & SF_FORMAT_SUBMASK) == f.sub);
      REQUIRE(r.frames() == N);
      for (long k = 0; k < static_cast<long>(N); ++k)
        CHECK(r.at(k) ==
              doctest::Approx(static_cast<float>(k % 7) * 0.125f - 0.375f).epsilon(0.02));
    }

    // Mu-law and a-law hold -1 to 1: louder is clipped, not wrapped.
    for (const ST law : {ST::MULAW, ST::ALAW}) {
      fileRecorder rec;
      rec.sampleType(law);
      REQUIRE(rec.open(dir / "law.wav"));
      Feeder feed(1);
      rec.record(true);
      feed.Block(rec, [](long n, int) { return n % 2 == 0 ? 300.f : -300.f; });
      rec.record(false);
      feed.Block(rec);
      rec.write();
      const readBack r = Read(dir / "law.wav");
      REQUIRE(r.frames() == N);
      CHECK(r.at(0) == doctest::Approx(1.f).epsilon(0.02));
      CHECK(r.at(1) == doctest::Approx(-1.f).epsilon(0.02));
    }

    // Raw, swapped from this machine's order: read back big- or
    // little-endian accordingly.
    for (const bool swap : {false, true}) {
      CAPTURE(swap);
      fileRecorder rec;
      rec.sampleType(ST::INT16).byteSwap(swap);
      REQUIRE(rec.open(dir / "h.raw"));
      Feeder feed(1);
      rec.record(true);
      feed.Block(rec, [](long, int) { return 0.5f; });
      rec.record(false);
      feed.Block(rec);
      rec.write();
      std::vector<unsigned char> bytes(2 * N);
      {
        std::FILE* fp = std::fopen((dir / "h.raw").c_str(), "rb");
        REQUIRE(fp != nullptr);
        CHECK(std::fread(bytes.data(), 1, bytes.size(), fp) == bytes.size());
        std::fclose(fp);
      }
      const std::uint16_t one = 1;
      unsigned char first = 0;
      std::memcpy(&first, &one, 1);
      const bool little = (first == 1) != swap;
      // 0.5 is 0x4000.
      CHECK(bytes[0] == (little ? 0x00 : 0x40));
      CHECK(bytes[1] == (little ? 0x40 : 0x00));
    }

    // FLAC takes 24 bits for floats, and does not loop.
    SF_INFO flac{};
    flac.channels = 1;
    flac.samplerate = 1000;
    flac.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
    if (sf_format_check(&flac) != 0) {
      fileRecorder rec;
      rec.sampleType(ST::FLOAT32);
      REQUIRE(rec.open(dir / "i.flac"));
      Feeder feed(1);
      rec.loop(50.f);
      feed.Block(rec);
      CHECK_FALSE(rec.recording());
      rec.record(true);
      feed.Block(rec, [](long, int) { return 0.25f; });
      rec.record(false);
      feed.Block(rec);
      rec.write();
      const readBack r = Read(dir / "i.flac");
      CHECK((r.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_24);
      CHECK(r.frames() == N);
      CHECK(r.at(3) == 0.25f);
    }

    // What cannot be made; a failed open leaves no file open.
    fileRecorder rec;
    CHECK_FALSE(rec.open(""));
    REQUIRE(rec.open(dir / "j.wav"));
    CHECK_FALSE(rec.open(dir / "missing/folder/x.wav"));
    CHECK_FALSE(rec.isOpen());
    CHECK(rec.file().empty());
  }

  TEST_CASE("fileRecorder: file channels other than the inputs (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_chans_1156");
    for (const UInt channels : {1u, 3u}) {
      CAPTURE(channels);
      fileRecorder rec(2);
      FloatFile(rec).fileChannels(channels);
      REQUIRE(rec.open(dir / "c.wav"));
      Feeder feed(2);
      rec.record(true);
      feed.Block(rec);
      rec.record(false);
      feed.Block(rec);
      rec.write();
      const readBack r = Read(dir / "c.wav");
      REQUIRE(r.channels == static_cast<int>(channels));
      REQUIRE(r.frames() == N);
      for (long k = 0; k < static_cast<long>(N); ++k) {
        CHECK(r.at(k, 0) == Value(k, 0));
        if (channels == 3) {
          CHECK(r.at(k, 1) == Value(k, 1));
          CHECK(r.at(k, 2) == 0.f);
        }
      }
    }
    // Fewer inputs given than the recorder has: silence for the rest.
    fileRecorder rec(2);
    FloatFile(rec);
    REQUIRE(rec.open(dir / "d.wav"));
    YSE::DSP::buffer in(N);
    in = 0.5f;
    rec.record(true);
    rec(in);
    rec.record(false);
    rec(in);
    rec.write();
    const readBack r = Read(dir / "d.wav");
    CHECK(r.at(4, 0) == 0.5f);
    CHECK(r.at(4, 1) == 0.f);
  }

  TEST_CASE("fileRecorder: a full ring drops and counts frames instead of waiting (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_drop_1156");
    fileRecorder rec(1, 100); // made MIN_RING_FRAMES long
    CHECK(rec.ringFrames() == fileRecorder::MIN_RING_FRAMES);
    FloatFile(rec);
    REQUIRE(rec.open(dir / "x.wav"));
    Feeder feed(1);
    rec.record(true);
    const UInt blocks = fileRecorder::MIN_RING_FRAMES / N + 8;
    for (UInt b = 0; b < blocks; ++b)
      feed.Block(rec);
    CHECK(rec.dropped() == 8 * N);
    // The time goes on.
    CHECK(rec.out().getPtr()[0] == static_cast<float>((blocks - 1) * N));
    rec.write();
    feed.Block(rec); // room again
    rec.record(false);
    feed.Block(rec);
    rec.write();
    const readBack r = Read(dir / "x.wav");
    REQUIRE(r.frames() == static_cast<long>(fileRecorder::MIN_RING_FRAMES + N));
    CHECK(r.at(0) == Value(0));
    CHECK(r.at(fileRecorder::MIN_RING_FRAMES - 1) == Value(fileRecorder::MIN_RING_FRAMES - 1));
    CHECK(r.at(fileRecorder::MIN_RING_FRAMES) == Value(blocks * N));
    // A new recording counts afresh.
    REQUIRE(rec.open(dir / "y.wav"));
    rec.record(true);
    feed.Block(rec);
    CHECK(rec.dropped() == 0);
  }

  TEST_CASE("fileRecorder: the file takes the engine's rate when the recording starts; close "
            "and open cut a recording (#1156)") {
    TempFolder dir("yse_frec_rate_1156");
    fileRecorder rec;
    FloatFile(rec);
    Feeder feed(1);
    {
      ScopedSampleRate rate(1000);
      REQUIRE(rec.open(dir / "r.wav"));
    }
    {
      ScopedSampleRate rate(3000);
      rec.recordFor(30.f); // 90 frames at the new rate
      feed.Block(rec);
      rec.write();
      const readBack r = Read(dir / "r.wav");
      CHECK(r.rate == 3000);
      CHECK(r.frames() == 90);
    }

    ScopedSampleRate rate(1000);
    REQUIRE(rec.open(dir / "c.wav"));
    rec.record(true);
    feed.next = 0;
    feed.Block(rec);
    CHECK(rec.close());
    CHECK_FALSE(rec.close());
    feed.Block(rec); // sees the file gone
    CHECK_FALSE(rec.recording());
    rec.write();
    CHECK(Read(dir / "c.wav").frames() == N);

    // open while recording: the first file keeps what it had.
    REQUIRE(rec.open(dir / "d.wav"));
    rec.record(true);
    feed.next = 0;
    feed.Block(rec);
    REQUIRE(rec.open(dir / "e.wav"));
    feed.Block(rec);
    CHECK_FALSE(rec.recording());
    rec.record(true);
    feed.Block(rec);
    rec.record(false);
    feed.Block(rec);
    rec.write();
    CHECK(Read(dir / "d.wav").frames() == N);
    const readBack e = Read(dir / "e.wav");
    REQUIRE(e.frames() == N);
    CHECK(e.at(0) == Value(2 * N));
  }

  TEST_CASE("fileRecorder: the destructor finishes the file (#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_dtor_1156");
    {
      fileRecorder rec;
      FloatFile(rec);
      REQUIRE(rec.open(dir / "x.wav"));
      Feeder feed(1);
      rec.record(true);
      feed.Block(rec);
      feed.Block(rec);
    }
    const readBack r = Read(dir / "x.wav");
    REQUIRE(r.frames() == 2 * N);
    CHECK(r.at(2 * N - 1) == Value(2 * N - 1));
  }

  TEST_CASE("fileRecorder: blocks and transport allocate nothing, take no lock, touch no disk "
            "(#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_rt_1156");
    fileRecorder rec(2);
    FloatFile(rec);
    REQUIRE(rec.open(dir / "x.wav"));
    Feeder feed(2);
    feed.Block(rec);
    {
      TestHelpers::ProbeScope probe;
      rec.beginRamp(10.f).endRamp(20.f);
      rec.record(true);
      for (int b = 0; b < 4; ++b)
        feed.Block(rec);
      rec.record(false);
      for (int b = 0; b < 2; ++b)
        feed.Block(rec);
      CHECK(rec.pending());
      CHECK(rec.dropped() == 0);
      CHECK(rec.isOpen());
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    rec.write();
    REQUIRE(rec.open(dir / "y.wav"));
    {
      TestHelpers::ProbeScope probe;
      rec.loop(100.f);
      for (int b = 0; b < 3; ++b)
        feed.Block(rec);
      rec.record(false);
      feed.Block(rec);
      rec.recordFor(10.f);
      feed.Block(rec);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    rec.write();
    CHECK(Read(dir / "y.wav").frames() == 100);
  }

  TEST_CASE("fileRecorder: a writer thread keeps up with the audio thread; every frame lands "
            "(#1156)") {
    ScopedSampleRate rate(1000);
    TempFolder dir("yse_frec_threads_1156");
    fileRecorder rec(2, 8192);
    FloatFile(rec);
    REQUIRE(rec.open(dir / "x.wav"));
    std::atomic<bool> run{true};
    std::thread writer([&] {
      while (run.load()) {
        if (rec.pending()) {
          rec.write();
        } else {
          std::this_thread::yield();
        }
      }
      rec.write();
    });
    Feeder feed(2);
    rec.record(true);
    constexpr int BLOCKS = 2000;
    for (int b = 0; b < BLOCKS; ++b) {
      // The ring holds 64 blocks: every 32 the audio thread lets the writer
      // catch up, as a real one does by running in time.
      if (b % 32 == 31)
        for (int spin = 0; spin < 10000000 && rec.pending(); ++spin)
          std::this_thread::yield();
      feed.Block(rec);
    }
    rec.record(false);
    feed.Block(rec);
    for (int spin = 0; spin < 1000000 && rec.isOpen(); ++spin)
      std::this_thread::yield();
    run.store(false);
    writer.join();
    CHECK_FALSE(rec.isOpen());
    CHECK(rec.dropped() == 0);
    const readBack r = Read(dir / "x.wav");
    REQUIRE(r.frames() == BLOCKS * static_cast<long>(N));
    bool exact = true;
    for (long k = 0; k < r.frames(); ++k)
      exact = exact && r.at(k, 0) == Value(k, 0) && r.at(k, 1) == Value(k, 1);
    CHECK(exact);
  }
}
