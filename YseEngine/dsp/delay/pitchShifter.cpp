/*
  ==============================================================================

    pitchShifter.cpp
    A real-time pitch shifter: a granular dual delay line for low latency,
    or a phase vocoder for quality (issue #1153).

  ==============================================================================
*/

#include "pitchShifter.hpp"

#include <algorithm>
#include <cmath>

namespace {
  constexpr Dbl TWO_PI = 6.283185307179586476925;

  // The delay line: a power of two above the longest delay a grain reads
  // and aligns over, 6 GOOD_GRAIN / 2 + 2 + GOOD_GRAIN / 8 (from the
  // constant latency at the highest ratio, the search reaching as far
  // again) plus GOOD_GRAIN / 4 compared before it.
  constexpr UInt LINE_SIZE = 8192;
  // The least delay a grain reads at: the Hermite interpolator reads two
  // samples past the read position, and the newest one is the one just
  // written.
  constexpr UInt MARGIN = 2;
  // How far below the best correlation an offset nearer 0 may be and still
  // win: cos 3.6 degrees.
  constexpr Flt ALIGN_TOLERANCE = 0.002f;

  // The k-th offset of the search: 0, 1, -1, 2, -2 ... A larger offset is a
  // longer delay.
  inline Int Offset(UInt k) {
    const auto i = static_cast<Int>(k);
    return (i % 2 == 1) ? (i + 1) / 2 : -(i / 2);
  }

  inline Flt Clamp(Flt ratio) {
    return std::min(std::max(ratio, YSE::DSP::pitchShifter::MINIMUM_RATIO),
                    YSE::DSP::pitchShifter::MAXIMUM_RATIO);
  }
} // namespace

namespace YSE {
  namespace DSP {

    Flt pitchShifter::centsToRatio(Flt cents) {
      if (!std::isfinite(cents)) return 1.f;
      return Clamp(static_cast<Flt>(std::exp2(static_cast<Dbl>(cents) / 1200.0)));
    }

    pitchShifter::vocoder::vocoder(UInt size, UInt overlap)
      : clock(size, size / overlap),
        analysis(size, false, stftWindow::HANNING),
        synthesis(size, overlap, false, stftWindow::HANNING, stftWindow::HANNING),
        shifter(1.f, size / 2 + 1) {
      // A partial moved past Nyquist is dropped, not folded back as an
      // alias.
      shifter.clip(true);
    }

    void pitchShifter::vocoder::reset() {
      clock.reset();
      analysis.reset();
      synthesis.reset();
      shifter.reset();
      frames = 0;
    }

    pitchShifter::pitchShifter(QUALITY quality)
      : mode(quality),
        line(LINE_SIZE, 0.f),
        mask(LINE_SIZE - 1),
        window(GOOD_GRAIN, 0.f),
        scores(GOOD_GRAIN / 4 + 1, 0.f),
        better(2048, 4),
        best(4096, 8),
        result(STANDARD_BUFFERSIZE) {
      for (UInt a = 0; a < GOOD_GRAIN; ++a)
        window[a] = static_cast<Flt>(
            0.5 - 0.5 * std::cos(TWO_PI * static_cast<Dbl>(a) / static_cast<Dbl>(GOOD_GRAIN)));
      result = 0.f;
      reset();
    }

    pitchShifter& pitchShifter::ratio(Flt value) {
      if (std::isfinite(value)) scale = Clamp(value);
      return *this;
    }

    pitchShifter& pitchShifter::cents(Flt value) {
      if (std::isfinite(value)) scale = centsToRatio(value);
      return *this;
    }

    Flt pitchShifter::cents() const {
      return static_cast<Flt>(1200.0 * std::log2(static_cast<Dbl>(scale)));
    }

    pitchShifter& pitchShifter::usesCents(bool on) {
      centsIn = on;
      return *this;
    }

    pitchShifter& pitchShifter::quality(QUALITY value) {
      if (value < BASIC || value > BEST || value == mode) return *this;
      mode = value;
      reset();
      return *this;
    }

    pitchShifter& pitchShifter::constantLatency(bool on) {
      constant = on;
      return *this;
    }

    pitchShifter& pitchShifter::enabled(bool on) {
      if (on && !active) reset();
      active = on;
      return *this;
    }

    UInt pitchShifter::GrainSize() const {
      return mode == GOOD ? GOOD_GRAIN : BASIC_GRAIN;
    }

    UInt pitchShifter::GrainLatency(Flt r) const {
      const Dbl half = static_cast<Dbl>(GrainSize()) / 2.0;
      // The farthest ratio from 1 is MAXIMUM_RATIO, 3 above it.
      // At ratio 1 every grain reads where the other does: nothing to align.
      if (!constant && r == 1.f) return MARGIN;
      const Dbl distance =
          constant ? static_cast<Dbl>(MAXIMUM_RATIO) - 1.0 : std::abs(1.0 - static_cast<Dbl>(r));
      return static_cast<UInt>(std::ceil(distance * half)) + MARGIN + GrainSize() / 8;
    }

    UInt pitchShifter::latency() const {
      if (!active) return 0;
      switch (mode) {
      case BETTER:
        return better.clock.size();
      case BEST:
        return best.clock.size();
      default:
        return GrainLatency(scale);
      }
    }

    pitchShifter& pitchShifter::reset() {
      std::fill(line.begin(), line.end(), 0.f);
      write = 0;
      // The first grain starts with the first sample; the second is already
      // halfway through, at the delay L, so the windows sum to 1 from the
      // start.
      age = 0;
      StartGrain(grains[0], scale, 0, nullptr);
      StartGrain(grains[1], scale, GrainSize() / 2, nullptr);
      fresh = true;
      better.reset();
      best.reset();
      return *this;
    }

    void pitchShifter::StartGrain(grain& g, Flt r, UInt into, const grain* other) {
      const Dbl step = 1.0 - static_cast<Dbl>(r);
      const Dbl half = static_cast<Dbl>(GrainSize()) / 2.0;
      g.step = step;
      g.delay = static_cast<Dbl>(GrainLatency(r)) - step * half + step * static_cast<Dbl>(into);
      if (other != nullptr && step != 0.0)
        g.delay += static_cast<Dbl>(Align(g.delay, other->delay));
    }

    // The offset, -S to S samples, that lines a grain starting at the delay
    // `start` up with the other grain, now at `playing`: the M samples it
    // would have read last are compared with the M the other grain has just
    // read, by their normalised correlation (-1 to 1). Of the offsets within
    // ALIGN_TOLERANCE of the best, the one nearest 0 wins (searched 0, 1,
    // -1, 2, -2 ...), so a periodic sound, which matches as well a period
    // further on, keeps the grains near their nominal delay instead of
    // drifting to one end of the search. Silence: 0.
    Int pitchShifter::Align(Dbl start, Dbl playing) {
      const auto search = static_cast<Int>(GrainSize() / 8);
      const UInt length = GrainSize() / 4;
      const UInt reference = (write + LINE_SIZE - static_cast<UInt>(std::lround(playing))) & mask;
      const UInt nominal = (write + LINE_SIZE - static_cast<UInt>(std::lround(start))) & mask;
      Flt power = 0.f;
      for (UInt j = 0; j < length; ++j) {
        const Flt r = line[(reference - j) & mask];
        power += r * r;
      }
      if (!(power > 0.f)) return 0;

      const auto count = static_cast<UInt>(2 * search + 1);
      Flt best = -2.f;
      for (UInt k = 0; k < count; ++k) {
        const Int offset = Offset(k);
        // `offset` more samples of delay: that many before `nominal`.
        const UInt candidate =
            (nominal + LINE_SIZE + static_cast<UInt>(search) - static_cast<UInt>(offset + search)) &
            mask;
        Flt dot = 0.f;
        Flt energy = 0.f;
        for (UInt j = 0; j < length; ++j) {
          const Flt c = line[(candidate - j) & mask];
          dot += line[(reference - j) & mask] * c;
          energy += c * c;
        }
        scores[k] = energy > 0.f ? dot / std::sqrt(energy * power) : 0.f;
        best = std::max(best, scores[k]);
      }
      for (UInt k = 0; k < count; ++k)
        if (scores[k] >= best - ALIGN_TOLERANCE) return Offset(k);
      return 0;
    }

    // The delay line `delay` samples before the sample just written, 4-point
    // Hermite: exact at a whole delay.
    Flt pitchShifter::Read(Dbl delay) const {
      const Dbl position = static_cast<Dbl>(write) + static_cast<Dbl>(LINE_SIZE) - delay;
      const Dbl whole = std::floor(position);
      const Flt f = static_cast<Flt>(position - whole);
      const UInt i = static_cast<UInt>(whole);
      const Flt ym = line[(i - 1) & mask];
      const Flt y0 = line[i & mask];
      const Flt y1 = line[(i + 1) & mask];
      const Flt y2 = line[(i + 2) & mask];
      const Flt c1 = 0.5f * (y1 - ym);
      const Flt c2 = ym - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
      const Flt c3 = 0.5f * (y2 - ym) + 1.5f * (y0 - y1);
      return ((c3 * f + c2) * f + c1) * f + y0;
    }

    Flt pitchShifter::Shift(Flt sample) const {
      if (!std::isfinite(sample)) return scale;
      return centsIn ? centsToRatio(sample) : Clamp(sample);
    }

    void pitchShifter::Granular(const Flt* in, const Flt* shift, Flt* out, UInt length) {
      const UInt size = GrainSize();
      const UInt half = size / 2;
      const UInt stride = GOOD_GRAIN / size;
      for (UInt n = 0; n < length; ++n) {
        line[write] = std::isfinite(in[n]) ? in[n] : 0.f;
        const Flt r = shift != nullptr ? Shift(shift[n]) : scale;
        // The settings made since reset() count from the very first sample:
        // the grain already halfway is placed again.
        if (fresh) StartGrain(grains[1], r, half, nullptr);
        fresh = false;
        if (age == 0) StartGrain(grains[0], r, 0, &grains[1]);
        if (age == half) StartGrain(grains[1], r, 0, &grains[0]);
        const Flt a = Read(grains[0].delay);
        const Flt b = Read(grains[1].delay);
        // The second grain's window is 1 minus the first's: at ratio 1 the
        // two reads are equal and this is exactly that sample.
        out[n] = b + window[age * stride] * (a - b);
        grains[0].delay += grains[0].step;
        grains[1].delay += grains[1].step;
        if (++age == size) age = 0;
        write = (write + 1) & mask;
      }
    }

    void pitchShifter::Vocode(vocoder& pv, const Flt* in, const Flt* shift, Flt* out, UInt length) {
      const UInt bins = pv.analysis.bins();
      UInt i = 0;
      while (i < length) {
        const UInt run = std::min(length - i, pv.clock.untilFrame());
        pv.analysis.write(in + i, run);
        pv.synthesis.read(out + i, run);
        i += run;
        if (!pv.clock.advance(run)) continue;

        // A frame completed at sample i - 1: it takes that sample's ratio.
        pv.analysis.analyse();
        pv.shifter.ratio(shift != nullptr ? Shift(shift[i - 1]) : scale);
        stftFrame frame;
        frame.firstBin = 0;
        frame.count = bins;
        frame.bins = bins;
        frame.size = pv.clock.size();
        frame.hop = pv.clock.interval();
        frame.fullSpectrum = false;
        frame.index = pv.frames;
        pv.shifter(frame, pv.analysis.real(), pv.analysis.imaginary(), pv.synthesis.real(),
                   pv.synthesis.imaginary());
        pv.synthesis.synthesise();
        pv.frames++;
      }
    }

    buffer& pitchShifter::Process(buffer& in, const Flt* shift, UInt length) {
      // Within the capacity set in the constructor: no allocation.
      if (result.getLength() != length) result.resize(length);
      const Flt* x = in.getPtr();
      Flt* y = result.getPtr();
      if (!active) {
        std::copy(x, x + length, y);
      } else if (mode == BETTER) {
        Vocode(better, x, shift, y, length);
      } else if (mode == BEST) {
        Vocode(best, x, shift, y, length);
      } else {
        Granular(x, shift, y, length);
      }
      if (shift != nullptr) {
        for (UInt n = length; n > 0; --n) {
          if (std::isfinite(shift[n - 1])) {
            scale = Shift(shift[n - 1]);
            break;
          }
        }
      }
      return result;
    }

    buffer& pitchShifter::operator()(buffer& in) {
      return Process(in, nullptr, std::min(in.getLength(), STANDARD_BUFFERSIZE));
    }

    buffer& pitchShifter::operator()(buffer& in, buffer& shift) {
      const UInt shorter = std::min(in.getLength(), shift.getLength());
      return Process(in, shift.getPtr(), std::min(shorter, STANDARD_BUFFERSIZE));
    }

  } // namespace DSP
} // namespace YSE
