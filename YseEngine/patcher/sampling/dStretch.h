#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/timeStretch.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stretch``: changes the length and the pitch of a named
     *         buffer's contents, in the background, through
     *         ``DSP::timeStretch`` (issue #1152).
     *
     *  Ported from Max's ``stretch~``. The wrapper only maps the messages
     *  and the parameter onto the DSP class and runs it off the audio
     *  thread; the stretching is ``DSP::timeStretch``'s.
     *
     *  - Argument: ``buffer-name``, then any of the attribute words with
     *    their values (saved with the patch).
     *  - Inlet 0: ``bang`` or ``apply`` processes the buffer; ``cancel``
     *    stops the job running or waiting; ``set <name>`` names another
     *    buffer (``set`` alone, none). The attributes as messages:
     *    ``stretch``, ``pitchshift``, ``pitchshiftcent``, ``quality``,
     *    ``mode``, ``readagain``, ``pitchcorrection``, ``basictuning``,
     *    ``followglobaltempo``, ``originaltempo``, ``originallength``,
     *    ``transport`` (the clock ``followglobaltempo`` follows) and
     *    ``progress_enable``; ``formant``, ``formantcorrection`` and
     *    ``slurtime`` are accepted and ignored.
     *  - Outlet 0: a bang when a job has written its result.
     *  - Outlet 1: the job's progress, 0 to 1, while ``progress_enable`` is
     *    on (the default): at most once per block and per hundredth while
     *    it runs, then 1 just before the bang.
     *
     *  ### The job runs on the background pool
     *
     *  A bang copies the buffer name and the settings into a fixed record
     *  and hands it to the patcher's ``fileScheduler`` as a ``TASK``
     *  (wait-free, no allocation). The task finds the buffer by name, runs
     *  ``DSP::timeStretch::process`` and writes the result back in one
     *  ``sampleBuffer::setContents``, so the players reading it hear the old
     *  samples or the new ones, never a mix. The done bang comes back
     *  through ``DeliverFileResult`` at the top of a later block.
     *
     *  The task never touches this object, which a live edit may delete
     *  meanwhile. What the two share is a ``state`` found by id in a
     *  process-wide table: the cancel mark, the progress, and the copy
     *  ``readagain`` keeps. ``cancel`` stores the number of the last job
     *  requested as the cancel mark (one atomic store); a job at or below
     *  it stops before it starts, or between two frames, and leaves the
     *  buffer untouched. Progress is an atomic the job stores and the block
     *  poll (``WantsBlockPoll``) reads. Deleting the object cancels its
     *  jobs, and one that is waiting finds no state and does nothing.
     *
     *  **readagain** (on by default): the first job keeps a copy of the
     *  buffer as it found it, and later jobs process that copy, so a second
     *  bang with a new ``stretch`` is not a stretch of the stretch. When the
     *  buffer has been changed since the last job wrote it (a new file, a
     *  fill), the next job takes the new contents as its original.
     *
     *  **followglobaltempo** reads the tempo of the domain clock named by
     *  ``transport`` when the job runs: the stretch is then ``originaltempo
     *  / tempo``, so a loop recorded at 120 BPM fits a clock at 100.
     *  ``originallength`` (in beats, a note value or ``<n> ticks``) gives
     *  the original tempo from the buffer's length instead. Without a
     *  clock of that name, the ``stretch`` attribute is used.
     *
     *  Limits that come from the scheduler: at most
     *  ``fileScheduler::CAPACITY`` requests in flight per patcher, results
     *  only while the patcher renders. The background pool is shared with
     *  file loads, which wait while a long job runs.
     *
     *  Not ported: ``dictionary`` (markers mapping source to destination
     *  times; a time map rather than one factor, a feature of its own:
     *  #1285), ``mouse`` (an editor window; the host draws), ``formant`` and
     *  ``formantcorrection`` (formant preservation is a non-goal of
     *  ``DSP::timeStretch``) and ``slurtime`` (``DSP::retune``, which does
     *  the correction, corrects at once and has no glide yet: #1284). The
     *  last three are accepted and ignored, so patches that set them load.
     */
    PATCHER_CLASS(dStretch, YSE::OBJ::D_STRETCH)
    _DO_MESSAGES
    _DO_CALCULATE
    _BANG_IN(Bang)

    _PARM_CLEAR
    _PARM_PARSE

    ~dStretch() override;
    dStretch(const dStretch&) = delete;
    dStretch& operator=(const dStretch&) = delete;
    dStretch(dStretch&&) = delete;
    dStretch& operator=(dStretch&&) = delete;

    void SetParent(pObject* newParent) override;
    /** @brief Control thread, as the object is deleted: its jobs are
     *         cancelled, the one running included. */
    void Teardown(YSE::THREAD thread) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief Yes: the progress of a job on the background pool has no
     *         cord to arrive on, so the block poll collects it. */
    bool WantsBlockPoll() const override {
      return true;
    }

    /** @brief The buffer a bang processes, empty for none. Tests. */
    const char* BufferName() const {
      return name;
    }

    /** @brief The DSP settings a bang would hand the job now (before a
     *         clock's tempo is applied). Tests. */
    DSP::timeStretch Stretcher() const;

    /** @brief Jobs that found no buffer of the name. Diagnostics, tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

    /** @brief Jobs that were cancelled. Diagnostics, tests. */
    std::uint64_t CancelledTasks() const {
      return cancelled.load(std::memory_order_relaxed);
    }

    /** @brief Bangs the scheduler refused (no patcher, a full table).
     *         Diagnostics, tests. */
    std::uint64_t RefusedRequests() const {
      return refused.load(std::memory_order_relaxed);
    }

    struct state;

  private:
    // Every attribute back to its default.
    void Defaults();
    // Apply one word with its values, from the settings or a message.
    // False when it is not one of ours.
    bool ApplyWord(std::string_view word, const std::string_view* values, std::size_t count);
    // Hand a job to the scheduler. Any thread, no allocation.
    void Request();
    // Stop every job requested so far. Any thread, wait-free.
    void Cancel();
    // Copy `text` (of `length`) into `out`; false, and no change, when it
    // does not fit.
    static bool SetText(char* out, std::size_t capacity, const char* text, std::size_t length);

    // The fileTaskFunction: background pool, never touches the object.
    struct stretchTask;
    static bool RunTask(char* args, std::size_t argBytes);

    static constexpr std::size_t CLOCK_CAPACITY = 128;

    std::string bufferName; // the argument
    std::vector<std::string> settings; // the attribute words that follow it

    // The buffer and the clock a job uses: the argument / `transport`, or
    // the last `set`. Written by PARM_PARSE and the message handlers.
    char name[fileScheduler::PATH_CAPACITY] = {};
    char clock[CLOCK_CAPACITY] = {};

    aFlt stretchFactor{1.f};
    aFlt pitchFactor{1.f};
    aInt quality{DSP::timeStretch::BETTER};
    aInt mode{DSP::timeStretch::MODE_BASIC};
    aFlt basicTuning{440.f};
    aFlt originalTempo{120.f};
    aFlt originalLength{0.f}; // beats
    std::atomic<bool> lengthGiven{false}; // originallength was set last
    std::atomic<bool> readAgain{true};
    std::atomic<bool> correction{false};
    std::atomic<bool> followTempo{false};
    std::atomic<bool> progressOn{true};

    std::shared_ptr<state> shared; // made in the constructor
    std::uint64_t stateId = 0;
    std::atomic<std::uint64_t> requested{0}; // the last job's number

    // The block poll's: the job it reports on and the hundredth last sent.
    std::uint64_t reportedJob = 0;
    int reportedStep = -1;

    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> cancelled{0};
    std::atomic<std::uint64_t> refused{0};
  };

} // namespace PATCHER
} // namespace YSE
