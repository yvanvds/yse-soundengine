/*
  ==============================================================================

    waveformView.cpp
    The view of a sample buffer a waveform display shows and edits (issue
    #1151).

  ==============================================================================
*/

#include "waveformView.hpp"
#include "../../headers/constants.hpp"

#include <cmath>

using namespace YSE::DSP;

namespace {

  bool Finite(double value) {
    return std::isfinite(value);
  }

  // Whether a gesture in state `state` reports the selection under `mode`.
  bool Reports(waveformView::OUTPUT mode, waveformView::MOUSE state) {
    switch (mode) {
    case waveformView::OUTPUT_NONE:
      return false;
    case waveformView::OUTPUT_DOWN:
      return state == waveformView::DOWN;
    case waveformView::OUTPUT_UP:
      return state == waveformView::UP;
    case waveformView::OUTPUT_DOWNUP:
      return state != waveformView::DRAG;
    case waveformView::OUTPUT_CONTINUOUS:
      return true;
    }
    return false;
  }

} // namespace

// ─── the buffer ────────────────────────────────────────────────────────────

waveformView& waveformView::shape(UInt frames, Flt sampleRate) {
  numFrames = frames;
  rate = sampleRate > 0.f ? sampleRate : 0.f;
  return *this;
}

UInt waveformView::frames() const {
  return numFrames;
}

Flt waveformView::sampleRate() const {
  if (rate > 0.f) return rate;
  return YSE::SAMPLERATE > 0 ? static_cast<Flt>(YSE::SAMPLERATE) : 44100.f;
}

Dbl waveformView::bufferLength() const {
  return static_cast<Dbl>(numFrames) * 1000.0 / static_cast<Dbl>(sampleRate());
}

// ─── display and selection ─────────────────────────────────────────────────

waveformView::event waveformView::displayStart(Dbl ms) {
  event e;
  if (std::isnan(ms)) return e;
  const Dbl next = ms < 0.0 ? 0.0 : ms;
  e.link = next != start;
  start = next;
  return e;
}

Dbl waveformView::displayStart() const {
  return start;
}

waveformView::event waveformView::displayLength(Dbl ms) {
  event e;
  if (std::isnan(ms)) return e;
  const Dbl before = displayLength();
  const bool wasWhole = whole;
  whole = ms <= 0.0;
  if (!whole) length = ms;
  e.link = whole != wasWhole || displayLength() != before;
  return e;
}

Dbl waveformView::displayLength() const {
  return whole ? bufferLength() : length;
}

bool waveformView::wholeBuffer() const {
  return whole;
}

bool waveformView::select(Dbl from, Dbl to, bool remember) {
  if (from == selStart && to == selEnd) return false;
  if (remember) {
    prevStart = selStart;
    prevEnd = selEnd;
  }
  selStart = from;
  selEnd = to;
  return true;
}

waveformView::event waveformView::selectionStart(Dbl ms) {
  event e;
  if (std::isnan(ms)) return e;
  e.link = select(ms < 0.0 ? 0.0 : ms, selEnd, true);
  return e;
}

Dbl waveformView::selectionStart() const {
  return selStart;
}

waveformView::event waveformView::selectionEnd(Dbl ms) {
  event e;
  if (std::isnan(ms)) return e;
  e.link = select(selStart, ms < 0.0 ? 0.0 : ms, true);
  return e;
}

Dbl waveformView::selectionEnd() const {
  return selEnd;
}

waveformView::event waveformView::link(Dbl from, Dbl span, Dbl selectFrom, Dbl selectTo) {
  event e;
  e.link |= displayStart(from).link;
  e.link |= displayLength(span).link;
  const Dbl a = std::isnan(selectFrom) ? selStart : (selectFrom < 0.0 ? 0.0 : selectFrom);
  const Dbl b = std::isnan(selectTo) ? selEnd : (selectTo < 0.0 ? 0.0 : selectTo);
  e.link |= select(a, b, true);
  return e;
}

waveformView::event waveformView::reset() {
  event e;
  e.link |= displayStart(0.0).link;
  e.link |= displayLength(0.0).link;
  e.link |= select(0.0, 0.0, true);
  held = false;
  return e;
}

waveformView::event waveformView::undo() {
  event e;
  const Dbl a = prevStart;
  const Dbl b = prevEnd;
  e.link = select(a, b, true);
  e.selection = true;
  return e;
}

// ─── the mouse ─────────────────────────────────────────────────────────────

waveformView::event waveformView::mouse(Flt x, Flt y, MOUSE state, sampleWriteView* samples) {
  event e;
  if (samples != nullptr && !samples->empty()) shape(samples->frames(), samples->sampleRate());
  if (std::isnan(x) || x < 0.f) x = 0.f;
  if (x > 1.f) x = 1.f;
  if (std::isnan(y)) y = 0.f;
  e.mouse = true;
  e.x = x;
  e.y = amplitude(y);
  e.state = state;

  const bool down = state == DOWN;
  const bool hush = silent && state == DRAG;
  const Dbl at = start + static_cast<Dbl>(x) * displayLength();

  switch (gesture) {
  case NONE:
    break;

  case SELECT:
  case LOOP: {
    if (!down && !held) break;
    const Dbl to = snapped(at, samples);
    bool changed = false;
    if (down) {
      if (gesture == SELECT) {
        anchor = to;
      } else {
        // The farther edge stays; the nearer one follows the mouse.
        const Dbl lo = selStart < selEnd ? selStart : selEnd;
        const Dbl hi = selStart < selEnd ? selEnd : selStart;
        anchor = std::fabs(to - lo) <= std::fabs(to - hi) ? hi : lo;
      }
      held = true;
      changed = select(anchor < to ? anchor : to, anchor < to ? to : anchor, true);
    } else {
      changed = select(anchor < to ? anchor : to, anchor < to ? to : anchor, false);
    }
    if (state == UP) held = false;
    if (hush) {
      // Reported when the button comes up, which every output mode but
      // OUTPUT_NONE and OUTPUT_DOWN reports anyway.
      pendingLink |= changed;
    } else {
      e.selection = Reports(outputMode, state);
      e.link = changed || (state == UP && pendingLink);
      pendingLink = false;
    }
    break;
  }

  case MOVE: {
    bool changed = false;
    if (down) {
      grabX = x;
      grabY = y;
      grabLength = displayLength();
      anchor = at;
      held = true;
    } else if (held) {
      const Dbl turn = downZooms ? -1.0 : 1.0;
      const Dbl factor = std::exp2(static_cast<Dbl>(y - grabY) * turn);
      Dbl span = grabLength / factor;
      const Dbl shortest = 1000.0 / static_cast<Dbl>(sampleRate());
      if (!Finite(span) || span < shortest) span = shortest;
      Dbl from = anchor - static_cast<Dbl>(x) * span;
      if (!Finite(from) || from < 0.0) from = 0.0;
      changed = from != start || whole || span != length;
      start = from;
      length = span;
      whole = false;
    }
    if (state == UP) held = false;
    if (hush) {
      pendingDisplay |= changed;
      pendingLink |= changed;
    } else {
      e.display = changed || (state == UP && pendingDisplay);
      e.link = changed || (state == UP && pendingLink);
      pendingDisplay = false;
      pendingLink = false;
    }
    break;
  }

  case DRAW: {
    if (samples == nullptr || samples->empty()) {
      held = false;
      break;
    }
    if (!down && !held) break;
    Flt value = e.y;
    if (clip) {
      const Flt lo = amplitude(-1.f);
      const Flt hi = amplitude(1.f);
      value = value < lo ? lo : (value > hi ? hi : value);
    }
    const UInt frame = toFrame(at);
    if (down) {
      e.edited = samples->set(chan, frame, value);
    } else {
      e.edited = draw(*samples, frame, value);
    }
    lastFrame = frame;
    lastValue = value;
    held = state != UP;
    break;
  }
  }
  return e;
}

bool waveformView::draw(sampleWriteView& samples, UInt frame, Flt value) {
  if (frame == lastFrame) return samples.set(chan, frame, value);
  const bool up = frame > lastFrame;
  const UInt steps = up ? frame - lastFrame : lastFrame - frame;
  bool wrote = false;
  for (UInt i = 0; i <= steps; ++i) {
    const Flt t = static_cast<Flt>(i) / static_cast<Flt>(steps);
    const UInt at = up ? lastFrame + i : lastFrame - i;
    wrote |= samples.set(chan, at, lastValue + (value - lastValue) * t);
  }
  return wrote;
}

// ─── the line ──────────────────────────────────────────────────────────────

waveformView& waveformView::line(Dbl position) {
  lineMs = std::isnan(position) || position < 0.0 ? -1.0 : toMs(position, lineIn);
  if (!Finite(lineMs)) lineMs = -1.0;
  return *this;
}

Dbl waveformView::line() const {
  return lineMs;
}

waveformView& waveformView::lineUnit(UNIT which) {
  lineIn = which;
  return *this;
}

waveformView::UNIT waveformView::lineUnit() const {
  return lineIn;
}

// ─── settings ──────────────────────────────────────────────────────────────

waveformView& waveformView::mode(MODE which) {
  gesture = which;
  held = false;
  pendingDisplay = false;
  pendingLink = false;
  return *this;
}

waveformView::MODE waveformView::mode() const {
  return gesture;
}

waveformView& waveformView::output(OUTPUT which) {
  outputMode = which;
  return *this;
}

waveformView::OUTPUT waveformView::output() const {
  return outputMode;
}

waveformView& waveformView::quiet(bool on) {
  silent = on;
  return *this;
}

bool waveformView::quiet() const {
  return silent;
}

waveformView& waveformView::snap(SNAP which) {
  snapTo = which;
  return *this;
}

waveformView::SNAP waveformView::snap() const {
  return snapTo;
}

waveformView& waveformView::grid(Dbl spacing) {
  if (!std::isnan(spacing)) gridValue = spacing;
  return *this;
}

Dbl waveformView::grid() const {
  return gridValue;
}

Dbl waveformView::gridSpacing() const {
  const Dbl ms =
      gridUnit == BPM ? static_cast<Dbl>(barBeats) * 60000.0 / bpm : toMs(gridValue, gridUnit);
  return Finite(ms) && ms > 0.0 ? ms : 0.0;
}

waveformView& waveformView::ticks(UInt count) {
  tickCount = count;
  return *this;
}

UInt waveformView::ticks() const {
  return tickCount;
}

waveformView& waveformView::offset(Dbl ms) {
  if (!std::isnan(ms)) gridOffset = ms;
  return *this;
}

Dbl waveformView::offset() const {
  return gridOffset;
}

waveformView& waveformView::unit(UNIT which) {
  gridUnit = which;
  return *this;
}

waveformView::UNIT waveformView::unit() const {
  return gridUnit;
}

waveformView& waveformView::tempo(Dbl beatsPerMinute, UInt beatsPerBar) {
  if (Finite(beatsPerMinute) && beatsPerMinute > 0.0) bpm = beatsPerMinute;
  if (beatsPerBar > 0) barBeats = beatsPerBar;
  return *this;
}

Dbl waveformView::tempo() const {
  return bpm;
}

UInt waveformView::beats() const {
  return barBeats;
}

waveformView::event waveformView::fitTempo() {
  event e;
  const Dbl lo = selStart < selEnd ? selStart : selEnd;
  const Dbl bar = std::fabs(selEnd - selStart);
  if (!(bar > 0.0)) return e;
  bpm = static_cast<Dbl>(barBeats) * 60000.0 / bar;
  gridOffset = std::fmod(lo, bar);
  gridUnit = BPM;
  e.tempo = true;
  return e;
}

waveformView& waveformView::verticalZoom(Flt zoom) {
  if (!std::isnan(zoom)) vZoom = zoom < MIN_VERTICAL_ZOOM ? MIN_VERTICAL_ZOOM : zoom;
  return *this;
}

Flt waveformView::verticalZoom() const {
  return vZoom;
}

waveformView& waveformView::verticalOffset(Flt where) {
  if (!std::isnan(where)) vOffset = where;
  return *this;
}

Flt waveformView::verticalOffset() const {
  return vOffset;
}

waveformView& waveformView::clipDraw(bool on) {
  clip = on;
  return *this;
}

bool waveformView::clipDraw() const {
  return clip;
}

waveformView& waveformView::zoomDown(bool on) {
  downZooms = on;
  return *this;
}

bool waveformView::zoomDown() const {
  return downZooms;
}

waveformView& waveformView::channel(UInt which) {
  chan = which;
  return *this;
}

UInt waveformView::channel() const {
  return chan;
}

// ─── conversions ───────────────────────────────────────────────────────────

Flt waveformView::amplitude(Flt y) const {
  return (y - vOffset) / vZoom;
}

Dbl waveformView::toMs(Dbl value, UNIT which) const {
  switch (which) {
  case MS:
    return value;
  case SAMPLES:
    return value * 1000.0 / static_cast<Dbl>(sampleRate());
  case PHASE:
    return value * bufferLength();
  case BPM:
    return gridOffset + value * 60000.0 / bpm;
  }
  return value;
}

UInt waveformView::toFrame(Dbl ms) const {
  if (numFrames == 0) return 0;
  const Dbl at = std::floor(ms * static_cast<Dbl>(sampleRate()) / 1000.0);
  if (!(at > 0.0)) return 0;
  const Dbl last = static_cast<Dbl>(numFrames - 1);
  return static_cast<UInt>(at > last ? last : at);
}

Dbl waveformView::hold(Dbl ms) const {
  if (!(ms > 0.0)) return 0.0;
  const Dbl end = bufferLength();
  return numFrames > 0 && ms > end ? end : ms;
}

Dbl waveformView::snapped(Dbl ms, const sampleWriteView* samples) const {
  Dbl step = 0.0;
  switch (snapTo) {
  case SNAP_NONE:
    break;
  case SNAP_GRID:
    step = gridSpacing();
    break;
  case SNAP_TICK:
    step = gridSpacing() / static_cast<Dbl>(tickCount > 0 ? tickCount : 1);
    break;
  case SNAP_ZERO:
    if (samples != nullptr && !samples->empty()) return hold(zeroCrossing(ms, *samples));
    break;
  }
  if (step > 0.0) ms = gridOffset + std::round((ms - gridOffset) / step) * step;
  return hold(ms);
}

Dbl waveformView::zeroCrossing(Dbl ms, const sampleWriteView& samples) const {
  const UInt total = samples.frames();
  if (total < 2 || chan >= samples.channels()) return ms;
  const UInt from = toFrame(ms) < total - 1 ? toFrame(ms) : total - 2;
  // A crossing at `i` lies between samples i and i + 1: one of them is 0, or
  // they differ in sign. It is the frame of the two nearer to 0.
  auto crossing = [&](UInt i, UInt& frame) {
    const Flt a = samples.at(chan, i);
    const Flt b = samples.at(chan, i + 1);
    if (a != 0.f && b != 0.f && (a < 0.f) == (b < 0.f)) return false;
    frame = std::fabs(a) <= std::fabs(b) ? i : i + 1;
    return true;
  };
  const Dbl perFrame = 1000.0 / static_cast<Dbl>(samples.sampleRate());
  for (UInt d = 0; d <= MAX_ZERO_SEARCH; ++d) {
    UInt frame = 0;
    const bool later = from + d <= total - 2;
    const bool earlier = d <= from;
    if (!later && !earlier) break;
    if (earlier && crossing(from - d, frame)) return static_cast<Dbl>(frame) * perFrame;
    if (later && d > 0 && crossing(from + d, frame)) return static_cast<Dbl>(frame) * perFrame;
  }
  return ms;
}
