/*
  ==============================================================================

    lfo.cpp
    Created: 18 Sep 2015 6:10:24pm
    Author:  yvan

  ==============================================================================
*/

#include "lfo.hpp"
#include "../utils/misc.hpp"
#include "oscillators.hpp"

// static lookup waves
YSE::DSP::fileBuffer LfoSawTable(0);
YSE::DSP::fileBuffer LfoTriangleTable(0);
YSE::DSP::fileBuffer LfoSineTable(0);

namespace {

  // Waveform table resolution: samples per LFO cycle.
  //
  // Deliberately a constant and *not* SAMPLERATE (issue #636). The tables used
  // to be sized to the live SAMPLERATE at construction while operator() wrapped
  // the cursor on the SAMPLERATE of the moment, so any rate change after the
  // last lfo was constructed made the wrap bound larger than the table and the
  // audio thread read past its end (44100 -> 48000 = a 3900 sample overrun;
  // the SIGSEGV of issue #48, which PR #71 only fixed for the construction
  // order that existed then).
  //
  // Decoupling resolution from rate removes the failure mode at the root
  // instead of adding another guard: there is no rate-derived table state left
  // to go stale, so the tables are built exactly once per process and never
  // resized again. 44100 keeps the resolution — and therefore the output —
  // bit-identical to the historical 44.1 kHz behaviour.
  constexpr UInt LFO_TABLE_LENGTH = 44100;

  // Anti-click flyback of the saw table, in table samples.
  constexpr UInt LFO_SAW_FLYBACK = 200;

  // Table-index advance per output sample, derived once per block from the live
  // engine rate. Never call this per sample: it divides.
  //
  // The result is clamped into [0, tableLength] so that the single wrap
  // subtraction in renderTable() always lands back inside the table. The
  // clamp is what makes a negative or NaN frequency (which used to produce a
  // garbage (UInt) cast and an unbounded index) degrade to a static LFO
  // instead of an out-of-bounds read. Note the !(x >= 0) spelling: it is
  // false-for-NaN on purpose, unlike (x < 0).
  Flt lfoPhaseStep(Flt frequency, Flt tableLength) {
    const Flt rate = static_cast<Flt>(YSE::SAMPLERATE);
    if (!(rate > 0.f)) return 0.f;
    const Flt step = frequency * tableLength / rate;
    if (!(step >= 0.f)) return 0.f;
    if (step > tableLength) return tableLength;
    return step;
  }

  // The single table read path shared by every table-driven LFO shape.
  //
  // There is exactly one of these on purpose. The bound the cursor wraps on is
  // read from `table` itself, in the same block that indexes it, so the wrap
  // bound and the allocation cannot disagree — whatever SAMPLERATE does before,
  // during or after. That is the property #48/#71 lacked: their guard lived on
  // a path (the constructor) a later rate change could not reach, while the
  // wrap bound kept tracking a value with no relation to the allocation.
  void renderTable(YSE::DSP::fileBuffer& table, YSE::DSP::buffer& out, Flt& cursor, Flt frequency,
                   bool reversed) {
    UInt samplesToProcess = out.getLength();
    Flt* dst = out.getPtr();

    const Flt limit = static_cast<Flt>(table.getLength());
    if (!(limit > 0.f)) {
      // Tables never built (or built empty): emit the neutral LFO value rather
      // than index an empty allocation.
      while (samplesToProcess--)
        *dst++ = 1.f;
      cursor = 0.f;
      return;
    }

    const Flt step = lfoPhaseStep(frequency, limit);

    // Normalise once per block, so a cursor left over from an earlier state
    // (or turned into a NaN by a bad frequency) can never reach the cast.
    if (!(cursor >= 0.f) || cursor >= limit) cursor = 0.f;

    const Flt* src = table.getPtr();

    if (reversed) {
      while (samplesToProcess--) {
        // Wrap before the cast: (UInt)negative-float is implementation-defined
        // and on arm64 produces a huge index (issue #48).
        if (cursor < 0.f) cursor += limit;
        *dst++ = src[static_cast<UInt>(cursor)];
        cursor -= step;
      }
    } else {
      while (samplesToProcess--) {
        *dst++ = src[static_cast<UInt>(cursor)];
        cursor += step;
        if (cursor >= limit) cursor -= limit;
      }
    }
  }

  // Build the shared waveform tables. Control thread only — this allocates.
  //
  // Runs at most once per process: the tables have a fixed length, so after the
  // first lfo is constructed the early-out below is always taken and the
  // buffers are never reallocated under an audio thread that may be reading
  // them. A rate change needs no rebuild at all, because nothing here depends
  // on the rate: the sine is generated at exactly one cycle per table length
  // whatever SAMPLERATE happens to be.
  void buildLfoTables() {
    if (LfoSawTable.getLength() == LFO_TABLE_LENGTH) return;

    const UInt length = LFO_TABLE_LENGTH;

    LfoSawTable.resize(length);
    LfoSawTable.drawLine(0, length - LFO_SAW_FLYBACK, 1.f, 0.f);
    LfoSawTable.drawLine(length - LFO_SAW_FLYBACK, length, 0.f, 1.f);

    LfoTriangleTable.resize(length);
    LfoTriangleTable.drawLine(0, (UInt)(length * 0.5f), 0.f, 1.f);
    LfoTriangleTable.drawLine((UInt)(length * 0.5f), length, 1.f, 0.f);

    LfoSineTable.resize(length);
    // perhaps this is a bit clumsy...
    YSE::DSP::sine s;
    // One full cycle across the table, independent of the engine rate: at
    // SAMPLERATE == LFO_TABLE_LENGTH this is the historical s(1).
    const Flt tableHz = static_cast<Flt>(YSE::SAMPLERATE) / static_cast<Flt>(length);
    for (UInt i = 0; i < length; i += YSE::STANDARD_BUFFERSIZE) {
      // The table length is not a multiple of STANDARD_BUFFERSIZE, so the last
      // block is partial. Clamp the copy length: buffer::copyFrom early-returns
      // on an out-of-range destination, which used to leave the final
      // length % STANDARD_BUFFERSIZE (68) samples of the table at zero (#643).
      const UInt remaining = length - i;
      const UInt block =
          remaining < YSE::STANDARD_BUFFERSIZE ? remaining : YSE::STANDARD_BUFFERSIZE;
      LfoSineTable.copyFrom(s(tableHz), 0, i, block);
    }
    LfoSineTable += 1;
    LfoSineTable *= 0.5;
  }

} // namespace

// lineLength / currentLineValue / previousLineValue were left indeterminate
// until issue #644: the LFO_SQUARE / LFO_RANDOM branch reads all three before
// its first-call reset assigns them, which is UB on the audio thread. Zero is
// behaviour-preserving there — `phaseLength < 0` is never true, and the
// `previousType != type` reset overwrites lineLength / currentLineValue on the
// first call anyway — while previousLineValue == 0.f makes the first block's
// anti-click ramp start from a defined 0 instead of stack garbage.
YSE::DSP::lfo::lfo()
  : cursor(0.f),
    previousType(LFO_NONE),
    lineLength(0),
    currentLineValue(0.f),
    previousLineValue(0.f) {
  result = 1;
  buildLfoTables();
}

YSE::DSP::buffer& YSE::DSP::lfo::operator()(
    LFO_TYPE type, Flt frequency,
    UInt size) { // NOSONAR S3776: per-LFO-type fast-path dispatch in audio-thread inner loop;
                 // switch cases are deliberately inlined for branch-predictor stability
  if (result.getLength() != size) result.resize(size);
  // avoid divisions by zero
  if (frequency == 0) type = LFO_NONE;

  switch (type) {
  case LFO_NONE: {
    result = 1;
    previousType = type;
    break;
  }

  case LFO_RANDOM:
  case LFO_SQUARE: {
    // shorten current value if new frequency is lower
    UInt phaseLength = (UInt)(SAMPLERATE / frequency * 0.5f);
    if (phaseLength < lineLength) lineLength = phaseLength;

    if (previousType != type) {
      lineLength = phaseLength;
      currentLineValue = type == LFO_SQUARE ? 1.f : RandomF();
      previousType = type;
    }

    if (lineLength > result.getLength()) {
      lineLength -= result.getLength();
      result = currentLineValue;
    } else {
      UInt samplesToProcess = result.getLength();
      UInt pos = 0;
      while (samplesToProcess) {
        UInt steps = lineLength > samplesToProcess ? samplesToProcess : lineLength;
        if (previousLineValue != currentLineValue) {
          UInt rampSteps = steps > 200 ? 200 : steps;
          result.drawLine(pos, pos + rampSteps, previousLineValue, currentLineValue);
          result.drawLine(pos + rampSteps, pos + steps, currentLineValue);
          previousLineValue = currentLineValue;
        } else {
          result.drawLine(pos, pos + steps, currentLineValue);
        }

        lineLength -= steps;

        if (!lineLength) {
          previousLineValue = currentLineValue;
          if (type == LFO_SQUARE)
            currentLineValue = currentLineValue > 0.9f ? 0.f : 1.f;
          else {
            // type = LFO_RANDOM
            currentLineValue = RandomF();
          }
          lineLength = phaseLength;
        }

        pos += steps;
        samplesToProcess -= steps;
      }
    }
    break;
  }

  case LFO_SAW: {
    previousType = LFO_SAW;
    renderTable(LfoSawTable, result, cursor, frequency, false);
    break;
  }

  case LFO_SAW_REVERSED: {
    previousType = LFO_SAW_REVERSED;
    renderTable(LfoSawTable, result, cursor, frequency, true);
    break;
  }

  case LFO_TRIANGLE: {
    previousType = LFO_TRIANGLE;
    renderTable(LfoTriangleTable, result, cursor, frequency, false);
    break;
  }

  case LFO_SINE: {
    previousType = LFO_SINE;
    renderTable(LfoSineTable, result, cursor, frequency, false);
    break;
  }
  }

  previousLineValue = result.getBack();
  return result;
}
