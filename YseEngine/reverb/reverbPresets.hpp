/*
  ==============================================================================

    reverbPresets.hpp
    Shared reverb preset table + interpolation core (issue #326).

  ==============================================================================
*/

#ifndef REVERBPRESETS_HPP_INCLUDED
#define REVERBPRESETS_HPP_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace REVERB {

    /**
     *  @brief One complete reverb parameter set — the payload of a named preset.
     *
     *  Historically the ``REVERB_PRESET`` values lived as a switch inside
     *  ``YSE::reverb::setPreset``. They are extracted here (issue #326) so the
     *  same parameter sets can feed both the zone/global reverb interface and
     *  the morphing reverb module (``DSP::MODULES::morphingReverb``), which
     *  blends two of them under a control input.
     *
     *  ``earlyTime`` is stored as float (the interface API uses whole samples)
     *  so a morph between two presets moves the early reflections continuously
     *  instead of stepping per sample.
     */
    struct API presetValues {
      Flt roomsize; // simulated room size, [0, 1]
      Flt damp; // high-frequency damping, [0, 1]
      Flt dry; // unprocessed level, [0, 1]
      Flt wet; // reverberated level, [0, 1]
      Flt modFrequency; // tail modulation rate, Hz (0 = off)
      Flt modWidth; // tail modulation depth (0 = off)
      Flt earlyTime[4]; // early reflection delays, samples, [0, 2999]
      Flt earlyGain[4]; // early reflection gains, [0, 1]
    };

    /**
     *  @brief Parameter set of a named preset.
     *
     *  The returned values are identical to what ``YSE::reverb::setPreset``
     *  has always applied — that function now reads from this table.
     */
    API const presetValues& getPresetValues(REVERB_PRESET preset);

    /**
     *  @brief The preset-interpolation core: linear blend of two parameter sets.
     *
     *  Every field is interpolated as ``a + (b - a) * t`` with ``t`` clamped to
     *  [0, 1], so ``t == 0`` returns ``a`` and ``t == 1`` returns ``b``.
     *  Allocation-free and safe to call on the audio thread.
     */
    API presetValues morph(const presetValues& a, const presetValues& b, Flt t);

  } // namespace REVERB
} // namespace YSE

#endif // REVERBPRESETS_HPP_INCLUDED
