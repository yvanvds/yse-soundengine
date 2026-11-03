/*
  ==============================================================================

    bufferPlayer.hpp
    Position-based playback of a sample buffer (issue #1140).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_BUFFERPLAYER_HPP
#define YSE_DSP_SAMPLING_BUFFERPLAYER_HPP

#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"
#include "playerOutputs.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief Plays a ``sampleBuffer`` from a start to an end point at a
     *         speed, or at the positions a signal gives: what the patcher's
     *         ``~play`` does.
     *
     *  It reads through the ``sampleView`` a ``sampleBufferReader`` pins for
     *  the block, with the 4-point cubic kernel of ``interpolate4``
     *  (``sampleView::cubic``). Positions are milliseconds into the buffer,
     *  at the buffer's own sample rate.
     *
     *  - **Following a position signal** (``operator()(view, positions)``).
     *    Each output sample is the buffer at the position of the same input
     *    sample: a rising signal plays forward, a falling one backward, a
     *    still one repeats one sample. Positions are held inside the buffer
     *    (below 0 reads the first sample, past the end the last).
     *  - **Playing on its own** (``operator()(view, length)``). ``start``
     *    plays from a start point to an end point, at normal speed or over
     *    a given duration, backward when the end comes before the start.
     *    Forward it plays the span from the start up to (not including) the
     *    end; backward from the start down to just above the end. ``stop``
     *    stops, ``pause`` holds the position and ``resume`` goes on from it.
     *    An end point below 0 (the default) is the end of the buffer, and
     *    both points are kept inside the buffer, as long as it is in that
     *    block, so they follow a buffer that is resized or reloaded.
     *  - **The end.** Reaching the end point stops playback and makes
     *    ``done()`` true for that block, or, with **loop** on, carries on
     *    from the start point. A span that holds nothing ends at once.
     *    ``stop`` is not an end: ``done()`` stays false.
     *  - **Loop crossfade** (``loopInterp``, ``interpTime`` in ms, 50 by
     *    default). With both loop and loop interpolation on, the last
     *    ``interpTime`` ms before the loop jumps back are crossfaded with
     *    the sound leading up to the start point, so the jump does not
     *    click (``loopCrossfade``). A crossfade longer than the loop is
     *    the loop's length.
     *  - **Channels.** Output ``k`` plays buffer channel ``k``. Outputs the
     *    buffer has no channel for are silent; buffer channels past the
     *    last output are mixed in, channel ``k`` into output
     *    ``k % channels()``.
     *
     *  With no buffer bound, or an empty one, the output is silence, but
     *  playback that was started stays started and plays as soon as there
     *  is something to play. Stopped or paused, the output is silence.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     DSP::sampleBufferReader reader(DSP::sampleBufferRegistry::acquire("drums"));
     *     DSP::bufferPlayer play(2);
     *     play.start(0.f, 500.f, 250.f); // the first half second, twice as fast
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     play(view);
     *     DSP::buffer& left = play.out(0);
     *     if (play.done()) { ... }
     *
     *  The speed of normal playback comes from ``SAMPLERATE``, read every
     *  block, and the buffer's points from the view, so it follows the live
     *  sample rate. The outputs are sized to ``STANDARD_BUFFERSIZE`` when
     *  the channel count is set; a shorter block resizes them down without
     *  allocating, and a longer one is cut to ``STANDARD_BUFFERSIZE``.
     *  Processing allocates nothing and takes no lock. Not thread-safe: one
     *  thread at a time, and the setters on that thread too; ``channels``
     *  allocates, so set it before the audio thread sees the object.
     */
    class API bufferPlayer {
    public:
      /** @brief The most outputs a player has. */
      static constexpr UInt MAX_CHANNELS = playerOutputs::MAX_CHANNELS;
      /** @brief The default loop crossfade, in milliseconds. */
      static constexpr Flt DEFAULT_INTERP_TIME = 50.f;

      /** @brief A stopped player with @p outputs outputs (1 to
       *         ``MAX_CHANNELS``). Allocates. */
      explicit bufferPlayer(UInt outputs = 1);

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         not on the audio thread. */
      bufferPlayer& channels(UInt outputs);
      UInt channels() const {
        return outputs.channels();
      }

      /**
       *  @brief Play from @p startMs to @p endMs over @p durationMs.
       *
       *  An end below 0 is the end of the buffer; a duration of 0 or below
       *  plays at normal speed, otherwise as fast as covers the span in that
       *  time. An end before the start plays backward. A start below 0, or
       *  a value that is not a number, is 0 (the end: the buffer's end).
       *  ``start()`` plays the whole buffer at normal speed. Starts again
       *  while playing, also while paused.
       */
      bufferPlayer& start(Flt startMs = 0.f, Flt endMs = -1.f, Flt durationMs = 0.f);
      /** @brief Stop where playback is. Not an end: ``done()`` stays
       *         false. */
      bufferPlayer& stop();
      /** @brief Hold playback where it is; ``resume`` goes on from there.
       *         Nothing while not playing. */
      bufferPlayer& pause();
      /** @brief Go on after a ``pause``. Nothing unless paused. */
      bufferPlayer& resume();

      /** @brief Whether playback runs, paused or not. False once it reached
       *         the end point or was stopped. */
      bool playing() const {
        return isPlaying;
      }
      /** @brief Whether playback is paused. */
      bool paused() const {
        return isPlaying && isPaused;
      }
      /** @brief Whether the last block reached the end point (not in a
       *         loop): the moment to send a done notification. */
      bool done() const {
        return finished;
      }
      /** @brief Where playback is, in ms into the buffer: the position the
       *         next sample is read at. */
      Dbl position() const {
        return cursor;
      }

      /** @brief Whether reaching the end point carries on from the start
       *         point instead of stopping. */
      bufferPlayer& loop(bool on) {
        looping = on;
        return *this;
      }
      bool loop() const {
        return looping;
      }

      /** @brief Whether a loop crossfades across its jump. */
      bufferPlayer& loopInterp(bool on) {
        crossfading = on;
        return *this;
      }
      bool loopInterp() const {
        return crossfading;
      }

      /** @brief The loop crossfade, in milliseconds. Below 0, or not a
       *         number, is 0. */
      bufferPlayer& interpTime(Flt ms) {
        fadeMs = ms > 0.f ? ms : 0.f;
        return *this;
      }
      Flt interpTime() const {
        return fadeMs;
      }

      /** @brief One block of @p length samples, up to
       *         ``STANDARD_BUFFERSIZE``, playing on its own. Returns output
       *         0. */
      buffer& operator()(const sampleView& view, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block at the positions of @p positionsMs, in ms; as
       *         long as it, up to ``STANDARD_BUFFERSIZE``. Playback on its
       *         own neither moves nor ends meanwhile. Returns output 0. */
      buffer& operator()(const sampleView& view, buffer& positionsMs);

      /** @brief Output @p channel of the last block (from 0); the last
       *         output past the count. */
      buffer& out(UInt channel = 0) {
        return outputs.out(channel);
      }

    private:
      playerOutputs outputs;
      bool isPlaying = false;
      bool isPaused = false;
      bool starting = false; // a start waiting for a block with something to play
      bool finished = false;
      bool looping = false;
      bool crossfading = false;
      Flt fadeMs = DEFAULT_INTERP_TIME;
      Flt startPoint = 0.f;
      Flt endPoint = -1.f;
      Flt duration = 0.f;
      Dbl cursor = 0.0; // ms
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_BUFFERPLAYER_HPP
