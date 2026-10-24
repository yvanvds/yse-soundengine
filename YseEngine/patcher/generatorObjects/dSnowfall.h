#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/snowfall.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~snowfall``: a particle of one to four values that moves
     *         while a 0..1 ramp runs, wrapping ``DSP::snowfall`` (issue
     *         #1085).
     *
     *  Ported from Max's ``snowfall~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the ramp that starts and ends each particle, usually a
     *    ``~phasor`` (unconnected, the last number sent here, 0 at first,
     *    #1036: a number above 0 starts a particle that lives until a 0).
     *    Messages take a value per dimension, in order: ``direction``,
     *    ``directiondev``, ``energyramp``, ``initial``, ``initialdev``,
     *    ``min``, ``max``, ``scalemin``, ``scalemax``, ``squish``,
     *    ``wanderprob``, ``endvalue``; and one value: ``interval``,
     *    ``intervaldev``, ``boundarymode <0-3>``, ``endmode <0-1>``,
     *    ``startmode <0-1>``. A list that starts with a word (text through
     *    a receive) is read as a message.
     *  - Outlets: one signal per dimension.
     *  - Parameter: ``dimensions``, the creation arguments: the number of
     *    dimensions (1 to 4, also ``dimensions <n>``; set only here, as in
     *    Max), then attribute keywords, the patcher having no attribute
     *    syntax: ``~snowfall 2 direction 0.5 -0.25 boundarymode 2``. They
     *    are saved with the patch; a later message is not.
     *
     *  Every attribute reaches the audio thread through an atomic and is
     *  applied to the DSP class at the start of each block.
     */
    PATCHER_CLASS(dSnowfall, YSE::OBJ::D_SNOWFALL)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetRamp)
    _INT_IN(SetRampInt)
    _LIST_IN(SetList)

  private:
    static constexpr unsigned int DIMS = DSP::snowfall::MAX_DIMENSIONS;
    // The attributes with a value per dimension, in dSnowfall.cpp's keyword
    // order.
    static constexpr unsigned int VECTORS = 12;

    // Rebuilds the outlets for `dims` dimensions; before the object is
    // wired or published only.
    void ShapePorts(unsigned int dims);
    // Applies a message or a keyword's numbers: `message` from `cursor` on.
    void Apply(const std::string& message, std::size_t cursor, int word);
    void Defaults();

    DSP::snowfall fn;

    std::vector<std::string> arguments;
    unsigned int dimensionCount = 1;

    std::atomic<float> ramp{0.f};
    std::atomic<float> vectors[VECTORS][DIMS];
    std::atomic<float> interval{100.f};
    std::atomic<float> intervaldev{0.f};
    std::atomic<int> boundarymode{DSP::snowfall::BOUNCE};
    std::atomic<int> endmode{DSP::snowfall::END_VALUE};
    std::atomic<int> startmode{DSP::snowfall::INITIAL};
  };

} // namespace PATCHER
} // namespace YSE
