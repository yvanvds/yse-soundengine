#include "plotData.hpp"

#include <cmath>
#include "../operators/units.hpp"

namespace {
  inline UInt Clamp(UInt value, UInt low, UInt high) {
    return value < low ? low : (value > high ? high : value);
  }

  // A float setting as a whole number, MIN to MAX, NaN the default.
  inline UInt Whole(Flt value, UInt low, UInt high, UInt fallback) {
    if (std::isnan(value)) return fallback;
    if (value <= static_cast<Flt>(low)) return low;
    if (value >= static_cast<Flt>(high)) return high;
    return static_cast<UInt>(value);
  }

  // NaN counts as 0; infinities are kept.
  inline Flt Sample(Flt value) {
    return std::isnan(value) ? 0.f : value;
  }

  // A region bound in milliseconds: below 0, and NaN, is 0.
  inline Flt Milliseconds(Flt value) {
    return std::isnan(value) || value < 0.f ? 0.f : value;
  }
} // namespace

namespace YSE {
  namespace DSP {

    plotData::plotData(UInt plots, UInt capacity)
      : numPlots(Clamp(plots, 1, MAX_PLOTS)),
        cap(Clamp(capacity, 1, MAX_POINTS)),
        slots(),
        numPoints(DEFAULT_POINTS < cap ? DEFAULT_POINTS : cap),
        samplesPerFrame(0),
        rate(DEFAULT_FRAME_RATE),
        dirty(true),
        serial(0),
        writeSlot(0),
        published(1),
        count(0),
        readSlot(2) {
      const std::size_t all = static_cast<std::size_t>(numPlots) * cap;
      values.assign(all, 0.f);
      capture.assign(all, 0.f);
      states.assign(numPlots, state());
      store.assign(all * 3, 0.f);
      shown.assign(static_cast<std::size_t>(numPlots) * 3, subplot());
      for (unsigned int s = 0; s < 3; ++s)
        for (UInt p = 0; p < numPlots; ++p)
          shown[s * numPlots + p].values = store.data() + (s * numPlots + p) * cap;
      for (state& s : states) {
        s.range.start = -1.f;
        s.range.end = 1.f;
      }
      // The reader starts out holding the defaults.
      publish();
      acquire();
    }

    Flt* plotData::live(UInt plot) {
      return values.data() + static_cast<std::size_t>(plot) * cap;
    }

    const Flt* plotData::live(UInt plot) const {
      return values.data() + static_cast<std::size_t>(plot) * cap;
    }

    Flt* plotData::captured(UInt plot) {
      return capture.data() + static_cast<std::size_t>(plot) * cap;
    }

    UInt plotData::plots() const {
      return numPlots;
    }

    UInt plotData::capacity() const {
      return cap;
    }

    plotData& plotData::points(UInt value) {
      const UInt wanted = Clamp(value, 1, cap);
      if (wanted != numPoints) {
        numPoints = wanted;
        dirty = true;
        restartAll();
      }
      return *this;
    }

    UInt plotData::points() const {
      return numPoints;
    }

    plotData& plotData::frameSize(UInt value) {
      const UInt wanted = value > MAX_FRAME_SIZE ? MAX_FRAME_SIZE : value;
      if (wanted != samplesPerFrame) {
        samplesPerFrame = wanted;
        restartAll();
      }
      return *this;
    }

    UInt plotData::frameSize() const {
      return samplesPerFrame;
    }

    plotData& plotData::frameRate(Flt value) {
      if (std::isnan(value)) return *this;
      rate = value < MIN_FRAME_RATE ? MIN_FRAME_RATE
                                    : (value > MAX_FRAME_RATE ? MAX_FRAME_RATE : value);
      return *this;
    }

    Flt plotData::frameRate() const {
      return rate;
    }

    plotData& plotData::set(UInt plot, const Flt* in, UInt n, UInt from) {
      if (plot >= numPlots || in == nullptr || from >= cap) return *this;
      const UInt room = cap - from;
      const UInt written = n < room ? n : room;
      Flt* to = live(plot) + from;
      for (UInt i = 0; i < written; ++i)
        to[i] = Sample(in[i]);
      if (written > 0) dirty = true;
      return *this;
    }

    Flt plotData::value(UInt plot, UInt index) const {
      if (plot >= numPlots || index >= cap) return 0.f;
      return live(plot)[index];
    }

    plotData& plotData::domain(UInt plot, Flt start, Flt end, SCALE scale) {
      if (plot >= numPlots) return *this;
      axis& a = states[plot].domain;
      if (!std::isnan(start)) a.start = start;
      if (!std::isnan(end)) a.end = end;
      a.scale = scale;
      dirty = true;
      return *this;
    }

    plotData::axis plotData::domain(UInt plot) const {
      return plot < numPlots ? states[plot].domain : axis();
    }

    plotData& plotData::range(UInt plot, Flt start, Flt end, SCALE scale) {
      if (plot >= numPlots) return *this;
      axis& a = states[plot].range;
      if (!std::isnan(start)) a.start = start;
      if (!std::isnan(end)) a.end = end;
      a.scale = scale;
      dirty = true;
      return *this;
    }

    plotData::axis plotData::range(UInt plot) const {
      if (plot < numPlots) return states[plot].range;
      axis a;
      a.start = -1.f;
      return a;
    }

    plotData& plotData::xOrigin(UInt plot, Flt value) {
      if (plot >= numPlots || std::isnan(value)) return *this;
      states[plot].xOrigin = value;
      dirty = true;
      return *this;
    }

    Flt plotData::xOrigin(UInt plot) const {
      return plot < numPlots ? states[plot].xOrigin : 0.f;
    }

    plotData& plotData::yOrigin(UInt plot, Flt value) {
      if (plot >= numPlots || std::isnan(value)) return *this;
      states[plot].yOrigin = value;
      dirty = true;
      return *this;
    }

    Flt plotData::yOrigin(UInt plot) const {
      return plot < numPlots ? states[plot].yOrigin : 0.f;
    }

    plotData& plotData::filter(UInt plot, FILTER which) {
      if (plot >= numPlots) return *this;
      states[plot].filter = which;
      dirty = true;
      return *this;
    }

    plotData::FILTER plotData::filter(UInt plot) const {
      return plot < numPlots ? states[plot].filter : NONE;
    }

    plotData& plotData::region(UInt plot, UInt channel, Flt startMs, Flt endMs) {
      if (plot >= numPlots) return *this;
      state& s = states[plot];
      s.channel = channel;
      s.startMs = Milliseconds(startMs);
      s.endMs = Milliseconds(endMs);
      return *this;
    }

    UInt plotData::regionChannel(UInt plot) const {
      return plot < numPlots ? states[plot].channel : 0;
    }

    Flt plotData::regionStart(UInt plot) const {
      return plot < numPlots ? states[plot].startMs : 0.f;
    }

    Flt plotData::regionEnd(UInt plot) const {
      return plot < numPlots ? states[plot].endMs : 0.f;
    }

    void plotData::begin(state& s) const {
      // A frame that starts late (after a frame longer than the wait)
      // starts the clock over from itself; otherwise the fraction is kept.
      if (s.wait <= -1.0) s.wait = 0.0;
      const Dbl sr = SAMPLERATE > 0 ? static_cast<Dbl>(SAMPLERATE) : 1.0;
      s.wait += sr / static_cast<Dbl>(rate);
    }

    void plotData::use(UInt plot, SOURCE source) {
      state& s = states[plot];
      if (s.source == source) return;
      s.source = source;
      restart(plot);
    }

    plotData& plotData::restart(UInt plot) {
      if (plot >= numPlots) return *this;
      state& s = states[plot];
      s.collecting = false;
      s.filled = 0;
      s.wait = 0.0;
      return *this;
    }

    void plotData::restartAll() {
      for (UInt p = 0; p < numPlots; ++p)
        restart(p);
    }

    void plotData::operator()(UInt plot, buffer& in) {
      if (plot >= numPlots) return;
      use(plot, SIGNAL);
      state& s = states[plot];
      const UInt length = in.getLength();
      const Flt* samples = in.getPtr();
      const UInt frame = samplesPerFrame > numPoints ? samplesPerFrame : numPoints;
      Flt* into = captured(plot);
      for (UInt i = 0; i < length; ++i) {
        if (!s.collecting) {
          if (s.wait > 0.0) {
            s.wait -= 1.0;
            continue;
          }
          s.collecting = true;
          s.filled = 0;
          begin(s);
        }
        if (s.filled < numPoints) into[s.filled] = Sample(samples[i]);
        ++s.filled;
        s.wait -= 1.0;
        if (s.filled >= frame) {
          Flt* to = live(plot);
          for (UInt j = 0; j < numPoints; ++j)
            to[j] = into[j];
          s.collecting = false;
          dirty = true;
        }
      }
    }

    void plotData::operator()(UInt plot, const sampleView& view, UInt samples) {
      if (plot >= numPlots) return;
      use(plot, SAMPLES);
      state& s = states[plot];
      bool due = false;
      UInt at = 0;
      while (at < samples) {
        if (s.wait > 0.0) {
          // The samples until the next frame start, this one among them.
          const Dbl skip = std::ceil(s.wait);
          const UInt left = samples - at;
          if (skip >= static_cast<Dbl>(left)) {
            s.wait -= static_cast<Dbl>(left);
            break;
          }
          at += static_cast<UInt>(skip);
          s.wait -= skip;
        }
        // A frame starts on sample `at` and takes only that sample.
        due = true;
        begin(s);
        s.wait -= 1.0;
        ++at;
      }
      if (!due) return;

      Flt* to = live(plot);
      const UInt total = view.frames();
      const Dbl perMs = static_cast<Dbl>(view.sampleRate()) / 1000.0;
      const Dbl startAt = std::floor(static_cast<Dbl>(s.startMs) * perMs);
      const UInt start = startAt >= static_cast<Dbl>(total) ? total : static_cast<UInt>(startAt);
      UInt end = total;
      if (s.endMs > s.startMs) {
        const Dbl endAt = std::floor(static_cast<Dbl>(s.endMs) * perMs);
        if (endAt < static_cast<Dbl>(total)) end = static_cast<UInt>(endAt);
      }
      const U64 length = end > start ? end - start : 0;
      for (UInt i = 0; i < numPoints; ++i) {
        if (length == 0) {
          to[i] = 0.f;
          continue;
        }
        const U64 offset = static_cast<U64>(i) * length / numPoints;
        to[i] = Sample(view.at(s.channel, start + static_cast<UInt>(offset)));
      }
      dirty = true;
    }

    bool plotData::publish() {
      if (!dirty) return false;
      dirty = false;
      slot& out = slots[writeSlot];
      out.points = numPoints;
      out.serial = ++serial;
      for (UInt p = 0; p < numPlots; ++p) {
        const state& s = states[p];
        subplot& to = shown[writeSlot * numPlots + p];
        to.domain = s.domain;
        to.range = s.range;
        to.xOrigin = s.xOrigin;
        to.yOrigin = s.yOrigin;
        to.filter = s.filter;
        Flt* into = store.data() + (static_cast<std::size_t>(writeSlot) * numPlots + p) * cap;
        const Flt* from = live(p);
        if (s.filter == ATODB) {
          for (UInt i = 0; i < numPoints; ++i)
            into[i] = AmpToDb(from[i]);
        } else {
          for (UInt i = 0; i < numPoints; ++i)
            into[i] = from[i];
        }
      }
      // Hand the frame over and take back the slot the reader let go of.
      writeSlot = published.exchange(writeSlot | FRESH, std::memory_order_acq_rel) & INDEX;
      count.store(serial, std::memory_order_release);
      return true;
    }

    UInt plotData::frames() const {
      return count.load(std::memory_order_acquire);
    }

    plotData::frame plotData::acquire() {
      if ((published.load(std::memory_order_acquire) & FRESH) != 0)
        readSlot = published.exchange(readSlot, std::memory_order_acq_rel) & INDEX;
      return held();
    }

    plotData::frame plotData::held() const {
      frame f;
      const slot& s = slots[readSlot];
      if (s.serial == 0) return f;
      f.plots = numPlots;
      f.points = s.points;
      f.serial = s.serial;
      f.subplots = shown.data() + static_cast<std::size_t>(readSlot) * numPlots;
      return f;
    }

    UInt plotData::Points(Flt value) {
      return Whole(value, 1, MAX_POINTS, DEFAULT_POINTS);
    }

    UInt plotData::Plots(Flt value) {
      return Whole(value, 1, MAX_PLOTS, 1);
    }

    UInt plotData::FrameSize(Flt value) {
      return Whole(value, 0, MAX_FRAME_SIZE, 0);
    }

  } // namespace DSP
} // namespace YSE
