/*
  ==============================================================================

    fmPatch.hpp
    DX7-class 6-operator FM voice parameter set for YSE::synth (issue #176).

    This struct is the explicit data contract between the FM engine (this
    issue, #176) and the DX7 SysEx importer (#177). #177 parses a DX7 voice
    out of a SysEx dump and fills an ``fmPatch``; ``fmVoice`` consumes it. The
    field set is the full DX7 155-parameter voice (6 operators + global pitch
    envelope, algorithm, feedback, LFO, transpose, name), laid out so it maps
    one-to-one onto the 156-byte "unpacked voice" the ported MSFA core
    (``msfa::Dx7Note::init``) expects — see ``toUnpacked``.

    Ranges follow the DX7 hardware (the values a SysEx voice already carries):
    most parameters are 0..99, curves 0..3, algorithm 0..31 (the DX7 front
    panel shows 1..32), feedback 0..7, detune 0..14 (7 = centre), transpose
    0..48 (24 = C3, i.e. no transpose). #177 is responsible for range-checking
    what it imports; ``fmVoice`` clamps defensively where a bad value could
    index out of bounds.

  ==============================================================================
*/

#ifndef YSE_DSP_FM_FMPATCH_HPP
#define YSE_DSP_FM_FMPATCH_HPP

#include <cstdint>

#include "../../headers/defines.hpp"

namespace YSE {
  namespace SYNTH {

    /**
     *  @brief One DX7 operator's parameters (21 fields).
     *
     *  Maps to bytes ``[op*21 .. op*21+20]`` of the 156-byte unpacked voice.
     *  Operators are stored in DX7 voice order: ``op[0]`` is OP1 … ``op[5]``
     *  is OP6, matching the algorithm routing tables in the ported core.
     */
    struct fmOperator {
      uint8_t egRate[4]; ///< Envelope rates R1..R4 (0..99).           [+0..3]
      uint8_t egLevel[4]; ///< Envelope levels L1..L4 (0..99).          [+4..7]

      uint8_t levelScaleBreakPoint; ///< Keyboard level-scaling break point.   [+8]
      uint8_t levelScaleLeftDepth; ///< Level scaling, left depth (0..99).     [+9]
      uint8_t levelScaleRightDepth; ///< Level scaling, right depth (0..99).    [+10]
      uint8_t levelScaleLeftCurve; ///< Left curve (0..3: -lin,-exp,+exp,+lin).[+11]
      uint8_t levelScaleRightCurve; ///< Right curve (0..3).                    [+12]

      uint8_t rateScaling; ///< Keyboard rate scaling (0..7).                   [+13]
      uint8_t ampModSens; ///< Amplitude-modulation sensitivity (0..3).       [+14]
      uint8_t keyVelSens; ///< Key-velocity sensitivity (0..7).               [+15]
      uint8_t outputLevel; ///< Operator output level (0..99).                 [+16]

      uint8_t oscMode; ///< 0 = frequency ratio, 1 = fixed frequency.       [+17]
      uint8_t freqCoarse; ///< Coarse frequency (0..31).                       [+18]
      uint8_t freqFine; ///< Fine frequency (0..99).                         [+19]
      uint8_t detune; ///< Detune (0..14, 7 = centre).                     [+20]
    };

    /**
     *  @brief A complete DX7 6-operator voice.
     *
     *  Plain data — no behaviour, no atomics — so it is trivially copyable and
     *  cheap to hand across the setup boundary. ``fmVoice`` snapshots one of
     *  these at construction and serialises it to the MSFA core on each
     *  note-on via ``toUnpacked``; the shared, live-editable copy the voice
     *  keeps is the ``fmVoice`` patch (see fmVoice.hpp).
     */
    struct API fmPatch {
      fmOperator op[6]; ///< OP1..OP6.                                    [0..125]

      uint8_t pitchEgRate[4]; ///< Pitch envelope rates R1..R4 (0..99).  [126..129]
      uint8_t pitchEgLevel[4]; ///< Pitch envelope levels L1..L4 (0..99). [130..133]

      uint8_t algorithm; ///< FM algorithm (0..31; DX7 shows 1..32).      [134]
      uint8_t feedback; ///< Feedback amount (0..7).                     [135]
      uint8_t oscKeySync; ///< Oscillator key sync (0/1).                 [136]

      uint8_t lfoSpeed; ///< LFO speed (0..99).                    [137]
      uint8_t lfoDelay; ///< LFO delay (0..99).                    [138]
      uint8_t lfoPitchModDepth; ///< LFO pitch-mod depth PMD (0..99).     [139]
      uint8_t lfoAmpModDepth; ///< LFO amp-mod depth AMD (0..99).        [140]
      uint8_t lfoSync; ///< LFO key sync (0/1).                   [141]
      uint8_t lfoWaveform; ///< LFO waveform (0..5: tri,saw-d,saw-u,sqr,sine,s&h). [142]
      uint8_t pitchModSens; ///< Pitch-mod sensitivity (0..7).         [143]

      uint8_t transpose; ///< Transpose in semitones (0..48, 24 = none).  [144]
      char name[10]; ///< Voice name, ASCII, space-padded.            [145..154]
      uint8_t opEnabled; ///< Operator on/off bitmask (bit n = OPn+1); 0x3f = all on. [155]

      /**
       *  @brief Serialise into the 156-byte unpacked voice the MSFA core reads.
       *
       *  ``dest`` must point to at least 156 bytes. This is the exact layout
       *  ``msfa::Dx7Note::init`` consumes; ``fmVoice`` calls it on note-on.
       */
      void toUnpacked(char dest[156]) const;

      /// @name Built-in test voices (defined in code; #176 acceptance).
      /// @{
      /** @brief A single unmodulated carrier — a pure sine (algorithm 32). */
      static fmPatch sine();
      /** @brief A textbook 2-operator FM patch: OP1 modulated by OP2 at a 1:1
       *  ratio (algorithm 5), producing a carrier plus FM sidebands. */
      static fmPatch fm2op();
      /** @brief A fuller, brass-like patch exercising all six operators. */
      static fmPatch brass();
      /// @}
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_DSP_FM_FMPATCH_HPP
