/*
  ==============================================================================

    bufferRecorder.cpp
    Records signals into a sample buffer (issue #1143).

  ==============================================================================
*/

#include "bufferRecorder.hpp"
#include "sampleBuffer.hpp"

#include <algorithm>

using namespace YSE::DSP;

bufferRecorder::bufferRecorder() : output(YSE::STANDARD_BUFFERSIZE) {}

bufferRecorder& bufferRecorder::record(bool on) {
  isRecording = on;
  starting = on;
  return *this;
}

sampleRegion bufferRecorder::region(const sampleWriteView& view) const {
  return sampleRegion::fromMs(startPoint, endPoint, view.sampleRate(), view.frames());
}

buffer& bufferRecorder::operator()(sampleWriteView& view, buffer& in) {
  buffer* one = &in;
  return (*this)(view, &one, 1);
}

buffer& bufferRecorder::operator()(sampleWriteView& view, buffer* const* inputs, UInt count) {
  count = std::min(count, MAX_CHANNELS);
  UInt length = count > 0 ? YSE::STANDARD_BUFFERSIZE : 0;
  for (UInt c = 0; c < count; ++c)
    length = std::min(length, inputs[c]->getLength());
  // Within the capacity set in the constructor: no allocation.
  if (output.getLength() != length) output.resize(length);
  Flt* sync = output.getPtr();

  const sampleRegion span = region(view);
  UInt done = 0;
  if (isRecording && !span.empty()) {
    if (starting) {
      // Append resumes where the last recording stopped, while that is
      // still inside the loop points.
      if (!appendMode || !span.contains(cursor)) cursor = span.start;
      starting = false;
    }
    while (done < length) {
      if (cursor < span.start) cursor = span.start; // the start point moved past it
      if (cursor >= span.end) {
        if (!looping) break;
        cursor = span.start;
      }
      // The samples up to the end point, or the rest of the block: one
      // stretch of frames, written channel by channel.
      const UInt run = std::min(length - done, span.end - cursor);
      for (UInt c = 0; c < count; ++c)
        view.write(c, cursor, inputs[c]->getPtr() + done, run);
      for (UInt i = 0; i < run; ++i)
        sync[done + i] = span.phase(static_cast<Dbl>(cursor) + i);
      cursor += run;
      done += run;
    }
    // Stopped as the end point is reached, not a block later.
    if (!looping && cursor >= span.end) isRecording = false;
  }
  std::fill(sync + done, sync + length, 0.f);
  return output;
}
