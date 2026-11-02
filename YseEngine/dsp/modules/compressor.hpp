/*
  ==============================================================================

    compressor.hpp
    Channel-strip dynamics compressor module (issue #163).

  ==============================================================================
*/

#ifndef COMPRESSOR_HPP_INCLUDED
#define COMPRESSOR_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include <cstddef>
#include <vector>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /** @brief Level-detector mode for the ``compressor``. */
      enum compressorDetector {
        DETECT_PEAK, ///< Track the instantaneous linked peak — fast, hits transients.
        DETECT_RMS, ///< Track a short mean-square window — smoother, loudness-like.
        DETECT_COUNT_, ///< Sentinel: number of detectors, not a valid value. Keep last.
      };

      /**
       *  @brief Feed-forward dynamics compressor packaged as a chainable ``dspObject``.
       *
       *  The dynamics half of the channel strip (the parametric EQ is the tone
       *  half). A classic feed-forward design: a switchable peak/RMS detector
       *  drives a static compression curve (threshold + ratio), the resulting
       *  gain is smoothed by separate attack and release time constants, and a
       *  makeup gain restores level.
       *
       *  ### Stereo-linked detection (N-channel)
       *
       *  The detector and the gain it computes are **shared across every
       *  channel** — one sidechain for the whole buffer. Each sample's detector
       *  level is taken from all channels at once (the peak across them, or their
       *  summed mean-square for RMS), one gain-reduction value is derived, and
       *  that *same* gain is applied to every channel. This is what keeps a
       *  stereo (or N-channel) image stable: if the channels were each detected
       *  and compressed independently, a transient on one side would duck only
       *  that side and the stereo image would wobble/pump. Linking guarantees the
       *  relative balance between channels is preserved through gain changes.
       *
       *  A mono buffer is the degenerate single-channel case. Linked, the gain
       *  is global, so the module tolerates a changing channel count between
       *  calls (see the N-channel contract on ``dspObject::process``).
       *
       *  The wet/dry balance is the inherited ``impact()``; ``impact(1)`` (the
       *  default) is the fully-compressed insert.
       *
       *  ### Further controls (issue #1241)
       *
       *  Each is off by default, and with all of them at their defaults the
       *  output is bit for bit what it was before they were added. Per
       *  sample, after the detector level is taken:
       *
       *  - ``sidechainFilter()``: the detector hears a 12 dB/octave high-pass
       *    of its input (``sidechainFrequency()``, default 100 Hz), so low
       *    frequencies, a kick or a bass line, do not drive the gain down.
       *    The signal that is compressed is not filtered.
       *  - ``range()``: the static curve's gain reduction is held at most
       *    this many dB. ``MAX_RANGE`` (the default) is no limit.
       *  - ``freeze()``: the gain stops following the detector and holds
       *    where it is; the detector keeps running.
       *  - ``progressiveRelease()``: the release speeds up with the gain
       *    reduction: ``PROGRESSIVE_DB`` dB of reduction releases twice as
       *    fast, twice that three times as fast.
       *  - ``gainSmoothing()``: a further one-pole smoother on the gain, with
       *    this time constant in ms, after attack and release (0, the
       *    default, is off).
       *  - ``gate()``: a noise gate. While the detector level is under
       *    ``gateThreshold()`` the signal is faded out, over the release
       *    time, and faded back in, over the attack time, once it is above.
       *    The gate's level follows the detector up at once and falls over
       *    the release time, so it does not close at every zero crossing.
       *    It works after the compression and before the makeup, and is not
       *    held by ``range()`` or ``freeze()``.
       *  - ``lookahead()``: the signal is delayed by this many samples while
       *    the detector listens to it undelayed, so the gain is already down
       *    when a transient comes out. ``latency()`` reports the delay. The
       *    delay line is allocated in the constructor for ``maxLookahead()``
       *    samples per channel; a longer lookahead is clamped to it, and a
       *    change clears the delay line (no allocation). The dry half of a
       *    ``impact()`` below 1 is delayed as well.
       *  - ``linked(false)``: every channel has a detector and a gain of its
       *    own (channel *c* of the sidechain keys channel *c*, or the
       *    sidechain's last channel when it has fewer), so a loud channel is
       *    turned down while a quiet one is left alone. ``gainReductionDb()``
       *    then reports the deepest channel.
       *
       *  The per-channel state of the sidechain filter, the lookahead delay and
       *  unlinked detection is allocated in the constructor for
       *  ``maxChannels()`` channels. Channels past that are detected without
       *  the filter and are not delayed; with ``linked(false)`` they pass
       *  through unchanged.
       *
       *  ``process`` allocates nothing, the first block included: the scratch
       *  is sized in the constructor, and a block longer than
       *  ``STANDARD_BUFFERSIZE`` is processed in pieces of at most that size.
       *  How a signal is cut into blocks does not change the result while the
       *  settings stay put (they are read once per piece).
       */
      class API compressor : public dspObject {
      public:
        /** @brief The largest ``range()``, in dB, which is also no limit. */
        static constexpr Flt MAX_RANGE = 120.0f;
        /** @brief The gain reduction, in dB, at which ``progressiveRelease()``
         *  releases twice as fast. */
        static constexpr Flt PROGRESSIVE_DB = 6.0f;
        /** @brief The longest ``gainSmoothing()``, in ms. */
        static constexpr Flt MAX_SMOOTHING = 1000.0f;
        /** @brief The ``gateThreshold()`` until it is set, in dBFS. */
        static constexpr Flt DEFAULT_GATE_THRESHOLD = -60.0f;
        /** @brief The lowest ``gateThreshold()``, in dBFS. */
        static constexpr Flt MIN_GATE_THRESHOLD = -96.0f;
        /** @brief The ``sidechainFrequency()`` until it is set, in Hz. */
        static constexpr Flt DEFAULT_SIDECHAIN_FREQUENCY = 100.0f;
        /** @brief The lowest ``sidechainFrequency()``, in Hz. */
        static constexpr Flt MIN_SIDECHAIN_FREQUENCY = 10.0f;
        /** @brief The highest ``sidechainFrequency()``, in Hz. */
        static constexpr Flt MAX_SIDECHAIN_FREQUENCY = 20000.0f;
        /** @brief The delay line when the constructor is not told, in samples. */
        static constexpr UInt DEFAULT_MAX_LOOKAHEAD = 512;
        /** @brief The largest delay line, in samples. */
        static constexpr UInt MAX_LOOKAHEAD = 65536;
        /** @brief The per-channel state when the constructor is not told. */
        static constexpr UInt DEFAULT_MAX_CHANNELS = 8;
        /** @brief The most channels with per-channel state. */
        static constexpr UInt MAX_CHANNELS = 64;

        /** @brief A compressor with a lookahead delay line of @p maxLookahead
         *         samples (at most ``MAX_LOOKAHEAD``) and per-channel state for
         *         @p maxChannels channels (1 to ``MAX_CHANNELS``). Everything
         *         is allocated here. */
        explicit compressor(UInt maxLookahead = DEFAULT_MAX_LOOKAHEAD,
                            UInt maxChannels = DEFAULT_MAX_CHANNELS);
        ~compressor() override {};

        /** @brief Select the peak or RMS level detector. */
        compressor& detector(compressorDetector value);

        /** @brief Current detector mode. */
        compressorDetector detector();

        /** @brief Set the threshold in dBFS (clamped to [-60, 0]). Signal above
         *  the threshold is compressed; below it passes unchanged. */
        compressor& threshold(Flt db);

        /** @brief Current threshold in dBFS. */
        Flt threshold();

        /** @brief Set the compression ratio (clamped to [1, 20]). 1 is no
         *  compression; 4 means 4 dB in above threshold yields 1 dB out. */
        compressor& ratio(Flt value);

        /** @brief Current compression ratio. */
        Flt ratio();

        /** @brief Set the attack time in milliseconds (clamped to a sane range) —
         *  how fast gain reduction engages when the signal rises. */
        compressor& attack(Flt ms);

        /** @brief Current attack time in milliseconds. */
        Flt attack();

        /** @brief Set the release time in milliseconds (clamped to a sane range) —
         *  how fast gain recovers when the signal falls. */
        compressor& release(Flt ms);

        /** @brief Current release time in milliseconds. */
        Flt release();

        /** @brief Set the makeup gain in dB (clamped to [-24, 24]) — a fixed gain
         *  applied after compression to restore level. */
        compressor& makeup(Flt db);

        /** @brief Current makeup gain in dB. */
        Flt makeup();

        /** @brief Current gain reduction being applied, in dB (<= 0). Useful for
         *  metering; reflects the last processed sample. */
        Flt gainReductionDb();

        /** @brief The deepest gain reduction of the static curve, in dB
         *  (clamped to [0, ``MAX_RANGE``]; ``MAX_RANGE``, the default, is no
         *  limit). Not finite: ignored. */
        compressor& range(Flt db);

        /** @brief Current range in dB. */
        Flt range();

        /** @brief Hold the gain where it is (true) or let it follow the
         *  detector again (false, the default). */
        compressor& freeze(bool on);

        /** @brief Whether the gain is held. */
        bool freeze();

        /** @brief Speed the release up with the gain reduction (off by
         *  default); see the class description. */
        compressor& progressiveRelease(bool on);

        /** @brief Whether the progressive release is on. */
        bool progressiveRelease();

        /** @brief Time constant of a further smoother on the gain, in ms
         *  (clamped to [0, ``MAX_SMOOTHING``]; 0, the default, is off). Not
         *  finite: ignored. */
        compressor& gainSmoothing(Flt ms);

        /** @brief Current gain smoothing in ms. */
        Flt gainSmoothing();

        /** @brief Turn the noise gate on or off (off by default). */
        compressor& gate(bool on);

        /** @brief Whether the noise gate is on. */
        bool gate();

        /** @brief The level under which the gate closes, in dBFS (clamped to
         *  [``MIN_GATE_THRESHOLD``, 0]). Not finite: ignored. */
        compressor& gateThreshold(Flt db);

        /** @brief Current gate threshold in dBFS. */
        Flt gateThreshold();

        /** @brief The lookahead in samples, clamped to ``maxLookahead()``.
         *  Takes effect at the next block, which clears the delay line. */
        compressor& lookahead(UInt samples);

        /** @brief The lookahead in samples. */
        UInt lookahead();

        /** @brief How much later than its input the output is, in samples: the
         *  lookahead. */
        UInt latency();

        /** @brief The longest lookahead, in samples. */
        UInt maxLookahead() const {
          return capacity - 1;
        }

        /** @brief The number of channels with per-channel state. */
        UInt maxChannels() const {
          return channelCapacity;
        }

        /** @brief High-pass the detector's input (off by default). */
        compressor& sidechainFilter(bool on);

        /** @brief Whether the sidechain high-pass is on. */
        bool sidechainFilter();

        /** @brief Cutoff of the sidechain high-pass, in Hz (clamped to
         *  [``MIN_SIDECHAIN_FREQUENCY``, ``MAX_SIDECHAIN_FREQUENCY``], and
         *  below half the sample rate). Not finite: ignored. */
        compressor& sidechainFrequency(Flt hz);

        /** @brief Current sidechain high-pass cutoff in Hz. */
        Flt sidechainFrequency();

        /** @brief One detector and gain for every channel (true, the default)
         *  or one per channel (false). */
        compressor& linked(bool on);

        /** @brief Whether the channels are linked. */
        bool linked();

        /** @brief dspObject lifecycle hook. */
        void create() override;

        /** @brief dspObject audio-thread entry point. */
        void process(MULTICHANNELBUFFER& buffer) override;

        /**
         *  @brief Process ``buffer`` in place with the detector listening to
         *         ``key`` instead (an external sidechain).
         *
         *  The level is taken from every channel of ``key`` (linked, as from
         *  the buffer in the one-argument ``process``) and the resulting gain
         *  is applied to every channel of ``buffer``. ``key`` may have a
         *  different channel count from ``buffer``. When ``key`` is empty or a
         *  channel of it is shorter than ``buffer``'s blocks, the detector
         *  listens to ``buffer`` itself, exactly as ``process(buffer)``. A key
         *  equal to the buffer gives the one-argument result bit for bit.
         *  ``key`` is only read. Allocates nothing.
         */
        void process(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key);

      private:
        // One detector's state: the linked one, or one per channel.
        struct detectorState {
          Flt gain = 1.0f; // smoothed linear gain (attack/release)
          Flt ms = 0.0f; // RMS mean-square follower
          Flt smoothed = 1.0f; // after gainSmoothing()
          Flt gate = 1.0f; // noise-gate gain
          Flt gateLevel = 0.0f; // the gate's held detector level
        };

        // The sidechain high-pass's state for one channel (direct form I).
        struct highPassState {
          Dbl x1 = 0.0;
          Dbl x2 = 0.0;
          Dbl y1 = 0.0;
          Dbl y2 = 0.0;
        };

        // The settings of one piece, read once from the atomics.
        struct pieceSettings;

        void run(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key);
        void runPiece(MULTICHANNELBUFFER& buffer, MULTICHANNELBUFFER& key, UInt offset,
                      UInt length);
        Flt highPass(highPassState& state, Flt in) const;
        static Flt step(detectorState& state, Flt level, const pieceSettings& p);
        void apply(MULTICHANNELBUFFER& buffer, std::size_t ch, UInt offset, UInt length,
                   UInt delay);

        aFlt parmThreshold; // dBFS
        aFlt parmRatio;
        aFlt parmAttack; // ms
        aFlt parmRelease; // ms
        aFlt parmMakeup; // dB
        aInt parmDetector; // compressorDetector
        aFlt parmRange; // dB, MAX_RANGE: no limit
        aBool parmFreeze;
        aBool parmProgressive;
        aFlt parmSmoothing; // ms, 0: off
        aBool parmGate;
        aFlt parmGateThreshold; // dBFS
        aUInt parmLookahead; // samples
        aBool parmSidechainFilter;
        aFlt parmSidechainFrequency; // Hz
        aBool parmLinked;

        UInt capacity; // delay-line length per channel: maxLookahead + 1
        UInt channelCapacity; // channels with per-channel state

        // Audio-thread-only state.
        detectorState shared; // the linked detector, for every channel at once
        Flt rmsCoef; // one-pole coefficient for the RMS window (from SR)
        std::vector<detectorState> channelStates; // linked(false): one per channel
        std::vector<highPassState> highPasses; // one per detector channel
        std::vector<Flt> lines; // channelCapacity * capacity, channel-major
        UInt delay = 0; // the lookahead in use
        UInt writePos = 0;
        bool wasLinked = true;
        // The sidechain high-pass's coefficients and what they were built for.
        Dbl hpA0 = 1.0, hpA1 = 0.0, hpA2 = 0.0, hpB1 = 0.0, hpB2 = 0.0;
        Flt hpBuiltFrequency = 0.0f;
        UInt hpBuiltRate = 0;

        aFlt reductionDb; // last gain reduction (dB, <= 0) for metering

        // Scratch, sized STANDARD_BUFFERSIZE in the constructor; a longer block
        // is processed in pieces of at most that size, so it never grows.
        DSP::buffer gainBuf; // per-sample applied linear gain (one channel, or all linked)
        DSP::buffer wet; // per-piece compressed scratch for the wet/dry mix
        DSP::buffer dry; // dry copy of one piece of a longer block, for the mix
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // COMPRESSOR_HPP_INCLUDED
