#include "spectrumAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include "../fourier/mayer.h"

namespace {
  UInt PowerOfTwoAtLeast(UInt value, UInt lowest, UInt highest) {
    UInt p = lowest;
    while (p < value && p < highest)
      p <<= 1;
    return p;
  }

  Flt HeldInterval(Flt ms) {
    using YSE::DSP::spectrumAnalyzer;
    if (ms < spectrumAnalyzer::MIN_INTERVAL) return spectrumAnalyzer::MIN_INTERVAL;
    return ms > spectrumAnalyzer::MAX_INTERVAL ? spectrumAnalyzer::MAX_INTERVAL : ms;
  }
} // namespace

namespace YSE {
  namespace DSP {

    spectrumAnalyzer::spectrumAnalyzer(UInt capacity)
      : cap(PowerOfTwoAtLeast(capacity, MIN_SIZE, MAX_SIZE)),
        history(cap, 0.f),
        shape(cap, 0.f),
        re(cap, 0.f),
        im(cap, 0.f),
        store(static_cast<std::size_t>(cap) * 3, 0.f),
        n(DEFAULT_SIZE < cap ? DEFAULT_SIZE : cap),
        intervalMs(DEFAULT_INTERVAL),
        what(AMPLITUDE),
        windowSum(0.0),
        pos(0),
        left(-1.0),
        elapsed(0.0),
        capturing(false),
        spectralSize(0),
        spectralBins(0),
        serial(0),
        writeSlot(0),
        published(1),
        finished(0),
        readSlot(2) {
      stftWindow::fill(stftWindow::HANNING, shape.data(), n);
      for (UInt k = 0; k < n; ++k)
        windowSum += static_cast<Dbl>(shape[k]);
    }

    Flt* spectrumAnalyzer::values(unsigned int s) {
      return store.data() + static_cast<std::size_t>(s) * cap;
    }

    const Flt* spectrumAnalyzer::values(unsigned int s) const {
      return store.data() + static_cast<std::size_t>(s) * cap;
    }

    spectrumAnalyzer& spectrumAnalyzer::size(UInt samples) {
      const UInt wanted = PowerOfTwoAtLeast(samples, MIN_SIZE, cap);
      if (wanted == n) return *this;
      n = wanted;
      // Within the capacity: refills the table, allocates nothing.
      stftWindow::fill(stftWindow::HANNING, shape.data(), n);
      windowSum = 0.0;
      for (UInt k = 0; k < n; ++k)
        windowSum += static_cast<Dbl>(shape[k]);
      return *this;
    }

    UInt spectrumAnalyzer::size() const {
      return n;
    }

    spectrumAnalyzer& spectrumAnalyzer::interval(Flt ms) {
      if (!std::isnan(ms)) intervalMs = HeldInterval(ms);
      return *this;
    }

    Flt spectrumAnalyzer::interval() const {
      return intervalMs;
    }

    spectrumAnalyzer& spectrumAnalyzer::mode(MODE value) {
      what = value;
      return *this;
    }

    spectrumAnalyzer::MODE spectrumAnalyzer::mode() const {
      return what;
    }

    UInt spectrumAnalyzer::capacity() const {
      return cap;
    }

    spectrumAnalyzer& spectrumAnalyzer::reset() {
      std::fill(history.begin(), history.end(), 0.f);
      pos = 0;
      left = -1.0;
      elapsed = 0.0;
      capturing = false;
      return *this;
    }

    // The interval in samples at the live rate, at least one.
    Dbl spectrumAnalyzer::wait() const {
      const Dbl rate = SAMPLERATE > 0 ? static_cast<Dbl>(SAMPLERATE) : 1.0;
      const Dbl samples = static_cast<Dbl>(intervalMs) * rate / 1000.0;
      return samples < 1.0 ? 1.0 : samples;
    }

    void spectrumAnalyzer::operator()(buffer& in) {
      capturing = false;
      const Flt* p = in.getPtr();
      const UInt length = in.getLength();
      const UInt mask = cap - 1;
      for (UInt i = 0; i < length; i++) {
        const Flt x = p[i];
        history[pos] = std::isfinite(x) ? x : 0.f;
        pos = (pos + 1) & mask;
        if (left < 0.0) left = wait();
        left -= 1.0;
        if (left <= 0.0) {
          analyse();
          left += wait();
        }
      }
    }

    void spectrumAnalyzer::analyse() {
      // The latest n samples, the oldest first.
      const UInt mask = cap - 1;
      const UInt start = (pos + cap - n) & mask;
      for (UInt k = 0; k < n; ++k) {
        re[k] = history[(start + k) & mask] * shape[k];
        im[k] = 0.f;
      }
      mayer_fft(static_cast<int>(n), re.data(), im.data());

      const UInt bins = n / 2 + 1;
      Flt* out = values(writeSlot);
      if (what == PHASE) {
        for (UInt k = 0; k < bins; ++k)
          out[k] = std::atan2(im[k], re[k]);
      } else {
        const Flt gain = windowSum > 0.0 ? static_cast<Flt>(2.0 / windowSum) : 0.f;
        for (UInt k = 0; k < bins; ++k)
          out[k] = std::hypot(re[k], im[k]) * gain;
        out[0] *= 0.5f;
        out[bins - 1] *= 0.5f;
      }
      publish(n, bins);
    }

    void spectrumAnalyzer::operator()(const stftFrame& f) {
      if (f.firstBin == 0) {
        left = -1.0;
        const Dbl interval = wait();
        elapsed += static_cast<Dbl>(f.hop);
        capturing = elapsed >= interval;
        if (capturing) elapsed = std::fmod(elapsed, interval);
        spectralSize = f.size;
        spectralBins = f.bins < cap ? f.bins : cap;
      }
      if (!capturing || f.real == nullptr || f.imaginary == nullptr) return;
      if (f.firstBin >= spectralBins) return;

      const UInt count = std::min(f.count, spectralBins - f.firstBin);
      Flt* out = values(writeSlot) + f.firstBin;
      if (what == PHASE) {
        for (UInt i = 0; i < count; ++i)
          out[i] = std::atan2(f.imaginary[i], f.real[i]);
      } else {
        const Flt gain = spectralSize > 0 ? 2.f / static_cast<Flt>(spectralSize) : 0.f;
        const UInt nyquist = spectralSize / 2;
        for (UInt i = 0; i < count; ++i) {
          const UInt bin = f.firstBin + i;
          const Flt scale = (bin == 0 || bin == nyquist) ? 0.5f * gain : gain;
          out[i] = std::hypot(f.real[i], f.imaginary[i]) * scale;
        }
      }
      if (f.firstBin + count >= spectralBins) {
        capturing = false;
        publish(spectralSize, spectralBins);
      }
    }

    void spectrumAnalyzer::publish(UInt fftSize, UInt bins) {
      slot& s = slots[writeSlot];
      s.mode = what;
      s.size = fftSize;
      s.bins = bins;
      s.serial = ++serial;
      s.rate = SAMPLERATE;
      // Hand the frame over and take back the slot the reader let go of.
      writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
      finished.store(serial, std::memory_order_release);
    }

    UInt spectrumAnalyzer::frames() const {
      return finished.load(std::memory_order_acquire);
    }

    spectrumAnalyzer::frame spectrumAnalyzer::acquire() {
      if ((published.load(std::memory_order_acquire) & FRESH) != 0)
        readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
      return held();
    }

    spectrumAnalyzer::frame spectrumAnalyzer::held() const {
      frame f;
      const slot& s = slots[readSlot];
      if (s.serial == 0) return f;
      f.mode = s.mode;
      f.size = s.size;
      f.bins = s.bins;
      f.serial = s.serial;
      f.rate = s.rate;
      f.values = values(readSlot);
      return f;
    }

    UInt spectrumAnalyzer::Size(Flt value) {
      if (std::isnan(value)) return DEFAULT_SIZE;
      if (value <= static_cast<Flt>(MIN_SIZE)) return MIN_SIZE;
      if (value >= static_cast<Flt>(MAX_SIZE)) return MAX_SIZE;
      return PowerOfTwoAtLeast(static_cast<UInt>(value), MIN_SIZE, MAX_SIZE);
    }

    spectrumAnalyzer::MODE spectrumAnalyzer::Mode(Flt value) {
      if (std::isnan(value)) return AMPLITUDE;
      return std::trunc(value) == 1.f ? PHASE : AMPLITUDE;
    }

  } // namespace DSP
} // namespace YSE
