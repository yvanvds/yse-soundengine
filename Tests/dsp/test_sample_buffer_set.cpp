// Tests for YSE::DSP::sampleBufferSet (YseEngine/dsp/sampling/), issue
// #1139: the numbered collection of named buffers behind ~polybuffer.
//
// The fixtures are float WAV files written by sampleBuffer::write, so what a
// set reads back is exact: file k holds frames whose sample n is 100 k + n.

#include <doctest/doctest.h>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/sampleBufferSet.hpp"
#include "headers/constants.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::SAMPLE_FILE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::sampleBufferSet;

namespace {

  namespace fs = std::filesystem;

  // A fresh, empty folder under the temp directory, removed again when the
  // test ends.
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

  // A float WAV at `rate` with `channels` channels of `frames` frames: sample
  // n of channel c is 100 k + n + 1000 c.
  void WriteFixture(const std::string& path, int k, UInt frames, UInt channels = 1,
                    float rate = 1000.f) {
    auto b = sampleBuffer::create(channels, frames, rate);
    std::vector<float> values(frames);
    for (UInt c = 0; c < channels; ++c) {
      for (UInt n = 0; n < frames; ++n)
        values[n] =
            100.f * static_cast<float>(k) + static_cast<float>(n) + 1000.f * static_cast<float>(c);
      b->setSamples(c, 0, values.data(), frames);
    }
    REQUIRE(b->write(path.c_str()));
  }

  std::vector<float> Channel(const sampleBuffer& b, UInt c = 0) {
    std::vector<float> out(b.frames());
    out.resize(b.getSamples(c, 0, out.data(), b.frames()));
    return out;
  }

  // What the fixture of `k` with `frames` frames holds in channel `c`.
  std::vector<float> Expected(int k, UInt frames, UInt c = 0) {
    std::vector<float> out(frames);
    for (UInt n = 0; n < frames; ++n)
      out[n] =
          100.f * static_cast<float>(k) + static_cast<float>(n) + 1000.f * static_cast<float>(c);
    return out;
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── naming and lifetime ──────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: sets are shared by name and live while held (#1139)") {
    bool created = false;
    auto a = sampleBufferSet::acquire("set_share_1139", &created);
    CHECK(created);
    CHECK(a->name() == "set_share_1139");
    auto b = sampleBufferSet::acquire("set_share_1139", &created);
    CHECK_FALSE(created);
    CHECK(a == b);
    CHECK(sampleBufferSet::find("set_share_1139") == a);
    CHECK(sampleBufferSet::findById(a->id()) == a);
    CHECK(a->count() == 0);
    CHECK(a->bufferName(3) == "set_share_1139.3");
    CHECK(sampleBufferSet::bufferName("x", 12) == "x.12");

    const std::uint64_t id = a->id();
    a.reset();
    b.reset();
    CHECK(sampleBufferSet::find("set_share_1139") == nullptr);
    CHECK(sampleBufferSet::findById(id) == nullptr);
    // The name is free again: a new set.
    auto c = sampleBufferSet::acquire("set_share_1139", &created);
    CHECK(created);
    CHECK(c->id() != id);
  }

  TEST_CASE("sampleBufferSet: no name is a set with a name of its own (#1139)") {
    bool created = false;
    auto a = sampleBufferSet::acquire("", &created);
    CHECK(created);
    auto b = sampleBufferSet::acquire("");
    CHECK(a != b);
    CHECK(a->name().rfind("polybuffer-", 0) == 0);
    CHECK(b->name().rfind("polybuffer-", 0) == 0);
    CHECK(a->name() != b->name());
    CHECK(sampleBufferSet::find(a->name()) == a);
    a->appendEmpty(10.f);
    CHECK(sampleBufferRegistry::find(a->name() + ".1") == a->buffer(1));
  }

  // ─── append ───────────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: append reads files into name.1, name.2 ... (#1139)") {
    TempFolder dir("yse_set_append_1139");
    WriteFixture(dir / "kick.wav", 1, 40);
    WriteFixture(dir / "snare.wav", 2, 25, 2, 22050.f);

    auto set = sampleBufferSet::acquire("set_append_1139");
    const std::uint64_t before = set->version();
    auto one = set->append((dir / "kick.wav").c_str());
    auto two = set->append((dir / "snare.wav").c_str());
    REQUIRE(one != nullptr);
    REQUIRE(two != nullptr);
    CHECK(set->count() == 2);
    CHECK(set->version() == before + 2);
    CHECK(set->buffer(1) == one);
    CHECK(set->buffer(2) == two);
    CHECK(set->buffer(0) == nullptr);
    CHECK(set->buffer(3) == nullptr);
    CHECK(set->buffers() == std::vector<std::shared_ptr<sampleBuffer>>{one, two});

    // Ordinary named buffers: what a ~play set_append_1139.2 finds.
    CHECK(one->name() == "set_append_1139.1");
    CHECK(sampleBufferRegistry::find("set_append_1139.2") == two);

    // The file's shape, rate and samples.
    CHECK(one->channels() == 1);
    CHECK(one->sampleRate() == 1000.f);
    CHECK(Channel(*one) == Expected(1, 40));
    CHECK(two->channels() == 2);
    CHECK(two->sampleRate() == 22050.f);
    CHECK(Channel(*two, 1) == Expected(2, 25, 1));

    CHECK(set->shortName(1) == "kick");
    CHECK(set->shortName(2) == "snare");
    CHECK(set->shortName(3).empty());
    CHECK(set->memoryBytes() == (40 + 25 * 2) * sizeof(float));
  }

  TEST_CASE("sampleBufferSet: a failed append changes nothing (#1139)") {
    auto set = sampleBufferSet::acquire("set_fail_1139");
    const std::uint64_t before = set->version();
    CHECK(set->append("no_such_file_1139.wav") == nullptr);
    CHECK(set->append("") == nullptr);
    CHECK(set->append(nullptr) == nullptr);
    CHECK(set->count() == 0);
    CHECK(set->version() == before);
    // The name it tried is not left held.
    CHECK(sampleBufferRegistry::find("set_fail_1139.1") == nullptr);
  }

  TEST_CASE("sampleBufferSet: a buffer named before the set fills it is the one filled "
            "(#1139)") {
    TempFolder dir("yse_set_held_1139");
    WriteFixture(dir / "a.wav", 3, 12);
    // A player made first holds the name.
    auto held = sampleBufferRegistry::acquire("set_held_1139.1");
    auto set = sampleBufferSet::acquire("set_held_1139");
    REQUIRE(set->append((dir / "a.wav").c_str()) == held);
    CHECK(Channel(*held) == Expected(3, 12));
  }

  // ─── appendEmpty ──────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: appendEmpty sizes at the live engine rate (#1139)") {
    auto set = sampleBufferSet::acquire("set_empty_1139");
    {
      ScopedSampleRate rate(48000);
      auto b = set->appendEmpty(10.f, 2);
      CHECK(b->frames() == 480);
      CHECK(b->channels() == 2);
      CHECK(b->sampleRate() == 48000.f);
      CHECK(Channel(*b, 1) == std::vector<float>(480, 0.f));
      CHECK_FALSE(b->info().hasFile());
    }
    {
      ScopedSampleRate rate(22050);
      auto b = set->appendEmpty(10.f);
      CHECK(b->frames() == 221); // 220.5 rounded
      CHECK(b->channels() == 1);
      // It follows the engine rate.
      CHECK(b->sampleRate() == 22050.f);
      CHECK(set->buffer(1)->sampleRate() == 22050.f);
    }
    CHECK(set->appendEmpty(-5.f, 0)->frames() == 0);
    CHECK(set->buffer(3)->channels() == 1);
    CHECK(set->count() == 3);
  }

  TEST_CASE("sampleBufferSet: appendEmpty forgets what a buffer held elsewhere had (#1139)") {
    TempFolder dir("yse_set_forget_1139");
    WriteFixture(dir / "a.wav", 1, 30);
    auto held = sampleBufferRegistry::acquire("set_forget_1139.1");
    REQUIRE(held->read((dir / "a.wav").c_str(), 0.f, -1.f, 0));
    REQUIRE(held->info().hasFile());
    ScopedSampleRate rate(1000);
    auto set = sampleBufferSet::acquire("set_forget_1139");
    REQUIRE(set->appendEmpty(5.f) == held);
    CHECK(held->frames() == 5);
    CHECK(Channel(*held) == std::vector<float>(5, 0.f));
    CHECK_FALSE(held->info().hasFile());
    CHECK(set->shortName(1).empty());
  }

  // ─── folders ──────────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: readFolder reads the sound files in natural order (#1139)") {
    TempFolder dir("yse_set_folder_1139");
    WriteFixture(dir / "hit10.wav", 10, 5);
    WriteFixture(dir / "hit2.wav", 2, 5);
    WriteFixture(dir / "Hit1.wav", 1, 5);
    WriteFixture(dir / "hit02b.wav", 3, 5); // 2 as a number, then 'b'
    WriteFixture(dir / ".hidden.wav", 9, 5);
    {
      std::ofstream text(dir / "notes.txt");
      text << "not a sound file";
    }
    fs::create_directories(dir.path / "sub");
    WriteFixture(dir / "sub/inner.wav", 8, 5);

    auto set = sampleBufferSet::acquire("set_folder_1139");
    CHECK(set->readFolder(dir.path.string().c_str()) == 4);
    REQUIRE(set->count() == 4);
    CHECK(set->shortName(1) == "Hit1");
    CHECK(set->shortName(2) == "hit2");
    CHECK(set->shortName(3) == "hit02b");
    CHECK(set->shortName(4) == "hit10");
    CHECK(Channel(*set->buffer(4)) == Expected(10, 5));

    // A second folder goes after the first.
    TempFolder more("yse_set_folder_more_1139");
    WriteFixture(more / "z.wav", 5, 3);
    CHECK(set->readFolder(more.path.string().c_str()) == 1);
    CHECK(set->bufferName(5) == "set_folder_1139.5");
    CHECK(sampleBufferRegistry::find("set_folder_1139.5") == set->buffer(5));

    CHECK(set->readFolder("no_such_folder_1139") == 0);
    CHECK(set->readFolder("") == 0);
    CHECK(set->count() == 5);
  }

  TEST_CASE("sampleBufferSet: writeToFolder writes every buffer under its short name "
            "(#1139)") {
    TempFolder dir("yse_set_write_1139");
    TempFolder second("yse_set_write_b_1139");
    WriteFixture(dir / "kick.wav", 1, 8);
    WriteFixture(second / "kick.wav", 2, 6);
    ScopedSampleRate rate(1000);
    auto set = sampleBufferSet::acquire("set_write_1139");
    REQUIRE(set->append((dir / "kick.wav").c_str()) != nullptr);
    REQUIRE(set->append((second / "kick.wav").c_str()) != nullptr); // the same short name
    set->appendEmpty(4.f); // no file: its buffer name
    set->appendEmpty(0.f); // empty: skipped

    TempFolder out("yse_set_write_out_1139");
    const fs::path target = out.path / "new";
    CHECK(set->writeToFolder(target.string().c_str()) == 3);
    CHECK(fs::exists(target / "kick.wav"));
    CHECK(fs::exists(target / "set_write_1139.2.wav"));
    CHECK(fs::exists(target / "set_write_1139.3.wav"));
    CHECK_FALSE(fs::exists(target / "set_write_1139.4.wav"));

    // Read back, they hold what the buffers held.
    auto back = sampleBufferSet::acquire("set_write_back_1139");
    CHECK(back->readFolder(target.string().c_str()) == 3);
    CHECK(back->shortName(1) == "kick");
    CHECK(Channel(*back->buffer(1)) == Expected(1, 8));
    CHECK(Channel(*back->buffer(2)) == Expected(2, 6));
    CHECK(back->buffer(3)->frames() == 4);

    CHECK(set->writeToFolder((out.path / "aiff").string().c_str(), SAMPLE_FILE::AIFF) == 3);
    CHECK(fs::exists(out.path / "aiff" / "kick.aif"));
  }

  // ─── clear ────────────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: clear empties and lets go of every buffer (#1139)") {
    TempFolder dir("yse_set_clear_1139");
    WriteFixture(dir / "a.wav", 1, 10);
    auto set = sampleBufferSet::acquire("set_clear_1139");
    REQUIRE(set->append((dir / "a.wav").c_str()) != nullptr);
    set->appendEmpty(10.f, 2);
    // A player holding the first one.
    auto player = sampleBufferRegistry::find("set_clear_1139.1");
    REQUIRE(player != nullptr);
    const std::uint64_t before = set->version();

    set->clear();
    CHECK(set->count() == 0);
    CHECK(set->version() == before + 1);
    CHECK(set->buffer(1) == nullptr);
    CHECK(set->memoryBytes() == 0);
    // The player's buffer is empty, its file forgotten.
    CHECK(player->frames() == 0);
    CHECK_FALSE(player->info().hasFile());
    // Nobody else held the second: its name is free.
    CHECK(sampleBufferRegistry::find("set_clear_1139.2") == nullptr);

    // Numbering starts again, into the buffer the player holds.
    CHECK(set->append((dir / "a.wav").c_str()) == player);
    CHECK(Channel(*player) == Expected(1, 10));
  }

  // ─── threads ──────────────────────────────────────────────────────────────

  TEST_CASE("sampleBufferSet: queries while another thread adds and clears (#1139)") {
    auto set = sampleBufferSet::acquire("set_threads_1139");
    std::atomic<bool> stop{false};
    std::thread writer([&] {
      for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 8; ++i)
          set->appendEmpty(1.f, 1);
        set->clear();
      }
      stop = true;
    });
    std::size_t seen = 0;
    while (!stop) {
      const auto list = set->buffers();
      for (const auto& b : list)
        REQUIRE(b != nullptr);
      seen += list.size();
      CHECK(set->count() <= 8);
      (void)set->memoryBytes();
      (void)set->buffer(1);
    }
    writer.join();
    CHECK(set->count() == 0);
    (void)seen;
  }

} // TEST_SUITE("dsp")
