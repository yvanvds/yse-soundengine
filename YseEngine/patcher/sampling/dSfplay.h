#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/fileStreamPlayer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sfplay``: plays sound files streamed from disk, with cues,
     *         speed, looping and pitch, wrapping ``DSP::fileStreamPlayer``
     *         (issue #1155).
     *
     *  Ported from Max's ``sfplay~``. The wrapper only maps pins, messages
     *  and arguments onto the DSP class; the stream is the engine's own (see
     *  the class).
     *
     *  - Arguments: ``list channels buffersize position``, Max's
     *    ``sflist-object-name``, ``number-of-output-channels`` (1 to 64,
     *    default 1), ``buffer-size`` and ``position-outlet-flag``. As in Max
     *    the list name may be left out: a first argument that is a number is
     *    the channel count. ``buffersize`` is read and has no effect: the
     *    engine's stream buffers are ``STREAM_BUFFERSIZE`` frames.
     *  - Inlet 0: ``0`` stops, ``1`` plays the open file, ``2`` and up play
     *    that cue; a list plays its cues one after the other. The words
     *    ``open``, ``fclose``, ``preload``, ``preloadn``, ``clear``,
     *    ``set``, ``seek``, ``pause``, ``resume``, ``loop``, ``loopone``,
     *    ``speed``, ``modout``, ``pitchshift``, ``pitchshiftcent``,
     *    ``timestretch``, ``quality``, ``audiofile`` and ``embed`` arrive
     *    here too.
     *  - Inlet 1: a number is the speed.
     *  - Outlets: one signal per channel; with ``position`` 1 a signal of
     *    where the cue plays, in ms; last, a bang after the block in which
     *    playback stopped (the end, or ``0``), armed in ``CALC()`` (#1037).
     *
     *  ### Files are opened in the background
     *
     *  ``open``, ``fclose``, ``preload``, ``preloadn``, ``clear`` and ``set``
     *  copy their words into a fixed record and hand it to the patcher's
     *  ``fileScheduler`` (wait-free, no allocation); file names go through
     *  the host's path resolver (issue #1255) when one is installed. The task
     *  finds the player by id, so it survives the object's deletion, and
     *  calls the class's control side, which opens the streams and hands
     *  them to the audio thread. A transport message (a cue number, a list,
     *  ``seek``, ``pause``, ``resume``) waits until those tasks have
     *  finished, so ``open song.wav, 1`` plays the song. When an ``~sflist``
     *  the object plays from changes, the next block asks for the streams to
     *  be opened again. Streams the audio thread let go are freed by a
     *  further task, never on the audio thread.
     *
     *  Limits, the scheduler's: at most ``fileScheduler::CAPACITY`` requests
     *  in flight per patcher, results only while the patcher renders,
     *  nothing outside a patcher. A refused or failed request counts in
     *  ``FailedTasks``.
     *
     *  **Saving.** The arguments are saved. With ``embed 1`` the patch also
     *  saves the open file and the object's own cues (not those of an
     *  ``~sflist``, which saves its own), and loading opens them again.
     *  The other messages are not saved.
     */
    PATCHER_CLASS(dSfplay, YSE::OBJ::D_SFPLAY)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _INT_IN(SetCue)
    _FLOAT_IN(SetCueFloat)
    _LIST_IN(SetList)
    _FLOAT_IN(SetSpeed)
    _INT_IN(SetSpeedInt)

    ~dSfplay() override;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    /** @brief The DSP class. Its audio-thread state only while no block
     *         runs (tests). */
    DSP::fileStreamPlayer& Player() {
      return *player;
    }

    /** @brief The number of signal outlets for the file's channels. */
    int Channels() const {
      return channels;
    }

    /** @brief Whether the position outlet is there. */
    bool HasPosition() const {
      return positionOutlet != 0;
    }

    /** @brief Requests refused by the scheduler, or whose file could not be
     *         opened. Diagnostics and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    struct sfTask;
    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    void ShapePorts();
    // Hand `task` to the scheduler (with its name through the resolver when
    // `resolve`). Any thread, no allocation. False when refused.
    bool Submit(sfTask& task, bool resolve);
    void Post(int what, int cue = 0, float start = 0.f, float end = 0.f);

    std::shared_ptr<DSP::fileStreamPlayer> player; // made at construction, never replaced
    std::uint64_t id = 0; // how a task finds the player

    // Arguments. Written by PARM_PARSE only, before any audio thread sees
    // the object (a change of argument rebuilds it).
    std::string listName;
    int channels = 1;
    int bufferSize = 0;
    int positionOutlet = 0;

    // Attributes, applied by the next block.
    aInt loop;
    aInt loopOne;
    aFlt speed;
    aInt modout;
    aFlt pitchShift;
    aFlt pitchShiftCents;
    aInt timeStretch;
    aInt quality;
    aInt embed;

    // A transport request for the next block that has no file task in
    // flight; the numbers are stored before the request. The last one wins.
    std::atomic<int> request{0};
    std::atomic<int> requestCue{0};
    aFlt requestStart;
    aFlt requestEnd;
    std::atomic<int> requestList[DSP::fileStreamPlayer::MAX_LIST] = {};
    std::atomic<int> requestListLength{0};

    // File tasks requested and not yet delivered, the ones a transport
    // request waits for.
    std::atomic<int> inFlight{0};
    std::atomic<bool> syncQueued{false};
    std::atomic<bool> releaseQueued{false};
    std::atomic<std::uint64_t> failed{0};
  };

} // namespace PATCHER
} // namespace YSE
