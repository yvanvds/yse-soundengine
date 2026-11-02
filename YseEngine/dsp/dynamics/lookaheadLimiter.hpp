/*
  ==============================================================================

    lookaheadLimiter.hpp
    Brickwall lookahead peak limiter with linked channels (issue #1115).

  ==============================================================================
*/

#ifndef YSE_DSP_DYNAMICS_LOOKAHEADLIMITER_HPP
#define YSE_DSP_DYNAMICS_LOOKAHEADLIMITER_HPP

#include <cstdint>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A brickwall lookahead peak limiter: no output sample exceeds
     *         the threshold, and the gain comes down before a peak arrives
     *         instead of after.
     *
     *  Ported from MSP's ``limi~`` (and used for the role of
     *  ``omx.peaklim~``, with an algorithm of its own). The input is
     *  delayed by the *lookahead*, ``lookahead()`` samples, and the gain is
     *  computed from the undelayed input, so it can ramp down over the
     *  lookahead and reach the gain a peak needs by the time that peak
     *  leaves the delay.
     *
     *  Per sample, in this order:
     *
     *  1. Each channel is DC-blocked when ``dcBlock()`` is on (a one-pole
     *     high-pass at ``DC_BLOCK_HZ``), then multiplied by the preamp.
     *  2. The channels are **linked**: the level is the largest magnitude
     *     over all of them, and one gain is applied to every channel, so the
     *     balance between them does not move.
     *  3. The needed gain is ``threshold / level`` above the threshold and 1
     *     below it. Its minimum over the last ``lookahead + 1`` samples is
     *     the target.
     *  4. The gain follows the target down at once and recovers towards it
     *     over the release time: by a straight line from 0 to 1 in the
     *     release time (``LINEAR``), or with the release time as time
     *     constant (``EXPONENTIAL``, the default; it lands on the target
     *     once a step is too small to change a float).
     *  5. The gain is averaged over the last ``lookahead + 1`` samples, which
     *     turns each step down into a ramp as long as the lookahead. The
     *     average never exceeds the gain the delayed sample needs, so the
     *     limiting is brickwall; the delayed sample times the gain is also
     *     clamped to the threshold, against rounding.
     *  6. The result is multiplied by the postamp.
     *
     *  ``bypass(true)`` sends the delayed input through untouched (no DC
     *  block, preamp, limiting or postamp). The delay stays, so bypassing
     *  does not jump in time, and the gain keeps being computed, so turning
     *  bypass off again does not jump in level.
     *
     *  The latency is ``latency()``, the lookahead in samples. The delay line
     *  is allocated in the constructor for ``maxLookahead()`` samples;
     *  changing the lookahead within it never allocates, but clears the
     *  limiter, as ``clear()`` does (it moves where the delay reads).
     *
     *  Levels are in dB: threshold (default 0 dB, full scale), preamp and
     *  postamp (default 0 dB). The release is in ms (default 100). A value
     *  that is not finite is ignored. An input sample that is not finite
     *  counts as 0. The release and the DC block follow the live sample
     *  rate: they are rebuilt when ``SAMPLERATE`` changes.
     *
     *  Each block call processes at most ``STANDARD_BUFFERSIZE`` samples. Call
     *  the class from one thread at a time. Processing allocates nothing.
     */
    class API lookaheadLimiter {
    public:
      /** @brief How the gain recovers after a peak. */
      enum MODE {
        LINEAR = 0, ///< A straight line from gain 0 to 1 in the release time.
        EXPONENTIAL = 1, ///< A one-pole curve with the release time as time constant.
      };

      /** @brief The most channels a limiter takes. */
      static constexpr UInt MAX_CHANNELS = 64;
      /** @brief The largest delay line, in samples. */
      static constexpr UInt MAX_LOOKAHEAD = 65536;
      /** @brief The delay line when the constructor is not told, in samples. */
      static constexpr UInt DEFAULT_MAX_LOOKAHEAD = 512;
      /** @brief The lookahead until it is set, in samples. */
      static constexpr UInt DEFAULT_LOOKAHEAD = 100;
      /** @brief The threshold until it is set, in dB. */
      static constexpr Flt DEFAULT_THRESHOLD = 0.f;
      /** @brief The release until it is set, in ms. */
      static constexpr Flt DEFAULT_RELEASE = 100.f;
      /** @brief The longest release, in ms. */
      static constexpr Flt MAX_RELEASE = 60000.f;
      /** @brief The cutoff of the DC block, in Hz. */
      static constexpr Flt DC_BLOCK_HZ = 5.f;

      /** @brief A limiter for @p channels channels (1 to ``MAX_CHANNELS``)
       *         with a delay line of @p maxLookahead samples (at most
       *         ``MAX_LOOKAHEAD``). The lookahead starts at
       *         ``DEFAULT_LOOKAHEAD``, or the delay line if that is shorter.
       *         Everything is allocated here. */
      explicit lookaheadLimiter(UInt channels = 1, UInt maxLookahead = DEFAULT_MAX_LOOKAHEAD);

      /** @brief The number of channels. */
      UInt channels() const {
        return channelCount;
      }

      /** @brief The longest lookahead, in samples. */
      UInt maxLookahead() const {
        return capacity - 1;
      }

      /** @brief The lookahead in samples, at most ``maxLookahead()``. A
       *         change clears the limiter. */
      lookaheadLimiter& lookahead(UInt samples);

      /** @brief The lookahead in samples. */
      UInt lookahead() const {
        return delay;
      }

      /** @brief How much later than its input the output is, in samples:
       *         the lookahead. */
      UInt latency() const {
        return delay;
      }

      /** @brief The level no output sample exceeds before the postamp, in
       *         dB. Not finite: ignored. */
      lookaheadLimiter& threshold(Flt dB);
      /** @brief The threshold in dB. */
      Flt threshold() const {
        return thresholdDb;
      }

      /** @brief How long the gain takes to recover, in ms (0 to
       *         ``MAX_RELEASE``). Not finite: ignored. */
      lookaheadLimiter& release(Flt ms);
      /** @brief The release in ms. */
      Flt release() const {
        return releaseMs;
      }

      /** @brief The gain before the limiter, in dB. Not finite: ignored. */
      lookaheadLimiter& preamp(Flt dB);
      /** @brief The preamp in dB. */
      Flt preamp() const {
        return preampDb;
      }

      /** @brief The gain after the limiter, in dB. Not finite: ignored. */
      lookaheadLimiter& postamp(Flt dB);
      /** @brief The postamp in dB. */
      Flt postamp() const {
        return postampDb;
      }

      /** @brief The release curve. */
      lookaheadLimiter& mode(MODE value);
      /** @brief The release curve. */
      MODE mode() const {
        return releaseMode;
      }

      /** @brief Whether each channel is DC-blocked before the preamp. Off by
       *         default. */
      lookaheadLimiter& dcBlock(bool on);
      /** @brief Whether the DC block is on. */
      bool dcBlock() const {
        return dcOn;
      }

      /** @brief Whether the delayed input is sent through untouched. Off by
       *         default. */
      lookaheadLimiter& bypass(bool on);
      /** @brief Whether bypass is on. */
      bool bypass() const {
        return bypassOn;
      }

      /** @brief Empties the delay line and the DC block, and returns the gain
       *         to 1. Keeps the settings. */
      lookaheadLimiter& clear();

      /** @brief The gain applied to the last sample (1: no limiting). */
      Flt gain() const {
        return currentGain;
      }

      /** @brief The deepest gain reduction during the last call, in dB (0 or
       *         less). */
      Flt gainReduction() const;

      /** @brief One frame: ``channels()`` samples from @p in, one per channel,
       *         and ``channels()`` samples into @p out. */
      void process(const Flt* in, Flt* out);

      /** @brief One sample of a one-channel limiter. On a limiter with more
       *         channels, the others read 0 and their output is dropped. */
      Flt process(Flt in);

      /** @brief One block of channel 0; the other channels read silence.
       *         Returns ``output(0)``. */
      buffer& operator()(buffer& in);

      /** @brief One block: @p count input buffers, one per channel from 0.
       *         Channels without a buffer read silence, buffers past
       *         ``channels()`` are ignored. As long as the shortest buffer.
       *         Returns ``output(0)``; the others are in ``output()``. */
      buffer& operator()(buffer* const* in, UInt count);

      /** @brief The output of @p channel from the last block call. Past the
       *         last channel: the last channel's. */
      buffer& output(UInt channel);

    private:
      void Prepare();
      Flt Step(const Flt* in, Flt* out);

      UInt channelCount;
      UInt capacity; // delay-line length: maxLookahead + 1
      UInt delay = 0;

      Flt thresholdDb = DEFAULT_THRESHOLD;
      Flt releaseMs = DEFAULT_RELEASE;
      Flt preampDb = 0.f;
      Flt postampDb = 0.f;
      MODE releaseMode = EXPONENTIAL;
      bool dcOn = false;
      bool bypassOn = false;

      // Derived, rebuilt by Prepare() when a setting or the rate changes.
      UInt builtRate = 0;
      bool dirty = true;
      Flt thresholdLin = 1.f;
      Flt preampLin = 1.f;
      Flt postampLin = 1.f;
      Flt releaseStep = 1.f; // linear: per sample; exponential: one-pole coefficient
      Flt dcPole = 0.f;

      // State.
      std::vector<Flt> lines; // channelCount * capacity, channel-major
      std::vector<Flt> dcIn; // last input per channel
      std::vector<Flt> dcOut; // last output per channel
      std::vector<Flt> frameIn; // one frame for the block and mono calls
      std::vector<Flt> frameOut;
      std::vector<Flt> gains; // the released gain, last capacity samples
      std::vector<Flt> minValue; // monotonic queue of needed gains
      std::vector<std::uint64_t> minIndex;
      std::uint64_t head = 0; // queue front, counts up
      std::uint64_t tail = 0; // queue back, one past
      std::uint64_t frame = 0; // frames since clear()
      UInt writePos = 0;
      double gainSum = 0.0; // sum of the last delay + 1 released gains
      Flt released = 1.f;
      Flt currentGain = 1.f;
      Flt lowestGain = 1.f; // during the last call

      std::vector<buffer> outputs;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DYNAMICS_LOOKAHEADLIMITER_HPP
