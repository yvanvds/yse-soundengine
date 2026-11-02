/*
  ==============================================================================

    multibandCompressor.hpp
    A 2 to 5 band compressor: a crossover split, one compressor per band, and
    the bands summed again (issue #1117).

  ==============================================================================
*/

#ifndef YSE_DSP_DYNAMICS_MULTIBANDCOMPRESSOR_HPP
#define YSE_DSP_DYNAMICS_MULTIBANDCOMPRESSOR_HPP

#include <array>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../filters/crossover.hpp"
#include "../modules/compressor.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A multiband compressor: the signal is split into 2 to 5
     *         frequency bands, each band has a compressor of its own, and the
     *         bands are added back together.
     *
     *  Covers the role of Max's ``omx.4band~`` and ``omx.5band~`` with an
     *  algorithm of its own, built from YSE's ``DSP::crossover`` and
     *  ``DSP::MODULES::compressor``.
     *
     *  **The split.** ``bands() - 1`` crossovers, at the frequencies set with
     *  ``crossover()``, are cascaded: the first splits the input into band 0
     *  (its lowpass) and the rest (its highpass), the next splits the rest
     *  again, and so on; the highpass of the last one is the top band. Each
     *  crossover is third-order Butterworth, so its lowpass and highpass add
     *  up to an allpass ``A_k``. A band that leaves the cascade early has not
     *  been through the later crossovers, so band ``b`` is also run through
     *  the allpass (lowpass plus highpass) of every crossover after its own,
     *  ``k = b + 1 .. bands() - 2``. With that, the bands add up to
     *  ``A_0 A_1 ... A_(bands - 2)`` applied to the input: a gain of exactly
     *  1 at every frequency, only the phase differs. With nothing compressed
     *  (and every gain at 0 dB) the output is the input with that phase
     *  shift. The frequencies are meant to rise from crossover 0 up, but the
     *  bands add up flat in any order.
     *
     *  **Per band**, in this order: the drive (a gain in dB, ``drive()``),
     *  the band's compressor (``threshold()``, ``ratio()``, ``attack()``,
     *  ``release()``, ``makeup()``, with the class's ranges and defaults; the
     *  detector of every band is set with ``detector()``), then the band's
     *  output level (``level()``, in dB). The bands are added and the sum is
     *  multiplied by the output gain (``output()``, in dB).
     *
     *  **Channels** (1 to ``MAX_CHANNELS``) are linked within a band: each
     *  band's compressor takes its level from every channel of that band and
     *  applies one gain to all of them, so the balance between channels does
     *  not move. The bands are independent of each other.
     *
     *  ``bypass(true)`` sends the input through unchanged (no phase shift);
     *  the split and the compressors keep running, so switching back does not
     *  jump. ``gainReduction()`` gives each band's deepest gain reduction
     *  during the last call, before the makeup.
     *
     *  Defaults: the crossover frequencies of ``DefaultCrossover()``, the
     *  compressor's own defaults (threshold -18 dB, ratio 4, attack 10 ms,
     *  release 100 ms, makeup 0 dB, peak detector), drive, level and output
     *  0 dB. A setting that is not a finite number, or a band or crossover
     *  index out of range, is ignored. An input sample that is not finite
     *  counts as 0.
     *
     *  The crossovers and the compressors follow the live sample rate. There
     *  is no latency. A call of any length is processed in pieces of at most
     *  ``STANDARD_BUFFERSIZE`` samples; how a signal is cut into calls does
     *  not change the result while the settings stay put (the compressor
     *  reads its settings once per piece). Settings may be changed from
     *  another thread than the one processing; process from one thread at a
     *  time. Everything is allocated in the constructor, which also runs
     *  each compressor over one silent block so that its own scratch is
     *  sized there; processing allocates nothing.
     */
    class API multibandCompressor {
    public:
      /** @brief The fewest bands. */
      static constexpr UInt MIN_BANDS = 2;
      /** @brief The most bands. */
      static constexpr UInt MAX_BANDS = 5;
      /** @brief The bands when the constructor is not told. */
      static constexpr UInt DEFAULT_BANDS = 4;
      /** @brief The most channels. */
      static constexpr UInt MAX_CHANNELS = 8;
      /** @brief The lowest drive, level and output gain, in dB. */
      static constexpr Flt MIN_GAIN_DB = -96.f;
      /** @brief The highest drive, level and output gain, in dB. */
      static constexpr Flt MAX_GAIN_DB = 24.f;

      /** @brief A compressor of @p bands bands (``MIN_BANDS`` to
       *         ``MAX_BANDS``) over @p channels channels (1 to
       *         ``MAX_CHANNELS``). Everything is allocated here. */
      explicit multibandCompressor(UInt bands = DEFAULT_BANDS, UInt channels = 2);

      /** @brief The number of bands. */
      UInt bands() const {
        return bandCount;
      }

      /** @brief The number of channels. */
      UInt channels() const {
        return channelCount;
      }

      /** @brief The default frequency of crossover @p index for @p bands
       *         bands, in Hz: 1000 for 2 bands; 300, 3000 for 3; 200, 1000,
       *         5000 for 4; 120, 500, 2000, 8000 for 5. 0 out of range. */
      static Flt DefaultCrossover(UInt bands, UInt index);

      /** @brief The frequency of crossover @p index (0 to ``bands() - 2``),
       *         between band ``index`` and band ``index + 1``, in Hz (0 or
       *         more; the crossover holds it under 0.49 times the sample
       *         rate). */
      multibandCompressor& crossover(UInt index, Flt hz);
      /** @brief The frequency of crossover @p index in Hz, 0 out of range. */
      Flt crossover(UInt index) const;

      /** @brief The gain into band @p band's compressor, in dB
       *         (``MIN_GAIN_DB`` to ``MAX_GAIN_DB``). */
      multibandCompressor& drive(UInt band, Flt dB);
      /** @brief Band @p band's drive in dB, 0 out of range. */
      Flt drive(UInt band) const;

      /** @brief Band @p band's level after its compressor, in dB
       *         (``MIN_GAIN_DB`` to ``MAX_GAIN_DB``). */
      multibandCompressor& level(UInt band, Flt dB);
      /** @brief Band @p band's level in dB, 0 out of range. */
      Flt level(UInt band) const;

      /** @brief The gain on the sum of the bands, in dB (``MIN_GAIN_DB`` to
       *         ``MAX_GAIN_DB``). */
      multibandCompressor& output(Flt dB);
      /** @brief The output gain in dB. */
      Flt output() const {
        return outputDb.load();
      }

      /** @brief Band @p band's threshold in dBFS (the compressor clamps it
       *         to -60 .. 0). */
      multibandCompressor& threshold(UInt band, Flt dB);
      /** @brief Band @p band's threshold, 0 out of range. */
      Flt threshold(UInt band);

      /** @brief Band @p band's ratio (the compressor clamps it to 1 .. 20). */
      multibandCompressor& ratio(UInt band, Flt value);
      /** @brief Band @p band's ratio, 1 out of range. */
      Flt ratio(UInt band);

      /** @brief Band @p band's attack in ms (the compressor clamps it to
       *         0.1 .. 2000). */
      multibandCompressor& attack(UInt band, Flt ms);
      /** @brief Band @p band's attack, 0 out of range. */
      Flt attack(UInt band);

      /** @brief Band @p band's release in ms (the compressor clamps it to
       *         0.1 .. 2000). */
      multibandCompressor& release(UInt band, Flt ms);
      /** @brief Band @p band's release, 0 out of range. */
      Flt release(UInt band);

      /** @brief Band @p band's makeup gain in dB (the compressor clamps it
       *         to -24 .. 24). */
      multibandCompressor& makeup(UInt band, Flt dB);
      /** @brief Band @p band's makeup, 0 out of range. */
      Flt makeup(UInt band);

      /** @brief The level detector of every band. */
      multibandCompressor& detector(MODULES::compressorDetector value);
      /** @brief The level detector. */
      MODULES::compressorDetector detector();

      /** @brief Whether the input is sent through unchanged. Off by
       *         default. */
      multibandCompressor& bypass(bool on);
      /** @brief Whether bypass is on. */
      bool bypass() const {
        return bypassOn.load();
      }

      /** @brief Band @p band's deepest gain reduction during the last call,
       *         in dB (0 or less, before the makeup); 0 out of range. */
      Flt gainReduction(UInt band) const;

      /** @brief Band @p band of channel @p channel after its compressor and
       *         before its level, for the last piece processed (at most
       *         ``STANDARD_BUFFERSIZE`` samples). Out of range: band 0 of
       *         channel 0. */
      buffer& band(UInt band, UInt channel);

      /** @brief Processes @p buffer in place: ``channels()`` channels of
       *         equal length. Fewer channels, or a shorter channel, and the
       *         call does nothing; extra channels are left alone. */
      void process(MULTICHANNELBUFFER& buffer);

    private:
      void Split(MULTICHANNELBUFFER& buffer, UInt offset, UInt length);
      void Mix(MULTICHANNELBUFFER& buffer, UInt offset, UInt length);
      // The allpass for band `band` and crossover `index` on channel
      // `channel`.
      DSP::crossover& Allpass(UInt channel, UInt band, UInt index);

      UInt bandCount;
      UInt channelCount;

      std::array<aFlt, MAX_BANDS - 1> frequency;
      std::array<aFlt, MAX_BANDS> driveDb;
      std::array<aFlt, MAX_BANDS> levelDb;
      std::array<aFlt, MAX_BANDS> reduction;
      aFlt outputDb{0.f};
      std::atomic<bool> bypassOn{false};

      std::array<MODULES::compressor, MAX_BANDS> compressors;
      // [band][channel], sized STANDARD_BUFFERSIZE in the constructor.
      std::array<MULTICHANNELBUFFER, MAX_BANDS> signal;
      // [channel * (bands - 1) + index]
      std::vector<DSP::crossover> splits;
      // [channel * allpassesPerChannel + allpassStart[band] + index - band - 1]
      std::vector<DSP::crossover> allpasses;
      std::array<UInt, MAX_BANDS> allpassStart{};
      UInt allpassesPerChannel = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DYNAMICS_MULTIBANDCOMPRESSOR_HPP
