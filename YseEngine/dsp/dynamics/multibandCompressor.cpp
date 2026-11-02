#include "multibandCompressor.hpp"

#include <algorithm>
#include <cmath>

namespace {
  inline Flt DbToLin(Flt dB) {
    return std::pow(10.f, dB / 20.f);
  }

  inline Flt Gain(Flt dB) {
    return std::clamp(dB, YSE::DSP::multibandCompressor::MIN_GAIN_DB,
                      YSE::DSP::multibandCompressor::MAX_GAIN_DB);
  }
} // namespace

namespace YSE {
  namespace DSP {

    multibandCompressor::multibandCompressor(UInt bands, UInt channels)
      : bandCount(std::clamp(bands, MIN_BANDS, MAX_BANDS)),
        channelCount(std::clamp<UInt>(channels, 1, MAX_CHANNELS)) {
      for (UInt k = 0; k + 1 < MAX_BANDS; ++k)
        frequency[k] = DefaultCrossover(bandCount, k);
      for (UInt b = 0; b < MAX_BANDS; ++b) {
        driveDb[b] = 0.f;
        levelDb[b] = 0.f;
        reduction[b] = 0.f;
      }

      // Band b runs through the allpass of crossovers b + 1 .. bands - 2.
      for (UInt b = 0; b < bandCount; ++b) {
        allpassStart[b] = allpassesPerChannel;
        if (b + 2 < bandCount) allpassesPerChannel += bandCount - 2 - b;
      }
      splits.resize(static_cast<std::size_t>(channelCount) * (bandCount - 1));
      allpasses.resize(static_cast<std::size_t>(channelCount) * allpassesPerChannel);

      for (UInt b = 0; b < bandCount; ++b) {
        signal[b].resize(channelCount);
        for (DSP::buffer& s : signal[b])
          s.resize(STANDARD_BUFFERSIZE);
        // The compressor sizes its scratch on the first block it sees: a
        // silent one here (its gain stays at 1) keeps that off the audio
        // thread. Shorter blocks later fit in what it has.
        compressors[b].process(signal[b]);
      }
    }

    Flt multibandCompressor::DefaultCrossover(UInt bands, UInt index) {
      static constexpr Flt TWO[] = {1000.f};
      static constexpr Flt THREE[] = {300.f, 3000.f};
      static constexpr Flt FOUR[] = {200.f, 1000.f, 5000.f};
      static constexpr Flt FIVE[] = {120.f, 500.f, 2000.f, 8000.f};
      if (bands < MIN_BANDS || bands > MAX_BANDS || index + 1 >= bands) return 0.f;
      switch (bands) {
      case 2:
        return TWO[index];
      case 3:
        return THREE[index];
      case 4:
        return FOUR[index];
      default:
        return FIVE[index];
      }
    }

    multibandCompressor& multibandCompressor::crossover(UInt index, Flt hz) {
      if (index + 1 < bandCount && std::isfinite(hz)) frequency[index] = std::max(hz, 0.f);
      return *this;
    }

    Flt multibandCompressor::crossover(UInt index) const {
      return index + 1 < bandCount ? frequency[index].load() : 0.f;
    }

    multibandCompressor& multibandCompressor::drive(UInt band, Flt dB) {
      if (band < bandCount && std::isfinite(dB)) driveDb[band] = Gain(dB);
      return *this;
    }

    Flt multibandCompressor::drive(UInt band) const {
      return band < bandCount ? driveDb[band].load() : 0.f;
    }

    multibandCompressor& multibandCompressor::level(UInt band, Flt dB) {
      if (band < bandCount && std::isfinite(dB)) levelDb[band] = Gain(dB);
      return *this;
    }

    Flt multibandCompressor::level(UInt band) const {
      return band < bandCount ? levelDb[band].load() : 0.f;
    }

    multibandCompressor& multibandCompressor::output(Flt dB) {
      if (std::isfinite(dB)) outputDb = Gain(dB);
      return *this;
    }

    multibandCompressor& multibandCompressor::threshold(UInt band, Flt dB) {
      if (band < bandCount && std::isfinite(dB)) compressors[band].threshold(dB);
      return *this;
    }

    Flt multibandCompressor::threshold(UInt band) {
      return band < bandCount ? compressors[band].threshold() : 0.f;
    }

    multibandCompressor& multibandCompressor::ratio(UInt band, Flt value) {
      if (band < bandCount && std::isfinite(value)) compressors[band].ratio(value);
      return *this;
    }

    Flt multibandCompressor::ratio(UInt band) {
      return band < bandCount ? compressors[band].ratio() : 1.f;
    }

    multibandCompressor& multibandCompressor::attack(UInt band, Flt ms) {
      if (band < bandCount && std::isfinite(ms)) compressors[band].attack(ms);
      return *this;
    }

    Flt multibandCompressor::attack(UInt band) {
      return band < bandCount ? compressors[band].attack() : 0.f;
    }

    multibandCompressor& multibandCompressor::release(UInt band, Flt ms) {
      if (band < bandCount && std::isfinite(ms)) compressors[band].release(ms);
      return *this;
    }

    Flt multibandCompressor::release(UInt band) {
      return band < bandCount ? compressors[band].release() : 0.f;
    }

    multibandCompressor& multibandCompressor::makeup(UInt band, Flt dB) {
      if (band < bandCount && std::isfinite(dB)) compressors[band].makeup(dB);
      return *this;
    }

    Flt multibandCompressor::makeup(UInt band) {
      return band < bandCount ? compressors[band].makeup() : 0.f;
    }

    multibandCompressor& multibandCompressor::detector(MODULES::compressorDetector value) {
      if (value < MODULES::DETECT_PEAK || value >= MODULES::DETECT_COUNT_) return *this;
      for (UInt b = 0; b < bandCount; ++b)
        compressors[b].detector(value);
      return *this;
    }

    MODULES::compressorDetector multibandCompressor::detector() {
      return compressors[0].detector();
    }

    multibandCompressor& multibandCompressor::bypass(bool on) {
      bypassOn = on;
      return *this;
    }

    Flt multibandCompressor::gainReduction(UInt band) const {
      return band < bandCount ? reduction[band].load() : 0.f;
    }

    buffer& multibandCompressor::band(UInt band, UInt channel) {
      if (band >= bandCount || channel >= channelCount) return signal[0][0];
      return signal[band][channel];
    }

    DSP::crossover& multibandCompressor::Allpass(UInt channel, UInt band, UInt index) {
      return allpasses[static_cast<std::size_t>(channel) * allpassesPerChannel +
                       allpassStart[band] + index - band - 1];
    }

    // Splits `length` samples of every channel, from `offset`, into the
    // bands.
    void multibandCompressor::Split(MULTICHANNELBUFFER& buffer, UInt offset, UInt length) {
      const UInt last = bandCount - 1;
      for (UInt c = 0; c < channelCount; ++c) {
        DSP::crossover* split = &splits[static_cast<std::size_t>(c) * last];
        const Flt* in = buffer[c].getPtr() + offset;
        for (UInt i = 0; i < length; ++i) {
          Flt rest = std::isfinite(in[i]) ? in[i] : 0.f;
          for (UInt k = 0; k < last; ++k) {
            const DSP::crossover::outputs o = split[k].process(rest);
            signal[k][c].getPtr()[i] = o.lowpass;
            rest = o.highpass;
          }
          signal[last][c].getPtr()[i] = rest;
          // A band that left the cascade early gets the phase of the later
          // crossovers, so the bands add up flat.
          for (UInt b = 0; b + 2 < bandCount; ++b) {
            Flt v = signal[b][c].getPtr()[i];
            for (UInt k = b + 1; k < last; ++k) {
              const DSP::crossover::outputs o = Allpass(c, b, k).process(v);
              v = o.lowpass + o.highpass;
            }
            signal[b][c].getPtr()[i] = v;
          }
        }
      }
    }

    // Adds the bands, at their levels, into `buffer` from `offset`, then the
    // output gain.
    void multibandCompressor::Mix(MULTICHANNELBUFFER& buffer, UInt offset, UInt length) {
      std::array<Flt, MAX_BANDS> gains{};
      for (UInt b = 0; b < bandCount; ++b)
        gains[b] = DbToLin(levelDb[b].load());
      const Flt out = DbToLin(outputDb.load());
      for (UInt c = 0; c < channelCount; ++c) {
        Flt* dst = buffer[c].getPtr() + offset;
        for (UInt i = 0; i < length; ++i) {
          Flt sum = 0.f;
          for (UInt b = 0; b < bandCount; ++b)
            sum += signal[b][c].getPtr()[i] * gains[b];
          dst[i] = sum * out;
        }
      }
    }

    void multibandCompressor::process(MULTICHANNELBUFFER& buffer) {
      if (buffer.size() < channelCount) return;
      const UInt length = buffer[0].getLength();
      for (UInt c = 1; c < channelCount; ++c)
        if (buffer[c].getLength() != length) return;

      // The crossover frequencies, once per call; each crossover derives its
      // coefficients again only when its frequency or the sample rate moved.
      const UInt last = bandCount - 1;
      for (UInt k = 0; k < last; ++k) {
        const Flt hz = frequency[k].load();
        for (UInt c = 0; c < channelCount; ++c) {
          splits[static_cast<std::size_t>(c) * last + k].cutoff(hz);
          for (UInt b = 0; b < k; ++b)
            Allpass(c, b, k).cutoff(hz);
        }
      }

      std::array<Flt, MAX_BANDS> deepest{};
      const bool through = bypassOn.load();
      for (UInt offset = 0; offset < length;) {
        const UInt piece = std::min<UInt>(length - offset, STANDARD_BUFFERSIZE);
        // Within the capacity set in the constructor: no allocation.
        for (UInt b = 0; b < bandCount; ++b)
          for (DSP::buffer& s : signal[b])
            if (s.getLength() != piece) s.resize(piece);

        Split(buffer, offset, piece);
        for (UInt b = 0; b < bandCount; ++b) {
          const Flt drive = DbToLin(driveDb[b].load());
          if (drive != 1.f)
            for (DSP::buffer& s : signal[b])
              s *= drive;
          compressors[b].process(signal[b]);
          deepest[b] = std::min(deepest[b], compressors[b].gainReductionDb());
        }
        if (!through) Mix(buffer, offset, piece);
        offset += piece;
      }
      for (UInt b = 0; b < bandCount; ++b)
        reduction[b] = deepest[b];
    }

  } // namespace DSP
} // namespace YSE
