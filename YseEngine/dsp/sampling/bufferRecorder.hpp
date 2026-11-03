/*
  ==============================================================================

    bufferRecorder.hpp
    Records signals into a sample buffer (issue #1143).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_BUFFERRECORDER_HPP
#define YSE_DSP_SAMPLING_BUFFERRECORDER_HPP

#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"
#include "sampleRegion.hpp"

namespace YSE {
  namespace DSP {

    class sampleWriteView;

    /**
     *  @brief Records one or more signals into a ``sampleBuffer``: what the
     *         patcher's ``~record`` does, for live looping and sampling.
     *
     *  Input ``i`` is written into channel ``i`` of the buffer, one sample
     *  per frame, in place through the ``sampleWriteView`` a
     *  ``sampleBufferWriter`` pins for the block. An input the buffer has no
     *  channel for is dropped, and channels the inputs do not reach are left
     *  as they are. Samples are written as they come, without resampling: a
     *  buffer whose rate differs from the engine's plays them back at the
     *  speed its own rate gives.
     *
     *  - **Recording** (``record``). ``record(true)`` starts at the start
     *    point, also while recording already runs; ``record(false)`` stops.
     *    With **append** on, a start resumes where the last recording
     *    stopped instead, unless that frame is outside the loop points.
     *  - **Loop points** (``loopPoints``, in milliseconds). Recording covers
     *    the span from the start point up to the end point. An end of 0 (the
     *    default) is the end of the buffer; ``reset`` sets both back to 0,
     *    the whole buffer. The points are turned into frames every block at
     *    the buffer's sample rate and length then (``sampleRegion::fromMs``),
     *    so they follow a buffer that is resized or reloaded. A start point
     *    moved past the frame being written moves the recording to it.
     *  - **The end.** Reaching the end point stops the recording, or, with
     *    **loop** on, carries on from the start point.
     *  - **Sync output.** The block ``operator()`` returns rises from 0 at
     *    the start point to 1 at the end point while recording, the
     *    position of each sample written (``sampleRegion::phase``), and is 0
     *    while not.
     *
     *  With no buffer bound, an empty buffer, or loop points that hold no
     *  frame, nothing is written and the sync output is 0, but a recording
     *  that was started stays started: it writes as soon as there is room.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     writer.bind(DSP::sampleBufferRegistry::acquire("loop"));
     *     rec.loop(true).record(true);
     *
     *     // audio thread, once per block
     *     DSP::sampleWriteView view = writer.pin();
     *     DSP::buffer& sync = rec(view, input);
     *
     *  Nothing is derived from the sample rate except the frames of the loop
     *  points, which come from the view each block. The sync output is
     *  sized to ``STANDARD_BUFFERSIZE`` when the object is made; a shorter
     *  block resizes it down without allocating, and a longer one is cut to
     *  ``STANDARD_BUFFERSIZE``. Processing allocates nothing and takes no
     *  lock. Not thread-safe: one thread at a time, and the setters on that
     *  thread too.
     */
    class API bufferRecorder {
    public:
      /** @brief The most inputs one block takes. */
      static constexpr UInt MAX_CHANNELS = 64;

      bufferRecorder();

      /** @brief Start recording (at the start point, or with append where
       *         the last recording stopped), or stop it. */
      bufferRecorder& record(bool on);
      /** @brief Whether a recording runs. False once it stopped at the end
       *         point. */
      bool recording() const {
        return isRecording;
      }

      /** @brief Whether a start resumes where the last recording stopped. */
      bufferRecorder& append(bool on) {
        appendMode = on;
        return *this;
      }
      bool append() const {
        return appendMode;
      }

      /** @brief Whether reaching the end point carries on from the start
       *         point instead of stopping. */
      bufferRecorder& loop(bool on) {
        looping = on;
        return *this;
      }
      bool loop() const {
        return looping;
      }

      /** @brief The start and end points, in milliseconds. An end of 0 is
       *         the end of the buffer. */
      bufferRecorder& loopPoints(Flt startMs, Flt endMs) {
        startPoint = startMs;
        endPoint = endMs;
        return *this;
      }
      bufferRecorder& loopStart(Flt ms) {
        startPoint = ms;
        return *this;
      }
      Flt loopStart() const {
        return startPoint;
      }
      bufferRecorder& loopEnd(Flt ms) {
        endPoint = ms;
        return *this;
      }
      Flt loopEnd() const {
        return endPoint;
      }

      /** @brief Both loop points back to 0: recording covers the whole
       *         buffer. Recording itself goes on. */
      bufferRecorder& reset() {
        return loopPoints(0.f, 0.f);
      }

      /** @brief The loop points of @p view in frames, as the next block
       *         uses them. */
      sampleRegion region(const sampleWriteView& view) const;

      /** @brief The frame the next sample goes to; where the last recording
       *         stopped while none runs. */
      UInt position() const {
        return cursor;
      }

      /** @brief One block of one input, into channel 0. Returns the sync
       *         output, as long as @p in, up to ``STANDARD_BUFFERSIZE``. */
      buffer& operator()(sampleWriteView& view, buffer& in);

      /**
       *  @brief One block of @p count inputs, input ``i`` into channel
       *         ``i``. Returns the sync output.
       *
       *  The block is as long as the shortest input, up to
       *  ``STANDARD_BUFFERSIZE``. None of the inputs may be null. Inputs past
       *  ``MAX_CHANNELS`` are dropped, and no input at all is an empty
       *  block.
       */
      buffer& operator()(sampleWriteView& view, buffer* const* inputs, UInt count);

      /** @brief The sync output of the last call. */
      buffer& out() {
        return output;
      }

    private:
      bool isRecording = false;
      bool starting = false; // a start waiting for a block with room
      bool appendMode = false;
      bool looping = false;
      Flt startPoint = 0.f;
      Flt endPoint = 0.f;
      UInt cursor = 0;
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_BUFFERRECORDER_HPP
