/*
  ==============================================================================

    delay.cpp
    Created: 31 Jan 2014 2:52:41pm
    Author:  yvan

  ==============================================================================
*/

#include "delay.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

#define XTRASAMPS 4
#define DEFDELVS 64
#define SAMPBLK 4

namespace {
  // The line length (excluding the XTRASAMPS guard) process() uses for a size
  // in ms at a rate. The product is 64-bit: as a UInt it wrapped once
  // sizeMs * rate passed 2^32 (a 45 s line at 96 kHz came out ~0.26 s, issue
  // #1212). Below that the result is bit-identical to the old expression.
  Int lineLength(UInt sizeMs, UInt rate) {
    Int nsamps = (Int)(static_cast<std::uint64_t>(sizeMs) * rate * 0.001f);
    if (nsamps < 1) nsamps = 1;
    nsamps += ((-nsamps) & (SAMPBLK - 1));
    nsamps += DEFDELVS;
    return nsamps;
  }
} // namespace

// All memory the line will use is allocated here, for the size at the highest
// rate it supports, so process() and read() never allocate (issue #1210).
// bufferlength starts at `size` as it always did; the first process() derives
// the real length.
YSE::DSP::delay::delay(Int size, UInt highestSampleRate)
  : bufferlength(size), phase(XTRASAMPS), currentLength(0), size(size) {
  const Int sizeMs = size < 0 ? 0 : size;
  std::size_t storage =
      static_cast<std::size_t>(lineLength(static_cast<UInt>(sizeMs), highestSampleRate)) +
      XTRASAMPS;
  // The initial bufferlength must address memory too (read() before the first
  // process() clamps against it).
  if (storage < static_cast<std::size_t>(sizeMs) + XTRASAMPS)
    storage = static_cast<std::size_t>(sizeMs) + XTRASAMPS;
  buffer.assign(storage, 0.f);
}

// Copy every member outright (issue #639). The previous body was
// `delay(source.size);` — an unnamed temporary, not a delegating call — which
// left `buffer` empty while `bufferlength` / `phase` stayed indeterminate, so
// process() could write through a null buffer.data() whenever the stale
// `bufferlength` happened to match the computed block size. Copying the full
// state also preserves the line's history when perChannel<State>::ensure()
// relocates channel states on the device-restart resize path (see
// perChannel.hpp: "copying preserves their history"). This runs only on that
// non-steady-state path, where allocation is the accepted trade-off.
YSE::DSP::delay::delay(const YSE::DSP::delay& source)
  : bufferlength(source.bufferlength),
    buffer(source.buffer),
    phase(source.phase),
    currentLength(source.currentLength),
    size(source.size.load()) {}

YSE::DSP::delay& YSE::DSP::delay::setSize(UInt size) {
  this->size = size;
  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::reserve(UInt size, UInt highestSampleRate) {
  const std::size_t needed =
      static_cast<std::size_t>(lineLength(size, highestSampleRate)) + XTRASAMPS;
  // Grow only: vector::resize keeps the prefix and zero-fills the rest, which
  // is what the line expects of memory it has not used yet.
  if (needed > buffer.size()) buffer.resize(needed, 0.f);
  this->size = size;
  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::process(YSE::DSP::buffer& s) {
  {
    Int nsamps = lineLength(this->size, SAMPLERATE);
    // Clamp to the memory reserved up front instead of reallocating on the
    // audio thread (issue #1210): a longer size or a higher rate than the
    // line was sized for gets the longest line that fits, rounded down to a
    // whole SAMPBLK so the length stays on the same grid.
    const Int maxLength = static_cast<Int>(buffer.size()) - XTRASAMPS;
    if (nsamps > maxLength) nsamps = maxLength & ~(SAMPBLK - 1);
    if ((Int)bufferlength != nsamps) {
      // What the old buffer.resize(nsamps + XTRASAMPS, 0.f) did, in place:
      // keep the samples below both lengths, and zero the ones a longer line
      // newly takes in (the memory may hold a previous, longer line's tail).
      const Int oldUsed = static_cast<Int>(bufferlength) + XTRASAMPS;
      const Int newUsed = nsamps + XTRASAMPS;
      if (newUsed > oldUsed) {
        const Int from = oldUsed < 0 ? 0 : oldUsed;
        std::fill(buffer.begin() + from, buffer.begin() + newUsed, 0.f);
      }
      bufferlength = nsamps;
      phase = XTRASAMPS;
    }
  }

  currentLength = s.getLength();
  Flt* in = s.getPtr();
  UInt length = currentLength;
  Int ph = phase;
  Flt* vp = buffer.data();
  Flt* bp = vp + ph;
  Flt* ep = vp + bufferlength + XTRASAMPS;
  ph += currentLength;

  while (length--) {
    Flt f = *in++;
    *bp++ = f;
    if (bp == ep) {
      vp[0] = ep[-4];
      vp[1] = ep[-3];
      vp[2] = ep[-2];
      vp[3] = ep[-1];
      bp = vp + XTRASAMPS;
      ph -= bufferlength;
    }
  }
  bp = vp + phase;
  phase = ph;

  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::read(YSE::DSP::buffer& result, UInt delayTime) {
  // No resize of a short result on the audio thread (issue #1210): every
  // engine caller pre-sizes it to the block; a shorter one gets its first
  // getLength() samples of the delayed block.
  const UInt readLength = result.getLength() < currentLength ? result.getLength() : currentLength;
  // 64-bit product for the same reason as lineLength() (issue #1212).
  UInt delaySamples =
      (UInt)(static_cast<std::uint64_t>(SAMPLERATE) * delayTime * 0.001f) + currentLength;
  if (delaySamples < result.getLength())
    delaySamples = result.getLength();
  else if (delaySamples > bufferlength - DEFDELVS)
    delaySamples = bufferlength - DEFDELVS;

  Int ph = phase - delaySamples;

  Flt* vp = buffer.data();
  Flt* ep = vp + (bufferlength + XTRASAMPS);
  if (ph < 0) ph += bufferlength;
  Flt* bp = vp + ph;

  Flt* out = result.getPtr();

  Int length = static_cast<Int>(readLength);
  while (length--) {
    *out++ = *bp++;
    if (bp == ep) bp -= bufferlength;
  }

  return (*this);
}

YSE::DSP::delay& YSE::DSP::delay::read(YSE::DSP::buffer& result, YSE::DSP::buffer& delayTime) {
  // No resize of a short result on the audio thread (issue #1210); this
  // variant writes result.getLength() samples, as it always did.
  Flt* ctrl = delayTime.getPtr();
  Flt* out = result.getPtr();

  Flt* vp = buffer.data();
  // Flt * ep = vp + (impl->bufferlength + XTRASAMPS);

  for (UInt i = 0; i < result.getLength(); i++) {
    UInt delaySamples = (UInt)(SAMPLERATE * ctrl[i] * 0.001f) + currentLength;
    if (delaySamples < result.getLength())
      delaySamples = result.getLength();
    else if (delaySamples > bufferlength - DEFDELVS)
      delaySamples = bufferlength - DEFDELVS;

    Int ph = phase - delaySamples + i;
    if (ph < 0) ph += bufferlength;
    Flt* bp = vp + ph;
    *out++ = *bp;
  }

  return (*this);
}

void YSE::DSP::readInterpolated(YSE::DSP::buffer& ctrl, YSE::DSP::buffer& out,
                                YSE::DSP::buffer& buffer, UInt& pos) {
  Flt* input = ctrl.getPtr();
  Flt* output = out.getPtr();
  Int n = ctrl.getLength();
  Int nsamps = buffer.getLength();
  Flt limit = nsamps - n - 1.0f;
  Flt fn = n - 1.0f;

  Flt* vp = buffer.getPtr();
  Flt* bp = nullptr;
  Flt* wp = vp + pos;

  while (n--) {
    Flt delsamps = SAMPLERATE * *input++, frac;
    Int idelsamps;
    Flt a, b, c, d, cminusb;
    if (delsamps < 1.00001f) delsamps = 1.00001f;
    if (delsamps > limit) delsamps = limit;
    delsamps += fn;
    fn = fn - 1.0f;
    idelsamps = (Int)delsamps;
    frac = delsamps - static_cast<Flt>(idelsamps);
    bp = wp - idelsamps;
    if (bp < vp + 4) bp += nsamps;
    d = bp[-3];
    c = bp[-2];
    b = bp[-1];
    a = bp[0];
    cminusb = c - b;
    *output++ =
        b + frac * (cminusb - 0.1666667f * (1.0f - frac) *
                                  ((d - a - 3.0f * cminusb) * frac + (d + 2.0f * a - 3.0f * b)));
  }
  pos = (UInt)(bp - vp);
}