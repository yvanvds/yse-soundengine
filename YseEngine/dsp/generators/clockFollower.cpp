#include "clockFollower.hpp"

#include <cmath>

namespace {
  // Wrap into [0, 1), as DSP::phasor does.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  // Keep the float output below 1 (the nearest float to 0.99999999 is 1.0f).
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Interval bounds for taps and clicks, in seconds, with room for the
  // rounding of a click track at exactly the limit.
  constexpr Dbl kTimeSlack = 1e-9;
} // namespace

namespace YSE {
  namespace DSP {

    clockFollower::clockFollower() : output(STANDARD_BUFFERSIZE) {}

    clockFollower& clockFollower::reset(Flt value) {
      bpm = DEFAULT_TEMPO;
      if (!std::isnan(value)) adoptTempo(static_cast<Dbl>(value));
      phaseNow = 0.0;
      isRunning = true;
      waitForTick = false;
      correctionLeft = 0;
      tapCount = 0;
      clickCount = 0;
      clickSeen = false;
      clickArmed = true;
      tickFill = 0;
      tickLocked = false;
      tickTempo = false;
      lastClockTick = -1;
      reportDone = true;
      openReport();
      return *this;
    }

    clockFollower& clockFollower::tempo(Flt value) {
      if (std::isnan(value)) return *this;
      openReport();
      adoptTempo(static_cast<Dbl>(value));
      return *this;
    }

    Flt clockFollower::tempo() const {
      return static_cast<Flt>(bpm);
    }

    clockFollower& clockFollower::offset(Flt phase) {
      if (!std::isfinite(phase)) return *this;
      outputOffset = Wrap(static_cast<Dbl>(phase));
      return *this;
    }

    Flt clockFollower::offset() const {
      return static_cast<Flt>(outputOffset);
    }

    clockFollower& clockFollower::clockOffset(Flt phase) {
      if (!std::isfinite(phase)) return *this;
      const Dbl value = Wrap(static_cast<Dbl>(phase));
      if (value == midiOffset) return *this;
      midiOffset = value;
      // Moving the grid is not a tick. A pending first tick stays pending.
      if (lastClockTick >= 0) lastClockTick = clockIndex();
      return *this;
    }

    Flt clockFollower::clockOffset() const {
      return static_cast<Flt>(midiOffset);
    }

    clockFollower& clockFollower::ppq(UInt ticks) {
      if (ticks < 1) ticks = 1;
      if (ticks > MAX_PPQ) ticks = MAX_PPQ;
      if (ticks == ticksPerBeat) return *this;
      ticksPerBeat = ticks;
      // Ticks counted at the old rate say nothing at the new one.
      tickFill = 0;
      tickLocked = false;
      tickTempo = false;
      if (lastClockTick >= 0) lastClockTick = clockIndex();
      return *this;
    }

    UInt clockFollower::ppq() const {
      return ticksPerBeat;
    }

    clockFollower& clockFollower::start() {
      openReport();
      isRunning = true;
      waitForTick = false;
      phaseNow = 0.0;
      correctionLeft = 0;
      tickLocked = false;
      lastClockTick = -1;
      pushClockByte(MIDI_START);
      return *this;
    }

    clockFollower& clockFollower::stop() {
      openReport();
      isRunning = false;
      waitForTick = false;
      correctionLeft = 0;
      pushClockByte(MIDI_STOP);
      return *this;
    }

    bool clockFollower::running() const {
      return isRunning;
    }

    clockFollower& clockFollower::tap() {
      openReport();
      reportTaps++;
      beat(tapTimes, tapCount, now);
      return *this;
    }

    clockFollower& clockFollower::midi(Int status) {
      switch (status) {
      case MIDI_TICK:
        openReport();
        tick(now);
        break;
      case MIDI_START:
        openReport();
        // The next tick is the first beat: hold at 0 until it comes.
        isRunning = true;
        waitForTick = true;
        phaseNow = 0.0;
        correctionLeft = 0;
        lastClockTick = -1;
        pushClockByte(MIDI_START);
        break;
      case MIDI_CONTINUE:
        openReport();
        if (!isRunning) {
          isRunning = true;
          lastClockTick = clockIndex();
        }
        pushClockByte(MIDI_CONTINUE);
        break;
      case MIDI_STOP:
        stop();
        break;
      default:
        break;
      }
      return *this;
    }

    Flt clockFollower::phase() const {
      return ToSample(phaseNow);
    }

    Int clockFollower::clockByte(UInt index) const {
      return index < clockCount ? clockOut[index] : 0;
    }

    buffer& clockFollower::operator()(UInt length) {
      return process(nullptr, Clamp(length));
    }

    buffer& clockFollower::operator()(buffer& click) {
      return process(click.getPtr(), Clamp(click.getLength()));
    }

    // ─── private ──────────────────────────────────────────────────────────

    void clockFollower::openReport() {
      if (!reportDone) return;
      reportDone = false;
      reportTaps = 0;
      reportClicks = 0;
      reportTicks = 0;
      reportTempo = false;
      clockCount = 0;
    }

    void clockFollower::pushClockByte(Int value) {
      if (clockCount < MAX_CLOCK_BYTES) clockOut[clockCount++] = value;
    }

    void clockFollower::adoptTempo(Dbl value) {
      if (value < static_cast<Dbl>(MIN_TEMPO)) value = MIN_TEMPO;
      if (value > static_cast<Dbl>(MAX_TEMPO)) value = MAX_TEMPO;
      // A difference of rounding is not a change: two taps a whole number of
      // blocks apart are not exactly the same interval in seconds.
      if (std::fabs(value - bpm) <= 1e-9 * bpm) return;
      bpm = value;
      reportTempo = true;
    }

    Int clockFollower::clockIndex() const {
      const Int index = static_cast<Int>(Wrap(phaseNow + midiOffset) * ticksPerBeat);
      const Int last = static_cast<Int>(ticksPerBeat) - 1;
      return index > last ? last : index;
    }

    void clockFollower::align(Dbl target, Dbl span, Dbl samples) {
      Dbl error = target - phaseNow;
      error -= std::floor(error + 0.5); // the nearest way round: -0.5 .. 0.5
      if (std::fabs(error) <= 0.25 * span && samples >= 1.0) {
        correctionLeft = static_cast<UInt>(samples);
        correction = error / static_cast<Dbl>(correctionLeft);
      } else {
        phaseNow = Wrap(target);
        correctionLeft = 0;
      }
    }

    void clockFollower::beat(Dbl* history, UInt& count, Dbl time) {
      const Dbl shortest = 60.0 / static_cast<Dbl>(MAX_BEAT_TEMPO);
      const Dbl longest = 60.0 / static_cast<Dbl>(MIN_BEAT_TEMPO);
      if (count > 0) {
        const Dbl interval = time - history[count - 1];
        if (interval < shortest - kTimeSlack || interval > longest + kTimeSlack) count = 0;
      }
      if (count == 3) {
        history[0] = history[1];
        history[1] = history[2];
        count = 2;
      }
      history[count++] = time;
      if (count < 3) return;

      const Dbl first = history[1] - history[0];
      const Dbl second = history[2] - history[1];
      const Dbl longer = first > second ? first : second;
      if (std::fabs(first - second) > static_cast<Dbl>(BEAT_TOLERANCE) * longer) return;
      adoptTempo(120.0 / (first + second));
      if (isRunning) align(0.0, 1.0, 60.0 * static_cast<Dbl>(SAMPLERATE) / bpm);
    }

    void clockFollower::tick(Dbl time) {
      reportTicks++;
      if (tickFill > 0) {
        const Dbl previous = tickTimes[(tickHead + TICK_HISTORY - 1) % TICK_HISTORY];
        if (time - previous > TICK_TIMEOUT) {
          tickFill = 0;
          tickLocked = false;
          tickTempo = false;
        }
      }
      tickTimes[tickHead] = time;
      tickHead = (tickHead + 1) % TICK_HISTORY;
      if (tickFill < TICK_HISTORY) tickFill++;

      // The tempo: least-squares seconds per tick over the last two beats.
      const UInt window = 2 * ticksPerBeat + 1;
      const UInt n = tickFill < window ? tickFill : window;
      const UInt needed = ticksPerBeat / 4 > 1 ? ticksPerBeat / 4 : 1;
      if (n - 1 >= needed) {
        const UInt oldest = (tickHead + TICK_HISTORY - n) % TICK_HISTORY;
        const Dbl base = tickTimes[oldest];
        const Dbl meanX = static_cast<Dbl>(n - 1) / 2.0;
        Dbl meanY = 0.0;
        for (UInt j = 0; j < n; ++j)
          meanY += tickTimes[(oldest + j) % TICK_HISTORY] - base;
        meanY /= static_cast<Dbl>(n);
        Dbl sxy = 0.0;
        Dbl sxx = 0.0;
        for (UInt j = 0; j < n; ++j) {
          const Dbl dx = static_cast<Dbl>(j) - meanX;
          sxy += dx * (tickTimes[(oldest + j) % TICK_HISTORY] - base - meanY);
          sxx += dx * dx;
        }
        const Dbl secondsPerTick = sxy / sxx;
        if (secondsPerTick > 0.0) {
          const Dbl estimate = 60.0 / (secondsPerTick * static_cast<Dbl>(ticksPerBeat));
          if (!tickTempo || std::fabs(estimate - bpm) > TEMPO_HYSTERESIS * bpm) {
            adoptTempo(estimate);
            tickTempo = true;
          }
        }
      }

      if (!isRunning) return;
      if (waitForTick) {
        // The first tick after a MIDI start is the first beat.
        waitForTick = false;
        tickIndex = 0;
        tickLocked = true;
        phaseNow = 0.0;
        correctionLeft = 0;
        return;
      }
      if (!tickLocked) {
        // No start to count from: take the ramp's nearest tick.
        tickIndex =
            static_cast<std::uint64_t>(std::llround(phaseNow * ticksPerBeat)) % ticksPerBeat;
        tickLocked = true;
      } else {
        tickIndex++;
      }
      const Dbl span = 1.0 / static_cast<Dbl>(ticksPerBeat);
      align(static_cast<Dbl>(tickIndex % ticksPerBeat) * span, span,
            60.0 * static_cast<Dbl>(SAMPLERATE) * span / bpm);
    }

    buffer& clockFollower::process(const Flt* click, UInt length) {
      openReport();
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);

      const Dbl rate = static_cast<Dbl>(SAMPLERATE);
      const Dbl dt = 1.0 / rate;
      const Dbl refractory = 0.5 * 60.0 / static_cast<Dbl>(MAX_BEAT_TEMPO);
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        if (click != nullptr) {
          const Dbl time = now + static_cast<Dbl>(i) * dt;
          const Flt level = std::fabs(click[i]);
          if (!(level >= CLICK_THRESHOLD)) { // below it, or NaN
            clickArmed = true;
          } else if (clickArmed) {
            clickArmed = false;
            if (!clickSeen || time - lastClick >= refractory - kTimeSlack) {
              clickSeen = true;
              lastClick = time;
              reportClicks++;
              beat(clickTimes, clickCount, time);
            }
          }
        }

        out[i] = ToSample(Wrap(phaseNow + outputOffset));

        if (isRunning && !waitForTick) {
          const Int index = clockIndex();
          if (index != lastClockTick) {
            pushClockByte(MIDI_TICK);
            lastClockTick = index;
          }
          Dbl step = bpm / (60.0 * rate);
          if (correctionLeft > 0) {
            step += correction;
            correctionLeft--;
          }
          phaseNow = Wrap(phaseNow + step);
        }
      }
      now += static_cast<Dbl>(length) * dt;
      reportDone = true;
      return output;
    }

  } // namespace DSP
} // namespace YSE
