// Tests for YSE::DSP::click (YseEngine/dsp/generators/click.hpp), issue
// #1058. The output has a closed form: silence, except the stored waveform
// (times the scale) starting at each trigger.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/click.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::click;

namespace {

  // `waveform` times `scale` from sample `at`, silence elsewhere, `length`
  // samples long.
  std::vector<float> Expected(const std::vector<float>& waveform, float scale, unsigned int at,
                              unsigned int length) {
    std::vector<float> out(length, 0.f);
    for (unsigned int i = 0; i < waveform.size() && at + i < length; ++i)
      out[at + i] = waveform[i] * scale;
    return out;
  }

  void CheckBlock(buffer& block, const std::vector<float>& expected) {
    REQUIRE(block.getLength() == expected.size());
    for (unsigned int i = 0; i < expected.size(); ++i) {
      if (block.getPtr()[i] != expected[i]) {
        CAPTURE(i);
        CHECK(block.getPtr()[i] == expected[i]);
        return;
      }
    }
  }

  bool Silent(buffer& block) {
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != 0.f) return false;
    return true;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("click: silent until triggered (#1058)") {
    click c;
    CHECK(c.length() == 1);
    CHECK(c.value(0) == 1.f);
    CHECK(Silent(c()));
    CHECK(c().getLength() == YSE::STANDARD_BUFFERSIZE);
  }

  TEST_CASE("click: a trigger is a unit impulse at the start of the next block (#1058)") {
    click c;
    c.trigger();
    CheckBlock(c(), Expected({1.f}, 1.f, 0, YSE::STANDARD_BUFFERSIZE));
    CHECK(Silent(c()));

    // Scaled, and several triggers before a block are one, the last scale.
    c.trigger(0.25f).trigger(-0.5f);
    CheckBlock(c(), Expected({1.f}, -0.5f, 0, YSE::STANDARD_BUFFERSIZE));
    CHECK(Silent(c()));

    // A non-finite scale plays silence.
    c.trigger(std::numeric_limits<float>::quiet_NaN());
    CHECK(Silent(c()));
  }

  TEST_CASE("click: a stored waveform plays once, scaled, across block edges (#1058)") {
    click c;
    const std::vector<float> wave = {1.f, 0.75f, 0.5f, 0.25f, 0.125f};
    c.set(wave.data(), static_cast<UInt>(wave.size()));
    CHECK(c.length() == 5);
    CHECK(c.value(4) == 0.125f);
    CHECK(c.value(5) == 0.f);

    c.trigger(2.f);
    CheckBlock(c(3), Expected(wave, 2.f, 0, 3));
    // The rest of the waveform starts the next block.
    CheckBlock(c(4), {0.5f, 0.25f, 0.f, 0.f});
    CHECK(Silent(c(8)));
  }

  TEST_CASE("click: a retrigger restarts the waveform (#1058)") {
    click c;
    const std::vector<float> wave = {1.f, 0.5f, 0.25f, 0.125f};
    c.set(wave.data(), 4);
    c.trigger();
    CheckBlock(c(2), {1.f, 0.5f});
    c.trigger();
    CheckBlock(c(6), {1.f, 0.5f, 0.25f, 0.125f, 0.f, 0.f});
  }

  TEST_CASE("click: set keeps at most 256 values, 0 values is the unit impulse (#1058)") {
    click c;
    std::vector<float> wave(300);
    for (unsigned int i = 0; i < wave.size(); ++i)
      wave[i] = static_cast<float>(i + 1) / 300.f;
    c.set(wave.data(), static_cast<UInt>(wave.size()));
    CHECK(c.length() == click::MAX_LENGTH);
    c.trigger();
    std::vector<float> played;
    for (int b = 0; b < 4; ++b) {
      buffer& out = c();
      played.insert(played.end(), out.getPtr(), out.getPtr() + out.getLength());
    }
    for (unsigned int i = 0; i < played.size(); ++i) {
      const float expected = i < click::MAX_LENGTH ? wave[i] : 0.f;
      if (played[i] != expected) {
        CAPTURE(i);
        CHECK(played[i] == expected);
        break;
      }
    }

    c.set(nullptr, 0);
    CHECK(c.length() == 1);
    CHECK(c.value(0) == 1.f);
    c.trigger();
    CheckBlock(c(), Expected({1.f}, 1.f, 0, YSE::STANDARD_BUFFERSIZE));

    // A finished waveform stays silent when a longer one is set, and a new
    // generator given a waveform does not start playing it.
    const float longer[] = {1.f, 0.5f, 0.25f};
    c.set(longer, 3);
    CHECK(Silent(c()));
    click fresh;
    fresh.set(longer, 3);
    CHECK(Silent(fresh()));

    // A value that is not a finite number is stored as 0.
    const float odd[] = {0.5f, std::numeric_limits<float>::infinity(), 0.25f};
    c.set(odd, 3);
    CHECK(c.value(1) == 0.f);
  }

  TEST_CASE("click: a trigger signal starts the waveform at each rise from 0 (#1058)") {
    click c;
    const std::vector<float> wave = {1.f, 0.5f};
    c.set(wave.data(), 2);
    buffer triggers(16);
    triggers = 0.f;
    triggers.getPtr()[3] = 1.f; // rise
    triggers.getPtr()[4] = 0.7f; // still not 0: no new trigger
    triggers.getPtr()[9] = -2.f; // a rise to a negative value counts too
    triggers.getPtr()[15] = 1.f; // rise at the last sample
    std::vector<float> expected(16, 0.f);
    expected[3] = 1.f;
    expected[4] = 0.5f;
    expected[9] = 1.f;
    expected[10] = 0.5f;
    expected[15] = 1.f;
    CheckBlock(c(triggers), expected);

    // The last sample carries over: the waveform runs on, and a signal that
    // starts non-zero after a non-zero sample does not trigger.
    buffer held(4);
    held = 1.f;
    CheckBlock(c(held), {0.5f, 0.f, 0.f, 0.f});

    // A signal trigger is unscaled, and a pending trigger lands on sample 0.
    held = 0.f;
    held.getPtr()[2] = 1.f;
    c.trigger(3.f);
    CheckBlock(c(held), {3.f, 1.5f, 1.f, 0.5f});
  }

  TEST_CASE("click: reset stops the waveform and forgets the trigger history (#1058)") {
    click c;
    const std::vector<float> wave = {1.f, 0.5f, 0.25f};
    c.set(wave.data(), 3);
    buffer high(2);
    high = 1.f;
    CheckBlock(c(high), {1.f, 0.5f});
    c.reset();
    // The signal's last sample is forgotten, so a held 1 is a rise again.
    CheckBlock(c(high), {1.f, 0.5f});
    c.trigger();
    c.reset();
    CHECK(Silent(c()));
  }

  TEST_CASE("click: the same samples at any sample rate (#1058)") {
    const std::vector<float> wave = {1.f, -1.f, 0.5f};
    for (unsigned int rate : {22050u, 44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      click c;
      c.set(wave.data(), 3);
      c.trigger();
      CheckBlock(c(), Expected(wave, 1.f, 0, YSE::STANDARD_BUFFERSIZE));
    }
  }

  TEST_CASE("click: processing allocates nothing (#1058)") {
    click c;
    std::vector<float> wave(click::MAX_LENGTH, 0.5f);
    buffer triggers(YSE::STANDARD_BUFFERSIZE);
    triggers = 0.f;
    triggers.getPtr()[10] = 1.f;
    c();
    {
      TestHelpers::ProbeScope probe;
      c.set(wave.data(), static_cast<UInt>(wave.size()));
      c.trigger(0.5f);
      c();
      c(32);
      c(triggers);
      c.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
