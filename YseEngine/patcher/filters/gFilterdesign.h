#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/filterDesign.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``.filterdesign``: designs a Butterworth or Chebyshev filter
     *         and sends it as a coefficient list for ``~cascade``, wrapping
     *         ``DSP::filterDesign`` (issue #1101).
     *
     *  Ported from Max's ``filterdesign``. The wrapper only maps messages,
     *  outlets and the parameter onto the DSP class; the design is the
     *  class's.
     *
     *  - Inlet 0: ``bang`` designs the filter from the current settings and
     *    sends it. A list sets one or more settings, each a word and its
     *    value, in any order and without sending: ``response lowpass``
     *    (``highpass``, ``bandpass``, ``bandstop``), ``topology butterworth``
     *    (``chebyshev-1``, ``chebyshev-2``), ``order <n>``,
     *    ``frequency <f> [<f2>]``, ``units hertz`` (``normalized``,
     *    ``radians``), ``samplerate <hz>`` (0: the engine's, read at the
     *    bang), ``passband_ripple <dB>``, ``stopband_attenuation <dB>``,
     *    ``sos_output <0|1>``, ``zpk_output <0|1>``, ``tf_output <0|1>``. A
     *    word that is not a setting, or a value that does not fit it, is
     *    skipped.
     *  - Outlet 0: with ``sos_output`` on (the default), the second-order
     *    sections as one list, five coefficients per section, which
     *    ``~cascade`` and ``.filterdetail`` take as they are.
     *  - Outlet 1: with ``zpk_output`` on, ``zeros <re im ...>``,
     *    ``poles <re im ...>`` and ``gain <k>``; with ``tf_output`` on,
     *    ``numerator <...>`` and ``denominator <...>``. Both off by default.
     *    Outlet 1 sends before outlet 0.
     *  - Parameter: ``settings``, the creation arguments, in the words of
     *    the inlet (``.filterdesign response bandpass order 4 frequency 500
     *    2000``). Saved with the patch; later messages are not.
     *
     *  A design that fails (a frequency not between 0 and the Nyquist
     *  frequency, equal band edges) sends nothing.
     *
     *  What is not mapped: the ``name`` attribute and the dictionary output,
     *  because a design leaves as lists, which ``~cascade`` and
     *  ``.filterdetail`` take directly; the double-click editor, which
     *  belongs to a host.
     *
     *  **Threads.** Nothing allocates after construction: the design runs in
     *  the DSP class's fixed arrays and the lists are written into a string
     *  reserved in the constructor, so a bang may arrive on the audio
     *  thread. Messages from two threads at once do not wait for each other:
     *  the one that finds the object busy is dropped and counted
     *  (``Dropped()``). That includes a message that comes back to this
     *  object from its own outlets.
     */
    PATCHER_CLASS(gFilterdesign, YSE::OBJ::G_FILTERDESIGN)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _LIST_IN(List)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Messages dropped because the object was busy. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief A copy of the design settings. Tests; not while messages
     *         arrive. */
    DSP::filterDesign Settings() const {
      return fn;
    }

    /** @brief Whether each output is on: sections, zeroes-poles-gain and
     *         transfer function. Tests. */
    bool SosOutput() const {
      return sosOutput;
    }
    bool ZpkOutput() const {
      return zpkOutput;
    }
    bool TfOutput() const {
      return tfOutput;
    }

  private:
    // Applies the settings in `text` (words and values). Caller holds the
    // guard, or runs on the control thread before the object is published.
    void Apply(const std::string& text);

    void ResetSettings();

    DSP::filterDesign fn;
    bool sosOutput = true;
    bool zpkOutput = false;
    bool tfOutput = false;

    std::vector<std::string> settings;

    // Where each list is written before it is sent; reserved in the
    // constructor so a bang never allocates.
    std::string scratch;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
