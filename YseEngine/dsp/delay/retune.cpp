/*
  ==============================================================================

    retune.cpp
    Pitch detection and correction toward a scale or a set of notes, built
    from DSP::pitchTracker and DSP::pitchShifter (issue #1154).

  ==============================================================================
*/

#include "retune.hpp"
#include "../../music/scale/scaleInterface.hpp"

#include <algorithm>
#include <cmath>

namespace {
  constexpr Dbl OCTAVE = 1200.0;
  // Two pitch classes of a YSE::scale this close, in cents, are one.
  constexpr Dbl SAME_CLASS = 0.01;
} // namespace

namespace YSE {
  namespace DSP {

    // ---------------------------------------------------------------- tuning

    Flt tuning::ratioToCents(Flt ratio) {
      if (!std::isfinite(ratio) || !(ratio > 0.f)) return 0.f;
      return static_cast<Flt>(OCTAVE * std::log2(static_cast<Dbl>(ratio)));
    }

    tuning::tuning() {
      chromatic();
    }

    tuning& tuning::standardPitch(Flt hz) {
      if (std::isfinite(hz) && hz > 0.f) standard = hz;
      return *this;
    }

    tuning& tuning::chromatic() {
      for (UInt i = 0; i < 12; ++i)
        degrees[i] = static_cast<Flt>(100 * i);
      count = 12;
      mask = ~U64(0);
      return *this;
    }

    tuning& tuning::notes(const Flt* cents, UInt values) {
      degrees[0] = 0.f;
      count = 1;
      if (cents == nullptr) return *this;
      for (UInt i = 0; i < values && count < MAX_NOTES; ++i) {
        const Flt c = cents[i];
        if (std::isfinite(c) && c > 0.f && c < static_cast<Flt>(OCTAVE)) degrees[count++] = c;
      }
      return *this;
    }

    tuning& tuning::base(Int semitone) {
      baseNote = ((semitone % 12) + 12) % 12;
      return *this;
    }

    tuning& tuning::enabled(U64 value) {
      mask = value;
      return *this;
    }

    tuning& tuning::enable(UInt index, bool on) {
      if (index >= MAX_NOTES) return *this;
      const U64 bit = U64(1) << index;
      mask = on ? (mask | bit) : (mask & ~bit);
      return *this;
    }

    bool tuning::isEnabled(UInt index) const {
      return index < count && (mask & (U64(1) << index)) != 0;
    }

    tuning& tuning::enableAll() {
      mask = ~U64(0);
      return *this;
    }

    tuning& tuning::midiNote(Int note, Int velocity) {
      const auto size = static_cast<Int>(count);
      const Int index = (((note - baseNote) % size) + size) % size;
      return enable(static_cast<UInt>(index), velocity > 0);
    }

    tuning& tuning::scale(const YSE::scale& source) {
      // The classes in cents above C, sorted and without doubles, in place:
      // degrees[0] stays C.
      degrees[0] = 0.f;
      count = 1;
      baseNote = 0;
      mask = 0;
      for (const Flt pitch : source.pitches) {
        if (!std::isfinite(pitch)) continue;
        Dbl c = std::fmod(static_cast<Dbl>(pitch) * 100.0, OCTAVE);
        if (c < 0.0) c += OCTAVE;
        if (c >= OCTAVE - SAME_CLASS) c = 0.0;
        if (c < SAME_CLASS) {
          mask |= 1;
          continue;
        }
        const auto value = static_cast<Flt>(c);
        UInt at = 1;
        while (at < count && degrees[at] < value - static_cast<Flt>(SAME_CLASS))
          at++;
        if (at < count && std::abs(degrees[at] - value) <= static_cast<Flt>(SAME_CLASS)) continue;
        if (count == MAX_NOTES) continue;
        for (UInt j = count; j > at; --j)
          degrees[j] = degrees[j - 1];
        // The enabled bits from `at` on move up with their degrees.
        const U64 low = mask & ((U64(1) << at) - 1);
        const U64 high = at + 1 < MAX_NOTES ? (mask >> at) << (at + 1) : 0;
        mask = low | high | (U64(1) << at);
        degrees[at] = value;
        count++;
      }
      return *this;
    }

    bool tuning::nearest(Flt cents, Flt& target) const {
      if (!std::isfinite(cents)) return false;
      const Dbl x = static_cast<Dbl>(cents);
      const Dbl origin = 100.0 * static_cast<Dbl>(baseNote - 9);
      bool found = false;
      Dbl distance = 0.0;
      Dbl best = 0.0;
      for (UInt i = 0; i < count; ++i) {
        if ((mask & (U64(1) << i)) == 0) continue;
        // The degree's octave nearest x, and the one below on a tie.
        const Dbl d = x - (origin + static_cast<Dbl>(degrees[i]));
        const Dbl r = d - OCTAVE * std::ceil(d / OCTAVE - 0.5);
        const Dbl a = std::abs(r);
        const Dbl t = x - r;
        if (!found || a < distance || (a == distance && t < best)) {
          found = true;
          distance = a;
          best = t;
        }
      }
      if (found) target = static_cast<Flt>(best);
      return found;
    }

    // ---------------------------------------------------------------- retune

    retune::retune(pitchShifter::QUALITY quality)
      : shifter(quality), cents(STANDARD_BUFFERSIZE), detected(STANDARD_BUFFERSIZE) {
      detector.period(DEFAULT_PERIOD).tolerance(DEFAULT_AMBIENCE);
      shifter.usesCents(true);
      cents = 0.f;
      detected = 0.f;
    }

    retune& retune::notes(const tuning& value) {
      scaleNotes = value;
      return *this;
    }

    retune& retune::amount(Flt percent) {
      if (std::isfinite(percent)) percentage = std::min(std::max(percent, 0.f), 100.f);
      return *this;
    }

    retune& retune::threshold(Flt value) {
      if (std::isfinite(value)) tolerance = std::max(value, 0.f);
      return *this;
    }

    retune& retune::ambience(Flt value) {
      if (std::isfinite(value)) detector.tolerance(std::min(std::max(value, 0.f), 1.f));
      return *this;
    }

    Flt retune::ambience() const {
      return detector.tolerance();
    }

    retune& retune::bypass(Flt percent) {
      if (std::isfinite(percent)) sibilanceLimit = percent;
      return *this;
    }

    retune& retune::period(UInt samples) {
      detector.period(samples == 0 ? DEFAULT_PERIOD : samples);
      return *this;
    }

    UInt retune::period() const {
      return detector.period();
    }

    retune& retune::quality(pitchShifter::QUALITY value) {
      shifter.quality(value);
      return *this;
    }

    retune& retune::enabled(bool on) {
      active = on;
      shifter.enabled(on);
      return *this;
    }

    retune& retune::pitchDetection(bool on) {
      detecting = on;
      return *this;
    }

    retune& retune::reset() {
      detector.reset();
      shifter.reset();
      heard = 0.f;
      sibilant = 0.f;
      shift = 0.f;
      crossings = 0;
      counted = 0;
      negative = false;
      return *this;
    }

    void retune::Analysed() {
      sibilant =
          counted > 0 ? 100.f * static_cast<Flt>(crossings) / static_cast<Flt>(counted) : 0.f;
      crossings = 0;
      counted = 0;
      heard = detector.voiced() ? detector.frequency() : 0.f;
    }

    Flt retune::Correct(Flt lockHz) const {
      if (!(heard > 0.f) || sibilant > sibilanceLimit || !(percentage > 0.f)) return 0.f;
      const Dbl standard = static_cast<Dbl>(scaleNotes.standardPitch());
      const auto x = static_cast<Flt>(OCTAVE * std::log2(static_cast<Dbl>(heard) / standard));
      Flt target = 0.f;
      if (std::isfinite(lockHz) && lockHz > 0.f) {
        target = static_cast<Flt>(OCTAVE * std::log2(static_cast<Dbl>(lockHz) / standard));
      } else if (!scaleNotes.nearest(x, target)) {
        return 0.f;
      }
      const Flt deviation = target - x;
      if (std::abs(deviation) <= tolerance) return 0.f;
      return deviation * percentage / 100.f;
    }

    buffer& retune::Process(buffer& in, const Flt* lock, const Flt* detune, UInt length) {
      // Within the capacity set in the constructor: no allocation.
      if (cents.getLength() != length) cents.resize(length);
      if (detected.getLength() != length) detected.resize(length);
      const Flt* x = in.getPtr();
      Flt* c = cents.getPtr();
      Flt* p = detected.getPtr();
      const bool track = active || detecting;
      for (UInt i = 0; i < length; i += CHUNK) {
        const UInt run = std::min(CHUNK, length - i);
        if (track) {
          for (UInt n = 0; n < run; ++n) {
            const Flt v = x[i + n];
            if (!std::isfinite(v) || v == 0.f) continue;
            const bool below = v < 0.f;
            if (below != negative) crossings++;
            negative = below;
          }
          counted += run;
          if (detector(x + i, run) > 0) Analysed();
        }
        shift = Correct(lock != nullptr ? lock[i] : 0.f);
        for (UInt n = 0; n < run; ++n) {
          const Flt d = detune != nullptr ? detune[i + n] : 0.f;
          c[i + n] = shift + (std::isfinite(d) ? d : 0.f);
          p[i + n] = detecting ? heard : 0.f;
        }
      }
      return shifter(in, cents);
    }

    buffer& retune::operator()(buffer& in) {
      return Process(in, nullptr, nullptr, std::min(in.getLength(), STANDARD_BUFFERSIZE));
    }

    buffer& retune::operator()(buffer& in, buffer& lock, buffer& detune) {
      const UInt shortest = std::min({in.getLength(), lock.getLength(), detune.getLength()});
      return Process(in, lock.getPtr(), detune.getPtr(), std::min(shortest, STANDARD_BUFFERSIZE));
    }

  } // namespace DSP
} // namespace YSE
