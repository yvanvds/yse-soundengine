// Tests for YSE::DSP::sampleBuffer, sampleBufferRegistry and
// sampleBufferReader (YseEngine/dsp/sampling/), issue #1039: the shared,
// named sample buffer behind ~buffer and the sampling family.
//
// Three groups: the edits in closed form, the registry's naming and lifetime
// rules, and the reader's promise that a pinned view stays readable while the
// buffer changes underneath it, which the last case hammers from three
// threads so ASan / TSan can see a freed or racing storage.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::sampleView;

namespace {

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

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

  // A buffer whose sample n of channel c is (c + 1) * 100 + n.
  std::shared_ptr<sampleBuffer> Counting(UInt channels, UInt frames, float rate = 48000.f) {
    auto b = sampleBuffer::create(channels, frames, rate);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 100 + n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── shape and edits ──────────────────────────────────────────────────────

  TEST_CASE("sampleBuffer: a new buffer is silent, private and has the shape asked (#1039)") {
    auto b = sampleBuffer::create(2, 100, 22050.f);
    CHECK(b->channels() == 2);
    CHECK(b->frames() == 100);
    CHECK(b->sampleRate() == doctest::Approx(22050.f));
    CHECK(b->name().empty());
    CHECK(b->version() == 0);
    for (UInt c = 0; c < 2; ++c)
      for (float v : Channel(*b, c))
        CHECK(v == 0.f);
    CHECK(sampleBufferRegistry::findById(b->id()) == b);
  }

  TEST_CASE("sampleBuffer: rate 0 follows the engine rate (#1039)") {
    SampleRateGuard guard;
    YSE::SAMPLERATE = 44100;
    auto b = sampleBuffer::create(1, 44100);
    CHECK(b->sampleRate() == doctest::Approx(44100.f));
    CHECK(b->lengthMs() == doctest::Approx(1000.f));
    YSE::SAMPLERATE = 48000;
    CHECK(b->sampleRate() == doctest::Approx(48000.f));
    b->setSizeMs(500.f);
    CHECK(b->frames() == 24000);
  }

  TEST_CASE("sampleBuffer: setSize keeps what fits and zeroes the rest (#1039)") {
    auto b = Counting(2, 10);
    b->setSize(20, 3);
    REQUIRE(b->channels() == 3);
    REQUIRE(b->frames() == 20);
    auto left = Channel(*b, 0);
    CHECK(left[9] == 109.f);
    CHECK(left[10] == 0.f);
    CHECK(Channel(*b, 1)[0] == 200.f);
    CHECK(Channel(*b, 2)[0] == 0.f);
    b->setSize(4);
    CHECK(b->channels() == 3);
    CHECK(Channel(*b, 1) == std::vector<float>{200.f, 201.f, 202.f, 203.f});
    CHECK(b->version() > 0);
  }

  TEST_CASE("sampleBuffer: fill, clear, gain, offset, deltas, normalize (#1039)") {
    auto b = Counting(1, 4);
    b->applyGain(2.f);
    CHECK(Channel(*b, 0) == std::vector<float>{200.f, 202.f, 204.f, 206.f});
    b->applyOffset(-200.f);
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 2.f, 4.f, 6.f});
    b->applyDeltas();
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 2.f, 2.f, 2.f});
    b->normalize(0.5f);
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 0.5f, 0.5f, 0.5f});
    b->fill(-3.f);
    CHECK(Channel(*b, 0) == std::vector<float>{-3.f, -3.f, -3.f, -3.f});
    b->normalize();
    CHECK(Channel(*b, 0)[0] == doctest::Approx(-1.f));
    b->clear();
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 0.f, 0.f, 0.f});
    b->normalize(); // silent stays silent, no division by zero
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 0.f, 0.f, 0.f});
  }

  TEST_CASE("sampleBuffer: fillSine writes the asked number of cycles (#1039)") {
    auto b = sampleBuffer::create(2, 8);
    b->fillSine(1.f);
    auto s = Channel(*b, 1);
    CHECK(s[0] == doctest::Approx(0.f));
    CHECK(s[2] == doctest::Approx(1.f));
    CHECK(s[6] == doctest::Approx(-1.f));
    b->fillSine(1.f, 4.f, true); // a quarter cycle of a cosine
    s = Channel(*b, 0);
    CHECK(s[0] == doctest::Approx(1.f));
    CHECK(s[4] == doctest::Approx(std::cos(3.14159265 / 4)));
  }

  TEST_CASE("sampleBuffer: windows are 0 at the ends and 1 in the middle (#1039)") {
    auto b = sampleBuffer::create(1, 9);
    b->fill(1.f);
    b->applyWindow(YSE::DSP::SAMPLE_WINDOW::HANNING);
    auto w = Channel(*b, 0);
    CHECK(w[0] == doctest::Approx(0.f));
    CHECK(w[4] == doctest::Approx(1.f));
    CHECK(w[8] == doctest::Approx(0.f));
    CHECK(w[2] == doctest::Approx(0.5f));

    b->fill(1.f);
    b->applyWindow(YSE::DSP::SAMPLE_WINDOW::TRIANGLE, true); // the falling half
    w = Channel(*b, 0);
    CHECK(w[0] == doctest::Approx(1.f));
    CHECK(w[8] == doctest::Approx(0.f));
    CHECK(w[4] == doctest::Approx(0.5f));

    b->fill(1.f);
    b->applyWindow(YSE::DSP::SAMPLE_WINDOW::KAISER, false, 8.f);
    w = Channel(*b, 0);
    CHECK(w[4] == doctest::Approx(1.f));
    CHECK(w[1] == doctest::Approx(w[7]));
    CHECK(w[0] < 0.01f);
  }

  TEST_CASE("sampleBuffer: crop keeps the range and refuses an empty one (#1039)") {
    auto b = Counting(2, 10, 1000.f);
    CHECK_FALSE(b->crop(5, 5));
    CHECK_FALSE(b->crop(20, 30));
    CHECK(b->frames() == 10);
    REQUIRE(b->crop(2, 5));
    CHECK(Channel(*b, 0) == std::vector<float>{102.f, 103.f, 104.f});
    CHECK(Channel(*b, 1) == std::vector<float>{202.f, 203.f, 204.f});
    REQUIRE(b->cropMs(1.f, 100.f)); // 1 kHz: from sample 1 to the end
    CHECK(Channel(*b, 0) == std::vector<float>{103.f, 104.f});
  }

  TEST_CASE("sampleBuffer: setSamples clips to the buffer, getSamples reports the count (#1039)") {
    auto b = sampleBuffer::create(1, 4);
    const float values[] = {1.f, 2.f, 3.f, 4.f};
    b->setSamples(0, 2, values, 4);
    CHECK(Channel(*b, 0) == std::vector<float>{0.f, 0.f, 1.f, 2.f});
    b->setSamples(3, 0, values, 4); // no such channel: nothing
    float out[8] = {};
    CHECK(b->getSamples(0, 1, out, 8) == 3);
    CHECK(out[2] == 2.f);
    CHECK(b->getSamples(1, 0, out, 8) == 0);
  }

  TEST_CASE("sampleBuffer: copyFrom takes shape, rate and samples (#1039)") {
    auto source = Counting(2, 3, 32000.f);
    auto b = sampleBuffer::create();
    b->copyFrom(*source);
    CHECK(b->channels() == 2);
    CHECK(b->sampleRate() == doctest::Approx(32000.f));
    CHECK(Channel(*b, 1) == std::vector<float>{200.f, 201.f, 202.f});
  }

  // ─── files ────────────────────────────────────────────────────────────────

  TEST_CASE("sampleBuffer: write then read round-trips channels, rate and samples (#1039)") {
    const std::string path = TempFile("yse_samplebuffer_1039.wav");
    auto source = Counting(2, 300, 32000.f);
    REQUIRE(source->write(path.c_str()));

    auto b = sampleBuffer::create(1, 0);
    SUBCASE("replace takes the file's channels and length") {
      REQUIRE(b->replace(path.c_str()));
      CHECK(b->channels() == 2);
      CHECK(b->frames() == 300);
      CHECK(b->sampleRate() == doctest::Approx(32000.f));
      CHECK(Channel(*b, 1) == Channel(*source, 1));
    }
    SUBCASE("read keeps the buffer's channels and mixes the extra ones in") {
      b->setSize(0, 1);
      REQUIRE(b->read(path.c_str()));
      CHECK(b->channels() == 1);
      CHECK(b->frames() == 300);
      CHECK(Channel(*b, 0)[7] == doctest::Approx(107.f + 207.f));
    }
    SUBCASE("a duration keeps the buffer's length and reads into its start") {
      b->setSize(50, 2);
      b->fill(9.f);
      // 1 ms at 32 kHz is 32 samples in, 0.5 ms is 16 samples long.
      REQUIRE(b->read(path.c_str(), 1.f, 0.5f));
      CHECK(b->frames() == 50);
      auto right = Channel(*b, 1);
      CHECK(right[0] == doctest::Approx(232.f));
      CHECK(right[15] == doctest::Approx(247.f));
      CHECK(right[16] == 0.f); // cleared, not left at 9
    }
    SUBCASE("an explicit channel count") {
      REQUIRE(b->read(path.c_str(), 0.f, -1.f, 3));
      CHECK(b->channels() == 3);
      CHECK(Channel(*b, 2)[0] == 0.f);
    }
    Remove(path);
  }

  TEST_CASE("sampleBuffer: a missing file leaves the buffer as it was (#1039)") {
    auto b = Counting(1, 3);
    const std::uint64_t before = b->version();
    CHECK_FALSE(b->read("no/such/file_1039.wav"));
    CHECK_FALSE(b->replace(""));
    CHECK(b->version() == before);
    CHECK(Channel(*b, 0) == std::vector<float>{100.f, 101.f, 102.f});
    CHECK_FALSE(sampleBuffer::create(1, 0)->write(TempFile("yse_empty_1039.wav").c_str()));
  }

  // ─── the registry ─────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferRegistry: one buffer per name, held weakly (#1039)") {
    const std::size_t before = sampleBufferRegistry::count();
    bool created = false;
    auto a = sampleBufferRegistry::acquire("reg_1039", &created);
    CHECK(created);
    CHECK(a->name() == "reg_1039");
    auto b = sampleBufferRegistry::acquire("reg_1039", &created);
    CHECK_FALSE(created);
    CHECK(a == b);
    CHECK(sampleBufferRegistry::find("reg_1039") == a);
    CHECK(sampleBufferRegistry::count() == before + 1);

    const std::uint64_t id = a->id();
    a.reset();
    b.reset();
    CHECK(sampleBufferRegistry::find("reg_1039") == nullptr);
    CHECK(sampleBufferRegistry::findById(id) == nullptr);
    CHECK(sampleBufferRegistry::count() == before);

    // The name is free again, for a new buffer with a new id.
    auto again = sampleBufferRegistry::acquire("reg_1039", &created);
    CHECK(created);
    CHECK(again->id() != id);
    CHECK(again->frames() == 0);
  }

  TEST_CASE("sampleBufferRegistry: private buffers have no name to find (#1039)") {
    auto b = sampleBuffer::create();
    CHECK(sampleBufferRegistry::find("") == nullptr);
    CHECK(sampleBufferRegistry::findById(b->id()) == b);
  }

  // ─── readers ──────────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferReader: an unbound reader pins an empty view (#1039)") {
    sampleBufferReader reader;
    sampleView view = reader.pin();
    CHECK(view.empty());
    CHECK(view.channel(0) == nullptr);
    CHECK(view.at(0, 0) == 0.f);
  }

  TEST_CASE("sampleBufferReader: a view outlives a change, and is freed after it (#1039)") {
    auto b = Counting(1, 4);
    sampleBufferReader reader(b);
    {
      sampleView view = reader.pin();
      REQUIRE(view.frames() == 4);
      const float* old = view.channel(0);
      b->setSize(1000);
      b->fill(7.f);
      // The view still shows the storage it pinned: nothing freed it.
      CHECK(old[3] == 103.f);
      CHECK(view.at(0, 3) == 103.f);
      CHECK(b->retiredCount() >= 1);
    }
    // The view is gone; the next collect frees what it held.
    b->collect();
    CHECK(b->retiredCount() == 0);

    sampleView next = reader.pin();
    CHECK(next.frames() == 1000);
    CHECK(next.at(0, 999) == 7.f);
    CHECK(next.version() == b->version());
  }

  TEST_CASE("sampleBufferReader: with no view held a change frees at once (#1039)") {
    auto b = Counting(1, 4);
    sampleBufferReader reader(b);
    { sampleView view = reader.pin(); }
    b->fill(1.f);
    CHECK(b->retiredCount() == 0);
  }

  TEST_CASE("sampleBufferRegistry: collect frees what a reader let go (#1039)") {
    auto b = sampleBufferRegistry::acquire("collect_1039");
    b->setSize(16);
    sampleBufferReader reader(b);
    {
      sampleView view = reader.pin();
      b->fill(2.f);
      sampleBufferRegistry::collect(); // still held: stays
      CHECK(b->retiredCount() == 1);
    }
    sampleBufferRegistry::collect();
    CHECK(b->retiredCount() == 0);
  }

  TEST_CASE("sampleBufferReader: rebinding and destroying let go of the buffer (#1039)") {
    auto a = Counting(1, 4);
    auto b = Counting(1, 8);
    {
      sampleBufferReader reader(a);
      reader.bind(b);
      CHECK(reader.buffer() == b);
      CHECK(reader.pin().frames() == 8);
    }
    // No hazard left on either buffer: a change frees at once.
    a->fill(0.f);
    b->fill(0.f);
    CHECK(a->retiredCount() == 0);
    CHECK(b->retiredCount() == 0);
  }

  TEST_CASE("sampleView: at and linear read in range and 0 outside (#1039)") {
    auto b = Counting(2, 4, 8000.f);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    CHECK(view.sampleRate() == doctest::Approx(8000.f));
    CHECK(view.at(1, 2) == 202.f);
    CHECK(view.at(2, 0) == 0.f);
    CHECK(view.at(0, 4) == 0.f);
    CHECK(view.linear(0, 1.25) == doctest::Approx(101.25f));
    CHECK(view.linear(0, 3.5) == doctest::Approx(51.5f)); // toward the 0 past the end
    CHECK(view.linear(0, -0.5) == 0.f);
    CHECK(view.linear(0, 4.0) == 0.f);
  }

  TEST_CASE("sampleBufferReader: pin and read allocate nothing (#1039)") {
    auto b = Counting(2, 256);
    sampleBufferReader reader(b);
    float sum = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 4; ++block) {
        sampleView view = reader.pin();
        for (UInt i = 0; i < view.frames(); ++i)
          sum += view.at(1, i) + view.linear(0, i + 0.5);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(sum > 0.f);
  }

  TEST_CASE("sampleBufferReader: a reader never sees freed or half-made storage (#1039)") {
    // The acceptance test for the reclamation design. One thread plays the
    // audio thread: it pins once per "block" and checks the view is whole
    // (every sample is the block's fill value, or 0 where a grow added
    // samples). One thread edits as fast as it can, one collects. Under ASan
    // a freed storage read is a use-after-free; under TSan a sample written
    // without the publish ordering is a race.
    auto b = sampleBufferRegistry::acquire("race_1039");
    b->setSize(64);
    b->fill(1.f);
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<long> blocks{0};

    std::thread audio([&] {
      sampleBufferReader reader(b);
      while (!stop.load(std::memory_order_relaxed)) {
        sampleView view = reader.pin();
        if (view.empty()) continue;
        const float first = view.at(0, 0);
        for (UInt c = 0; c < view.channels(); ++c) {
          const float* s = view.channel(c);
          for (UInt i = 0; i < view.frames(); ++i) {
            if (s[i] != first && s[i] != 0.f) bad.fetch_add(1, std::memory_order_relaxed);
          }
        }
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });
    std::thread collector([&] {
      while (!stop.load(std::memory_order_relaxed))
        sampleBufferRegistry::collect();
    });

    for (int k = 1; k <= 400; ++k) {
      b->setSize(32 + static_cast<UInt>((k * 37) % 200), 1 + k % 3);
      b->fill(static_cast<float>(k));
    }
    // Let the reader see the last edit before stopping.
    const long seen = blocks.load();
    while (blocks.load() < seen + 4)
      std::this_thread::yield();
    stop = true;
    audio.join();
    collector.join();

    CHECK(bad.load() == 0);
    CHECK(blocks.load() > 0);
    // Every reader is gone, so everything retired can go.
    b->collect();
    CHECK(b->retiredCount() == 0);
  }

} // TEST_SUITE
