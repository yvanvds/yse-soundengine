#pragma once

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../../dsp/delay/retune.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~retune``: pitch detection and correction toward a scale or
     *         a set of notes (autotune), wrapping ``DSP::retune`` (issue
     *         #1154).
     *
     *  Ported from Max's ``retune~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal (unconnected, the last number sent to it, else
     *    0, #1036). A list of MIDI notes and velocities enables (velocity
     *    above 0) or disables the scale degree each note falls on.
     *  - Inlet 1: a pitch in Hz to lock onto instead of the scale, a signal
     *    or a number; 0 (the default) follows the scale.
     *  - Inlet 2: a detune in cents, a signal or a number, added to the
     *    correction.
     *  - Every inlet takes the attributes as messages:
     *    ``correction_amount <0-100>``, ``correction_threshold <cents>``,
     *    ``correction_ambience_threshold <0-1>``, ``correction_bypass
     *    <percent>``, ``notelist <cents or a/b>...``, ``notebase <0-11>``,
     *    ``enablednotes all|<degree>...``, ``quality
     *    <basic|good|better|best>``, ``retune <0|1>``, ``pitchdetection
     *    <0|1>``, ``windowsize <samples>``, ``reportlatency <0|1>`` and
     *    ``use_16bit <0|1>`` (accepted and ignored: YSE has no 16-bit
     *    path), and the commands ``defaulttuning``, ``flush``, ``getlatency``
     *    (``getlatency <0|1>`` is ``reportlatency``) and ``list <note>
     *    <velocity>...``.
     *  - Outlet 0: the corrected signal, the latency in samples late.
     *    Outlet 1: the detected pitch in Hz per sample (0: none), with
     *    ``pitchdetection 1``, else zeros. Outlet 2: the latency in samples,
     *    an int, sent after the block (#1037) when reporting is turned on,
     *    whenever it changes while it is on, and on ``getlatency``.
     *  - Parameter: ``settings``, the standard pitch (the A in Hz, default
     *    440), then any of the attribute words with their values. It is
     *    saved with the patch; a later message is not.
     *
     *  The numbers reach the audio thread through atomics, applied at the
     *  start of each block. The notes (``notelist``, ``notebase``,
     *  ``enablednotes``, ``list``, ``defaulttuning``, ``flush``) are edited
     *  on a ``DSP::tuning`` the senders share, under a non-blocking flag,
     *  and published through a triple buffer from which ``CALC()`` takes
     *  the newest with one exchange: the audio thread never waits and never
     *  reads a half-written scale. A sender that finds the flag taken (a
     *  note from a rendering graph while the control thread edits) drops its
     *  edit and counts it in ``Dropped()``, as ``~click``'s ``set`` does.
     */
    PATCHER_CLASS(dRetune, YSE::OBJ::D_RETUNE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The DSP class, for tests. Not while the object renders. */
    const DSP::retune& Retuner() const {
      return fn;
    }

    /** @brief Note edits dropped because another sender held the tuning.
     *         Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // The most tokens one message is read as: a whole note list and its
    // word.
    static constexpr std::size_t MAX_TOKENS = DSP::tuning::MAX_NOTES + 2;

    // Every setting back to its default, the tuning included (published).
    void Defaults(Flt standardPitch);
    // Apply one word with its values, from the settings or a message.
    // False when the word or its values are not one.
    bool ApplyWord(std::string_view word, const std::string_view* values, std::size_t count);
    // Take the senders' flag; false (and counted) when another holds it.
    bool BeginEdit();
    // Publish the edited tuning and release the flag.
    void Publish();

    std::vector<std::string> settings;
    DSP::retune fn;

    aFlt level{0.f};
    aFlt lockHz{0.f};
    aFlt detuneCents{0.f};
    aFlt amount{100.f};
    aFlt threshold{0.f};
    aFlt ambience{DSP::retune::DEFAULT_AMBIENCE};
    aFlt bypass{100.f};
    aInt quality{DSP::pitchShifter::BASIC};
    aUInt windowSize{0};
    std::atomic<bool> retuning{true};
    std::atomic<bool> detection{false};
    std::atomic<bool> reportLatency{false};
    std::atomic<bool> reportOnce{false};

    // The tuning the senders edit (theirs while they hold `writing`), and
    // its triple buffer. `back` belongs to the sender holding `writing`,
    // `front` to CALC(); `middle` is the slot in between, with FRESH set
    // while CALC() has not taken it.
    static constexpr unsigned int FRESH = 4u;
    DSP::tuning edited;
    std::array<DSP::tuning, 3> slots;
    unsigned int back = 0;
    std::atomic<unsigned int> middle{1u};
    unsigned int front = 2;
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};

    // The latency last sent. Audio thread only.
    int reported = -1;
  };

} // namespace PATCHER
} // namespace YSE
