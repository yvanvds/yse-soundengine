// Tests for YSE::DSP::firFilter (YseEngine/dsp/sampling/firFilter.hpp),
// issue #1149: a FIR filter whose taps are part of a sample buffer, what
// ~buffir does. The impulse response is the taps, a convolution against a
// direct sum, closed-form frequency responses, the offset, size and channel
// with their limits, the history across blocks and size changes, clear, a
// buffer edit, a constant input, block lengths, the sample rate, and
// allocation-free blocks. The view is a sampleBufferReader's, tested in
// test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/firFilter.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::firFilter;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleView;

namespace {

  constexpr double PI = 3.14159265358979323846;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  std::shared_ptr<sampleBuffer> Taps(std::initializer_list<float> values) {
    auto b = sampleBuffer::create(1, static_cast<UInt>(values.size()));
    std::vector<float> v(values);
    b->setSamples(0, 0, v.data(), static_cast<UInt>(v.size()));
    return b;
  }

  // Frame n of channel c is (c + 1) * 10 * n + 1: channel 0 is 10 * n + 1.
  std::shared_ptr<sampleBuffer> Tens(UInt frames, UInt channels = 1) {
    auto b = sampleBuffer::create(channels, frames);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> v(frames);
      for (UInt n = 0; n < frames; ++n)
        v[n] = static_cast<float>((c + 1) * 10 * n + 1);
      b->setSamples(c, 0, v.data(), frames);
    }
    return b;
  }

  // A block holding 1 at sample `at` and 0 elsewhere.
  buffer Impulse(UInt at = 0, UInt length = STANDARD_BUFFERSIZE) {
    buffer out(length);
    out = 0.f;
    out.getPtr()[at] = 1.f;
    return out;
  }

  buffer Silence(UInt length = STANDARD_BUFFERSIZE) {
    buffer out(length);
    out = 0.f;
    return out;
  }

  // One block of `in` through `fir`: the output samples.
  const Flt* Run(firFilter& fir, const sampleView& view, buffer in) {
    return fir(view, in).getPtr();
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("firFilter: an impulse gives the taps back, in buffer order (#1149)") {
    auto b = Taps({0.5f, -0.25f, 1.f, 0.125f, 2.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    firFilter fir;
    CHECK(fir.size() == 0);
    CHECK(fir.offset() == 0);
    CHECK(fir.channel() == 0);
    fir.size(5);
    buffer in = Impulse(3);
    const Flt* out = fir(view, in).getPtr();
    CHECK(fir.taps() == 5);
    CHECK(out[0] == 0.f);
    CHECK(out[2] == 0.f);
    CHECK(out[3] == 0.5f);
    CHECK(out[4] == -0.25f);
    CHECK(out[5] == 1.f);
    CHECK(out[6] == 0.125f);
    CHECK(out[7] == 2.f);
    CHECK(out[8] == 0.f);
    CHECK(out[STANDARD_BUFFERSIZE - 1] == 0.f);

    // Offset 2, size 2: the taps are frames 2 and 3.
    fir.clear().offset(2).size(2);
    out = fir(view, in).getPtr();
    CHECK(fir.taps() == 2);
    CHECK(out[3] == 1.f);
    CHECK(out[4] == 0.125f);
    CHECK(out[5] == 0.f);
  }

  TEST_CASE("firFilter: a block is the direct-form convolution sum (#1149)") {
    constexpr UInt TAPS = 37;
    auto b = sampleBuffer::create(1, TAPS);
    std::vector<float> taps(TAPS);
    for (UInt k = 0; k < TAPS; ++k)
      taps[k] = static_cast<float>(std::sin(0.7 * k) / (1.0 + k));
    b->setSamples(0, 0, taps.data(), TAPS);
    sampleBufferReader reader(b);
    firFilter fir;
    fir.size(TAPS);

    std::vector<double> x;
    buffer in(STANDARD_BUFFERSIZE);
    for (int block = 0; block < 3; ++block) {
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const double v = std::cos(0.05 * x.size()) + 0.3 * std::sin(1.9 * x.size());
        in.getPtr()[i] = static_cast<float>(v);
        x.push_back(static_cast<float>(v));
      }
      sampleView view = reader.pin();
      const Flt* out = fir(view, in).getPtr();
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const std::size_t n = x.size() - STANDARD_BUFFERSIZE + i;
        double expected = 0.0;
        for (UInt k = 0; k < TAPS && k <= n; ++k)
          expected += static_cast<double>(taps[k]) * x[n - k];
        CAPTURE(n);
        CHECK(std::fabs(static_cast<double>(out[i]) - expected) <= 1e-5);
      }
    }
  }

  TEST_CASE("firFilter: a moving average has the closed-form response (#1149)") {
    // N taps of 1/N on a sine of frequency w: once the history is full the
    // output is the sine scaled by sin(N w / 2) / (N sin(w / 2)) and delayed
    // by (N - 1) / 2 samples.
    constexpr UInt N = 8;
    auto b = sampleBuffer::create(1, N);
    b->fill(1.f / N);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    for (double w : {0.1, 0.5, 2.0 * PI / N, PI}) {
      CAPTURE(w);
      firFilter fir;
      fir.size(N);
      buffer in(STANDARD_BUFFERSIZE);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
        in.getPtr()[i] = static_cast<float>(std::sin(w * i));
      const Flt* out = fir(view, in).getPtr();
      const double gain = std::sin(N * w / 2.0) / (N * std::sin(w / 2.0));
      for (UInt i = N; i < STANDARD_BUFFERSIZE; ++i) {
        CAPTURE(i);
        const double expected = gain * std::sin(w * (i - (N - 1) / 2.0));
        CHECK(std::fabs(static_cast<double>(out[i]) - expected) <= 2e-6);
      }
    }
  }

  TEST_CASE("firFilter: the size is held to MAX_TAPS and stops at the buffer's end (#1149)") {
    auto b = sampleBuffer::create(1, firFilter::MAX_TAPS + 100);
    b->fill(1.f);
    sampleBufferReader reader(b);
    firFilter fir;
    fir.size(firFilter::MAX_TAPS + 1);
    CHECK(fir.size() == firFilter::MAX_TAPS);
    fir.size(-3);
    CHECK(fir.size() == 0);
    fir.offset(-3);
    CHECK(fir.offset() == 0);

    // All 4096 taps: an impulse is heard for 4096 samples, then not.
    fir.size(1000000);
    UInt heard = 0;
    UInt sample = 0;
    buffer first = Impulse(0);
    buffer quiet = Silence();
    for (int block = 0; sample < firFilter::MAX_TAPS + 2 * STANDARD_BUFFERSIZE; ++block) {
      sampleView view = reader.pin();
      const Flt* out = fir(view, block == 0 ? first : quiet).getPtr();
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i, ++sample) {
        if (out[i] == 1.f) {
          heard++;
        } else {
          CAPTURE(sample);
          CHECK(out[i] == 0.f);
          CHECK(sample >= static_cast<UInt>(firFilter::MAX_TAPS));
        }
      }
    }
    CHECK(heard == static_cast<UInt>(firFilter::MAX_TAPS));

    // A size past the buffer's end stops at its last frame.
    auto shortBuffer = Tens(6);
    sampleBufferReader shortReader(shortBuffer);
    sampleView view = shortReader.pin();
    firFilter cut;
    cut.offset(4).size(100);
    const Flt* out = Run(cut, view, Impulse(0));
    CHECK(cut.taps() == 2);
    CHECK(out[0] == 41.f);
    CHECK(out[1] == 51.f);
    CHECK(out[2] == 0.f);
  }

  TEST_CASE("firFilter: no taps is silence (#1149)") {
    auto b = Tens(8, 2);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    buffer in(STANDARD_BUFFERSIZE);
    in = 1.f;

    firFilter fir; // size 0, as in Max
    CHECK(fir(view, in).getPtr()[5] == 0.f);
    CHECK(fir.taps() == 0);
    fir.size(4).offset(8); // offset at the end
    CHECK(fir(view, in).getPtr()[5] == 0.f);
    fir.offset(0).channel(2); // a channel the buffer lacks
    CHECK(fir(view, in).getPtr()[5] == 0.f);
    fir.channel(1);
    CHECK(fir(view, in).getPtr()[5] == 1.f + 21.f + 41.f + 61.f);

    sampleBufferReader unbound;
    sampleView none = unbound.pin();
    CHECK(fir(none, in).getPtr()[5] == 0.f);
    auto empty = sampleBuffer::create(1, 0);
    sampleBufferReader emptyReader(empty);
    sampleView emptyView = emptyReader.pin();
    CHECK(fir(emptyView, in).getPtr()[5] == 0.f);
  }

  TEST_CASE("firFilter: the history runs across blocks and size changes; clear empties it "
            "(#1149)") {
    // Tap k is 10 k + 1, 700 taps: an impulse near the end of one block
    // rings into the next two.
    auto b = Tens(700);
    sampleBufferReader reader(b);
    firFilter fir;
    fir.size(700);
    const UInt at = STANDARD_BUFFERSIZE - 3;
    {
      sampleView view = reader.pin();
      const Flt* out = Run(fir, view, Impulse(at));
      CHECK(out[at] == 1.f);
      CHECK(out[at + 2] == 21.f);
    }
    {
      sampleView view = reader.pin();
      const Flt* out = Run(fir, view, Silence());
      CHECK(out[0] == 31.f); // tap 3
      CHECK(out[100] == 1031.f);
    }

    // Size 1 hides the impulse; growing the size brings it back where it
    // would have been, as the history is kept whatever the size.
    fir.size(1);
    {
      sampleView view = reader.pin();
      CHECK(Run(fir, view, Silence())[0] == 0.f);
    }
    fir.size(700);
    {
      sampleView view = reader.pin();
      // Sample 0 of the fourth block is 3 + 2 * STANDARD_BUFFERSIZE after it.
      const UInt k = 3 + 2 * STANDARD_BUFFERSIZE;
      REQUIRE(k < 700);
      CHECK(Run(fir, view, Silence())[0] == static_cast<float>(10 * k + 1));
    }

    // clear: nothing left to ring.
    fir.clear().size(700);
    {
      sampleView view = reader.pin();
      const Flt* out = Run(fir, view, Silence());
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
        CHECK(out[i] == 0.f);
    }
  }

  TEST_CASE("firFilter: a buffer edit is heard from the next block (#1149)") {
    auto b = Taps({1.f, 0.f, 0.f});
    sampleBufferReader reader(b);
    firFilter fir;
    fir.size(3);
    buffer in(STANDARD_BUFFERSIZE);
    in = 1.f;
    {
      sampleView view = reader.pin();
      CHECK(fir(view, in).getPtr()[10] == 1.f);
    }
    const float edit[3] = {0.5f, 0.5f, 2.f};
    b->setSamples(0, 0, edit, 3);
    sampleView view = reader.pin();
    CHECK(fir(view, in).getPtr()[10] == 3.f);
  }

  TEST_CASE("firFilter: a constant input, and block lengths (#1149)") {
    auto b = Taps({1.f, 2.f, 3.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    firFilter fir;
    fir.size(3);
    // The step response: the running sum of the taps.
    buffer& step = fir(view, 1.f, 10);
    REQUIRE(step.getLength() == 10);
    CHECK(step.getPtr()[0] == 1.f);
    CHECK(step.getPtr()[1] == 3.f);
    CHECK(step.getPtr()[2] == 6.f);
    CHECK(step.getPtr()[9] == 6.f);
    CHECK(fir(view, 0.f).getLength() == STANDARD_BUFFERSIZE);
    CHECK(fir.out().getPtr()[0] == 5.f); // two 1s still in the history
    CHECK(fir.out().getPtr()[2] == 0.f);
    // A longer input is cut to STANDARD_BUFFERSIZE.
    buffer longer(STANDARD_BUFFERSIZE * 2);
    longer = 2.f;
    CHECK(fir(view, longer).getLength() == STANDARD_BUFFERSIZE);
    CHECK(fir.out().getPtr()[STANDARD_BUFFERSIZE - 1] == 12.f);
  }

  TEST_CASE("firFilter: no state follows the sample rate (#1149)") {
    SampleRateGuard guard;
    auto b = Tens(9);
    sampleBufferReader reader(b);
    firFilter fir;
    fir.offset(1).size(4);
    YSE::SAMPLERATE = 22050;
    {
      sampleView view = reader.pin();
      CHECK(Run(fir, view, Impulse(0))[3] == 41.f);
    }
    YSE::SAMPLERATE = 96000;
    fir.clear();
    sampleView view = reader.pin();
    CHECK(Run(fir, view, Impulse(0))[3] == 41.f);
  }

  TEST_CASE("firFilter: processing allocates nothing (#1149)") {
    auto b = Tens(firFilter::MAX_TAPS + 10, 2);
    sampleBufferReader reader(b);
    firFilter fir;
    buffer in(STANDARD_BUFFERSIZE);
    buffer shorter(16);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
      in.getPtr()[i] = static_cast<float>(i % 5) * 0.5f - 1.f;
    shorter = 0.5f;
    float last = 0.f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 6; ++block) {
        sampleView view = reader.pin();
        fir.channel(static_cast<UInt>(block % 2)).offset(block * 3).size(1000 * block);
        fir(view, in);
        fir(view, shorter); // down and back up, within capacity
        fir(view, 0.25f);
        fir.clear();
        last += fir.out().getPtr()[0];
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(std::isfinite(last));
  }

} // TEST_SUITE
