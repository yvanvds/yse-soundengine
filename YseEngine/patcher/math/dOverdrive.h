#pragma once

#include "../pObject.h"
#include "../../dsp/operators/overdrive.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~overdrive``: a soft clipper with a drive factor, wrapping
     *         ``DSP::overdrive`` (issue #1112).
     *
     *  Ported from MSP's ``overdrive~``. The wrapper only maps the pins and
     *  the parameter onto the DSP class.
     *
     *  - Inlet 0: the signal (unconnected reads 0).
     *  - Inlet 1: the drive: 1 is linear, higher values distort more. A
     *    number (float or int) or a signal read per sample; unconnected
     *    reads the last number sent to it, else the creation argument
     *    (#1036).
     *  - Outlet: the distorted signal, within -1..1.
     *  - Parameter: ``drive`` (argument 1, default 1). It is saved with the
     *    patch; a later number is not, as for ``~slide``.
     *
     *  The number reaches the audio thread through an atomic and is read as
     *  a constant signal at the start of each block.
     */
    PATCHER_CLASS(dOverdrive, YSE::OBJ::D_OVERDRIVE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetDrive)
    _INT_IN(SetDriveInt)

  private:
    DSP::overdrive fn;

    aFlt drive;
  };

} // namespace PATCHER
} // namespace YSE
