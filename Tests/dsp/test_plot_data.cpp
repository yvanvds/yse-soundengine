// Tests for YSE::DSP::plotData (YseEngine/dsp/analysis/plotData.hpp), issue
// #1171. Reference output is closed form: fed the sample index n as the
// signal, frame k starts on sample ceil(k * SAMPLERATE / frameRate) and shows
// the samples from there; a buffer whose sample n is n shows, at point i of a
// region from s for L samples, s + floor(i * L / points).

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
#include "dsp/analysis/plotData.hpp"
#include "dsp/operators/units.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::plotData;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;

  // The whole sample a point reads: a region's floor(i * length / points).
  UInt Step(UInt i, UInt length, UInt points) {
    return i * length / points;
  }

  std::vector<float> Values(const plotData::frame& f, UInt plot = 0) {
    REQUIRE(plot < f.plots);
    return std::vector<float>(f.subplots[plot].values, f.subplots[plot].values + f.points);
  }

  // Feeds `total` samples of the sample index through subplot 0 in blocks of
  // `block`, publishing and acquiring after every block, and returns the
  // first value of every new frame, in order. The caller keeps at most one
  // frame finishing per block, so none is skipped.
  std::vector<float> FrameStarts(plotData& plot, unsigned int total, unsigned int block) {
    std::vector<float> starts;
    UInt serial = plot.acquire().serial;
    unsigned int n = 0;
    while (n < total) {
      const unsigned int length = (total - n) < block ? (total - n) : block;
      buffer in(length);
      for (unsigned int i = 0; i < length; ++i)
        in.getPtr()[i] = static_cast<float>(n + i);
      plot(0, in);
      plot.publish();
      const plotData::frame f = plot.acquire();
      if (f.serial != serial) {
        serial = f.serial;
        starts.push_back(f.subplots[0].values[0]);
      }
      n += length;
    }
    return starts;
  }

  // A buffer whose sample n of channel c is (c + 1) * 1000 + n.
  std::shared_ptr<sampleBuffer> Counting(UInt channels, UInt frames, Flt rate) {
    std::shared_ptr<sampleBuffer> b = sampleBuffer::create(channels, frames, rate);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 1000 + n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("plotData: defaults, clamps and the first frame (#1171)") {
    plotData plot;
    CHECK(plot.plots() == 1);
    CHECK(plot.capacity() == plotData::MAX_POINTS);
    CHECK(plot.points() == 4);
    CHECK(plot.frameSize() == 0);
    CHECK(plot.frameRate() == 15.f);
    CHECK(plot.domain(0).start == 0.f);
    CHECK(plot.domain(0).end == 1.f);
    CHECK(plot.domain(0).scale == plotData::LINEAR);
    CHECK(plot.range(0).start == -1.f);
    CHECK(plot.range(0).end == 1.f);
    CHECK(plot.filter(0) == plotData::NONE);

    // The reader holds the defaults from the start.
    const plotData::frame first = plot.held();
    CHECK(first.serial == 1);
    CHECK(first.plots == 1);
    CHECK(first.points == 4);
    CHECK(first.subplots[0].range.start == -1.f);
    CHECK(Values(first) == std::vector<float>(4, 0.f));
    CHECK(plot.frames() == 1);
    CHECK_FALSE(plot.publish()); // nothing changed
    CHECK(plot.acquire().serial == 1);

    plotData many(99, 1u << 20);
    CHECK(many.plots() == plotData::MAX_PLOTS);
    CHECK(many.capacity() == plotData::MAX_POINTS);
    plotData few(0, 2);
    CHECK(few.plots() == 1);
    CHECK(few.capacity() == 2);
    CHECK(few.points() == 2);
    few.points(0);
    CHECK(few.points() == 1);
    few.points(50);
    CHECK(few.points() == 2);

    CHECK(plot.frameRate(0.f).frameRate() == plotData::MIN_FRAME_RATE);
    CHECK(plot.frameRate(1e6f).frameRate() == plotData::MAX_FRAME_RATE);
    CHECK(plot.frameRate(std::nanf("")).frameRate() == plotData::MAX_FRAME_RATE);
    CHECK(plot.frameSize(1u << 30).frameSize() == plotData::MAX_FRAME_SIZE);

    CHECK(plotData::Points(std::nanf("")) == 4);
    CHECK(plotData::Points(0.f) == 1);
    CHECK(plotData::Points(64.9f) == 64);
    CHECK(plotData::Points(1e9f) == plotData::MAX_POINTS);
    CHECK(plotData::Plots(3.5f) == 3);
    CHECK(plotData::Plots(-2.f) == 1);
    CHECK(plotData::Plots(40.f) == plotData::MAX_PLOTS);
    CHECK(plotData::FrameSize(-1.f) == 0);
    CHECK(plotData::FrameSize(1024.f) == 1024);
  }

  TEST_CASE("plotData: set writes from a point, cut at the capacity; points chooses what "
            "shows (#1171)") {
    plotData plot(2, 8);
    const float list[] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    plot.set(0, list, 6);
    CHECK(plot.publish());
    plotData::frame f = plot.acquire();
    CHECK(f.serial == 2);
    CHECK(Values(f, 0) == std::vector<float>{1.f, 2.f, 3.f, 4.f});
    CHECK(Values(f, 1) == std::vector<float>(4, 0.f));
    // Kept past points(), and shown when points() grows.
    CHECK(plot.value(0, 5) == 6.f);
    plot.points(6);
    plot.publish();
    CHECK(Values(plot.acquire(), 0) == std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f});

    // From a later point, cut at the capacity; NaN is 0, infinity kept.
    const float more[] = {7.f, std::nanf(""), std::numeric_limits<float>::infinity(), 9.f};
    plot.set(1, more, 4, 5);
    plot.publish();
    f = plot.acquire();
    CHECK(Values(f, 1) == std::vector<float>{0.f, 0.f, 0.f, 0.f, 0.f, 7.f});
    CHECK(plot.value(1, 6) == 0.f);
    CHECK(std::isinf(plot.value(1, 7)));
    CHECK(plot.value(1, 8) == 0.f); // past the capacity

    // Out of range, or nothing: no change, no frame.
    plot.set(2, list, 6);
    plot.set(0, list, 0);
    plot.set(0, list, 3, 8);
    CHECK_FALSE(plot.publish());
  }

  TEST_CASE("plotData: axes, origins and the filter reach the frame; atodb filters what is "
            "published (#1171)") {
    plotData plot(2, 16);
    plot.domain(0, 20.f, 20000.f, plotData::LOG)
        .range(0, -60.f, 0.f)
        .xOrigin(0, 1000.f)
        .yOrigin(0, -6.f)
        .filter(0, plotData::ATODB);
    plot.domain(1, std::nanf(""), 8.f); // NaN keeps the start
    plot.range(1, 0.f, std::nanf(""), plotData::LOG);
    plot.xOrigin(1, std::nanf("")); // ignored
    const float amps[] = {1.f, 0.5f, 0.f, -2.f};
    plot.set(0, amps, 4);
    plot.set(1, amps, 4);
    plot.publish();
    const plotData::frame f = plot.acquire();
    const plotData::subplot& a = f.subplots[0];
    CHECK(a.domain.start == 20.f);
    CHECK(a.domain.end == 20000.f);
    CHECK(a.domain.scale == plotData::LOG);
    CHECK(a.range.start == -60.f);
    CHECK(a.range.end == 0.f);
    CHECK(a.range.scale == plotData::LINEAR);
    CHECK(a.xOrigin == 1000.f);
    CHECK(a.yOrigin == -6.f);
    CHECK(a.filter == plotData::ATODB);
    for (UInt i = 0; i < 4; ++i) {
      CAPTURE(i);
      CHECK(a.values[i] == YSE::DSP::AmpToDb(amps[i]));
    }
    CHECK(a.values[0] == doctest::Approx(0.f));
    CHECK(a.values[1] == doctest::Approx(-6.0206f).epsilon(1e-4));
    CHECK(a.values[2] == YSE::DSP::DB_FLOOR);
    CHECK(a.values[3] == doctest::Approx(6.0206f).epsilon(1e-4));
    CHECK(plot.value(0, 1) == 0.5f); // stored as it came

    const plotData::subplot& b = f.subplots[1];
    CHECK(b.domain.start == 0.f);
    CHECK(b.domain.end == 8.f);
    CHECK(b.range.start == 0.f);
    CHECK(b.range.end == 1.f);
    CHECK(b.range.scale == plotData::LOG);
    CHECK(b.xOrigin == 0.f);
    CHECK(Values(f, 1) == std::vector<float>{1.f, 0.5f, 0.f, -2.f});

    // Back to none: the stored values again.
    plot.filter(0, plotData::NONE);
    plot.publish();
    CHECK(Values(plot.acquire(), 0) == std::vector<float>{1.f, 0.5f, 0.f, -2.f});
  }

  TEST_CASE("plotData: signal frames start on ceil(k * rate / fps) and hold the samples "
            "from there, whatever the block split (#1171)") {
    // 1001 Hz at 4 frames a second: a wait of 250.25 samples, exact in
    // binary, so ceil(k * 250.25) is the closed form with no rounding.
    ScopedSampleRate rate(1001);
    const double interval = 1001.0 / 4.0;
    auto expect = [&](unsigned int frame, unsigned int total) {
      std::vector<float> out;
      for (unsigned int k = 0;; ++k) {
        const double at = std::ceil(k * interval);
        if (at + frame > total) break;
        out.push_back(static_cast<float>(at));
      }
      return out;
    };
    for (unsigned int block : {1u, 7u, 13u, 64u, 128u}) {
      CAPTURE(block);
      plotData plot(1, 64);
      plot.frameRate(4.f);
      const std::vector<float> starts = FrameStarts(plot, 3000, block);
      const std::vector<float> expected = expect(4, 3000);
      REQUIRE(expected.size() == 12);
      CHECK(starts == expected);
      const std::vector<float> last = Values(plot.held());
      for (UInt j = 0; j < 4; ++j)
        CHECK(last[j] == expected.back() + static_cast<float>(j));
    }

    // A frame size longer than the points: the frame ends later, it starts
    // on the same samples and shows its first points.
    plotData sized(1, 64);
    sized.frameRate(4.f).frameSize(100);
    CHECK(FrameStarts(sized, 3000, 13) == expect(100, 3000));

    // Longer than the wait: frames back to back, the clock counting from
    // each late frame.
    plotData late(1, 64);
    late.frameRate(4.f).frameSize(300);
    std::vector<float> expected;
    for (unsigned int k = 0; k * 300 + 300 <= 3000; ++k)
      expected.push_back(static_cast<float>(k * 300));
    CHECK(FrameStarts(late, 3000, 13) == expected);
  }

  TEST_CASE("plotData: the wait follows the live sample rate, read when each wait begins "
            "(#1171)") {
    for (UInt sr : {8000u, 22050u, 44100u, 48000u, 96000u}) {
      CAPTURE(sr);
      ScopedSampleRate rate(sr);
      plotData plot;
      const std::vector<float> starts = FrameStarts(plot, sr / 5, N);
      REQUIRE(starts.size() >= 3);
      for (unsigned int k = 0; k < 3; ++k)
        CHECK(starts[k] == static_cast<float>(std::ceil(k * static_cast<double>(sr) / 15.0)));
    }

    // 10 frames a second: frame 1 waits 100 samples at 1000 Hz. The rate
    // changes to 2000 Hz during that wait; frame 2 waits 200.
    ScopedSampleRate rate(1000);
    plotData plot;
    plot.frameRate(10.f);
    std::vector<float> starts = FrameStarts(plot, 50, 10);
    YSE::SAMPLERATE = 2000;
    const std::vector<float> rest = FrameStarts(plot, 400, 10);
    starts.insert(starts.end(), rest.begin(), rest.end());
    // The index restarts at 0 in the second run, which began on sample 50.
    REQUIRE(starts.size() >= 3);
    CHECK(starts[0] == 0.f);
    CHECK(starts[1] + 50.f == 100.f);
    CHECK(starts[2] + 50.f == 300.f);
  }

  TEST_CASE("plotData: NaN samples count as 0; changing the points or the frame size, or "
            "restart, drops the frame being collected (#1171)") {
    ScopedSampleRate rate(1000);
    plotData plot(1, 16);
    plot.frameRate(1000.f); // a wait of one sample: frames back to back
    buffer in(4);
    in.getPtr()[0] = 1.f;
    in.getPtr()[1] = std::nanf("");
    in.getPtr()[2] = -std::numeric_limits<float>::infinity();
    in.getPtr()[3] = 4.f;
    plot(0, in);
    plot.publish();
    std::vector<float> v = Values(plot.acquire());
    CHECK(v[0] == 1.f);
    CHECK(v[1] == 0.f);
    CHECK(std::isinf(v[2]));
    CHECK(v[3] == 4.f);

    // Two samples into a frame of four, the points change: the next frame
    // starts with the next sample.
    auto feed = [&](std::initializer_list<float> samples) {
      buffer b(static_cast<UInt>(samples.size()));
      UInt i = 0;
      for (float s : samples)
        b.getPtr()[i++] = s;
      plot(0, b);
    };
    feed({10.f, 11.f});
    plot.points(3);
    feed({20.f, 21.f, 22.f});
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>{20.f, 21.f, 22.f});
    feed({30.f});
    plot.frameSize(5);
    feed({40.f, 41.f, 42.f, 43.f});
    CHECK_FALSE(plot.publish()); // four of five
    feed({44.f});
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>{40.f, 41.f, 42.f});
    feed({50.f, 51.f});
    plot.restart(0);
    feed({60.f, 61.f, 62.f, 63.f, 64.f});
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>{60.f, 61.f, 62.f});
    // An unchanged setting changes nothing.
    feed({70.f, 71.f});
    plot.points(3).frameSize(5);
    feed({72.f, 73.f, 74.f});
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>{70.f, 71.f, 72.f});
  }

  TEST_CASE("plotData: a buffer frame reads a region, s + floor(i * L / points), on the "
            "frame clock (#1171)") {
    ScopedSampleRate rate(1000);
    std::shared_ptr<sampleBuffer> b = Counting(2, 1000, 2000.f); // 500 ms
    sampleBufferReader reader(b);
    plotData plot(1, 64);
    plot.points(8).frameRate(10.f); // a frame every 100 samples

    // The whole of channel 0.
    {
      const YSE::DSP::sampleView view = reader.pin();
      plot(0, view, N);
    }
    plot.publish();
    std::vector<float> v = Values(plot.acquire());
    for (UInt i = 0; i < 8; ++i)
      CHECK(v[i] == static_cast<float>(1000 + Step(i, 1000, 8)));

    // Channel 1 from 100 ms (sample 200) to 300 ms (600), on a fresh clock
    // whose frames fall on samples 0, 100, 200 ...
    plotData exact(1, 64);
    std::vector<float> region(8);
    for (UInt i = 0; i < 8; ++i)
      region[i] = static_cast<float>(2000 + 200 + Step(i, 400, 8));
    exact.points(8).frameRate(10.f).region(0, 1, 100.f, 300.f);
    CHECK(exact.regionChannel(0) == 1);
    CHECK(exact.regionStart(0) == 100.f);
    CHECK(exact.regionEnd(0) == 300.f);
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 1); // frame 0, on sample 0
    }
    exact.publish();
    CHECK(Values(exact.acquire()) == region);
    b->setSamples(1, 200, std::vector<float>(1, -5.f).data(), 1);
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 99); // samples 1 to 99: no frame
    }
    CHECK_FALSE(exact.publish());
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 1); // sample 100: frame 1
    }
    exact.publish();
    v = Values(exact.acquire());
    CHECK(v[0] == -5.f);
    for (UInt i = 1; i < 8; ++i)
      CHECK(v[i] == region[i]);

    // An end at or before the start reads to the end; a start past the end,
    // a channel the buffer lacks, or no buffer, reads 0.
    exact.region(0, 0, 400.f, 0.f);
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 100);
    }
    exact.publish();
    v = Values(exact.acquire());
    for (UInt i = 0; i < 8; ++i)
      CHECK(v[i] == static_cast<float>(1000 + 800 + Step(i, 200, 8)));
    exact.region(0, 0, 600.f);
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 100);
    }
    exact.publish();
    CHECK(Values(exact.acquire()) == std::vector<float>(8, 0.f));
    exact.region(0, 5);
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 100);
    }
    exact.publish();
    CHECK(Values(exact.acquire()) == std::vector<float>(8, 0.f));
    exact.region(0, 0, -3.f, std::nanf("")); // below 0 and NaN are 0
    CHECK(exact.regionStart(0) == 0.f);
    CHECK(exact.regionEnd(0) == 0.f);
    sampleBufferReader none;
    {
      const YSE::DSP::sampleView view = none.pin();
      exact(0, view, 100);
    }
    exact.publish();
    CHECK(Values(exact.acquire()) == std::vector<float>(8, 0.f));

    // A region shorter than the points repeats samples.
    exact.region(0, 0, 0.f, 2.f); // samples 0 to 3
    {
      const YSE::DSP::sampleView view = reader.pin();
      exact(0, view, 100);
    }
    exact.publish();
    CHECK(Values(exact.acquire()) ==
          std::vector<float>{1000.f, 1000.f, 1001.f, 1001.f, 1002.f, 1002.f, 1003.f, 1003.f});
  }

  TEST_CASE("plotData: switching a subplot from the signal to a buffer drops the signal frame "
            "(#1171)") {
    ScopedSampleRate rate(1000);
    std::shared_ptr<sampleBuffer> b = Counting(1, 100, 1000.f);
    sampleBufferReader reader(b);
    plotData plot(1, 16);
    plot.frameRate(1000.f);
    buffer two(2);
    two = 7.f;
    plot(0, two); // half a frame
    {
      const YSE::DSP::sampleView view = reader.pin();
      plot(0, view, 1); // a buffer frame on its first sample
    }
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>{1000.f, 1025.f, 1050.f, 1075.f});
    buffer four(4);
    four = 3.f;
    plot(0, four); // a whole frame, started afresh
    plot.publish();
    CHECK(Values(plot.acquire()) == std::vector<float>(4, 3.f));
  }

  TEST_CASE("plotData: subplots are independent; out-of-range calls are ignored (#1171)") {
    ScopedSampleRate rate(1000);
    plotData plot(3, 8);
    plot.frameRate(1000.f);
    buffer in(4);
    in = 2.f;
    plot(1, in);
    plot(3, in); // no such subplot
    const float one = 9.f;
    plot.set(2, &one, 1);
    plot.domain(5, 1.f, 2.f).range(5, 1.f, 2.f).xOrigin(5, 1.f).yOrigin(5, 1.f);
    plot.filter(5, plotData::ATODB).region(5, 1);
    plot.restart(5);
    plot.publish();
    const plotData::frame f = plot.acquire();
    CHECK(f.plots == 3);
    CHECK(Values(f, 0) == std::vector<float>(4, 0.f));
    CHECK(Values(f, 1) == std::vector<float>(4, 2.f));
    CHECK(Values(f, 2) == std::vector<float>{9.f, 0.f, 0.f, 0.f});
    CHECK(plot.value(5, 0) == 0.f);
    CHECK(plot.domain(5).end == 1.f);
    CHECK(plot.range(5).start == -1.f);
    CHECK(plot.filter(5) == plotData::NONE);
    CHECK(plot.regionChannel(5) == 0);
  }

  TEST_CASE("plotData: the held frame stays put while newer ones are published (#1171)") {
    plotData plot(1, 4);
    float v = 1.f;
    plot.set(0, &v, 1);
    plot.publish();
    const plotData::frame held = plot.acquire();
    CHECK(held.serial == 2);
    for (int k = 0; k < 10; ++k) {
      v = static_cast<float>(10 + k);
      plot.set(0, &v, 1);
      plot.publish();
    }
    CHECK(held.subplots[0].values[0] == 1.f);
    CHECK(plot.held().serial == 2);
    const plotData::frame newest = plot.acquire();
    CHECK(newest.serial == 12);
    CHECK(newest.subplots[0].values[0] == 19.f);
    CHECK(plot.frames() == 12);
  }

  TEST_CASE("plotData: a reader on another thread only ever sees whole frames (#1171)") {
    // Every frame sets all values of both subplots to its own number: a
    // frame whose values differ was torn.
    plotData plot(2, 64);
    plot.points(64);
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> reads{0};
    std::thread reader([&] {
      while (!done.load(std::memory_order_acquire)) {
        const plotData::frame f = plot.acquire();
        const float first = f.subplots[0].values[0];
        for (UInt p = 0; p < f.plots; ++p)
          for (UInt i = 0; i < f.points; ++i)
            if (f.subplots[p].values[i] != first) torn.fetch_add(1);
        if (f.subplots[1].domain.end != first + 1.f) torn.fetch_add(1);
        reads.fetch_add(1);
      }
    });
    std::vector<float> values(64);
    for (unsigned int k = 1; k <= 5000; ++k) {
      const auto x = static_cast<float>(k);
      for (float& v : values)
        v = x;
      plot.set(0, values.data(), 64).set(1, values.data(), 64);
      plot.domain(1, 0.f, x + 1.f);
      plot.publish();
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(plot.acquire().subplots[0].values[0] == 5000.f);
  }

  TEST_CASE("plotData: setters, blocks, publishing and reading allocate nothing (#1171)") {
    ScopedSampleRate rate(1000);
    std::shared_ptr<sampleBuffer> b = Counting(1, 1000, 1000.f);
    sampleBufferReader reader(b);
    plotData plot(2, 256);
    buffer in(N);
    in = 0.25f;
    std::vector<float> list(256, 0.5f);
    {
      TestHelpers::ProbeScope probe;
      for (int k = 0; k < 8; ++k) {
        plot.points(static_cast<UInt>(16 + k))
            .frameSize(static_cast<UInt>(k * 10))
            .frameRate(100.f);
        plot.set(0, list.data(), 256);
        plot.domain(0, 0.f, 10.f, plotData::LOG).range(0, -1.f, 1.f).xOrigin(0, 1.f);
        plot.yOrigin(0, 0.f).filter(0, plotData::ATODB).region(1, 0, 10.f, 500.f);
        plot(0, in);
        const YSE::DSP::sampleView view = reader.pin();
        plot(1, view, N);
        plot.publish();
        (void)plot.acquire();
        (void)plot.held();
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
