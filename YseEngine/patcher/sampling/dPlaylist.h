#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/filePlaylist.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~playlist``: a list of sound files with selections, played
     *         from disk one clip at a time, wrapping ``DSP::filePlaylist``
     *         (issue #1158).
     *
     *  Ported from Max's ``playlist~``. The engine holds the list and the
     *  playback; the host draws them, from the GUI value below, and turns
     *  the mouse into the messages here. The wrapper only maps pins,
     *  messages and parameters onto the DSP class.
     *
     *  - Parameters: ``channelcount`` (1 to 64, default 2), then Max's
     *    attributes ``loop``, ``speed``, ``timestretch``, ``pitchshift``,
     *    ``pitchshiftcent``, ``quality`` (0 to 3), ``loopreport`` and
     *    ``reportprogress``; a message of the same name changes one until
     *    the patch is loaded again.
     *  - Inlet 0: ``0`` stops, a number above 0 plays that clip (from 1).
     *    The words ``append <file> [place]``, ``remove <clip>``, ``clear``,
     *    ``move <clip> <place>``, ``selection [clip] <start> <end>`` (0 to
     *    1; without a clip, every clip; a clip alone clears its
     *    selection), ``selectionms`` (the same in ms), ``setclip <clip>
     *    <attribute> [value]``, ``next``, ``pause``, ``resume``,
     *    ``getcontent`` and the parameters by name arrive here too.
     *  - Outlets: one signal per channel; the playhead signal (clip number
     *    plus how far into its file); notifications ``start <clip>
     *    <name>``, ``done <clip> <name>``, ``loopnotify <clip> <name>``
     *    (with ``loopreport``) and ``progress <clip> <0-1>`` (with
     *    ``reportprogress``); last, ``getcontent``'s answer: ``clips
     *    <count>``, then per clip ``clip <n> <name> <file> <length ms>
     *    <start ms> <end ms>`` followed by its own settings as ``<name>
     *    <value>`` pairs.
     *
     *  ### Files are opened in the background
     *
     *  ``append``, ``remove``, ``clear``, ``move``, ``selection``,
     *  ``selectionms`` and ``setclip`` copy their words into a fixed record
     *  and hand it to the patcher's ``fileScheduler`` (wait-free, no
     *  allocation); an appended file's name goes through the host's path
     *  resolver (issue #1255). The task finds the playlist by id, so it
     *  survives the object's deletion. A transport message (a clip number,
     *  ``next``, ``pause``, ``resume``) and ``getcontent`` wait until those
     *  tasks have finished, so ``append song.wav, 1`` plays the song. What
     *  the audio thread let go is freed by a further task.
     *
     *  Limits, the scheduler's: at most ``fileScheduler::CAPACITY`` requests
     *  in flight per patcher, results only while the patcher renders,
     *  nothing outside a patcher. A refused or failed request counts in
     *  ``FailedTasks``.
     *
     *  **GUI value**, read only: ``<serial> <clips> <playing> <paused>
     *  <where>`` and then per clip ``<name> <length ms> <start ms> <end
     *  ms>``. ``serial`` counts the changes to the list; ``playing`` is the
     *  clip that plays (0: none), ``where`` how far into its file (0 to
     *  1). Host reads take only the list's own lock, never one the audio
     *  thread takes.
     *
     *  **Saving.** The parameters, and the clips with their files,
     *  selections and settings; loading opens them again.
     *
     *  Not mapped: the colours, ``candycane``, ``clipheight``,
     *  ``expansion``, ``showname``, ``waveformdisplay``, the shadow and
     *  ``style`` are drawing; ``allowreorder``, ``drag`` and ``mouse`` are
     *  how the host's mouse reaches the messages above (a drag is ``move``
     *  or ``append``); see the class for the rest.
     */
    PATCHER_CLASS(dPlaylist, YSE::OBJ::D_PLAYLIST)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _INT_IN(SetClip)
    _FLOAT_IN(SetClipFloat)
    _LIST_IN(SetList)

    ~dPlaylist() override;

    // Read only: no _HAS_GUI_CELLS, which promises the write half.
    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    void FlushArmedMessages(YSE::THREAD thread) override;
    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    /** @brief The cells before the clips in the GUI value. */
    static constexpr unsigned int HEAD_CELLS = 5;
    /** @brief The cells of each clip. */
    static constexpr unsigned int CLIP_CELLS = 4;

    /** @brief The DSP class. Its audio-thread state only while no block
     *         runs (tests). */
    DSP::filePlaylist& Playlist() {
      return *list;
    }

    /** @brief The number of signal outlets for the file's channels. */
    int Channels() const {
      return channelCount;
    }

    /** @brief Requests refused by the scheduler, or that failed. Diagnostics
     *         and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    struct plTask;
    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    void ShapePorts();
    // Hand `task` to the scheduler (with its name through the resolver when
    // `resolve`). Any thread, no allocation. False when refused.
    bool Submit(plTask& task, bool resolve);
    void Post(int what, int clip = 0);
    // The cells of the GUI value, built from one read of the list.
    std::vector<std::string> Cells() const;

    std::shared_ptr<DSP::filePlaylist> list; // made at construction, never replaced
    std::uint64_t id = 0; // how a task finds the playlist

    // Parameters. channelCount is written by PARM_PARSE only, before any
    // audio thread sees the object (a change of argument rebuilds it); the
    // others are read by the next block.
    int channelCount = 2;
    aInt loop;
    aFlt speed;
    aInt timeStretch;
    aFlt pitchShift;
    aFlt pitchShiftCents;
    aInt quality;
    aInt loopReport;
    aInt reportProgress;

    // A transport request for the next block that has no task in flight;
    // the clip is stored before the request. The last one wins.
    std::atomic<int> request{0};
    std::atomic<int> requestClip{0};
    std::atomic<bool> contentWanted{false};

    // Tasks requested and not yet delivered, the ones a transport request
    // waits for.
    std::atomic<int> inFlight{0};
    std::atomic<bool> releaseQueued{false};
    std::atomic<std::uint64_t> failed{0};

    // What the last block has for FlushArmedMessages to send. Audio thread.
    Int startedClip = 0;
    Int doneClip = 0;
    Int loopedClip = 0;
    Int progressClip = 0;
    Flt progressValue = 0.f;
    bool contentPending = false;
    std::string line; // reserved at construction
  };

} // namespace PATCHER
} // namespace YSE
