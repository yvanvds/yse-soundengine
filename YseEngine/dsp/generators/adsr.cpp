#include "adsr.hpp"

#include <cmath>

namespace {
  // Samples for a time in ms at the live rate; 0 for no time.
  inline Dbl Samples(Flt ms) {
    if (!(ms > 0.f) || !std::isfinite(ms) || YSE::SAMPLERATE <= 0) return 0.0;
    return static_cast<Dbl>(ms) * static_cast<Dbl>(YSE::SAMPLERATE) / 1000.0;
  }

  inline Flt CleanTime(Flt ms) {
    return (std::isfinite(ms) && ms > 0.f) ? ms : 0.f;
  }

  inline Flt CleanLevel(Flt factor) {
    return std::isfinite(factor) ? factor : 0.f;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // A segment whose progress is this close to 1 is done: the sum of N steps
  // of 1/N can fall a rounding error short of it.
  constexpr Dbl Done = 1.0 - 1e-9;
} // namespace

namespace YSE {
  namespace DSP {

    adsr::adsr()
      : attackMs(0.f),
        decayMs(0.f),
        sustainLevel(1.f),
        releaseMs(0.f),
        retriggerMs(5.f),
        maxSustainMs(-1.f),
        legatoMode(false),
        playEnd(false),
        current(IDLE),
        amplitude(0.f),
        from(0.f),
        progress(0.0),
        held(0.0),
        value(0.f),
        gateHigh(false),
        pending(false),
        pendingAmplitude(0.f),
        startedFlag(false),
        finishedFlag(false),
        output(STANDARD_BUFFERSIZE),
        gateOutput(STANDARD_BUFFERSIZE) {}

    // A new segment, from the current level.
    void adsr::enter(STAGE next) {
      current = next;
      from = value;
      progress = 0.0;
      held = 0.0;
    }

    // One sample of a straight line from `from` to `target` over `ms`; true
    // when it has arrived.
    bool adsr::ramp(Flt ms, Flt target) {
      const Dbl n = Samples(ms);
      progress = n > 1.0 ? progress + 1.0 / n : 1.0;
      if (progress >= Done) {
        value = target;
        return true;
      }
      value = from + (target - from) * static_cast<Flt>(progress);
      return false;
    }

    void adsr::begin(Flt peak) {
      if (playEnd && current != IDLE) return;
      if (current == IDLE) startedFlag = true;
      amplitude = peak;
      if (!legatoMode && value != 0.f) {
        if (retriggerMs > 0.f) {
          enter(RETRIGGER);
          return;
        }
        value = 0.f; // no ramp down: the attack starts from 0 at once
      }
      enter(ATTACK);
    }

    void adsr::end() {
      if (playEnd || current == IDLE || current == RELEASE) return;
      enter(RELEASE);
    }

    buffer& adsr::run(UInt length, const Flt* gates, const Flt* attacks, const Flt* decays,
                      const Flt* sustains, const Flt* releases) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      if (gateOutput.getLength() != length) gateOutput.resize(length);
      Flt* out = output.getPtr();
      Flt* open = gateOutput.getPtr();
      startedFlag = false;
      finishedFlag = false;

      if (pending) {
        pending = false;
        if (pendingAmplitude != 0.f)
          begin(pendingAmplitude);
        else
          end();
      }

      bool closing = false;
      for (UInt i = 0; i < length; ++i) {
        if (gates != nullptr) {
          const Flt g = std::isfinite(gates[i]) ? gates[i] : 0.f;
          const bool high = g != 0.f;
          if (high && !gateHigh)
            begin(g);
          else if (!high && gateHigh)
            end();
          gateHigh = high;
        }

        const Flt s = CleanLevel(sustains != nullptr ? sustains[i] : sustainLevel);
        const Flt r = releases != nullptr ? releases[i] : releaseMs;

        switch (current) {
        case IDLE:
          value = 0.f;
          break;
        case ATTACK:
          if (ramp(attacks != nullptr ? attacks[i] : attackMs, amplitude)) enter(DECAY);
          break;
        case DECAY:
          if (ramp(decays != nullptr ? decays[i] : decayMs, s * amplitude)) enter(SUSTAIN);
          break;
        case SUSTAIN: {
          value = s * amplitude;
          const Flt limit = (playEnd && maxSustainMs < 0.f) ? 0.f : maxSustainMs;
          if (limit < 0.f || held < Samples(limit)) {
            held += 1.0;
            break;
          }
          // The sustain is over: this sample is the release's first.
          enter(RELEASE);
          if (ramp(r, 0.f)) {
            enter(IDLE);
            finishedFlag = true;
          }
          break;
        }
        case RELEASE:
          if (ramp(r, 0.f)) {
            enter(IDLE);
            finishedFlag = true;
          }
          break;
        case RETRIGGER:
          // The attack starts at the next sample. This one, at 0, is still
          // the ramp's, so the gate stays closed on it.
          closing = ramp(retriggerMs, 0.f);
          break;
        }

        out[i] = value;
        open[i] = (current == ATTACK || current == DECAY || current == SUSTAIN) ? 1.f : 0.f;
        if (closing) {
          enter(ATTACK);
          closing = false;
        }
      }
      return output;
    }

    buffer& adsr::operator()(UInt length) {
      return run(length, nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    buffer& adsr::operator()(buffer& gates) {
      return run(gates.getLength(), gates.getPtr(), nullptr, nullptr, nullptr, nullptr);
    }

    buffer& adsr::operator()(buffer& attacks, buffer& decays, buffer& sustains, buffer& releases) {
      UInt length = attacks.getLength();
      if (decays.getLength() < length) length = decays.getLength();
      if (sustains.getLength() < length) length = sustains.getLength();
      if (releases.getLength() < length) length = releases.getLength();
      return run(length, nullptr, attacks.getPtr(), decays.getPtr(), sustains.getPtr(),
                 releases.getPtr());
    }

    buffer& adsr::operator()(buffer& gates, buffer& attacks, buffer& decays, buffer& sustains,
                             buffer& releases) {
      UInt length = gates.getLength();
      if (attacks.getLength() < length) length = attacks.getLength();
      if (decays.getLength() < length) length = decays.getLength();
      if (sustains.getLength() < length) length = sustains.getLength();
      if (releases.getLength() < length) length = releases.getLength();
      return run(length, gates.getPtr(), attacks.getPtr(), decays.getPtr(), sustains.getPtr(),
                 releases.getPtr());
    }

    buffer& adsr::gate() {
      return gateOutput;
    }

    bool adsr::started() const {
      return startedFlag;
    }

    bool adsr::finished() const {
      return finishedFlag;
    }

    bool adsr::active() const {
      return current != IDLE;
    }

    adsr::STAGE adsr::stage() const {
      return current;
    }

    Flt adsr::level() const {
      return value;
    }

    adsr& adsr::trigger(Flt peak) {
      if (!std::isfinite(peak)) return *this;
      pendingAmplitude = peak;
      pending = true;
      return *this;
    }

    adsr& adsr::attack(Flt ms) {
      attackMs = CleanTime(ms);
      return *this;
    }

    Flt adsr::attack() const {
      return attackMs;
    }

    adsr& adsr::decay(Flt ms) {
      decayMs = CleanTime(ms);
      return *this;
    }

    Flt adsr::decay() const {
      return decayMs;
    }

    adsr& adsr::sustain(Flt factor) {
      sustainLevel = CleanLevel(factor);
      return *this;
    }

    Flt adsr::sustain() const {
      return sustainLevel;
    }

    adsr& adsr::release(Flt ms) {
      releaseMs = CleanTime(ms);
      return *this;
    }

    Flt adsr::release() const {
      return releaseMs;
    }

    adsr& adsr::retrigger(Flt ms) {
      retriggerMs = CleanTime(ms);
      return *this;
    }

    Flt adsr::retrigger() const {
      return retriggerMs;
    }

    adsr& adsr::maxSustain(Flt ms) {
      // Negative, not a number or forever: no limit.
      maxSustainMs = (std::isfinite(ms) && ms >= 0.f) ? ms : -1.f;
      return *this;
    }

    Flt adsr::maxSustain() const {
      return maxSustainMs;
    }

    adsr& adsr::legato(bool on) {
      legatoMode = on;
      return *this;
    }

    bool adsr::legato() const {
      return legatoMode;
    }

    adsr& adsr::playToEnd(bool on) {
      playEnd = on;
      return *this;
    }

    bool adsr::playToEnd() const {
      return playEnd;
    }

    adsr& adsr::reset() {
      current = IDLE;
      amplitude = 0.f;
      from = 0.f;
      progress = 0.0;
      held = 0.0;
      value = 0.f;
      gateHigh = false;
      pending = false;
      startedFlag = false;
      finishedFlag = false;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
