// Tests for YSE::DSP::waveformView (YseEngine/dsp/sampling/waveformView.hpp)
// and the min/max summary of YSE::DSP::sampleBuffer (peaks, peaksMs), issue
// #1151. Reference output is closed form: a buffer of 1000 samples at 1000 Hz
// is 1000 ms long, so a time in ms is its sample index; the mouse at x across
// a display of L ms from S is at S + x * L; a move zooms by 2^dy around the
// time grabbed; a drawn line between samples f0 and f1 holds the linear
// interpolation of its two values; bucket b of a region of L samples from s
// covers s + floor(b * L / B) up to s + floor((b + 1) * L / B).

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/waveformView.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferWriter;
using YSE::DSP::sampleWriteView;
using YSE::DSP::waveformView;
using view = YSE::DSP::waveformView;

namespace {

  const double NaN = std::numeric_limits<double>::quiet_NaN();

  // A private buffer of `frames` samples at `rate`, sample n of channel c
  // equal to `fill(c, n)`.
  template <class Fill>
  std::shared_ptr<sampleBuffer> Make(UInt channels, UInt frames, Flt rate, Fill fill) {
    std::shared_ptr<sampleBuffer> b = sampleBuffer::create(channels, frames, rate);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<Flt> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = fill(c, n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  std::shared_ptr<sampleBuffer> Silent(UInt frames = 1000, Flt rate = 1000.f) {
    return sampleBuffer::create(1, frames, rate);
  }

  Flt At(const sampleBuffer& b, UInt c, UInt frame) {
    Flt out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  // A view and a writer of a buffer, so mouse events have samples.
  struct Rig {
    std::shared_ptr<sampleBuffer> buffer;
    sampleBufferWriter writer;
    waveformView v;

    explicit Rig(std::shared_ptr<sampleBuffer> b) : buffer(std::move(b)), writer(buffer) {
      sampleWriteView w = writer.pin();
      v.shape(w.frames(), w.sampleRate());
    }
    view::event Mouse(Flt x, Flt y, view::MOUSE state) {
      sampleWriteView w = writer.pin();
      return v.mouse(x, y, state, &w);
    }
    // A whole gesture: down at x0, drags through xs, up at the last.
    view::event Gesture(Flt x0, const std::vector<Flt>& xs, Flt y = 0.f) {
      Mouse(x0, y, view::DOWN);
      for (Flt x : xs)
        Mouse(x, y, view::DRAG);
      return Mouse(xs.empty() ? x0 : xs.back(), y, view::UP);
    }
  };

} // namespace

TEST_SUITE("dsp") {

  // ─── sampleBuffer::peaks ──────────────────────────────────────────────────

  TEST_CASE("sampleBuffer::peaks: each bucket's smallest and largest sample (#1151)") {
    // Sample n is (n % 7) - 3, and n's sign flips every 10: a closed form
    // for each bucket's extremes.
    auto value = [](UInt, UInt n) {
      const Flt v = static_cast<Flt>(n % 7) - 3.f;
      return (n / 10) % 2 == 0 ? v : -v * 0.5f;
    };
    auto b = Make(1, 1000, 1000.f, value);
    for (UInt start : {0u, 13u, 400u}) {
      for (UInt count : {1000u - start, 97u, 333u}) {
        for (UInt buckets : {1u, 7u, 64u, 97u}) {
          CAPTURE(start);
          CAPTURE(count);
          CAPTURE(buckets);
          std::vector<Flt> lo(buckets), hi(buckets);
          REQUIRE(b->peaks(0, start, count, buckets, lo.data(), hi.data()) == buckets);
          for (UInt k = 0; k < buckets; ++k) {
            const UInt first = start + count * k / buckets;
            UInt last = start + count * (k + 1) / buckets;
            if (last <= first) last = first + 1;
            Flt a = value(0, first), z = a;
            for (UInt f = first; f < last; ++f) {
              a = std::fmin(a, value(0, f));
              z = std::fmax(z, value(0, f));
            }
            CHECK(lo[k] == a);
            CHECK(hi[k] == z);
          }
        }
      }
    }
  }

  TEST_CASE("sampleBuffer::peaks: fewer frames than buckets, the end, channels (#1151)") {
    auto b = Make(2, 10, 1000.f, [](UInt c, UInt n) { return static_cast<Flt>(c * 100 + n); });
    // 4 frames over 8 buckets: each frame fills the two buckets it falls on.
    std::vector<Flt> lo(8), hi(8);
    REQUIRE(b->peaks(1, 2, 4, 8, lo.data(), hi.data()) == 8);
    for (UInt k = 0; k < 8; ++k) {
      const UInt frame = 2 + k / 2; // the frame bucket k falls on
      CHECK(lo[k] == static_cast<Flt>(100 + frame));
      CHECK(hi[k] == lo[k]);
    }
    // A region running past the end: the buckets wholly past it are 0.
    REQUIRE(b->peaks(0, 6, 8, 4, lo.data(), hi.data()) == 4);
    CHECK(lo[0] == 6.f);
    CHECK(hi[0] == 7.f);
    CHECK(lo[1] == 8.f);
    CHECK(hi[1] == 9.f);
    CHECK(lo[2] == 0.f);
    CHECK(hi[3] == 0.f);
    // A channel it lacks, no buckets, no frames, null outputs: nothing.
    lo.assign(8, 42.f);
    CHECK(b->peaks(2, 0, 10, 8, lo.data(), hi.data()) == 0);
    CHECK(b->peaks(0, 0, 10, 0, lo.data(), hi.data()) == 0);
    CHECK(b->peaks(0, 0, 0, 8, lo.data(), hi.data()) == 0);
    CHECK(b->peaks(0, 0, 10, 8, nullptr, hi.data()) == 0);
    CHECK(lo[0] == 42.f);
  }

  TEST_CASE("sampleBuffer::peaksMs: a region in ms at the buffer's rate (#1151)") {
    // 48 kHz: 1 ms is 48 samples.
    auto b = Make(1, 4800, 48000.f, [](UInt, UInt n) { return static_cast<Flt>(n); });
    std::vector<Flt> lo(4), hi(4), want(4), wantHi(4);
    REQUIRE(b->peaksMs(0, 10.f, 20.f, 4, lo.data(), hi.data()) == 4);
    REQUIRE(b->peaks(0, 480, 960, 4, want.data(), wantHi.data()) == 4);
    CHECK(lo == want);
    CHECK(hi == wantHi);
    // A length of 0 runs to the end; a fraction of a sample starts on its
    // floor and lasts its ceiling.
    REQUIRE(b->peaksMs(0, 50.f, 0.f, 4, lo.data(), hi.data()) == 4);
    CHECK(lo[0] == 2400.f);
    CHECK(hi[3] == 4799.f);
    REQUIRE(b->peaksMs(0, 0.01f, 0.01f, 1, lo.data(), hi.data()) == 1);
    CHECK(lo[0] == 0.f);
    CHECK(hi[0] == 0.f);
    // Negative and NaN starts are 0; a start past the end gives empty buckets.
    REQUIRE(b->peaksMs(0, -5.f, 1.f, 1, lo.data(), hi.data()) == 1);
    CHECK(lo[0] == 0.f);
    CHECK(hi[0] == 47.f);
    REQUIRE(b->peaksMs(0, std::nanf(""), 1.f, 1, lo.data(), hi.data()) == 1);
    CHECK(hi[0] == 47.f);
    REQUIRE(b->peaksMs(0, 500.f, 10.f, 2, lo.data(), hi.data()) == 2);
    CHECK(lo[0] == 0.f);
    CHECK(hi[1] == 0.f);
  }

  // ─── the view ─────────────────────────────────────────────────────────────

  TEST_CASE("waveformView: defaults, and the whole buffer follows its shape (#1151)") {
    waveformView v;
    CHECK(v.frames() == 0);
    CHECK(v.bufferLength() == 0.0);
    CHECK(v.displayStart() == 0.0);
    CHECK(v.wholeBuffer());
    CHECK(v.displayLength() == 0.0);
    CHECK(v.selectionStart() == 0.0);
    CHECK(v.selectionEnd() == 0.0);
    CHECK(v.line() == -1.0);
    CHECK(v.mode() == view::SELECT);
    CHECK(v.output() == view::OUTPUT_CONTINUOUS);
    CHECK(v.snap() == view::SNAP_NONE);
    CHECK(v.unit() == view::MS);
    CHECK(v.grid() == 1000.0);
    CHECK(v.ticks() == 0);
    CHECK(v.offset() == 0.0);
    CHECK(v.tempo() == 120.0);
    CHECK(v.beats() == 4);
    CHECK(v.verticalZoom() == 1.f);
    CHECK(v.verticalOffset() == 0.f);
    CHECK_FALSE(v.clipDraw());
    CHECK_FALSE(v.quiet());
    CHECK_FALSE(v.zoomDown());
    CHECK(v.channel() == 0);

    v.shape(44100, 44100.f);
    CHECK(v.displayLength() == doctest::Approx(1000.0));
    v.shape(22050, 44100.f); // the buffer was cropped: the display follows
    CHECK(v.displayLength() == doctest::Approx(500.0));
    // An explicit length stays when the buffer changes, and 0 gives it back.
    CHECK(v.displayLength(200.0).link);
    v.shape(44100, 44100.f);
    CHECK_FALSE(v.wholeBuffer());
    CHECK(v.displayLength() == 200.0);
    CHECK(v.displayLength(-1.0).link);
    CHECK(v.wholeBuffer());
    CHECK(v.displayLength() == doctest::Approx(1000.0));
  }

  TEST_CASE("waveformView: follows the buffer's rate, else the live engine rate (#1151)") {
    waveformView v;
    for (Flt rate : {1000.f, 44100.f, 48000.f, 96000.f}) {
      CAPTURE(rate);
      v.shape(static_cast<UInt>(rate) * 3, rate);
      CHECK(v.sampleRate() == rate);
      CHECK(v.bufferLength() == doctest::Approx(3000.0));
    }
    // A rate of 0 is the engine's, read each time it is used.
    v.shape(48000, 0.f);
    {
      ScopedSampleRate engine(48000);
      CHECK(v.sampleRate() == 48000.f);
      CHECK(v.bufferLength() == doctest::Approx(1000.0));
    }
    {
      ScopedSampleRate engine(96000);
      CHECK(v.bufferLength() == doctest::Approx(500.0));
      v.unit(view::SAMPLES).grid(96.0);
      CHECK(v.gridSpacing() == doctest::Approx(1.0));
    }
  }

  TEST_CASE("waveformView: setters hold, ignore NaN, and report the link on a change (#1151)") {
    waveformView v;
    v.shape(1000, 1000.f);
    CHECK(v.displayStart(-5.0).link == false); // 0 already
    CHECK(v.displayStart(100.0).link);
    CHECK_FALSE(v.displayStart(100.0).link);
    CHECK_FALSE(v.displayStart(NaN).link);
    CHECK(v.displayStart() == 100.0);
    CHECK(v.selectionStart(300.0).link);
    CHECK(v.selectionEnd(200.0).link); // as set: the end may come first
    CHECK(v.selectionStart() == 300.0);
    CHECK(v.selectionEnd() == 200.0);
    CHECK(v.selectionEnd(-1.0).link);
    CHECK(v.selectionEnd() == 0.0);
    CHECK_FALSE(v.selectionStart(NaN).link);
    // None of these report anything but the link.
    const view::event e = v.selectionStart(10.0);
    CHECK_FALSE(e.display);
    CHECK_FALSE(e.selection);
    CHECK_FALSE(e.mouse);

    // link() sets the four; NaN keeps one; reported only when one changed.
    v.reset();
    view::event l = v.link(10.0, 500.0, 20.0, 30.0);
    CHECK(l.link);
    CHECK(v.displayStart() == 10.0);
    CHECK(v.displayLength() == 500.0);
    CHECK(v.selectionStart() == 20.0);
    CHECK(v.selectionEnd() == 30.0);
    CHECK_FALSE(v.link(10.0, 500.0, 20.0, 30.0).link);
    CHECK(v.link(NaN, NaN, NaN, 40.0).link);
    CHECK(v.selectionStart() == 20.0);
    CHECK(v.selectionEnd() == 40.0);
    CHECK(v.link(0.0, 0.0, NaN, NaN).link);
    CHECK(v.wholeBuffer());

    CHECK(v.reset().link);
    CHECK_FALSE(v.reset().link);
    CHECK(v.selectionEnd() == 0.0);
  }

  TEST_CASE("waveformView: undo swaps with the selection before the last change (#1151)") {
    waveformView v;
    v.shape(1000, 1000.f);
    v.selectionStart(100.0);
    v.selectionEnd(200.0); // before: 100, 0
    view::event u = v.undo();
    CHECK(u.selection);
    CHECK(u.link);
    CHECK(v.selectionStart() == 100.0);
    CHECK(v.selectionEnd() == 0.0);
    v.undo();
    CHECK(v.selectionEnd() == 200.0);
    v.undo();
    CHECK(v.selectionEnd() == 0.0);

    // A gesture is one change: undo goes back to before the button went down.
    Rig r(Silent());
    r.v.selectionStart(50.0);
    r.v.selectionEnd(60.0);
    r.Gesture(0.1f, {0.2f, 0.3f, 0.4f});
    CHECK(r.v.selectionStart() == doctest::Approx(100.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(400.0));
    r.v.undo();
    CHECK(r.v.selectionStart() == 50.0);
    CHECK(r.v.selectionEnd() == 60.0);
  }

  TEST_CASE("waveformView: SELECT runs from the click to the mouse, in order (#1151)") {
    Rig r(Silent());
    r.v.displayStart(200.0);
    r.v.displayLength(400.0);
    // The mouse at x is at 200 + x * 400.
    view::event e = r.Mouse(0.5f, 0.f, view::DOWN);
    CHECK(e.mouse);
    CHECK(e.state == view::DOWN);
    CHECK(e.x == 0.5f);
    CHECK(e.selection);
    CHECK(e.link);
    CHECK_FALSE(e.display);
    CHECK(r.v.selectionStart() == doctest::Approx(400.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(400.0));
    r.Mouse(0.75f, 0.f, view::DRAG);
    CHECK(r.v.selectionEnd() == doctest::Approx(500.0));
    r.Mouse(0.25f, 0.f, view::DRAG); // behind the click: in order
    CHECK(r.v.selectionStart() == doctest::Approx(300.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(400.0));
    // Off the display: held at its edge, and within the buffer.
    r.Mouse(-3.f, 0.f, view::DRAG);
    CHECK(r.v.selectionStart() == doctest::Approx(200.0));
    e = r.Mouse(9.f, 0.f, view::UP);
    CHECK(e.x == 1.f);
    CHECK(r.v.selectionEnd() == doctest::Approx(600.0));
    // A drag after the button came up does nothing but report the mouse.
    e = r.Mouse(0.9f, 0.f, view::DRAG);
    CHECK(e.mouse);
    CHECK_FALSE(e.selection);
    CHECK_FALSE(e.link);
    CHECK(r.v.selectionEnd() == doctest::Approx(600.0));
    // The display past the buffer's end: a time is held at the end.
    r.v.displayStart(900.0);
    r.Gesture(0.f, {1.f});
    CHECK(r.v.selectionStart() == doctest::Approx(900.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(1000.0));
  }

  TEST_CASE("waveformView: outmode says when a gesture reports the selection (#1151)") {
    struct row {
      view::OUTPUT mode;
      bool down, drag, up;
    };
    const row rows[] = {
        {view::OUTPUT_NONE, false, false, false},    {view::OUTPUT_DOWN, true, false, false},
        {view::OUTPUT_UP, false, false, true},       {view::OUTPUT_DOWNUP, true, false, true},
        {view::OUTPUT_CONTINUOUS, true, true, true},
    };
    for (const row& w : rows) {
      CAPTURE(static_cast<int>(w.mode));
      Rig r(Silent());
      r.v.output(w.mode);
      CHECK(r.Mouse(0.1f, 0.f, view::DOWN).selection == w.down);
      CHECK(r.Mouse(0.2f, 0.f, view::DRAG).selection == w.drag);
      CHECK(r.Mouse(0.3f, 0.f, view::UP).selection == w.up);
      // The link follows every change, whatever the output mode.
      Rig s(Silent());
      s.v.output(w.mode);
      CHECK(s.Mouse(0.1f, 0.f, view::DOWN).link);
      CHECK(s.Mouse(0.2f, 0.f, view::DRAG).link);
      CHECK_FALSE(s.Mouse(0.2f, 0.f, view::UP).link); // nothing moved
    }
  }

  TEST_CASE("waveformView: quiet reports only the mouse while dragging (#1151)") {
    Rig r(Silent());
    r.v.quiet(true);
    view::event e = r.Mouse(0.1f, 0.f, view::DOWN);
    CHECK(e.selection);
    CHECK(e.link);
    e = r.Mouse(0.2f, 0.f, view::DRAG);
    CHECK(e.mouse);
    CHECK_FALSE(e.selection);
    CHECK_FALSE(e.link);
    // The button comes up where the last drag was: the link is still sent.
    e = r.Mouse(0.2f, 0.f, view::UP);
    CHECK(e.selection);
    CHECK(e.link);

    // MOVE: the display moved during the drag is reported on the release.
    r.v.mode(view::MOVE);
    r.Mouse(0.5f, 0.f, view::DOWN);
    e = r.Mouse(0.5f, 1.f, view::DRAG);
    CHECK_FALSE(e.display);
    CHECK_FALSE(e.link);
    e = r.Mouse(0.5f, 1.f, view::UP);
    CHECK(e.display);
    CHECK(e.link);
  }

  TEST_CASE("waveformView: LOOP moves the selection edge nearer the click (#1151)") {
    Rig r(Silent());
    r.v.mode(view::LOOP);
    r.v.selectionStart(400.0);
    r.v.selectionEnd(600.0);
    r.Gesture(0.45f, {0.3f}); // near 400: the start moves, 600 stays
    CHECK(r.v.selectionStart() == doctest::Approx(300.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(600.0));
    r.Gesture(0.55f, {0.7f, 0.8f}); // near 600
    CHECK(r.v.selectionStart() == doctest::Approx(300.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(800.0));
    r.Gesture(0.75f, {0.1f}); // the end dragged past the start: in order
    CHECK(r.v.selectionStart() == doctest::Approx(100.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(300.0));
  }

  TEST_CASE("waveformView: MOVE scrolls and zooms around the time grabbed (#1151)") {
    Rig r(Silent());
    r.v.mode(view::MOVE);
    // Grab 500 ms at the middle of the whole buffer.
    view::event e = r.Mouse(0.5f, 0.f, view::DOWN);
    CHECK_FALSE(e.display);
    CHECK_FALSE(e.selection);
    // Up by 1 zooms in by 2, the grabbed time staying under the mouse.
    e = r.Mouse(0.5f, 1.f, view::DRAG);
    CHECK(e.display);
    CHECK(e.link);
    CHECK_FALSE(r.v.wholeBuffer());
    CHECK(r.v.displayLength() == doctest::Approx(500.0));
    CHECK(r.v.displayStart() == doctest::Approx(250.0));
    // Sideways: 500 ms is now at x = 0.25.
    r.Mouse(0.25f, 1.f, view::DRAG);
    CHECK(r.v.displayStart() == doctest::Approx(500.0 - 0.25 * 500.0));
    // Down by 0.5 from the grab zooms out by sqrt(2).
    r.Mouse(0.5f, -0.5f, view::DRAG);
    const double span = 1000.0 * std::sqrt(2.0);
    CHECK(r.v.displayLength() == doctest::Approx(span));
    CHECK(r.v.displayStart() == doctest::Approx(0.0)); // 500 - 707: held at 0
    r.Mouse(0.5f, -0.5f, view::UP);
    CHECK(r.v.selectionEnd() == 0.0); // the selection stays

    // zoom_orientation 1: down zooms in.
    r.v.zoomDown(true);
    r.v.displayStart(0.0);
    r.v.displayLength(1000.0);
    r.Mouse(0.5f, 0.f, view::DOWN);
    r.Mouse(0.5f, -2.f, view::DRAG);
    CHECK(r.v.displayLength() == doctest::Approx(250.0));
    CHECK(r.v.displayStart() == doctest::Approx(375.0));
    // Never shorter than one sample.
    r.Mouse(0.5f, -100.f, view::UP);
    CHECK(r.v.displayLength() == doctest::Approx(1.0));
  }

  TEST_CASE("waveformView: DRAW writes the amplitude, and lines between moves (#1151)") {
    Rig r(Silent());
    r.v.mode(view::DRAW);
    // x = (f + 0.5) / 1000 is sample f; y = 0.5 is 0.5 at no zoom.
    view::event e = r.Mouse(0.1005f, 0.5f, view::DOWN);
    CHECK(e.edited);
    CHECK(e.y == 0.5f);
    CHECK_FALSE(e.selection);
    CHECK_FALSE(e.link);
    CHECK(At(*r.buffer, 0, 100) == 0.5f);
    CHECK(At(*r.buffer, 0, 99) == 0.f);
    // A drag to sample 110 at -0.5: every sample between on the line.
    r.Mouse(0.1105f, -0.5f, view::DRAG);
    for (UInt f = 100; f <= 110; ++f) {
      CAPTURE(f);
      CHECK(At(*r.buffer, 0, f) ==
            doctest::Approx(0.5 - static_cast<double>(f - 100) / 10.0).epsilon(1e-6));
    }
    // Backwards, to sample 105 at 1.
    r.Mouse(0.1055f, 1.f, view::UP);
    for (UInt f = 105; f <= 110; ++f)
      CHECK(At(*r.buffer, 0, f) ==
            doctest::Approx(-0.5 + 1.5 * static_cast<double>(110 - f) / 5.0).epsilon(1e-6));
    CHECK(At(*r.buffer, 0, 111) == 0.f);
    // Up: a drag no longer draws.
    CHECK_FALSE(r.Mouse(0.5005f, 1.f, view::DRAG).edited);
    CHECK(At(*r.buffer, 0, 500) == 0.f);

    // The amplitude is (y - voffset) / vzoom; clipdraw holds it to the display.
    r.v.verticalZoom(2.f).verticalOffset(0.5f);
    CHECK(r.v.amplitude(1.f) == 0.25f);
    CHECK(r.v.amplitude(-1.f) == -0.75f);
    r.Mouse(0.2005f, 0.9f, view::DOWN);
    CHECK(At(*r.buffer, 0, 200) == doctest::Approx(0.2));
    r.v.verticalZoom(1.f).verticalOffset(0.f);
    r.Mouse(0.3005f, 3.f, view::DOWN);
    CHECK(At(*r.buffer, 0, 300) == 3.f);
    r.v.clipDraw(true);
    e = r.Mouse(0.3005f, 3.f, view::DOWN);
    CHECK(e.y == 3.f); // the mouse is reported as it is
    CHECK(At(*r.buffer, 0, 300) == 1.f);
    r.Mouse(0.3005f, -3.f, view::UP);
    CHECK(At(*r.buffer, 0, 300) == -1.f);
  }

  TEST_CASE("waveformView: DRAW writes the channel shown, and nothing without samples (#1151)") {
    Rig r(sampleBuffer::create(2, 1000, 1000.f));
    r.v.mode(view::DRAW).channel(1);
    r.Mouse(0.5005f, 0.25f, view::DOWN);
    CHECK(At(*r.buffer, 1, 500) == 0.25f);
    CHECK(At(*r.buffer, 0, 500) == 0.f);
    r.v.channel(5); // a channel the buffer lacks
    CHECK_FALSE(r.Mouse(0.5f, 0.75f, view::DOWN).edited);
    // No samples: nothing written, the mouse still reported.
    waveformView alone;
    alone.mode(view::DRAW);
    const view::event e = alone.mouse(0.5f, 0.5f, view::DOWN);
    CHECK(e.mouse);
    CHECK_FALSE(e.edited);
    // NONE: only the mouse.
    Rig n(Silent());
    n.v.mode(view::NONE);
    const view::event m = n.Mouse(0.5f, 0.5f, view::DOWN);
    CHECK(m.mouse);
    CHECK_FALSE(m.selection);
    CHECK_FALSE(m.link);
    CHECK_FALSE(m.edited);
    CHECK(n.v.selectionStart() == 0.0);
  }

  TEST_CASE("waveformView: snaps to the grid in each unit, to ticks, from the offset (#1151)") {
    Rig r(Silent()); // 1000 samples at 1000 Hz
    r.v.snap(view::SNAP_GRID).grid(100.0);
    CHECK(r.v.gridSpacing() == 100.0);
    CHECK(r.v.snapped(149.0) == 100.0);
    CHECK(r.v.snapped(151.0) == 200.0);
    r.v.offset(30.0);
    CHECK(r.v.snapped(149.0) == 130.0);
    r.v.offset(0.0);
    r.Gesture(0.23f, {0.78f});
    CHECK(r.v.selectionStart() == doctest::Approx(200.0));
    CHECK(r.v.selectionEnd() == doctest::Approx(800.0));

    r.v.unit(view::SAMPLES).grid(250.0); // 250 samples at 1000 Hz
    CHECK(r.v.gridSpacing() == doctest::Approx(250.0));
    r.v.unit(view::PHASE).grid(0.125);
    CHECK(r.v.gridSpacing() == doctest::Approx(125.0));
    r.v.unit(view::BPM).tempo(240.0, 2); // a bar is 2 beats of 250 ms
    CHECK(r.v.gridSpacing() == doctest::Approx(500.0));
    CHECK(r.v.snapped(700.0) == doctest::Approx(500.0));
    r.v.unit(view::MS).grid(100.0).ticks(4).snap(view::SNAP_TICK);
    CHECK(r.v.snapped(137.0) == doctest::Approx(125.0));
    r.v.ticks(0);
    CHECK(r.v.snapped(137.0) == doctest::Approx(100.0));
    // No grid: no snap. Held within the buffer.
    r.v.grid(0.0).snap(view::SNAP_GRID);
    CHECK(r.v.gridSpacing() == 0.0);
    CHECK(r.v.snapped(137.5) == 137.5);
    CHECK(r.v.snapped(-4.0) == 0.0);
    CHECK(r.v.snapped(1500.0) == doctest::Approx(1000.0));
  }

  TEST_CASE("waveformView: snaps to the nearest zero crossing (#1151)") {
    // A sine of period 100 samples starting at 0.1 rad: it crosses zero
    // between samples 48 and 49, 98 and 99, ...: the frame nearer to 0.
    auto b = Make(1, 1000, 1000.f, [](UInt, UInt n) {
      return static_cast<Flt>(
          std::sin(2.0 * 3.14159265358979 * (static_cast<double>(n) + 1.6) / 100.0));
    });
    Rig r(b);
    std::vector<UInt> crossings;
    for (UInt n = 0; n + 1 < 1000; ++n) {
      const Flt a = At(*b, 0, n), z = At(*b, 0, n + 1);
      if ((a < 0.f) != (z < 0.f)) crossings.push_back(std::fabs(a) <= std::fabs(z) ? n : n + 1);
    }
    REQUIRE(crossings.size() > 10);
    r.v.snap(view::SNAP_ZERO);
    sampleWriteView w = r.writer.pin();
    for (UInt target : {120u, 333u, 702u}) {
      CAPTURE(target);
      UInt best = crossings[0];
      for (UInt c : crossings)
        if (std::abs(static_cast<int>(c) - static_cast<int>(target)) <
            std::abs(static_cast<int>(best) - static_cast<int>(target)))
          best = c;
      CHECK(r.v.snapped(static_cast<double>(target), &w) == doctest::Approx(best));
      CHECK(std::fabs(At(*b, 0, best)) < 0.04f);
    }
    // Without samples, or none to find, the time is kept.
    CHECK(r.v.snapped(333.0) == 333.0);
    auto flat = Make(1, 1000, 1000.f, [](UInt, UInt) { return 0.5f; });
    Rig f(flat);
    f.v.snap(view::SNAP_ZERO);
    sampleWriteView fw = f.writer.pin();
    CHECK(f.v.snapped(333.0, &fw) == 333.0);
  }

  TEST_CASE("waveformView: the line in each unit, and fitTempo (#1151)") {
    waveformView v;
    v.shape(2000, 1000.f); // 2000 ms
    v.line(250.0);
    CHECK(v.line() == 250.0);
    v.line(-1.0);
    CHECK(v.line() == -1.0);
    v.line(NaN);
    CHECK(v.line() == -1.0);
    v.lineUnit(view::SAMPLES).line(500.0);
    CHECK(v.line() == doctest::Approx(500.0));
    v.lineUnit(view::PHASE).line(0.25);
    CHECK(v.line() == doctest::Approx(500.0));
    v.lineUnit(view::BPM).tempo(60.0).offset(100.0).line(1.5); // 1.5 beats of 1000 ms
    CHECK(v.line() == doctest::Approx(1600.0));

    // The selection 300..800 is a bar of 4 beats: 4 * 60000 / 500 = 480.
    CHECK_FALSE(v.fitTempo().tempo); // empty selection
    v.tempo(120.0, 4);
    v.selectionStart(800.0);
    v.selectionEnd(300.0);
    CHECK(v.fitTempo().tempo);
    CHECK(v.tempo() == doctest::Approx(480.0));
    CHECK(v.unit() == view::BPM);
    CHECK(v.offset() == doctest::Approx(300.0));
    CHECK(v.gridSpacing() == doctest::Approx(500.0));
    v.tempo(-5.0, 0).tempo(NaN, 0);
    CHECK(v.tempo() == doctest::Approx(480.0));
    CHECK(v.beats() == 4);
    v.verticalZoom(0.f);
    CHECK(v.verticalZoom() == view::MIN_VERTICAL_ZOOM);
  }

  TEST_CASE("waveformView: changing the mode ends a gesture (#1151)") {
    Rig r(Silent());
    r.Mouse(0.1f, 0.f, view::DOWN);
    r.v.mode(view::SELECT);
    const view::event e = r.Mouse(0.9f, 0.f, view::DRAG);
    CHECK_FALSE(e.link);
    CHECK(r.v.selectionEnd() == doctest::Approx(100.0));
  }

  TEST_CASE("waveformView: mouse gestures allocate nothing (#1151)") {
    Rig r(Silent());
    {
      TestHelpers::ProbeScope probe;
      for (int m = view::NONE; m <= view::DRAW; ++m) {
        r.v.mode(static_cast<view::MODE>(m)).snap(m % 2 == 0 ? view::SNAP_ZERO : view::SNAP_GRID);
        sampleWriteView w = r.writer.pin();
        r.v.mouse(0.1f, 0.2f, view::DOWN, &w);
        r.v.mouse(0.6f, 0.4f, view::DRAG, &w);
        r.v.mouse(0.3f, -0.4f, view::UP, &w);
      }
      r.v.undo();
      r.v.fitTempo();
      r.v.link(1.0, 2.0, 3.0, 4.0);
      r.v.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
