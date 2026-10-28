#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/filterBands.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~filtergraph``: the state of a filter editor, one to 24 biquad
     *         bands, sent as the coefficient list ``~biquad`` and
     *         ``~cascade`` take, wrapping ``DSP::filterBands`` (issue #1102).
     *
     *  Ported from Max's ``filtergraph~``. The engine owns the state; a host
     *  draws it, reading it through the GUI value below and drawing the
     *  curve with ``DSP::filterBands::response`` / ``DSP::filterResponse``,
     *  and edits it with the same messages a patch sends. The wrapper only
     *  maps messages and outlets onto the DSP class.
     *
     *  "The band" below is the band being edited (``setfilter``), 0 at
     *  first. Messages that change the coefficients send them; their
     *  ``set...`` forms do not.
     *
     *  - Inlet 0: ``bang`` sends the coefficients; for a band in a filter
     *    mode it first sends the band's index, bandwidth, Q, gain and
     *    frequency (right to left). A number is the band's a0 (in display
     *    mode). A list of numbers is a cascade, five coefficients per band.
     *    ``display``, ``lowpass``, ``highpass``, ``bandpass``,
     *    ``bandstop``, ``peaknotch``, ``lowshelf``, ``highshelf``,
     *    ``resonant`` and ``allpass`` set the band's mode, with an optional
     *    frequency, gain and Q; ``mode <0-9>`` the same by number;
     *    ``cascade <coefficients>``; ``set <coefficients>`` (no output);
     *    ``params [<band>] <f> <g> <q>`` and ``setparams``; ``gainmode``;
     *    ``analog <0|1>`` (a bandpass or peaknotch band in the matched
     *    design of ``DSP::filterCoefficients::ComputeMatched``, #1209);
     *    ``displaydot`` (no output); ``options <band> <mode> <gain>
     *    <analog> <dot>`` and ``setoptions``; ``constraints <band> <6
     *    bounds>`` and ``setconstraints``; ``flat <bands...>``; ``setfilter`` /
     *    ``whichfilt`` (the band edited), ``selectfilt`` (the band a host
     *    highlights), ``nfilters <1-24>``, ``query <hz>``, ``autoout``, and
     *    the ``edit_*`` attributes as messages (no output).
     *  - Inlets 1-4: a1, a2, b1, b2 of the band, in display mode.
     *  - Inlets 5-7: the band's frequency, gain and Q. A list of three on
     *    inlet 5 is ``params``; ``set <f> <g> <q>`` there is ``setparams``.
     *  - Outlets: 0 the coefficient list; 1-4 frequency, gain, Q and
     *    bandwidth; 5 ``<amplitude> <phase>`` for a ``query``; 6 the band
     *    index.
     *  - Parameter: ``settings``, ``nfilters <n>`` and ``autoout <0|1>``.
     *    The bands themselves are saved with the patch as state.
     *
     *  **GUI value.** Cell 0 is ``<bands> <edited> <highlighted>``; cell
     *  ``1 + i`` is band ``i``: ``<mode> <frequency> <gain> <q> <gainmode>
     *  <displaydot> <a0> <a1> <a2> <b1> <b2> <analog>``, the coefficients at
     *  the live sample rate. Not settable: Max's ``set`` already means
     *  "coefficients, no output" on inlet 0, which the protocol's ``set
     *  <index>`` form would collide with. A host writes through the messages
     *  above.
     *
     *  Not mapped: ``highorder`` and ``dictionary`` (a display of a
     *  non-biquad filter, and designs travel as lists), ``markers``, the
     *  mouse and every display attribute (colours, axes, ranges, markers,
     *  ``phasespect``,
     *  ``fullspect``: the host's), the Max for Live parameter attributes, and
     *  the output when audio starts (``autoout`` and ``bang`` cover it).
     *
     *  **Threads.** Messages allocate nothing: tokens are read in place and
     *  lists are written into a string reserved in the constructor. Two
     *  messages at once do not wait for each other: the one that finds the
     *  object busy is dropped and counted (``Dropped()``), including one that
     *  comes back from its own outlets. A GUI read copies the state under
     *  the same guard and answers "" when it is busy.
     */
    PATCHER_CLASS(dFiltergraph, YSE::OBJ::D_FILTERGRAPH)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _FLOAT_IN(FloatIn)
    _INT_IN(IntIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    void Loadbang(YSE::THREAD thread) override;

    /** @brief Messages dropped because the object was busy. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief A copy of the bands. Tests; not while messages arrive. */
    DSP::filterBands Bands() const {
      return fn;
    }

    /** @brief The band being edited and the band highlighted (-1: none).
     *         Tests. */
    unsigned int Edited() const {
      return edited;
    }
    int Highlighted() const {
      return highlighted;
    }

    /** @brief Whether the coefficients are sent when the patch loads. */
    bool AutoOut() const {
      return autoOut;
    }

  private:
    // The handlers below run with the guard held.
    void Handle(const std::string& text, YSE::THREAD thread);
    void HandleFrequencyInlet(const std::string& text, YSE::THREAD thread);
    void SendCoefficients(YSE::THREAD thread);
    void SendAll(YSE::THREAD thread);
    void Select(float index);
    void ApplySettings(const std::string& text);

    // Formats the state into a string (host thread).
    static std::string Cell(const DSP::filterBands& bands, unsigned int index, unsigned int edited,
                            int highlighted);

    DSP::filterBands fn;
    unsigned int edited = 0;
    int highlighted = -1;
    bool autoOut = false;

    std::vector<std::string> settings;

    // Where the coefficient list is written; reserved in the constructor.
    std::string scratch;

    mutable std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
