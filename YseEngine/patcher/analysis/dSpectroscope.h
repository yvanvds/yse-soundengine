#pragma once

#include <mutex>
#include "../pObject.h"
#include "../../dsp/analysis/spectrumAnalyzer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~spectroscope``: a spectrum analyser, wrapping
     *         ``DSP::spectrumAnalyzer`` (issue #1170).
     *
     *  Ported from Max's ``spectroscope~``. The engine analyses, the host
     *  draws: every update interval a frame is published to the analyser's
     *  triple buffer, and the host polls the newest one through the GUI
     *  value, ``5 + bins`` cells: ``<mode> <size> <bins> <serial> <rate>``
     *  and then one number per bin, DC first. Mode 0 is the amplitude
     *  spectrum, 1 the phase spectrum in radians; bin ``j`` is
     *  ``j * rate / size`` Hz. ``serial`` counts the frames, so a host sees
     *  when a new one arrived, and a host drawing a sonogram appends one
     *  column per frame and sees how many it missed; before the first it is
     *  ``0 0 0 0 0``. It is not settable.
     *
     *  The bulk read and cell 0 take the newest finished frame; the count
     *  and cells 1 on read the frame cell 0 or the bulk read took, so a host
     *  walking the cells reads cell 0 first, then the count, then the rest.
     *
     *  - Left inlet: the signal; unconnected it reads as silence. Inside a
     *    ``~pfft``, the real part of the bins (``~fftin``'s left outlet).
     *    Also takes ``interval <ms>``, ``phasespect <0|1>`` and
     *    ``size <n>``.
     *  - Right inlet: inside a ``~pfft``, the imaginary part of the bins;
     *    elsewhere it does nothing, as in Max.
     *  - No outlets.
     *  - Parameters: ``interval`` in ms (argument 1, default 20, 10 to
     *    2000); ``phasespect`` (argument 2, default 0: amplitude, 1 phase);
     *    ``size``, the FFT size outside a ``~pfft`` (argument 3, default
     *    4096, a power of two from 16 to 16384). Messages are not saved.
     *
     *  Max's ``bgcolor``, ``fgcolor``, ``markercolor``, the ``sono*``
     *  colours, ``monochrome``, ``style``, ``orientation``, ``domain``,
     *  ``range``, ``logamp``, ``logfreq``, ``sono``, ``scroll`` and the
     *  mouse are drawing, left to the host, which has the frames, their
     *  numbers and their frequencies; ``displaychan`` picks a channel of a
     *  multichannel signal, which YSE signals are not.
     *
     *  Settings reach the audio thread through atomics read at the start of
     *  each block. CALC() never copies a frame: the analyser writes into the
     *  triple buffer and publishes with one atomic exchange. The host's
     *  reads take a mutex only host readers share, so two host threads
     *  polling at once do not both act as the one reader the triple buffer
     *  allows; the audio thread never touches it. The wrapper contains no
     *  sample math.
     */
    PATCHER_CLASS(dSpectroscope, YSE::OBJ::D_SPECTROSCOPE)
    _DO_MESSAGES
    _DO_CALCULATE

    // Read only: no _HAS_GUI_CELLS, which promises the write half.
    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

  private:
    DSP::spectrumAnalyzer fn;

    aFlt interval;
    aFlt phasespect;
    aFlt size;

    // Host readers only: the triple buffer has one reader at a time.
    mutable std::mutex reading;
  };

} // namespace PATCHER
} // namespace YSE
