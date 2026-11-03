/*
  ==============================================================================

    sampleAccess.cpp
    Reading and writing single samples of a sample buffer by index
    (issues #1144, #1145).

  ==============================================================================
*/

#include "sampleAccess.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>

using namespace YSE::DSP;

namespace {

  // A frame index from a signal sample: truncated toward 0. False for a
  // negative index (-1 pauses ~poke), not a number, or one past the end.
  bool FrameOf(Flt index, UInt frames, UInt& frame) {
    if (!(index >= 0.f)) return false;
    if (!(static_cast<double>(index) < static_cast<double>(frames))) return false;
    frame = static_cast<UInt>(index);
    return frame < frames;
  }

  // The frame ~index reads for a signal sample: truncated toward 0 and
  // clamped to the buffer, so every index reads a frame. Below 0 or not a
  // number is the first; at or past the end the last. `frames` > 0.
  inline UInt ClampedFrameOf(Flt index, UInt frames) {
    if (!(index > 0.f)) return 0;
    if (!(static_cast<double>(index) < static_cast<double>(frames))) return frames - 1;
    return static_cast<UInt>(index);
  }

  inline UInt BlockLength(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

} // namespace

// ─── sampleAccess ─────────────────────────────────────────────────────────────

bool sampleAccess::read(const sampleWriteView& view, Int index, Flt& out) const {
  if (index < 0) return false;
  const auto frame = static_cast<UInt>(index);
  if (targetChannel >= view.channels() || frame >= view.frames()) return false;
  out = view.at(targetChannel, frame);
  return true;
}

bool sampleAccess::write(sampleWriteView& view, Int index, Flt value) const {
  if (index < 0) return false;
  if (clipping) value = std::min(1.f, std::max(-1.f, value));
  return view.set(targetChannel, static_cast<UInt>(index), value);
}

// ─── sampleIndexWriter ────────────────────────────────────────────────────────

UInt sampleIndexWriter::operator()(sampleWriteView& view, buffer& values, buffer& indexes) const {
  if (targetChannel >= view.channels()) return 0;
  const UInt frames = view.frames();
  const UInt length = std::min(values.getLength(), indexes.getLength());
  const Flt* value = values.getPtr();
  const Flt* index = indexes.getPtr();
  UInt written = 0;
  for (UInt i = 0; i < length; ++i) {
    UInt frame = 0;
    if (!FrameOf(index[i], frames, frame)) continue;
    view.set(targetChannel, frame, value[i]);
    written++;
  }
  return written;
}

UInt sampleIndexWriter::operator()(sampleWriteView& view, Flt value, buffer& indexes) const {
  if (targetChannel >= view.channels()) return 0;
  const UInt frames = view.frames();
  const UInt length = indexes.getLength();
  const Flt* index = indexes.getPtr();
  UInt written = 0;
  for (UInt i = 0; i < length; ++i) {
    UInt frame = 0;
    if (!FrameOf(index[i], frames, frame)) continue;
    view.set(targetChannel, frame, value);
    written++;
  }
  return written;
}

UInt sampleIndexWriter::operator()(sampleWriteView& view, buffer& values, Flt index) const {
  const UInt length = values.getLength();
  if (length == 0) return 0;
  return (*this)(view, values.getPtr()[length - 1], index);
}

UInt sampleIndexWriter::operator()(sampleWriteView& view, Flt value, Flt index) const {
  UInt frame = 0;
  if (!FrameOf(index, view.frames(), frame)) return 0;
  return view.set(targetChannel, frame, value) ? 1 : 0;
}

// ─── sampleIndexReader ────────────────────────────────────────────────────────

sampleIndexReader::sampleIndexReader() : output(YSE::STANDARD_BUFFERSIZE) {}

Flt sampleIndexReader::read(const sampleView& view, Flt index) const {
  const Flt* samples = view.channel(targetChannel);
  if (samples == nullptr || view.frames() == 0) return 0.f;
  return detail::LoadSample(samples + ClampedFrameOf(index, view.frames()));
}

buffer& sampleIndexReader::operator()(const sampleView& view, buffer& indexes) {
  const UInt length = BlockLength(indexes.getLength());
  // Within the capacity set in the constructor: no allocation.
  if (output.getLength() != length) output.resize(length);
  Flt* out = output.getPtr();
  const Flt* samples = view.channel(targetChannel);
  const UInt frames = view.frames();
  if (samples == nullptr || frames == 0) {
    std::fill_n(out, length, 0.f);
    return output;
  }
  const Flt* index = indexes.getPtr();
  for (UInt i = 0; i < length; ++i)
    out[i] = detail::LoadSample(samples + ClampedFrameOf(index[i], frames));
  return output;
}

buffer& sampleIndexReader::operator()(const sampleView& view, Flt index, UInt length) {
  length = BlockLength(length);
  if (output.getLength() != length) output.resize(length);
  std::fill_n(output.getPtr(), length, read(view, index));
  return output;
}
