#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/dynamics/gainFader.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~gain``: a mixer fader, wrapping ``DSP::gainFader``
     *         (issue #1168).
     *
     *  Ported from Max's ``gain~``. The engine keeps the slider, the host
     *  draws it: the GUI value is two cells ``<position> <size>``, the
     *  slider position (0 to size - 1) and the number of steps the host
     *  draws. It is settable (issue #551): inlet 0 takes that string back as
     *  a list, and ``set 0 <position>`` / ``set 1 <size>`` write one cell. A
     *  host that lets the user drag the slider sends the position to inlet
     *  0 as a number, as Max's mouse does.
     *
     *  - Left inlet: the signal to scale; unconnected it reads as silence.
     *    A number (float or int, used as its whole part) sets the position:
     *    the gain ramps to it and the position goes out the right outlet. A
     *    list is the GUI value: a size after the position is set first, then
     *    the position as a number. A bang sends the position. ``set <n>``
     *    sets the position without sending it, and also takes the cell
     *    writes above (two numbers after ``set``). ``size <n>``,
     *    ``scale <x>``, ``inc <x>`` and ``interp <ms>`` change the settings.
     *  - Right inlet: the interpolation time in milliseconds, as Max's.
     *  - Left outlet: the signal times the ramped gain.
     *  - Right outlet: the position, an int.
     *  - Parameters: ``interp`` in ms (argument 1, default 10), ``size``
     *    (argument 2, default 158), ``scale`` (argument 3, default 7.94321)
     *    and ``inc`` (argument 4, default 1.071519). Positions and later
     *    messages are not saved.
     *
     *  Max's ``relative``, ``orientation``, ``style``, the colour messages
     *  and attributes and the mouse are drawing, left to the host;
     *  ``multislider`` and ``setvalue`` address the channels of
     *  ``mc.gain~``, which YSE signals do not have; ``interpinlet`` hides
     *  the right inlet, which is always there; ``annotation_name``,
     *  ``param_connect`` and the ``parameter_*`` attributes belong to Max's
     *  parameter system.
     *
     *  The position and settings reach the audio thread through atomics
     *  read at the start of each block. The wrapper contains no sample
     *  math: the taper, the ramp and the rounding are the DSP class's.
     */
    PATCHER_CLASS(dGain, YSE::OBJ::D_GAIN)
    _DO_MESSAGES
    _DO_CALCULATE

    _HAS_GUI_CELLS

    _FLOAT_IN(SetPosition)
    _INT_IN(SetPositionInt)
    _BANG_IN(SendPosition)
    _LIST_IN(SetList)
    _FLOAT_IN(SetTime)
    _INT_IN(SetTimeInt)

  private:
    // The stored position, held to the size in use. Any thread.
    int Held() const;
    // A new position, held to the slider. Any thread.
    void Move(float to);
    // A new size; the position is held to it. Any thread.
    void Resize(float steps);

    DSP::gainFader fn;

    aFlt interp;
    aFlt size;
    aFlt scale;
    aFlt inc;

    std::atomic<int> position{0};
  };

} // namespace PATCHER
} // namespace YSE
