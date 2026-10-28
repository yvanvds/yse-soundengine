#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/zPlane.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~zplane``: biquad sections as poles and zeroes on the z
     *         plane, held for a host to draw and drag, wrapping
     *         ``DSP::zPlane`` (issue #1103).
     *
     *  Ported from Max's ``zplane~``. The engine owns the sections; a host
     *  draws their poles and zeroes, reading them through the GUI value
     *  below, and edits them with the ``zero`` and ``pole`` messages in
     *  place of Max's mouse. The wrapper only maps messages and outlets onto
     *  the DSP class; the conversions are ``DSP::filterDesign``'s.
     *
     *  - Inlet 0: ``bang`` sends everything. A number is a0 of the first
     *    section. A list of numbers is a cascade, five coefficients per
     *    section (``~filtergraph``, ``.filterdesign`` and ``~cascade`` use
     *    the same lists). ``zero <index> <real> <imaginary>`` and ``pole
     *    <index> <real> <imaginary>`` move one root (counting from 0 over
     *    all sections, two per section); ``order <n>`` and ``pconstrain
     *    <0|1>`` set the attributes, without output.
     *  - Inlets 1-4: a1, a2, b1, b2 of the first section.
     *  - Outlets: 0 the coefficient list; 1 the zeroes and 2 the poles, as
     *    ``real imaginary`` pairs; 3 the gain (a0) of each section. A
     *    cascade sends outlets 3 to 1; a coefficient inlet, a moved root or
     *    a bang all four, right to left.
     *  - Parameter: ``settings``, ``order <n>`` and ``pconstrain <0|1>``.
     *    The sections and ``pconstrain`` are saved with the patch as state.
     *
     *  **GUI value.** Cell 0 is ``<sections> <pconstrain>``; cell ``1 + s``
     *  is section ``s``: ``<a0> <a1> <a2> <b1> <b2>``, then its two zeroes
     *  and two poles as ``real imaginary`` pairs. Not settable, as for
     *  ``~filtergraph``: a host edits through the messages above, which also
     *  send what an edit must send.
     *
     *  Not mapped: ``dictionary`` (designs travel as lists, as for
     *  ``.filterdesign``), the mouse (the ``zero`` and ``pole`` messages),
     *  ``logscale``, ``range`` and every display attribute (colours,
     *  ``style``: the host's).
     *
     *  **Threads.** Messages allocate nothing: tokens are read in place and
     *  lists are written into a string reserved in the constructor. Two
     *  messages at once do not wait for each other: the one that finds the
     *  object busy is dropped and counted (``Dropped()``), including one that
     *  comes back from its own outlets. A GUI read copies the state under
     *  the same guard and answers "" when it is busy.
     */
    PATCHER_CLASS(dZplane, YSE::OBJ::D_ZPLANE)
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

    /** @brief Messages dropped because the object was busy. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief A copy of the sections. Tests; not while messages arrive. */
    DSP::zPlane Plane() const {
      return plane;
    }

  private:
    // The handlers below run with the guard held.
    void Handle(const std::string& text, YSE::THREAD thread);
    void SendRoots(YSE::THREAD thread);
    void SendAll(YSE::THREAD thread);
    void ApplySettings(const std::string& text);

    // Formats the state into a string (host thread).
    static std::string Cell(const DSP::zPlane& plane, unsigned int index);

    DSP::zPlane plane;

    std::vector<std::string> settings;

    // Where the lists are written; reserved in the constructor.
    std::string scratch;

    mutable std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
